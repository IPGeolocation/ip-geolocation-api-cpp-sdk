#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <clocale>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <memory>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "ipgeolocation/ipgeolocation.hpp"
#include "client_test_access.hpp"
#include "internal.hpp"
#include "json.hpp"

namespace {

struct MockTransportState {
    std::vector<ipgeolocation::internal::HttpRequestData> requests;
    std::vector<ipgeolocation::internal::HttpResponseData> responses;
    std::size_t send_count = 0;
    bool closed = false;
};

class MockTransport final : public ipgeolocation::internal::HttpTransport {
public:
    explicit MockTransport(std::shared_ptr<MockTransportState> state) : state_(std::move(state)) {}

    ipgeolocation::internal::HttpResponseData Send(
        const ipgeolocation::internal::HttpRequestData& request,
        std::size_t) override {
        state_->requests.push_back(request);
        if (state_->send_count >= state_->responses.size()) {
            throw std::runtime_error("no mock response configured");
        }
        return state_->responses[state_->send_count++];
    }

    void Close() noexcept override {
        state_->closed = true;
    }

private:
    std::shared_ptr<MockTransportState> state_;
};

struct ConcurrentTransportState {
    std::vector<ipgeolocation::internal::HttpRequestData> requests;
    std::vector<ipgeolocation::internal::HttpResponseData> responses;
    std::mutex mutex;
    std::condition_variable barrier;
    std::size_t next_response = 0;
    int active_requests = 0;
    int peak_active_requests = 0;
    std::size_t started_requests = 0;
    std::size_t barrier_target = 0;
    bool barrier_released = false;
    bool closed = false;
};

class ConcurrentMockTransport final : public ipgeolocation::internal::HttpTransport {
public:
    explicit ConcurrentMockTransport(std::shared_ptr<ConcurrentTransportState> state) : state_(std::move(state)) {}

    ipgeolocation::internal::HttpResponseData Send(
        const ipgeolocation::internal::HttpRequestData& request,
        std::size_t) override {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->requests.push_back(request);
        ++state_->active_requests;
        state_->peak_active_requests = std::max(state_->peak_active_requests, state_->active_requests);
        ++state_->started_requests;
        if (state_->started_requests >= state_->barrier_target) {
            state_->barrier_released = true;
            state_->barrier.notify_all();
        } else {
            state_->barrier.wait(lock, [this]() {
                return state_->barrier_released;
            });
        }

        if (state_->next_response >= state_->responses.size()) {
            --state_->active_requests;
            throw std::runtime_error("no concurrent mock response configured");
        }

        const auto response = state_->responses[state_->next_response++];
        --state_->active_requests;
        return response;
    }

    void Close() noexcept override {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->closed = true;
    }

private:
    std::shared_ptr<ConcurrentTransportState> state_;
};

class ScopedMockTransportFactory {
public:
    explicit ScopedMockTransportFactory(std::vector<ipgeolocation::internal::HttpResponseData> responses)
        : state_(std::make_shared<MockTransportState>()) {
        state_->responses = std::move(responses);
    }

    const std::shared_ptr<MockTransportState>& state() const {
        return state_;
    }

    ipgeolocation::IpGeolocationClient CreateClient(const ipgeolocation::IpGeolocationClientConfig& config) const {
        return ipgeolocation::internal::ClientTestAccess::CreateClientWithTransport(
            config,
            std::make_unique<MockTransport>(state_));
    }

    std::unique_ptr<ipgeolocation::internal::HttpTransport> CreateTransport() const {
        return std::make_unique<MockTransport>(state_);
    }

private:
    std::shared_ptr<MockTransportState> state_;
};

class ScopedConcurrentTransportFactory {
public:
    explicit ScopedConcurrentTransportFactory(std::vector<ipgeolocation::internal::HttpResponseData> responses)
        : state_(std::make_shared<ConcurrentTransportState>()) {
        state_->barrier_target = responses.size();
        state_->responses = std::move(responses);
    }

    const std::shared_ptr<ConcurrentTransportState>& state() const {
        return state_;
    }

    ipgeolocation::IpGeolocationClient CreateClient(const ipgeolocation::IpGeolocationClientConfig& config) const {
        return ipgeolocation::internal::ClientTestAccess::CreateClientWithTransport(
            config,
            std::make_unique<ConcurrentMockTransport>(state_));
    }

private:
    std::shared_ptr<ConcurrentTransportState> state_;
};

void WriteAll(int fd, const std::string& data) {
    std::size_t total = 0;
    while (total < data.size()) {
        const auto sent = send(fd, data.data() + total, data.size() - total, 0);
        if (sent <= 0) {
            throw std::runtime_error("failed to write loopback response");
        }
        total += static_cast<std::size_t>(sent);
    }
}

std::string ReadHttpRequest(int fd) {
    std::string request;
    std::size_t expected_body_size = 0;
    bool saw_headers = false;

    while (true) {
        char buffer[1024];
        const auto received = recv(fd, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            throw std::runtime_error("failed to read loopback request");
        }
        request.append(buffer, static_cast<std::size_t>(received));

        const std::size_t headers_end = request.find("\r\n\r\n");
        if (!saw_headers && headers_end != std::string::npos) {
            saw_headers = true;
            const std::size_t content_length_pos = request.find("Content-Length:");
            if (content_length_pos != std::string::npos && content_length_pos < headers_end) {
                const std::size_t value_start = content_length_pos + std::string("Content-Length:").size();
                const std::size_t value_end = request.find("\r\n", value_start);
                expected_body_size = static_cast<std::size_t>(
                    std::stoul(request.substr(value_start, value_end - value_start)));
            }
        }

        if (saw_headers) {
            const std::size_t body_start = request.find("\r\n\r\n") + 4;
            if (request.size() >= body_start + expected_body_size) {
                return request;
            }
        }
    }
}

class LoopbackServer {
public:
    explicit LoopbackServer(std::function<void(int)> handler) : handler_(std::move(handler)) {
        listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            throw std::runtime_error("failed to create loopback socket");
        }

        int reuse = 1;
        static_cast<void>(setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)));

        sockaddr_in address{};
#ifdef __APPLE__
        address.sin_len = sizeof(address);
#endif
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;

        if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            close(listen_fd_);
            throw std::runtime_error(std::string("failed to bind loopback socket: ") + std::strerror(errno));
        }

        socklen_t address_length = sizeof(address);
        if (getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&address), &address_length) != 0) {
            close(listen_fd_);
            throw std::runtime_error("failed to inspect loopback socket");
        }

        port_ = ntohs(address.sin_port);

        if (listen(listen_fd_, 1) != 0) {
            close(listen_fd_);
            throw std::runtime_error("failed to listen on loopback socket");
        }

        thread_ = std::thread([this]() {
            sockaddr_in client_address{};
            socklen_t client_length = sizeof(client_address);
            const int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_address), &client_length);
            if (client_fd < 0) {
                return;
            }

            try {
                handler_(client_fd);
            } catch (...) {
            }

            shutdown(client_fd, SHUT_RDWR);
            close(client_fd);
        });
    }

    ~LoopbackServer() {
        if (thread_.joinable()) {
            thread_.join();
        }
        if (listen_fd_ >= 0) {
            close(listen_fd_);
        }
    }

    std::string url(const std::string& path = "/") const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }

private:
    int listen_fd_ = -1;
    int port_ = 0;
    std::thread thread_;
    std::function<void(int)> handler_;
};

void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool ApproximatelyEqual(double left, double right, double tolerance = 1e-12) {
    return std::fabs(left - right) <= tolerance;
}

class ScopedNumericLocale {
public:
    ScopedNumericLocale() {
        const char* current = std::setlocale(LC_NUMERIC, nullptr);
        if (current != nullptr) {
            original_ = current;
        }
    }

    ~ScopedNumericLocale() {
        if (!original_.empty()) {
            std::setlocale(LC_NUMERIC, original_.c_str());
        }
    }

    bool TrySetCommaDecimalLocale() {
        static const char* const candidates[] = {
            "de_DE.UTF-8",
            "de_DE.utf8",
            "fr_FR.UTF-8",
            "fr_FR.utf8",
            "German_Germany.1252",
            "French_France.1252",
        };

        for (const char* candidate : candidates) {
            if (std::setlocale(LC_NUMERIC, candidate) != nullptr) {
                return true;
            }
        }

        return false;
    }

private:
    std::string original_;
};

template <typename ExceptionType>
void ExpectThrows(const std::function<void()>& callback, const std::string& expected_message) {
    try {
        callback();
    } catch (const ExceptionType& error) {
        Expect(error.what() == expected_message,
               "expected error message '" + expected_message + "', got '" + error.what() + "'");
        return;
    }

    throw std::runtime_error("expected exception was not thrown");
}

void TestConfigNormalizesRequestOriginAndBaseUrl() {
    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "  secret  ";
    config.request_origin = "https://app.example.com/";
    config.base_url = "https://api.ipgeolocation.io/";

    const auto normalized = ipgeolocation::internal::NormalizeConfig(config);
    Expect(normalized.api_key.has_value() && *normalized.api_key == "secret", "api key should be trimmed");
    Expect(normalized.request_origin.has_value() && *normalized.request_origin == "https://app.example.com",
           "request origin should be normalized");
    Expect(normalized.base_url == "https://api.ipgeolocation.io", "base url should drop trailing slash");
}

void TestConfigRejectsRequestOriginPath() {
    ipgeolocation::IpGeolocationClientConfig config;
    config.request_origin = "https://app.example.com/path";

    ExpectThrows<ipgeolocation::ValidationError>(
        [&config]() {
            static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
        },
        "requestOrigin must not include a path");
}

void TestConfigRejectsInvalidBaseUrlAndTimeoutValues() {
    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.base_url = "   ";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "baseUrl must not be blank");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.base_url = "ftp://api.ipgeolocation.io";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "baseUrl must be an absolute http or https URL");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.base_url = "https://user:pass@api.ipgeolocation.io";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "baseUrl must not include userinfo");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.base_url = "https://api.ipgeolocation.io?debug=true";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "baseUrl must not include query or fragment");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.connect_timeout = std::chrono::milliseconds(0);
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "connectTimeout must be greater than zero");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.connect_timeout = std::chrono::milliseconds(200);
        config.read_timeout = std::chrono::milliseconds(100);
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "connectTimeout must be <= readTimeout");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.max_response_body_chars = 0;
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "maxResponseBodyChars must be greater than zero");
    }
}

void TestConfigRejectsInvalidRequestOriginForms() {
    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "app.example.com";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must be an absolute http or https origin");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://user@app.example.com";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must not include userinfo");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://app.example.com?x=1";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must not include query or fragment");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = std::string("https://app.example.com\r\nmalicious");
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must not contain CR or LF");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://app example.com";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must include a valid host");
    }
}

void TestConfigCoversAdditionalValidationBranches() {
    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.api_key = "   ";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "apiKey must not be blank");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.api_key = "secret\r\nbad";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "apiKey must not contain CR or LF");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "   ";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must not be blank");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.base_url = "https://api.ipgeolocation.io\r\nbad";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "baseUrl must not contain CR or LF");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.base_url = "https://api.ipgeolocation.io/custom/";
        const auto normalized = ipgeolocation::internal::NormalizeConfig(config);
        Expect(normalized.base_url == "https://api.ipgeolocation.io/custom",
               "base url should preserve non-root paths");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.read_timeout = std::chrono::milliseconds(0);
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "readTimeout must be greater than zero");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://localhost:3000";
        config.base_url = "https://127.0.0.1:8443/custom";
        const auto normalized = ipgeolocation::internal::NormalizeConfig(config);
        Expect(normalized.request_origin.has_value() && *normalized.request_origin == "https://localhost:3000",
               "request origin should preserve a valid port");
        Expect(normalized.base_url == "https://127.0.0.1:8443/custom",
               "base url should preserve a valid IPv4 host and port");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://[2001:db8::1]:443";
        const auto normalized = ipgeolocation::internal::NormalizeConfig(config);
        Expect(normalized.request_origin.has_value() && *normalized.request_origin == "https://[2001:db8::1]:443",
               "request origin should preserve a valid bracketed IPv6 literal");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://[2001:db8::1]";
        const auto normalized = ipgeolocation::internal::NormalizeConfig(config);
        Expect(normalized.request_origin.has_value() && *normalized.request_origin == "https://[2001:db8::1]",
               "request origin should preserve a valid bracketed IPv6 literal without a port");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://app.example.com:70000";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must include a valid host");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://2001:db8::1";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must include a valid host");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://[2001:db8::1";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must include a valid host");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://app.example.com:http";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must include a valid host");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://-bad.example.com";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must include a valid host");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://bad-.example.com";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must include a valid host");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://app..example.com";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must include a valid host");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.request_origin = "https://[2001:db8::1]suffix";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "requestOrigin must include a valid host");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.base_url = "https://api_example.ipgeolocation.io";
        ExpectThrows<ipgeolocation::ValidationError>(
            [&config]() {
                static_cast<void>(ipgeolocation::internal::NormalizeConfig(config));
            },
            "baseUrl must include a valid host");
    }
}

void TestLookupRequestAllowsCallerIpAndTrimsUserAgent() {
    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "   ";
    request.user_agent = "  Agent/1.0  ";
    request.headers = {{"X-Test", " value "}};

    const auto normalized = ipgeolocation::internal::NormalizeLookupRequest(request);
    Expect(!normalized.ip.has_value(), "blank ip should be treated as omission");
    Expect(normalized.user_agent.has_value() && *normalized.user_agent == "Agent/1.0",
           "user agent should be trimmed");
    Expect(normalized.headers.at("X-Test") == "value", "headers should be trimmed");
}

void TestLookupRequestNormalizesLanguageAndDeduplicatesLists() {
    ipgeolocation::LookupIpGeolocationRequest request;
    request.lang = ipgeolocation::Language::kFr;
    request.include = {"security", "security", "abuse"};
    request.fields = {"ip", "ip", "location.country_name"};
    request.excludes = {"time_zone", "time_zone"};

    const auto normalized = ipgeolocation::internal::NormalizeLookupRequest(request);
    Expect(normalized.lang.has_value() && *normalized.lang == "fr", "language enum should normalize to wire code");
    Expect(normalized.include.size() == 2, "include list should deduplicate values");
    Expect(normalized.fields.size() == 2, "fields list should deduplicate values");
    Expect(normalized.excludes.size() == 1, "excludes list should deduplicate values");
}

void TestLookupRequestRejectsBlankAndCrLfValues() {
    const auto expect_lookup_validation = [](const ipgeolocation::LookupIpGeolocationRequest& request,
                                             const std::string& expected_message,
                                             const std::string& case_name) {
        try {
            static_cast<void>(ipgeolocation::internal::NormalizeLookupRequest(request));
        } catch (const ipgeolocation::ValidationError& error) {
            Expect(error.what() == expected_message,
                   case_name + ": expected error message '" + expected_message + "', got '" + error.what() + "'");
            return;
        }

        throw std::runtime_error(case_name + ": expected validation exception was not thrown");
    };

    {
        ipgeolocation::LookupIpGeolocationRequest request;
        request.ip = "8.8.8.8\r\nx";
        expect_lookup_validation(request, "ip must not contain CR or LF", "lookup ip CRLF");
    }

    {
        ipgeolocation::LookupIpGeolocationRequest request;
        request.include = {"security", "   "};
        expect_lookup_validation(request, "include must not contain blank values", "lookup include blank");
    }

    {
        ipgeolocation::LookupIpGeolocationRequest request;
        request.fields = {"ip", "location.country_name\r\nbad"};
        expect_lookup_validation(request, "fields must not contain CR or LF", "lookup fields CRLF");
    }

    {
        ipgeolocation::LookupIpGeolocationRequest request;
        request.user_agent = "Agent\r\nInjected";
        expect_lookup_validation(request, "userAgent must not contain CR or LF", "lookup user agent CRLF");
    }
}

void TestBulkRequestRejectsBlankValues() {
    ipgeolocation::BulkLookupIpGeolocationRequest request;
    request.ips = {"8.8.8.8", "   "};

    ExpectThrows<ipgeolocation::ValidationError>(
        [&request]() {
            static_cast<void>(ipgeolocation::internal::NormalizeBulkLookupRequest(request));
        },
        "ips must not contain blank values");
}

void TestBulkRequestRejectsEmptyOversizedAndCrLfValues() {
    {
        ipgeolocation::BulkLookupIpGeolocationRequest request;
        ExpectThrows<ipgeolocation::ValidationError>(
            [&request]() {
                static_cast<void>(ipgeolocation::internal::NormalizeBulkLookupRequest(request));
            },
            "ips must contain at least one IP address or domain");
    }

    {
        ipgeolocation::BulkLookupIpGeolocationRequest request;
        request.ips = std::vector<std::string>(50001, "8.8.8.8");
        ExpectThrows<ipgeolocation::ValidationError>(
            [&request]() {
                static_cast<void>(ipgeolocation::internal::NormalizeBulkLookupRequest(request));
            },
            "ips must not contain more than 50000 entries");
    }

    {
        ipgeolocation::BulkLookupIpGeolocationRequest request;
        request.ips = {"8.8.8.8\nx"};
        ExpectThrows<ipgeolocation::ValidationError>(
            [&request]() {
                static_cast<void>(ipgeolocation::internal::NormalizeBulkLookupRequest(request));
            },
            "ip must not contain CR or LF");
    }
}

void TestBulkRequestNormalizesLanguageListsAndUserAgent() {
    ipgeolocation::BulkLookupIpGeolocationRequest request;
    request.ips = {" 8.8.8.8 ", " ipgeolocation.io "};
    request.lang = ipgeolocation::Language::kPt;
    request.include = {"security", "security", "abuse"};
    request.fields = {"ip", "location.country_name", "ip"};
    request.excludes = {"time_zone", "time_zone"};
    request.user_agent = "  BulkAgent/1.0  ";
    request.headers = {{"X-Test", " value "}};

    const auto normalized = ipgeolocation::internal::NormalizeBulkLookupRequest(request);
    Expect(normalized.ips.size() == 2 && normalized.ips[0] == "8.8.8.8" && normalized.ips[1] == "ipgeolocation.io",
           "bulk ips should trim values");
    Expect(normalized.lang.has_value() && *normalized.lang == "pt", "bulk language should normalize");
    Expect(normalized.include.size() == 2, "bulk include should deduplicate");
    Expect(normalized.fields.size() == 2, "bulk fields should deduplicate");
    Expect(normalized.excludes.size() == 1, "bulk excludes should deduplicate");
    Expect(normalized.user_agent.has_value() && *normalized.user_agent == "BulkAgent/1.0",
           "bulk user agent should trim");
    Expect(normalized.headers.at("X-Test") == "value", "bulk headers should trim");
}

void TestNormalizeHeadersRejectsInvalidShapes() {
    const auto expect_header_validation = [](const ipgeolocation::LookupIpGeolocationRequest& request,
                                             const std::string& expected_message,
                                             const std::string& case_name) {
        try {
            static_cast<void>(ipgeolocation::internal::NormalizeLookupRequest(request));
        } catch (const ipgeolocation::ValidationError& error) {
            Expect(error.what() == expected_message,
                   case_name + ": expected error message '" + expected_message + "', got '" + error.what() + "'");
            return;
        }

        throw std::runtime_error(case_name + ": expected validation exception was not thrown");
    };

    {
        ipgeolocation::LookupIpGeolocationRequest request;
        request.headers = {{"   ", "value"}};
        expect_header_validation(request, "headers must not contain blank names", "header blank name");
    }

    {
        ipgeolocation::LookupIpGeolocationRequest request;
        request.headers = {{"X-Test", "   "}};
        expect_header_validation(request, "headers must not contain blank values", "header blank value");
    }

    {
        ipgeolocation::LookupIpGeolocationRequest request;
        request.headers = {{"X-Test", "bad\r\nInjected"}};
        expect_header_validation(request, "headers must not contain CR or LF", "header CRLF");
    }
}

void TestBuildLookupRequestRespectsUserAgentPrecedence() {
    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";
    config.request_origin = "https://app.example.com";

    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "8.8.8.8";
    request.user_agent = "FieldAgent/2.0";
    request.headers = {{"User-Agent", "HeaderAgent/1.0"}};
    request.include = {"security", "abuse"};

    const auto http_request = ipgeolocation::internal::BuildLookupHttpRequest(
        ipgeolocation::internal::NormalizeConfig(config),
        ipgeolocation::internal::NormalizeLookupRequest(request));

    Expect(http_request.method == "GET", "single lookup should use GET");
    Expect(http_request.headers.at("User-Agent") == "FieldAgent/2.0", "field user agent should win");
    Expect(http_request.headers.at("Origin") == "https://app.example.com", "origin should be included");
    Expect(http_request.url.find("include=security%2Cabuse") != std::string::npos,
           "include values should be encoded");
}

void TestBuildLookupRequestUsesHeaderUserAgentFallbackAndEncodesOptions() {
    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";

    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "ipgeolocation.io";
    request.lang = ipgeolocation::Language::kKo;
    request.headers = {{"user-agent", "HeaderAgent/9.0"}};
    request.fields = {"ip", "location.country_name"};
    request.excludes = {"time_zone"};

    const auto http_request = ipgeolocation::internal::BuildLookupHttpRequest(
        ipgeolocation::internal::NormalizeConfig(config),
        ipgeolocation::internal::NormalizeLookupRequest(request));

    Expect(http_request.headers.at("User-Agent") == "HeaderAgent/9.0",
           "header user-agent should be reused when request user_agent is unset");
    Expect(http_request.url.find("lang=ko") != std::string::npos, "language should be encoded in the query string");
    Expect(http_request.url.find("fields=ip%2Clocation.country_name") != std::string::npos,
           "fields should be encoded in the query string");
    Expect(http_request.url.find("excludes=time_zone") != std::string::npos,
           "excludes should be encoded in the query string");
}

void TestBuildBulkRequestSetsJsonBodyAndHeaders() {
    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";

    ipgeolocation::BulkLookupIpGeolocationRequest request;
    request.ips = {"8.8.8.8", "ipgeolocation.io"};
    request.output = ipgeolocation::ResponseFormat::kXml;

    const auto http_request = ipgeolocation::internal::BuildBulkHttpRequest(
        ipgeolocation::internal::NormalizeConfig(config),
        ipgeolocation::internal::NormalizeBulkLookupRequest(request));

    Expect(http_request.method == "POST", "bulk lookup should use POST");
    Expect(http_request.headers.at("Content-Type") == "application/json", "bulk lookup should send JSON body");
    Expect(http_request.headers.at("Accept") == "application/xml", "xml output should request XML");
    Expect(http_request.body == "{\"ips\":[\"8.8.8.8\",\"ipgeolocation.io\"]}", "bulk request body should match");
}

void TestInternalHelpersCoverEnumsFactoriesAndEscaping() {
    Expect(ipgeolocation::internal::WireValue(ipgeolocation::ResponseFormat::kJson) == "json",
           "json wire value should be json");
    Expect(ipgeolocation::internal::WireValue(ipgeolocation::ResponseFormat::kXml) == "xml",
           "xml wire value should be xml");
    Expect(ipgeolocation::internal::AcceptHeaderFor(ipgeolocation::ResponseFormat::kJson) == "application/json",
           "json accept header should match");
    Expect(ipgeolocation::internal::AcceptHeaderFor(ipgeolocation::ResponseFormat::kXml) == "application/xml",
           "xml accept header should match");

    const std::vector<std::pair<ipgeolocation::Language, std::string>> languages = {
        {ipgeolocation::Language::kEn, "en"},
        {ipgeolocation::Language::kDe, "de"},
        {ipgeolocation::Language::kRu, "ru"},
        {ipgeolocation::Language::kJa, "ja"},
        {ipgeolocation::Language::kFr, "fr"},
        {ipgeolocation::Language::kCn, "cn"},
        {ipgeolocation::Language::kEs, "es"},
        {ipgeolocation::Language::kCs, "cs"},
        {ipgeolocation::Language::kIt, "it"},
        {ipgeolocation::Language::kKo, "ko"},
        {ipgeolocation::Language::kFa, "fa"},
        {ipgeolocation::Language::kPt, "pt"},
    };
    for (const auto& [language, code] : languages) {
        Expect(ipgeolocation::internal::LanguageCode(language) == code,
               "language enum should map to the expected wire code");
    }
    Expect(ipgeolocation::internal::LanguageCode(static_cast<ipgeolocation::Language>(999)) == "en",
           "unknown language enum values should fall back to english");

    const std::string escaped = ipgeolocation::internal::EscapeJsonString(
        std::string("\"\\") + '\b' + '\f' + '\n' + '\r' + '\t' + "x");
    Expect(escaped == "\\\"\\\\\\b\\f\\n\\r\\tx", "json string escaping should cover control characters");

    const std::string escaped_c0_controls =
        ipgeolocation::internal::EscapeJsonString(std::string("\x01\x1F", 2));
    Expect(escaped_c0_controls == "\\u0001\\u001F",
           "json string escaping should cover the remaining C0 control characters");

    const std::string body = ipgeolocation::internal::BuildBulkRequestBody(
        {"8.8.8.8", std::string("bad\"quote\\path\tline\nbreak\rreturn") + std::string("\x01", 1)});
    Expect(body == "{\"ips\":[\"8.8.8.8\",\"bad\\\"quote\\\\path\\tline\\nbreak\\rreturn\\u0001\"]}",
           "bulk request body should escape quotes, slashes, and control characters");

    ScopedMockTransportFactory factory({});

    auto mock_transport = factory.CreateTransport();
    Expect(dynamic_cast<MockTransport*>(mock_transport.get()) != nullptr,
           "test helper should create the configured mock transport");

    auto default_transport = ipgeolocation::internal::CreateDefaultTransport();
    Expect(dynamic_cast<ipgeolocation::internal::CurlHttpTransport*>(default_transport.get()) != nullptr,
           "default transport creation should still use the curl transport");
}

void TestMetadataParsesCreditsAndSuccessfulRecords() {
    std::map<std::string, std::vector<std::string>> headers = {
        {"X-Credits-Charged", {"2"}},
        {"X-Successful-Record", {"3"}},
    };

    const auto metadata = ipgeolocation::internal::ToMetadata(200, 42, headers);
    Expect(metadata.credits_charged.has_value() && *metadata.credits_charged == 2, "credits should parse");
    Expect(metadata.successful_records.has_value() && *metadata.successful_records == 3,
           "successful records should parse from singular header");
}

void TestApiErrorUsesJsonMessage() {
    const auto error = ipgeolocation::internal::ToApiError(401, R"({"error":{"message":"Unauthorized"}})");
    Expect(error.status_code() == 401, "status code should be preserved");
    Expect(std::string(error.what()) == "Unauthorized", "json error message should be extracted");
}

void TestMetadataIgnoresInvalidCreditHeader() {
    std::map<std::string, std::vector<std::string>> headers = {
        {"x-credits-charged", {"nope"}},
        {"X-Successful-Records", {"4"}},
    };

    const auto metadata = ipgeolocation::internal::ToMetadata(200, 42, headers);
    Expect(!metadata.credits_charged.has_value(), "invalid credit header should be ignored");
    Expect(metadata.successful_records.has_value() && *metadata.successful_records == 4,
           "successful records should parse case-insensitively");
}

void TestMetadataIgnoresInvalidSuccessfulRecordsHeader() {
    std::map<std::string, std::vector<std::string>> headers = {
        {"X-Successful-Records", {"NaN"}},
    };

    const auto metadata = ipgeolocation::internal::ToMetadata(200, 1, headers);
    Expect(!metadata.successful_records.has_value(), "invalid successful-records header should be ignored");
}

void TestMetadataAndErrorHelpersCoverAdditionalFallbacks() {
    {
        std::map<std::string, std::vector<std::string>> headers = {
            {"X-Credits-Chargex", {"2"}},
            {"X-Credits-Charged", {}},
        };

        const auto metadata = ipgeolocation::internal::ToMetadata(200, 5, headers);
        Expect(!metadata.credits_charged.has_value(), "empty matching header values should be ignored");
    }

    const std::vector<std::pair<int, std::string>> statuses = {
        {400, "bad request"},
        {401, "unauthorized"},
        {403, "forbidden"},
        {405, "method not allowed"},
        {413, "content too large"},
        {415, "unsupported media type"},
        {423, "locked"},
        {429, "too many requests"},
        {499, "client closed request"},
        {500, "internal server error"},
    };

    for (const auto& [status, message] : statuses) {
        const auto error = ipgeolocation::internal::ToApiError(status, "");
        Expect(std::string(error.what()) == message, "generic status mapping should stay stable");
    }

    Expect(ipgeolocation::internal::ExtractApiMessage(R"("simple error")") == "simple error",
           "top-level JSON strings should be returned directly");
}

void TestApiErrorFallsBackToNestedDetailAndGenericMessages() {
    const auto nested = ipgeolocation::internal::ToApiError(423, R"({"detail":{"message":"Temporarily locked"}})");
    Expect(std::string(nested.what()) == "Temporarily locked", "nested detail message should be extracted");

    const auto generic = ipgeolocation::internal::ToApiError(404, "");
    Expect(std::string(generic.what()) == "not found", "empty body should fall back to generic status message");

    const auto unknown = ipgeolocation::internal::ToApiError(599, "");
    Expect(std::string(unknown.what()) == "api request failed", "unknown status should fall back to generic api message");
}

void TestApiErrorPreservesResponseBody() {
    const auto error = ipgeolocation::internal::ToApiError(400, R"({"message":"Bad request"})");
    Expect(error.response_body() == R"({"message":"Bad request"})", "api error should preserve the raw response body");
}

void TestExtractApiMessageReturnsTrimmedTextWhenBodyIsNotJson() {
    const auto message = ipgeolocation::internal::ExtractApiMessage("  plain text body  ");
    Expect(message == "plain text body", "non-json body should return trimmed text");
}

void TestParseIpGeolocationResponseRejectsNonObjectPayload() {
    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseIpGeolocationResponse("[]"));
        },
        "Failed to deserialize API response: expected an object payload");
}

void TestParseBulkLookupRejectsInvalidShapes() {
    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseBulkLookupResults("{}"));
        },
        "Failed to deserialize bulk response: expected an array payload");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseBulkLookupResults(R"([1])"));
        },
        "Failed to deserialize bulk response: expected object items");
}

void TestParseResponseToleratesWrongTypesAndSkipsNonStringArrayItems() {
    const auto response = ipgeolocation::internal::ParseIpGeolocationResponse(
        R"({
            "ip":"8.8.8.8",
            "location":"not-an-object",
            "country_metadata":{"languages":[1,"en",true]},
            "security":"bad",
            "abuse":"bad",
            "user_agent":"bad"
        })");

    Expect(response.ip.has_value() && *response.ip == "8.8.8.8", "ip should still parse");
    Expect(!response.location.has_value(), "wrong-type location should be ignored");
    Expect(response.country_metadata.has_value() && response.country_metadata->languages.has_value(),
           "country metadata languages should still parse");
    Expect(response.country_metadata->languages->size() == 1 &&
               response.country_metadata->languages->front() == "en",
           "non-string language items should be skipped");
    Expect(!response.security.has_value(), "wrong-type security should be ignored");
    Expect(!response.abuse.has_value(), "wrong-type abuse should be ignored");
    Expect(!response.user_agent.has_value(), "wrong-type user agent should be ignored");
}

void TestParseResponseIgnoresWrongNestedObjectTypesAcrossFamilies() {
    const auto response = ipgeolocation::internal::ParseIpGeolocationResponse(
        R"({
            "ip":"8.8.8.8",
            "country_metadata":{"languages":"en"},
            "currency":"usd",
            "network":"wifi",
            "asn":"AS15169",
            "company":"Google",
            "time_zone":{"dst_start":"bad","dst_end":[]},
            "security":{"proxy_provider_names":"bad","vpn_provider_names":"bad"},
            "user_agent":{"device":"bad","engine":1,"operating_system":false},
            "abuse":{"emails":"bad","phone_numbers":"bad"}
        })");

    Expect(response.country_metadata.has_value(), "country metadata object should still parse");
    Expect(!response.country_metadata->languages.has_value(), "non-array languages should be ignored");
    Expect(!response.currency.has_value(), "wrong-type currency should be ignored");
    Expect(!response.network.has_value(), "wrong-type network should be ignored");
    Expect(!response.asn.has_value(), "wrong-type asn should be ignored");
    Expect(!response.company.has_value(), "wrong-type company should be ignored");
    Expect(response.time_zone.has_value(), "time zone object should still parse");
    Expect(!response.time_zone->dst_start.has_value(), "wrong-type dst_start should be ignored");
    Expect(!response.time_zone->dst_end.has_value(), "wrong-type dst_end should be ignored");
    Expect(response.security.has_value(), "security object should still parse");
    Expect(!response.security->proxy_provider_names.has_value(), "non-array proxy providers should be ignored");
    Expect(!response.security->vpn_provider_names.has_value(), "non-array vpn providers should be ignored");
    Expect(response.user_agent.has_value(), "user agent object should still parse");
    Expect(!response.user_agent->device.has_value(), "wrong-type user agent device should be ignored");
    Expect(!response.user_agent->engine.has_value(), "wrong-type user agent engine should be ignored");
    Expect(!response.user_agent->operating_system.has_value(), "wrong-type user agent operating system should be ignored");
    Expect(response.abuse.has_value(), "abuse object should still parse");
    Expect(!response.abuse->emails.has_value(), "non-array abuse emails should be ignored");
    Expect(!response.abuse->phone_numbers.has_value(), "non-array abuse phone numbers should be ignored");
}

void TestParseResponseTreatsNullNestedObjectsAsAbsent() {
    const auto response = ipgeolocation::internal::ParseIpGeolocationResponse(
        R"({
            "ip":"8.8.8.8",
            "location":null,
            "country_metadata":{"languages":null},
            "time_zone":{"dst_start":null,"dst_end":null},
            "security":null,
            "user_agent":{"device":null,"engine":null,"operating_system":null},
            "abuse":{"emails":null,"phone_numbers":null}
        })");

    Expect(!response.location.has_value(), "null nested objects should be treated as absent");
    Expect(response.country_metadata.has_value() && !response.country_metadata->languages.has_value(),
           "null string arrays should be treated as absent");
    Expect(response.time_zone.has_value() && !response.time_zone->dst_start.has_value() &&
               !response.time_zone->dst_end.has_value(),
           "null nested DST objects should be treated as absent");
    Expect(!response.security.has_value(), "null security object should be treated as absent");
    Expect(response.user_agent.has_value() && !response.user_agent->device.has_value() &&
               !response.user_agent->engine.has_value() &&
               !response.user_agent->operating_system.has_value(),
           "null user agent families should be treated as absent");
    Expect(response.abuse.has_value() && !response.abuse->emails.has_value() &&
               !response.abuse->phone_numbers.has_value(),
           "null abuse arrays should be treated as absent");
}

void TestBulkLookupResultIsSuccessCoversMixedShape() {
    ipgeolocation::BulkLookupResult result;
    result.data = ipgeolocation::IpGeolocationResponse{};
    result.error = ipgeolocation::BulkLookupError{std::string("both-set")};
    Expect(!result.is_success(), "results with both data and error should not count as success");
}

void TestParseBulkLookupParsesDirectMessageErrors() {
    const auto response = ipgeolocation::internal::ParseBulkLookupResults(
        R"([
            {"message":"Plain bulk failure"},
            {"ip":"8.8.8.8"}
        ])");

    Expect(response.size() == 2, "bulk parser should return both items");
    Expect(response[0].error.has_value() && response[0].error->message.has_value() &&
               *response[0].error->message == "Plain bulk failure",
           "direct top-level bulk message should map to error.message");
    Expect(response[1].data.has_value() && response[1].data->ip.has_value() &&
               *response[1].data->ip == "8.8.8.8",
           "direct bulk success should parse");
}

void TestParseBulkLookupUsesFallbackMessageForMalformedErrorItems() {
    const auto response = ipgeolocation::internal::ParseBulkLookupResults(
        R"([
            {"error":{}},
            {"error":null},
            {"error":{"code":123}}
        ])");

    Expect(response.size() == 3, "bulk parser should keep malformed error items");
    for (const auto& result : response) {
        Expect(!result.data.has_value(), "malformed bulk errors should not fall through to success parsing");
        Expect(result.error.has_value() && result.error->message.has_value(),
               "malformed bulk errors should still expose error.message");
        Expect(*result.error->message == "bulk item returned an error without a parsable message",
               "malformed bulk errors should use the fallback error message");
    }
}

void TestParseBulkLookupPrefersSuccessMarkersOverTopLevelMessage() {
    const auto response = ipgeolocation::internal::ParseBulkLookupResults(
        R"([
            {
                "ip":"8.8.8.8",
                "message":"not-an-error",
                "location":{"country_name":"United States"}
            }
        ])");

    Expect(response.size() == 1, "bulk parser should keep success items with informational message fields");
    Expect(response[0].data.has_value(), "success markers should win over a top-level message field");
    Expect(!response[0].error.has_value(), "success markers should prevent accidental error classification");
    Expect(response[0].data->ip.has_value() && *response[0].data->ip == "8.8.8.8",
           "bulk success item should still parse correctly");
}

void TestParseBulkLookupRejectsWrappedSuccessItems() {
    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseBulkLookupResults(
                R"([{"data":{"ip":"8.8.8.8"}}])"));
        },
        "Failed to deserialize bulk response: unexpected wrapped success item");
}

void TestTypedSingleLookupParsesRepresentativeFields() {
    ScopedMockTransportFactory factory({
        {
            200,
            R"({
                "ip":"8.8.8.8",
                "hostname":"dns.google",
                "domain":"dns.google",
                "location":{
                    "country_name":"United States",
                    "city":"Mountain View",
                    "country_code2":"US"
                },
                "country_metadata":{
                    "calling_code":"+1",
                    "languages":["en"]
                },
                "asn":{
                    "as_number":"AS15169",
                    "organization":"Google LLC"
                },
                "company":{
                    "name":"Google LLC",
                    "type":"business"
                },
                "time_zone":{
                    "name":"America/Los_Angeles",
                    "current_timezone_abbreviation":"PDT",
                    "current_timezone_name":"Pacific Daylight Time",
                    "timezone_abbreviation":"PST",
                    "timezone_name":"Pacific Standard Time",
                    "is_dst":true
                },
                "security":{
                    "is_proxy":false,
                    "threat_score":0.1,
                    "vpn_provider_names":["ExampleVPN"]
                },
                "user_agent":{
                    "user_agent_string":"Mozilla/5.0",
                    "name":"Chrome",
                    "version_major":"146",
                    "operating_system":{
                        "name":"macOS",
                        "type":"desktop",
                        "build":"23A344"
                    }
                },
                "abuse":{
                    "country":"US",
                    "emails":["abuse@example.com"]
                }
            })",
            {
                {"X-Credits-Charged", {"1"}},
                {"X-Successful-Records", {"1"}},
            },
        },
    });

    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";
    ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

    const auto response = client.LookupIpGeolocation();
    Expect(response.data.ip.has_value() && *response.data.ip == "8.8.8.8", "typed single ip should parse");
    Expect(response.data.location.has_value() && response.data.location->country_name.has_value() &&
               *response.data.location->country_name == "United States",
           "location country should parse");
    Expect(response.data.country_metadata.has_value() && response.data.country_metadata->languages.has_value() &&
               response.data.country_metadata->languages->size() == 1,
           "country metadata languages should parse");
    Expect(response.data.time_zone.has_value() &&
               response.data.time_zone->standard_tz_abbreviation.has_value() &&
               *response.data.time_zone->standard_tz_abbreviation == "PST",
           "time zone alias fallback should parse");
    Expect(response.data.user_agent.has_value() &&
               response.data.user_agent->operating_system.has_value() &&
               response.data.user_agent->operating_system->build.has_value() &&
               *response.data.user_agent->operating_system->build == "23A344",
           "user agent operating system should parse");
    Expect(response.metadata.credits_charged.has_value() && *response.metadata.credits_charged == 1,
           "typed metadata should parse");
    Expect(factory.state()->requests.size() == 1, "typed single lookup should issue one request");
}

void TestTypedSingleLookupParsesExhaustiveRepresentativeResponse() {
    ScopedMockTransportFactory factory({
        {
            200,
            R"({
                "ip":"8.8.8.8",
                "hostname":"dns.google",
                "domain":"dns.google",
                "location":{
                    "continent_code":"NA",
                    "continent_name":"North America",
                    "country_code2":"US",
                    "country_code3":"USA",
                    "country_name":"United States",
                    "country_name_official":"United States of America",
                    "country_capital":"Washington, D.C.",
                    "state_prov":"California",
                    "state_code":"CA",
                    "district":"Santa Clara",
                    "city":"Mountain View",
                    "locality":"North Bayshore",
                    "accuracy_radius":"5",
                    "confidence":"90",
                    "dma_code":"807",
                    "zipcode":"94043",
                    "latitude":"37.4220",
                    "longitude":"-122.0841",
                    "is_eu":false,
                    "country_flag":"https://flags.example/us.png",
                    "geoname_id":"5375480",
                    "country_emoji":"🇺🇸"
                },
                "country_metadata":{
                    "calling_code":"+1",
                    "tld":".us",
                    "languages":["en","es"]
                },
                "currency":{
                    "code":"USD",
                    "name":"US Dollar",
                    "symbol":"$"
                },
                "network":{
                    "connection_type":"broadband",
                    "route":"8.8.8.0/24",
                    "is_anycast":true
                },
                "asn":{
                    "as_number":"AS15169",
                    "organization":"Google LLC",
                    "country":"US",
                    "type":"isp",
                    "domain":"google.com",
                    "date_allocated":"2000-03-30",
                    "rir":"ARIN"
                },
                "company":{
                    "name":"Google LLC",
                    "type":"business",
                    "domain":"google.com"
                },
                "time_zone":{
                    "name":"America/Los_Angeles",
                    "offset":-8,
                    "offset_with_dst":-7,
                    "current_time":"2026-04-18 01:02:03",
                    "current_time_unix":1776474123,
                    "current_tz_abbreviation":"PDT",
                    "current_tz_full_name":"Pacific Daylight Time",
                    "standard_tz_abbreviation":"PST",
                    "standard_tz_full_name":"Pacific Standard Time",
                    "is_dst":true,
                    "dst_savings":1,
                    "dst_exists":true,
                    "dst_tz_abbreviation":"PDT",
                    "dst_tz_full_name":"Pacific Daylight Time",
                    "dst_start":{
                        "utc_time":"2026-03-08T10:00:00Z",
                        "duration":"+1:00",
                        "gap":true,
                        "date_time_after":"2026-03-08 03:00:00",
                        "date_time_before":"2026-03-08 01:59:59",
                        "overlap":false
                    },
                    "dst_end":{
                        "utc_time":"2026-11-01T09:00:00Z",
                        "duration":"-1:00",
                        "gap":false,
                        "date_time_after":"2026-11-01 01:00:00",
                        "date_time_before":"2026-11-01 01:59:59",
                        "overlap":true
                    }
                },
                "security":{
                    "threat_score":0.2,
                    "is_tor":false,
                    "is_proxy":true,
                    "proxy_provider_names":["Proxy A","Proxy B"],
                    "proxy_confidence_score":0.95,
                    "proxy_last_seen":"2026-04-17",
                    "is_residential_proxy":false,
                    "is_vpn":true,
                    "vpn_provider_names":["VPN A"],
                    "vpn_confidence_score":0.91,
                    "vpn_last_seen":"2026-04-16",
                    "is_relay":false,
                    "relay_provider_name":"Relay X",
                    "is_anonymous":true,
                    "is_known_attacker":false,
                    "is_bot":false,
                    "is_spam":true,
                    "is_cloud_provider":true,
                    "cloud_provider_name":"Example Cloud"
                },
                "user_agent":{
                    "user_agent_string":"Mozilla/5.0",
                    "name":"Chrome",
                    "type":"browser",
                    "version":"146.0.1",
                    "version_major":"146",
                    "device":{
                        "name":"Mac",
                        "type":"desktop",
                        "brand":"Apple",
                        "cpu":"x86_64"
                    },
                    "engine":{
                        "name":"Blink",
                        "type":"browser-engine",
                        "version":"146.0.0",
                        "version_major":"146"
                    },
                    "operating_system":{
                        "name":"macOS",
                        "type":"desktop",
                        "version":"14.5",
                        "version_major":"14",
                        "build":"23F79"
                    }
                },
                "abuse":{
                    "route":"8.8.8.0/24",
                    "country":"US",
                    "name":"Google Abuse",
                    "organization":"Google LLC",
                    "kind":"abuse",
                    "address":"1600 Amphitheatre Parkway",
                    "emails":["abuse@example.com"],
                    "phone_numbers":["+1-555-0100"]
                }
            })",
            {},
        },
    });

    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";
    ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

    const auto response = client.LookupIpGeolocation();
    Expect(response.data.location.has_value() && response.data.location->country_code3.has_value() &&
               *response.data.location->country_code3 == "USA",
           "location fields should parse exhaustively");
    Expect(response.data.currency.has_value() && response.data.currency->code.has_value() &&
               *response.data.currency->code == "USD",
           "currency should parse");
    Expect(response.data.network.has_value() && response.data.network->is_anycast.has_value() &&
               *response.data.network->is_anycast,
           "network should parse");
    Expect(response.data.asn.has_value() && response.data.asn->rir.has_value() &&
               *response.data.asn->rir == "ARIN",
           "asn should parse");
    Expect(response.data.company.has_value() && response.data.company->domain.has_value() &&
               *response.data.company->domain == "google.com",
           "company should parse");
    Expect(response.data.time_zone.has_value() && response.data.time_zone->dst_end.has_value() &&
               response.data.time_zone->dst_end->overlap.has_value() &&
               *response.data.time_zone->dst_end->overlap,
           "time zone transitions should parse");
    Expect(response.data.security.has_value() && response.data.security->cloud_provider_name.has_value() &&
               *response.data.security->cloud_provider_name == "Example Cloud",
           "security should parse");
    Expect(response.data.user_agent.has_value() && response.data.user_agent->engine.has_value() &&
               response.data.user_agent->engine->name.has_value() &&
               *response.data.user_agent->engine->name == "Blink",
           "user agent engine should parse");
    Expect(response.data.user_agent->device.has_value() &&
               response.data.user_agent->device->brand.has_value() &&
               *response.data.user_agent->device->brand == "Apple",
           "user agent device should parse");
    Expect(response.data.abuse.has_value() && response.data.abuse->phone_numbers.has_value() &&
               response.data.abuse->phone_numbers->front() == "+1-555-0100",
           "abuse should parse");
}

void TestTypedMethodsRejectXmlBeforeTransport() {
    ScopedMockTransportFactory factory({});

    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";
    ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

    ipgeolocation::LookupIpGeolocationRequest request;
    request.output = ipgeolocation::ResponseFormat::kXml;

    ExpectThrows<ipgeolocation::ValidationError>(
        [&client, &request]() {
            static_cast<void>(client.LookupIpGeolocation(request));
        },
        "typed methods support JSON only");
    Expect(factory.state()->requests.empty(), "typed xml rejection should happen before transport");
}

void TestTypedBulkLookupParsesSuccessAndErrorShapes() {
    ScopedMockTransportFactory factory({
        {
            200,
            R"([
                {
                    "ip":"8.8.8.8",
                    "location":{"country_name":"United States"}
                },
                {
                    "error":{"message":"Lookup failed"}
                },
                {
                    "ip":"1.1.1.1",
                    "location":{"country_name":"Australia"}
                }
            ])",
            {
                {"X-Successful-Record", {"2"}},
            },
        },
    });

    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";
    ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

    ipgeolocation::BulkLookupIpGeolocationRequest request;
    request.ips = {"8.8.8.8", "bad.example", "1.1.1.1"};

    const auto response = client.BulkLookupIpGeolocation(request);
    Expect(response.data.size() == 3, "typed bulk should keep item count");
    Expect(response.data[0].is_success(), "first bulk result should be success");
    Expect(response.data[0].data.has_value() && response.data[0].data->ip.has_value() &&
               *response.data[0].data->ip == "8.8.8.8",
           "first bulk result data should parse");
    Expect(!response.data[1].is_success() && response.data[1].error.has_value() &&
               response.data[1].error->message.has_value() &&
               *response.data[1].error->message == "Lookup failed",
           "bulk error should use error.message");
    Expect(response.data[2].data.has_value() && response.data[2].data->ip.has_value() &&
               *response.data[2].data->ip == "1.1.1.1",
           "direct bulk success should parse");
    Expect(response.metadata.successful_records.has_value() && *response.metadata.successful_records == 2,
           "bulk metadata should parse");
}

void TestTypedSingleLookupInvalidJsonThrowsSerializationError() {
    ScopedMockTransportFactory factory({
        {
            200,
            "{not-json}",
            {},
        },
    });

    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";
    ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

    ExpectThrows<ipgeolocation::SerializationError>(
        [&client]() {
            static_cast<void>(client.LookupIpGeolocation());
        },
        "Failed to parse JSON: expected string object key");
}

void TestRawSingleLookupThrowsApiErrorOnNonSuccessStatus() {
    ScopedMockTransportFactory factory({
        {
            404,
            "",
            {},
        },
    });

    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";
    ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

    try {
        static_cast<void>(client.LookupIpGeolocationRaw());
    } catch (const ipgeolocation::ApiError& error) {
        Expect(error.status_code() == 404, "raw single error should preserve status");
        Expect(std::string(error.what()) == "not found", "raw single error should use generic fallback");
        return;
    }

    throw std::runtime_error("expected ApiError for raw single non-2xx response");
}

void TestRawBulkLookupThrowsApiErrorOnNonSuccessStatus() {
    ScopedMockTransportFactory factory({
        {
            423,
            R"({"detail":{"message":"Locked now"}})",
            {},
        },
    });

    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";
    ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

    ipgeolocation::BulkLookupIpGeolocationRequest request;
    request.ips = {"8.8.8.8"};

    try {
        static_cast<void>(client.BulkLookupIpGeolocationRaw(request));
    } catch (const ipgeolocation::ApiError& error) {
        Expect(error.status_code() == 423, "raw bulk error should preserve status");
        Expect(std::string(error.what()) == "Locked now", "raw bulk error should extract nested detail message");
        return;
    }

    throw std::runtime_error("expected ApiError for raw bulk non-2xx response");
}

void TestRawAndBulkMethodsEnforceAuthRules() {
    ScopedMockTransportFactory factory({});

    ipgeolocation::IpGeolocationClient client_without_auth =
        factory.CreateClient(ipgeolocation::IpGeolocationClientConfig{});
    ExpectThrows<ipgeolocation::ValidationError>(
        [&client_without_auth]() {
            static_cast<void>(client_without_auth.LookupIpGeolocationRaw());
        },
        "single lookup requires apiKey or requestOrigin in client config");

    ipgeolocation::IpGeolocationClientConfig origin_only_config;
    origin_only_config.request_origin = "https://app.example.com";
    ipgeolocation::IpGeolocationClient origin_only_client = factory.CreateClient(origin_only_config);

    ipgeolocation::BulkLookupIpGeolocationRequest bulk_request;
    bulk_request.ips = {"8.8.8.8"};

    ExpectThrows<ipgeolocation::ValidationError>(
        [&origin_only_client, &bulk_request]() {
            static_cast<void>(origin_only_client.BulkLookupIpGeolocationRaw(bulk_request));
        },
        "bulk lookup requires apiKey in client config");
}

void TestClientCoversAdditionalTypedAndBulkPaths() {
    {
        ScopedMockTransportFactory factory({
            {401, R"({"error":{"message":"Denied"}})", {}},
        });

        ipgeolocation::IpGeolocationClientConfig config;
        config.api_key = "secret";
        ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

        ExpectThrows<ipgeolocation::ApiError>(
            [&client]() {
                static_cast<void>(client.LookupIpGeolocation());
            },
            "Denied");
    }

    {
        ScopedMockTransportFactory factory({
            {429, R"({"message":"Rate limited"})", {}},
        });

        ipgeolocation::IpGeolocationClientConfig config;
        config.api_key = "secret";
        ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

        ipgeolocation::BulkLookupIpGeolocationRequest request;
        request.ips = {"8.8.8.8"};
        ExpectThrows<ipgeolocation::ApiError>(
            [&client, &request]() {
                static_cast<void>(client.BulkLookupIpGeolocation(request));
            },
            "Rate limited");
    }

    {
        ScopedMockTransportFactory factory({
            {200, R"([{"ip":"8.8.8.8"}])", {}},
        });

        ipgeolocation::IpGeolocationClientConfig config;
        config.api_key = "secret";
        ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

        ipgeolocation::BulkLookupIpGeolocationRequest request;
        request.ips = {"8.8.8.8"};
        const auto response = client.BulkLookupIpGeolocationRaw(request);
        Expect(response.data == R"([{"ip":"8.8.8.8"}])", "raw bulk success path should return the raw body");
    }

    {
        ipgeolocation::IpGeolocationClientConfig config;
        config.api_key = "secret";

        ipgeolocation::IpGeolocationClient client(config);
        client.Close();

        ExpectThrows<ipgeolocation::ClientClosedError>(
            [&client]() {
                static_cast<void>(client.LookupIpGeolocation());
            },
            "client is closed");

        ipgeolocation::BulkLookupIpGeolocationRequest request;
        request.ips = {"8.8.8.8"};
        ExpectThrows<ipgeolocation::ClientClosedError>(
            [&client, &request]() {
                static_cast<void>(client.BulkLookupIpGeolocation(request));
            },
            "client is closed");
        ExpectThrows<ipgeolocation::ClientClosedError>(
            [&client, &request]() {
                static_cast<void>(client.BulkLookupIpGeolocationRaw(request));
            },
            "client is closed");
    }

    {
        ipgeolocation::IpGeolocationClient no_auth_client(ipgeolocation::IpGeolocationClientConfig{});
        ExpectThrows<ipgeolocation::ValidationError>(
            [&no_auth_client]() {
                static_cast<void>(no_auth_client.LookupIpGeolocation());
            },
            "single lookup requires apiKey or requestOrigin in client config");

        ipgeolocation::IpGeolocationClientConfig origin_only_config;
        origin_only_config.request_origin = "https://app.example.com";
        ipgeolocation::IpGeolocationClient origin_only_client(origin_only_config);
        ipgeolocation::BulkLookupIpGeolocationRequest request;
        request.ips = {"8.8.8.8"};
        ExpectThrows<ipgeolocation::ValidationError>(
            [&origin_only_client, &request]() {
                static_cast<void>(origin_only_client.BulkLookupIpGeolocation(request));
            },
            "bulk lookup requires apiKey in client config");
    }
}

void TestTypedBulkRejectsXmlBeforeTransport() {
    ScopedMockTransportFactory factory({});

    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";
    ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

    ipgeolocation::BulkLookupIpGeolocationRequest request;
    request.ips = {"8.8.8.8"};
    request.output = ipgeolocation::ResponseFormat::kXml;

    ExpectThrows<ipgeolocation::ValidationError>(
        [&client, &request]() {
            static_cast<void>(client.BulkLookupIpGeolocation(request));
        },
        "typed methods support JSON only");
    Expect(factory.state()->requests.empty(), "typed bulk xml rejection should happen before transport");
}

void TestRequestOriginOnlySingleLookupWorks() {
    ScopedMockTransportFactory factory({
        {
            200,
            R"({"ip":"8.8.8.8"})",
            {},
        },
    });

    ipgeolocation::IpGeolocationClientConfig config;
    config.request_origin = "https://app.example.com";
    ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

    const auto response = client.LookupIpGeolocationRaw();
    Expect(response.data == R"({"ip":"8.8.8.8"})", "request-origin-only lookup should succeed");
    Expect(factory.state()->requests.size() == 1, "request-origin-only lookup should send one request");
    Expect(factory.state()->requests.front().headers.at("Origin") == "https://app.example.com",
           "request-origin-only lookup should send the origin header");
}

void TestResolveUserAgentHeaderUsesDefaultsAndHeaderFallback() {
    const std::map<std::string, std::string> no_headers;
    Expect(
        ipgeolocation::internal::ResolveUserAgentHeader(std::nullopt, no_headers) ==
            ipgeolocation::IpGeolocationClient::DefaultUserAgent(),
        "default user agent should be used when neither request field nor header is set");

    const std::map<std::string, std::string> header_only = {
        {"user-agent", "HeaderAgent/1.0"},
    };
    Expect(
        ipgeolocation::internal::ResolveUserAgentHeader(std::nullopt, header_only) == "HeaderAgent/1.0",
        "existing user-agent header should be reused case-insensitively");

    const std::map<std::string, std::string> mismatched_same_length = {
        {"User-Agenx", "Wrong"},
    };
    Expect(
        ipgeolocation::internal::ResolveUserAgentHeader(std::nullopt, mismatched_same_length) ==
            ipgeolocation::IpGeolocationClient::DefaultUserAgent(),
        "same-length non-matching headers should not be treated as user-agent");
}

void TestCloseIsIdempotentAndMoveSemanticsKeepClientUsable() {
    ScopedMockTransportFactory factory({
        {
            200,
            R"({"ip":"8.8.8.8"})",
            {},
        },
    });

    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";

    ipgeolocation::IpGeolocationClient original = factory.CreateClient(config);
    ipgeolocation::IpGeolocationClient moved(std::move(original));

    Expect(original.closed(), "moved-from client should report closed");
    Expect(!moved.closed(), "moved-to client should remain open");
    ExpectThrows<ipgeolocation::ClientClosedError>(
        [&original]() {
            static_cast<void>(original.LookupIpGeolocationRaw());
        },
        "client is closed");

    const auto response = moved.LookupIpGeolocationRaw();
    Expect(response.data == R"({"ip":"8.8.8.8"})", "moved-to client should still work");

    moved.Close();
    moved.Close();
    Expect(moved.closed(), "close should be idempotent");
    Expect(factory.state()->closed, "transport should be closed once");
}

void TestMoveAssignmentKeepsDestinationUsable() {
    ScopedMockTransportFactory factory({
        {200, R"({"ip":"1.1.1.1"})", {}},
    });

    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";

    ipgeolocation::IpGeolocationClient source = factory.CreateClient(config);
    ipgeolocation::IpGeolocationClient destination = factory.CreateClient(config);
    destination = std::move(source);

    Expect(source.closed(), "moved-from source should report closed");
    source.Close();
    const auto response = destination.LookupIpGeolocationRaw();
    Expect(response.data == R"({"ip":"1.1.1.1"})", "move-assigned client should remain usable");
}

void TestSharedClientSupportsConcurrentRequests() {
    ScopedConcurrentTransportFactory factory({
        {200, R"({"ip":"8.8.8.8"})", {}},
        {200, R"({"ip":"1.1.1.1"})", {}},
        {200, R"({"ip":"9.9.9.9"})", {}},
        {200, R"({"ip":"208.67.222.222"})", {}},
    });

    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";
    ipgeolocation::IpGeolocationClient client = factory.CreateClient(config);

    std::vector<std::string> results(4);
    std::vector<std::thread> threads;
    threads.reserve(results.size());
    for (std::size_t index = 0; index < results.size(); ++index) {
        threads.emplace_back([&client, &results, index]() {
            results[index] = client.LookupIpGeolocationRaw().data;
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    std::size_t success_count = 0;
    for (const auto& result : results) {
        if (!result.empty()) {
            ++success_count;
        }
    }

    Expect(success_count == results.size(), "all concurrent lookups should complete successfully");
    Expect(factory.state()->requests.size() == results.size(), "all concurrent lookups should reach transport");
    Expect(factory.state()->peak_active_requests > 1,
           "concurrent client test should observe overlapping transport calls");
}

void TestMovedFromClientRejectsAllRequestEntryPointsSafely() {
    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";

    ipgeolocation::IpGeolocationClient source(config);
    ipgeolocation::IpGeolocationClient moved(std::move(source));
    static_cast<void>(moved);

    ipgeolocation::BulkLookupIpGeolocationRequest bulk_request;
    bulk_request.ips = {"8.8.8.8"};

    ExpectThrows<ipgeolocation::ClientClosedError>(
        [&source]() {
            static_cast<void>(source.LookupIpGeolocation());
        },
        "client is closed");
    ExpectThrows<ipgeolocation::ClientClosedError>(
        [&source]() {
            static_cast<void>(source.LookupIpGeolocationRaw());
        },
        "client is closed");
    ExpectThrows<ipgeolocation::ClientClosedError>(
        [&source, &bulk_request]() {
            static_cast<void>(source.BulkLookupIpGeolocation(bulk_request));
        },
        "client is closed");
    ExpectThrows<ipgeolocation::ClientClosedError>(
        [&source, &bulk_request]() {
            static_cast<void>(source.BulkLookupIpGeolocationRaw(bulk_request));
        },
        "client is closed");
}

void TestJsonParserCoversNullEscapesExponentAndFailures() {
    const auto null_value = ipgeolocation::internal::ParseJson("null");
    Expect(null_value.is_null(), "null literal should parse");
    Expect(null_value.type() == ipgeolocation::internal::JsonValue::Type::kNull, "null type should be preserved");

    const auto ascii_escape = ipgeolocation::internal::ParseJson(R"("\u0041")");
    Expect(ascii_escape.is_string() && ascii_escape.string_value() == "A", "ascii unicode escape should parse");

    const auto latin_escape = ipgeolocation::internal::ParseJson(R"("\u00e9")");
    Expect(latin_escape.is_string() && latin_escape.string_value() == "\xC3\xA9",
           "two-byte unicode escape should parse");

    const auto surrogate_pair_escape = ipgeolocation::internal::ParseJson(R"("\uD83D\uDE00")");
    Expect(surrogate_pair_escape.is_string() && surrogate_pair_escape.string_value() == "\xF0\x9F\x98\x80",
           "utf-16 surrogate pairs should parse into four-byte UTF-8");

    const auto escaped = ipgeolocation::internal::ParseJson(R"("\"\\\/\b\f\n\r\t")");
    Expect(escaped.is_string(), "escaped string should parse");

    const auto exponent = ipgeolocation::internal::ParseJson("1e3");
    Expect(exponent.is_number() && exponent.number_value() == 1000.0, "exponent number should parse");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson("{}{}"));
        },
        "Failed to parse JSON: unexpected trailing characters");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson(R"("\x")"));
        },
        "Failed to parse JSON: invalid escape sequence");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson("\""));
        },
        "Failed to parse JSON: unterminated string");
}

void TestJsonParserUsesLocaleIndependentNumberParsing() {
    ScopedNumericLocale locale_guard;
    static_cast<void>(locale_guard.TrySetCommaDecimalLocale());

    const auto parsed = ipgeolocation::internal::ParseJson(
        R"({"latitude":37.38605,"offset":-2.5e1})");
    Expect(parsed.is_object(), "locale-independent number test should parse an object");
    Expect(parsed.object_value().at("latitude").is_number() &&
               ApproximatelyEqual(parsed.object_value().at("latitude").number_value(), 37.38605),
           "fractional numbers should parse correctly under the active locale");
    Expect(parsed.object_value().at("offset").is_number() &&
               ApproximatelyEqual(parsed.object_value().at("offset").number_value(), -25.0),
           "exponent numbers should parse correctly under the active locale");
}

void TestJsonParserRejectsMoreInvalidForms() {
    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson(""));
        },
        "Failed to parse JSON: unexpected end of input");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson("@"));
        },
        "Failed to parse JSON: unexpected character");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson("truth"));
        },
        "Failed to parse JSON: invalid boolean");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson("1e+"));
        },
        "Failed to parse JSON: expected digits");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson(R"("\u00G0")"));
        },
        "Failed to parse JSON: invalid unicode escape");

    const auto euro = ipgeolocation::internal::ParseJson(R"("\u20AC")");
    Expect(euro.is_string() && euro.string_value() == "\xE2\x82\xAC",
           "three-byte unicode escape should parse");

    const auto negative = ipgeolocation::internal::ParseJson("-2");
    Expect(negative.is_number() && negative.number_value() == -2.0, "negative numbers should parse");

    const auto duplicate_keys = ipgeolocation::internal::ParseJson(R"({"key":"first","key":"second"})");
    Expect(duplicate_keys.is_object() &&
               duplicate_keys.object_value().at("key").is_string() &&
               duplicate_keys.object_value().at("key").string_value() == "second",
           "duplicate object keys should keep the last value");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson("nul"));
        },
        "Failed to parse JSON: expected 'null'");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson("\"abc\\"));
        },
        "Failed to parse JSON: unexpected end of escape sequence");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson(R"("\u12")"));
        },
        "Failed to parse JSON: incomplete unicode escape");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson(R"("\uD83D")"));
        },
        "Failed to parse JSON: invalid unicode surrogate pair");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson(R"("\uDE00")"));
        },
        "Failed to parse JSON: invalid unicode surrogate pair");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson(R"({"a" 1})"));
        },
        "Failed to parse JSON: expected ':'");

    ExpectThrows<ipgeolocation::SerializationError>(
        []() {
            static_cast<void>(ipgeolocation::internal::ParseJson("1e999999"));
        },
        "Failed to parse JSON: invalid number");

    std::string deeply_nested_array;
    deeply_nested_array.reserve(2 * 257 + 1);
    for (int count = 0; count < 257; ++count) {
        deeply_nested_array.push_back('[');
    }
    deeply_nested_array.push_back('0');
    for (int count = 0; count < 257; ++count) {
        deeply_nested_array.push_back(']');
    }

    ExpectThrows<ipgeolocation::SerializationError>(
        [&deeply_nested_array]() {
            static_cast<void>(ipgeolocation::internal::ParseJson(deeply_nested_array));
        },
        "Failed to parse JSON: maximum nesting depth exceeded");
}

void TestCurlTransportCapturesHeadersAndTrimsHeaderNameWhitespace() {
    LoopbackServer server([](int client_fd) {
        WriteAll(
            client_fd,
            "HTTP/1.1 200 OK\r\n"
            ":\tignored\r\n"
            "X-Test \t: value\r\n"
            "X-Test: second\r\n"
            "X-Tab:\ttrimmed\r\n"
            "\r\n"
            "ok");
    });

    ipgeolocation::internal::CurlHttpTransport transport;
    ipgeolocation::internal::HttpRequestData request;
    request.url = server.url();
    request.method = "GET";
    request.connect_timeout = std::chrono::milliseconds(1000);
    request.read_timeout = std::chrono::milliseconds(1000);

    const auto response = transport.Send(request, 1024);
    Expect(response.status_code == 200, "loopback transport GET should succeed");
    Expect(response.body == "ok", "loopback transport should read the response body");
    Expect(response.headers.at("X-Test").size() == 2, "loopback transport should keep duplicate headers");
    Expect(response.headers.at("X-Test").front() == "value", "loopback transport should trim header name whitespace");
    Expect(response.headers.at("X-Tab").front() == "trimmed",
           "loopback transport should trim leading tab whitespace in header values");
    Expect(response.headers.count("") == 0,
           "loopback transport should ignore response headers whose names normalize to empty");
}

void TestCurlTransportTimeoutAndSizeLimitErrors() {
    {
        LoopbackServer timeout_server([](int client_fd) {
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            WriteAll(client_fd, "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok");
        });

        ipgeolocation::internal::CurlHttpTransport transport;
        ipgeolocation::internal::HttpRequestData request;
        request.url = timeout_server.url();
        request.method = "GET";
        request.connect_timeout = std::chrono::milliseconds(1000);
        request.read_timeout = std::chrono::milliseconds(50);

        ExpectThrows<ipgeolocation::RequestTimeoutError>(
            [&transport, &request]() {
                static_cast<void>(transport.Send(request, 1024));
            },
            "HTTP request timed out after 50ms while waiting for response data");
    }

    {
        LoopbackServer oversized_server([](int client_fd) {
            WriteAll(client_fd, "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nhello");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            WriteAll(client_fd, "world");
        });

        ipgeolocation::internal::CurlHttpTransport transport;
        ipgeolocation::internal::HttpRequestData request;
        request.url = oversized_server.url();
        request.method = "GET";
        request.connect_timeout = std::chrono::milliseconds(1000);
        request.read_timeout = std::chrono::milliseconds(1000);

        ExpectThrows<ipgeolocation::TransportError>(
            [&transport, &request]() {
                static_cast<void>(transport.Send(request, 5));
            },
            "response body exceeded maxResponseBodyChars");
    }

    {
        LoopbackServer first_chunk_oversized_server([](int client_fd) {
            const std::string body(65536, 'x');
            WriteAll(
                client_fd,
                "HTTP/1.1 200 OK\r\nContent-Length: 65536\r\n\r\n" + body);
        });

        ipgeolocation::internal::CurlHttpTransport transport;
        ipgeolocation::internal::HttpRequestData request;
        request.url = first_chunk_oversized_server.url();
        request.method = "GET";
        request.connect_timeout = std::chrono::milliseconds(1000);
        request.read_timeout = std::chrono::milliseconds(1000);

        ExpectThrows<ipgeolocation::TransportError>(
            [&transport, &request]() {
                static_cast<void>(transport.Send(request, 1));
            },
            "response body exceeded maxResponseBodyChars");
    }
}

void TestCurlTransportReusesConnectionAcrossSequentialRequests() {
    auto captured_requests = std::make_shared<std::vector<std::string>>();
    LoopbackServer server([captured_requests](int client_fd) {
        captured_requests->push_back(ReadHttpRequest(client_fd));
        WriteAll(
            client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 5\r\n"
            "Connection: keep-alive\r\n"
            "\r\n"
            "first");

        captured_requests->push_back(ReadHttpRequest(client_fd));
        WriteAll(
            client_fd,
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 6\r\n"
            "Connection: close\r\n"
            "\r\n"
            "second");
    });

    ipgeolocation::internal::CurlHttpTransport transport;
    ipgeolocation::internal::HttpRequestData request;
    request.url = server.url();
    request.method = "GET";
    request.connect_timeout = std::chrono::milliseconds(1000);
    request.read_timeout = std::chrono::milliseconds(1000);

    const auto first = transport.Send(request, 1024);
    const auto second = transport.Send(request, 1024);

    Expect(first.body == "first", "first keep-alive request should succeed");
    Expect(second.body == "second", "second keep-alive request should succeed over the reused connection");
    Expect(captured_requests->size() == 2, "server should observe two requests on the same connection");
}

void TestCurlTransportRejectsSendAfterClose() {
    ipgeolocation::internal::CurlHttpTransport transport;
    transport.Close();

    ipgeolocation::internal::HttpRequestData request;
    request.url = "https://api.ipgeolocation.io/v3/ipgeo";
    request.method = "GET";
    request.connect_timeout = std::chrono::milliseconds(1000);
    request.read_timeout = std::chrono::milliseconds(1000);

    ExpectThrows<ipgeolocation::TransportError>(
        [&transport, &request]() {
            static_cast<void>(transport.Send(request, 1024));
        },
        "transport is closed");
}

void TestCurlTransportCapsIdleHandlePool() {
    {
        ipgeolocation::internal::CurlHttpTransport transport;
        std::vector<CURL*> handles;
        handles.reserve(ipgeolocation::internal::CurlHttpTransportTestAccess::MaxIdleHandleCount() + 1);

        for (std::size_t index = 0;
             index < ipgeolocation::internal::CurlHttpTransportTestAccess::MaxIdleHandleCount() + 1;
             ++index) {
            CURL* handle = curl_easy_init();
            Expect(handle != nullptr, "curl_easy_init should create handles for the pool-cap test");
            handles.push_back(handle);
        }

        for (CURL* handle : handles) {
            ipgeolocation::internal::CurlHttpTransportTestAccess::ReleaseHandle(transport, handle);
        }

        Expect(
            ipgeolocation::internal::CurlHttpTransportTestAccess::IdleHandleCount(transport) ==
                ipgeolocation::internal::CurlHttpTransportTestAccess::MaxIdleHandleCount(),
            "transport should cap the number of cached idle handles");

        transport.Close();
        Expect(ipgeolocation::internal::CurlHttpTransportTestAccess::IdleHandleCount(transport) == 0,
               "Close should release every cached idle handle");
    }

    {
        ipgeolocation::internal::CurlHttpTransport transport;
        CURL* handle = curl_easy_init();
        Expect(handle != nullptr, "curl_easy_init should create handles for the closed-transport branch");

        transport.Close();
        ipgeolocation::internal::CurlHttpTransportTestAccess::ReleaseHandle(transport, handle);

        Expect(ipgeolocation::internal::CurlHttpTransportTestAccess::IdleHandleCount(transport) == 0,
               "releasing a handle after Close should clean it up instead of caching it");
    }
}

void TestCurlTransportSurfacesGenericLibcurlErrors() {
    ipgeolocation::internal::CurlHttpTransport transport;
    ipgeolocation::internal::HttpRequestData request;
    request.url = "http://%zz";
    request.method = "GET";
    request.connect_timeout = std::chrono::milliseconds(1000);
    request.read_timeout = std::chrono::milliseconds(1000);

    try {
        static_cast<void>(transport.Send(request, 1024));
    } catch (const ipgeolocation::TransportError& error) {
        Expect(std::string(error.what()).find("libcurl transport error:") == 0,
               "generic libcurl errors should keep the transport-error prefix");
        return;
    }

    throw std::runtime_error("expected a generic TransportError from malformed URL");
}

void TestCurlTransportPostPathAndOutgoingHeaders() {
    auto captured_request = std::make_shared<std::string>();
    LoopbackServer server([captured_request](int client_fd) {
        *captured_request = ReadHttpRequest(client_fd);
        WriteAll(
            client_fd,
            "HTTP/1.1 201 Created\r\n"
            "X-Reply: yes\r\n"
            "\r\n"
            "created");
    });

    ipgeolocation::internal::CurlHttpTransport transport;
    ipgeolocation::internal::HttpRequestData request;
    request.url = server.url("/submit");
    request.method = "POST";
    request.body = "{\"ips\":[\"8.8.8.8\"]}";
    request.headers = {
        {"Content-Type", "application/json"},
        {"X-Test", "abc"},
        {"User-Agent", "CurlTransportTest/1.0"},
    };
    request.connect_timeout = std::chrono::milliseconds(1000);
    request.read_timeout = std::chrono::milliseconds(1000);

    const auto response = transport.Send(request, 1024);
    Expect(response.status_code == 201, "loopback POST should preserve the response status");
    Expect(response.body == "created", "loopback POST should read the response body");
    Expect(captured_request->find("POST /submit HTTP/1.1") != std::string::npos, "transport should send POST");
    Expect(captured_request->find("Content-Type: application/json") != std::string::npos,
           "transport should forward explicit headers");
    Expect(captured_request->find("X-Test: abc") != std::string::npos,
           "transport should forward custom headers");
    Expect(captured_request->find("{\"ips\":[\"8.8.8.8\"]}") != std::string::npos,
           "transport should send the configured POST body");
}

void TestBuildRequestsCoverHeaderReplacementAndBulkOrigin() {
    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";
    config.request_origin = "https://app.example.com";

    ipgeolocation::LookupIpGeolocationRequest lookup_request;
    lookup_request.headers = {
        {"User-Agenx", "Wrong"},
        {"user-agent", "LegacyAgent/1.0"},
        {"accept", "text/plain"},
    };
    lookup_request.user_agent = "FieldAgent/3.0";

    const auto lookup_http_request = ipgeolocation::internal::BuildLookupHttpRequest(
        ipgeolocation::internal::NormalizeConfig(config),
        ipgeolocation::internal::NormalizeLookupRequest(lookup_request));

    Expect(lookup_http_request.headers.count("user-agent") == 0,
           "header replacement should remove pre-existing user-agent casing");
    Expect(lookup_http_request.headers.at("User-Agenx") == "Wrong",
           "same-length non-matching headers should be preserved");
    Expect(lookup_http_request.headers.at("User-Agent") == "FieldAgent/3.0",
           "request field user agent should replace legacy header");
    Expect(lookup_http_request.headers.at("Accept") == "application/json",
           "accept header should be normalized");

    ipgeolocation::BulkLookupIpGeolocationRequest bulk_request;
    bulk_request.ips = {"8.8.8.8"};
    const auto bulk_http_request = ipgeolocation::internal::BuildBulkHttpRequest(
        ipgeolocation::internal::NormalizeConfig(config),
        ipgeolocation::internal::NormalizeBulkLookupRequest(bulk_request));
    Expect(bulk_http_request.headers.at("Origin") == "https://app.example.com",
           "bulk request builder should include the configured origin header");
}

void TestDefaultUserAgentIsStable() {
    Expect(ipgeolocation::IpGeolocationClient::DefaultUserAgent() == "ipgeolocation-cpp-sdk/1.0.0",
           "default user agent should match version");
}

void TestClosedClientRejectsRequestsBeforeTransport() {
    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = "secret";

    ipgeolocation::IpGeolocationClient client(config);
    client.Close();

    ExpectThrows<ipgeolocation::ClientClosedError>(
        [&client]() {
            static_cast<void>(client.LookupIpGeolocationRaw());
        },
        "client is closed");
}

}  // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"TestConfigNormalizesRequestOriginAndBaseUrl", TestConfigNormalizesRequestOriginAndBaseUrl},
        {"TestConfigRejectsRequestOriginPath", TestConfigRejectsRequestOriginPath},
        {"TestConfigRejectsInvalidBaseUrlAndTimeoutValues", TestConfigRejectsInvalidBaseUrlAndTimeoutValues},
        {"TestConfigRejectsInvalidRequestOriginForms", TestConfigRejectsInvalidRequestOriginForms},
        {"TestConfigCoversAdditionalValidationBranches", TestConfigCoversAdditionalValidationBranches},
        {"TestLookupRequestAllowsCallerIpAndTrimsUserAgent", TestLookupRequestAllowsCallerIpAndTrimsUserAgent},
        {"TestLookupRequestNormalizesLanguageAndDeduplicatesLists",
         TestLookupRequestNormalizesLanguageAndDeduplicatesLists},
        {"TestLookupRequestRejectsBlankAndCrLfValues", TestLookupRequestRejectsBlankAndCrLfValues},
        {"TestBulkRequestRejectsBlankValues", TestBulkRequestRejectsBlankValues},
        {"TestBulkRequestRejectsEmptyOversizedAndCrLfValues", TestBulkRequestRejectsEmptyOversizedAndCrLfValues},
        {"TestBulkRequestNormalizesLanguageListsAndUserAgent", TestBulkRequestNormalizesLanguageListsAndUserAgent},
        {"TestNormalizeHeadersRejectsInvalidShapes", TestNormalizeHeadersRejectsInvalidShapes},
        {"TestBuildLookupRequestRespectsUserAgentPrecedence", TestBuildLookupRequestRespectsUserAgentPrecedence},
        {"TestBuildLookupRequestUsesHeaderUserAgentFallbackAndEncodesOptions",
         TestBuildLookupRequestUsesHeaderUserAgentFallbackAndEncodesOptions},
        {"TestBuildBulkRequestSetsJsonBodyAndHeaders", TestBuildBulkRequestSetsJsonBodyAndHeaders},
        {"TestInternalHelpersCoverEnumsFactoriesAndEscaping", TestInternalHelpersCoverEnumsFactoriesAndEscaping},
        {"TestMetadataParsesCreditsAndSuccessfulRecords", TestMetadataParsesCreditsAndSuccessfulRecords},
        {"TestMetadataIgnoresInvalidCreditHeader", TestMetadataIgnoresInvalidCreditHeader},
        {"TestMetadataIgnoresInvalidSuccessfulRecordsHeader", TestMetadataIgnoresInvalidSuccessfulRecordsHeader},
        {"TestMetadataAndErrorHelpersCoverAdditionalFallbacks", TestMetadataAndErrorHelpersCoverAdditionalFallbacks},
        {"TestApiErrorUsesJsonMessage", TestApiErrorUsesJsonMessage},
        {"TestApiErrorFallsBackToNestedDetailAndGenericMessages", TestApiErrorFallsBackToNestedDetailAndGenericMessages},
        {"TestApiErrorPreservesResponseBody", TestApiErrorPreservesResponseBody},
        {"TestExtractApiMessageReturnsTrimmedTextWhenBodyIsNotJson", TestExtractApiMessageReturnsTrimmedTextWhenBodyIsNotJson},
        {"TestParseIpGeolocationResponseRejectsNonObjectPayload", TestParseIpGeolocationResponseRejectsNonObjectPayload},
        {"TestParseBulkLookupRejectsInvalidShapes", TestParseBulkLookupRejectsInvalidShapes},
        {"TestParseResponseToleratesWrongTypesAndSkipsNonStringArrayItems",
         TestParseResponseToleratesWrongTypesAndSkipsNonStringArrayItems},
        {"TestParseResponseIgnoresWrongNestedObjectTypesAcrossFamilies",
         TestParseResponseIgnoresWrongNestedObjectTypesAcrossFamilies},
        {"TestParseResponseTreatsNullNestedObjectsAsAbsent", TestParseResponseTreatsNullNestedObjectsAsAbsent},
        {"TestParseBulkLookupParsesDirectMessageErrors", TestParseBulkLookupParsesDirectMessageErrors},
        {"TestParseBulkLookupUsesFallbackMessageForMalformedErrorItems",
         TestParseBulkLookupUsesFallbackMessageForMalformedErrorItems},
        {"TestParseBulkLookupPrefersSuccessMarkersOverTopLevelMessage",
         TestParseBulkLookupPrefersSuccessMarkersOverTopLevelMessage},
        {"TestParseBulkLookupRejectsWrappedSuccessItems", TestParseBulkLookupRejectsWrappedSuccessItems},
        {"TestBulkLookupResultIsSuccessCoversMixedShape", TestBulkLookupResultIsSuccessCoversMixedShape},
        {"TestTypedSingleLookupParsesRepresentativeFields", TestTypedSingleLookupParsesRepresentativeFields},
        {"TestTypedSingleLookupParsesExhaustiveRepresentativeResponse",
         TestTypedSingleLookupParsesExhaustiveRepresentativeResponse},
        {"TestTypedMethodsRejectXmlBeforeTransport", TestTypedMethodsRejectXmlBeforeTransport},
        {"TestTypedBulkLookupParsesSuccessAndErrorShapes", TestTypedBulkLookupParsesSuccessAndErrorShapes},
        {"TestTypedSingleLookupInvalidJsonThrowsSerializationError", TestTypedSingleLookupInvalidJsonThrowsSerializationError},
        {"TestRawSingleLookupThrowsApiErrorOnNonSuccessStatus", TestRawSingleLookupThrowsApiErrorOnNonSuccessStatus},
        {"TestRawBulkLookupThrowsApiErrorOnNonSuccessStatus", TestRawBulkLookupThrowsApiErrorOnNonSuccessStatus},
        {"TestRawAndBulkMethodsEnforceAuthRules", TestRawAndBulkMethodsEnforceAuthRules},
        {"TestClientCoversAdditionalTypedAndBulkPaths", TestClientCoversAdditionalTypedAndBulkPaths},
        {"TestTypedBulkRejectsXmlBeforeTransport", TestTypedBulkRejectsXmlBeforeTransport},
        {"TestRequestOriginOnlySingleLookupWorks", TestRequestOriginOnlySingleLookupWorks},
        {"TestResolveUserAgentHeaderUsesDefaultsAndHeaderFallback",
         TestResolveUserAgentHeaderUsesDefaultsAndHeaderFallback},
        {"TestBuildRequestsCoverHeaderReplacementAndBulkOrigin", TestBuildRequestsCoverHeaderReplacementAndBulkOrigin},
        {"TestCloseIsIdempotentAndMoveSemanticsKeepClientUsable", TestCloseIsIdempotentAndMoveSemanticsKeepClientUsable},
        {"TestMoveAssignmentKeepsDestinationUsable", TestMoveAssignmentKeepsDestinationUsable},
        {"TestSharedClientSupportsConcurrentRequests", TestSharedClientSupportsConcurrentRequests},
        {"TestMovedFromClientRejectsAllRequestEntryPointsSafely", TestMovedFromClientRejectsAllRequestEntryPointsSafely},
        {"TestJsonParserCoversNullEscapesExponentAndFailures", TestJsonParserCoversNullEscapesExponentAndFailures},
        {"TestJsonParserUsesLocaleIndependentNumberParsing", TestJsonParserUsesLocaleIndependentNumberParsing},
        {"TestJsonParserRejectsMoreInvalidForms", TestJsonParserRejectsMoreInvalidForms},
        {"TestCurlTransportCapturesHeadersAndTrimsHeaderNameWhitespace",
         TestCurlTransportCapturesHeadersAndTrimsHeaderNameWhitespace},
        {"TestCurlTransportTimeoutAndSizeLimitErrors", TestCurlTransportTimeoutAndSizeLimitErrors},
        {"TestCurlTransportReusesConnectionAcrossSequentialRequests",
         TestCurlTransportReusesConnectionAcrossSequentialRequests},
        {"TestCurlTransportRejectsSendAfterClose", TestCurlTransportRejectsSendAfterClose},
        {"TestCurlTransportCapsIdleHandlePool", TestCurlTransportCapsIdleHandlePool},
        {"TestCurlTransportSurfacesGenericLibcurlErrors", TestCurlTransportSurfacesGenericLibcurlErrors},
        {"TestCurlTransportPostPathAndOutgoingHeaders", TestCurlTransportPostPathAndOutgoingHeaders},
        {"TestDefaultUserAgentIsStable", TestDefaultUserAgentIsStable},
        {"TestClosedClientRejectsRequestsBeforeTransport", TestClosedClientRejectsRequestsBeforeTransport},
    };

    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return 1;
        }
    }

    std::cout << "All C++ SDK tests passed.\n";
    return 0;
}
