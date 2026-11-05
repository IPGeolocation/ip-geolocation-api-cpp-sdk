#include "internal.hpp"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <mutex>
#include <memory>
#include <string>

namespace ipgeolocation::internal {
namespace {

std::once_flag g_curl_global_init_once;

void CleanupCurlGlobalState() {
    curl_global_cleanup();
}

void EnsureCurlGlobalState() {
    std::call_once(g_curl_global_init_once, []() {
        const CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (code != CURLE_OK) {
            throw TransportError(std::string("failed to initialize libcurl: ") + curl_easy_strerror(code));
        }
        std::atexit(&CleanupCurlGlobalState);
    });
}

struct WriteContext {
    std::string body;
    std::size_t max_size = 0;
    bool size_exceeded = false;
};

std::size_t WriteBody(char* pointer, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* context = static_cast<WriteContext*>(userdata);
    if (size != 0 && nmemb > (context->max_size - context->body.size()) / size) {
        context->size_exceeded = true;
        return 0;
    }
    const std::size_t bytes = size * nmemb;
    try {
        context->body.append(pointer, bytes);
        return bytes;
    } catch (...) {
        return 0;
    }
}

std::size_t ReadHeaders(char* buffer, std::size_t size, std::size_t nitems, void* userdata) try {
    auto* headers = static_cast<std::map<std::string, std::vector<std::string>>*>(userdata);
    const std::size_t bytes = size * nitems;
    std::string line(buffer, bytes);

    if (line == "\r\n") {
        return bytes;
    }

    const std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return bytes;
    }

    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);

    while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' ' || name.back() == '\t')) {
        name.pop_back();
    }
    value.erase(0, value.find_first_not_of(" \t"));
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
        value.pop_back();
    }

    if (!name.empty()) {
        (*headers)[name].push_back(value);
    }

    return bytes;
} catch (...) {
    return 0;
}

class CurlHeaderList {
public:
    CurlHeaderList() = default;

    ~CurlHeaderList() {
        if (list_ != nullptr) {
            curl_slist_free_all(list_);
        }
    }

    CurlHeaderList(const CurlHeaderList&) = delete;
    CurlHeaderList& operator=(const CurlHeaderList&) = delete;

    void Append(const std::string& header_line) {
        curl_slist* appended = curl_slist_append(list_, header_line.c_str());
        if (appended == nullptr) {
            throw TransportError("failed to prepare request headers");
        }
        list_ = appended;
    }

    curl_slist* get() const noexcept {
        return list_;
    }

private:
    curl_slist* list_ = nullptr;
};

void CheckCurl(CURLcode result) {
    if (result != CURLE_OK) {
        throw TransportError(std::string("libcurl transport error: ") + curl_easy_strerror(result));
    }
}

}  // namespace

long CurlTimeoutMilliseconds(std::chrono::milliseconds timeout) {
    return static_cast<long>(std::min<std::chrono::milliseconds::rep>(timeout.count(), LONG_MAX));
}

bool NeedsHttp11ForCurl(unsigned int version) {
    return version >= 0x080c00 && version < 0x081700 &&
           !(version >= 0x080e03 && version < 0x080f00) &&
           !(version >= 0x081002 && version < 0x081100) &&
           !(version >= 0x081402 && version < 0x081500);
}

CurlHttpTransport::CurlHttpTransport() {
    EnsureCurlGlobalState();
    idle_handles_.reserve(kMaxIdleHandles);
}

CurlHttpTransport::~CurlHttpTransport() {
    Close();
}

CURL* CurlHttpTransport::AcquireHandle() {
    std::lock_guard<std::mutex> lock(handles_mutex_);
    if (closed_) {
        throw TransportError("transport is closed");
    }
    if (!idle_handles_.empty()) {
        CURL* handle = idle_handles_.back();
        idle_handles_.pop_back();
        return handle;
    }

    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
        throw TransportError("failed to create libcurl easy handle");
    }

    return handle;
}

void CurlHttpTransport::ReleaseHandle(CURL* handle) noexcept {
    std::lock_guard<std::mutex> lock(handles_mutex_);
    if (closed_ || idle_handles_.size() >= kMaxIdleHandles) {
        curl_easy_cleanup(handle);
        return;
    }
    idle_handles_.push_back(handle);
}

HttpResponseData CurlHttpTransport::Send(const HttpRequestData& request, std::size_t max_response_body_chars) {
    CURL* handle = AcquireHandle();
    struct HandleReturn {
        CurlHttpTransport* owner;
        CURL* handle;

        ~HandleReturn() {
            if (handle != nullptr) {
                owner->ReleaseHandle(handle);
            }
        }
    } handle_return{this, handle};

    curl_easy_reset(handle);

    WriteContext write_context;
    write_context.max_size = max_response_body_chars;
    std::map<std::string, std::vector<std::string>> headers;
    CurlHeaderList header_list;

    for (const auto& [name, value] : request.headers) {
        header_list.Append(name + ": " + value);
    }

    if (NeedsHttp11ForCurl(curl_version_info(CURLVERSION_NOW)->version_num)) {
        CheckCurl(curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1)));
    }
    CheckCurl(curl_easy_setopt(handle, CURLOPT_URL, request.url.c_str()));
    CheckCurl(curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, CurlTimeoutMilliseconds(request.connect_timeout)));
    CheckCurl(curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, CurlTimeoutMilliseconds(request.read_timeout)));
    CheckCurl(curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L));
    CheckCurl(curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L));
    CheckCurl(curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L));
    CheckCurl(curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L));
    CheckCurl(curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header_list.get()));
    CheckCurl(curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &WriteBody));
    CheckCurl(curl_easy_setopt(handle, CURLOPT_WRITEDATA, &write_context));
    CheckCurl(curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, &ReadHeaders));
    CheckCurl(curl_easy_setopt(handle, CURLOPT_HEADERDATA, &headers));

    if (request.method == "POST") {
        CheckCurl(curl_easy_setopt(handle, CURLOPT_POST, 1L));
        CheckCurl(curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body.c_str()));
        CheckCurl(curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request.body.size())));
    } else {
        CheckCurl(curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L));
    }

    const CURLcode result = curl_easy_perform(handle);
    long status_code = 0;
    CheckCurl(curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status_code));
    if (result != CURLE_OK) {
        if (status_code >= 300 && status_code < 600) {
            return {static_cast<int>(status_code), {}, std::move(headers)};
        }
        if (result == CURLE_OPERATION_TIMEDOUT) {
            throw RequestTimeoutError(
                "HTTP request timed out after " + std::to_string(request.read_timeout.count()) +
                "ms while waiting for response data");
        }
        if (result == CURLE_WRITE_ERROR && write_context.size_exceeded) {
            throw TransportError("response body exceeded maxResponseBodyChars");
        }

        throw TransportError(std::string("libcurl transport error: ") + curl_easy_strerror(result));
    }

    HttpResponseData response;
    response.status_code = static_cast<int>(status_code);
    response.body = std::move(write_context.body);
    response.headers = std::move(headers);
    return response;
}

void CurlHttpTransport::Close() noexcept {
    std::lock_guard<std::mutex> lock(handles_mutex_);
    closed_ = true;
    for (CURL* handle : idle_handles_) {
        if (handle != nullptr) {
            curl_easy_cleanup(handle);
        }
    }
    idle_handles_.clear();
}

}  // namespace ipgeolocation::internal
