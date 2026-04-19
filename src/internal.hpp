#pragma once

#include <curl/curl.h>

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ipgeolocation/api_response.hpp"
#include "ipgeolocation/config.hpp"
#include "ipgeolocation/errors.hpp"
#include "ipgeolocation/language.hpp"
#include "ipgeolocation/models.hpp"
#include "ipgeolocation/requests.hpp"
#include "ipgeolocation/response_format.hpp"

namespace ipgeolocation::internal {

#if defined(IPGEOLOCATION_ENABLE_TESTING_ACCESS)
class CurlHttpTransportTestAccess;
#endif

struct NormalizedConfig {
    std::optional<std::string> api_key;
    std::optional<std::string> request_origin;
    std::string base_url;
    std::chrono::milliseconds connect_timeout{10000};
    std::chrono::milliseconds read_timeout{30000};
    std::size_t max_response_body_chars = 32u * 1024u * 1024u;
};

struct NormalizedLookupRequest {
    std::optional<std::string> ip;
    std::optional<std::string> lang;
    std::vector<std::string> include;
    std::vector<std::string> fields;
    std::vector<std::string> excludes;
    std::optional<std::string> user_agent;
    std::map<std::string, std::string> headers;
    ResponseFormat output = ResponseFormat::kJson;
};

struct NormalizedBulkLookupRequest {
    std::vector<std::string> ips;
    std::optional<std::string> lang;
    std::vector<std::string> include;
    std::vector<std::string> fields;
    std::vector<std::string> excludes;
    std::optional<std::string> user_agent;
    std::map<std::string, std::string> headers;
    ResponseFormat output = ResponseFormat::kJson;
};

struct HttpRequestData {
    std::string url;
    std::string method;
    std::map<std::string, std::string> headers;
    std::string body;
    std::chrono::milliseconds connect_timeout{10000};
    std::chrono::milliseconds read_timeout{30000};
};

struct HttpResponseData {
    int status_code = 0;
    std::string body;
    std::map<std::string, std::vector<std::string>> headers;
};

class HttpTransport {
public:
    virtual ~HttpTransport() = default;
    virtual HttpResponseData Send(const HttpRequestData& request, std::size_t max_response_body_chars) = 0;
    virtual void Close() noexcept = 0;
};

class CurlHttpTransport final : public HttpTransport {
public:
    CurlHttpTransport();
    ~CurlHttpTransport() override;

    HttpResponseData Send(const HttpRequestData& request, std::size_t max_response_body_chars) override;
    void Close() noexcept override;

private:
#if defined(IPGEOLOCATION_ENABLE_TESTING_ACCESS)
    friend class CurlHttpTransportTestAccess;
#endif

    static constexpr std::size_t kMaxIdleHandles = 32;

    CURL* AcquireHandle();
    void ReleaseHandle(CURL* handle) noexcept;

    std::mutex handles_mutex_;
    std::vector<CURL*> idle_handles_;
    bool closed_ = false;
};

#if defined(IPGEOLOCATION_ENABLE_TESTING_ACCESS)
class CurlHttpTransportTestAccess {
public:
    static std::size_t IdleHandleCount(const CurlHttpTransport& transport) {
        return transport.idle_handles_.size();
    }

    static constexpr std::size_t MaxIdleHandleCount() {
        return CurlHttpTransport::kMaxIdleHandles;
    }

    static void ReleaseHandle(CurlHttpTransport& transport, CURL* handle) noexcept {
        transport.ReleaseHandle(handle);
    }
};
#endif

std::unique_ptr<HttpTransport> CreateDefaultTransport();

NormalizedConfig NormalizeConfig(const IpGeolocationClientConfig& config);
NormalizedLookupRequest NormalizeLookupRequest(const LookupIpGeolocationRequest& request);
NormalizedBulkLookupRequest NormalizeBulkLookupRequest(const BulkLookupIpGeolocationRequest& request);

std::string DefaultUserAgent();
std::string WireValue(ResponseFormat format);
std::string LanguageCode(Language language);
std::string AcceptHeaderFor(ResponseFormat format);
std::string ResolveUserAgentHeader(
    const std::optional<std::string>& request_user_agent,
    const std::map<std::string, std::string>& headers);

HttpRequestData BuildLookupHttpRequest(
    const NormalizedConfig& config,
    const NormalizedLookupRequest& request);

HttpRequestData BuildBulkHttpRequest(
    const NormalizedConfig& config,
    const NormalizedBulkLookupRequest& request);

ApiResponseMetadata ToMetadata(
    int status_code,
    long long duration_ms,
    const std::map<std::string, std::vector<std::string>>& headers);

IpGeolocationResponse ParseIpGeolocationResponse(const std::string& body);
std::vector<BulkLookupResult> ParseBulkLookupResults(const std::string& body);
ApiError ToApiError(int status_code, const std::string& body);
std::string ExtractApiMessage(const std::string& body);
std::string BuildBulkRequestBody(const std::vector<std::string>& ips);
std::string EscapeJsonString(const std::string& value);

}  // namespace ipgeolocation::internal
