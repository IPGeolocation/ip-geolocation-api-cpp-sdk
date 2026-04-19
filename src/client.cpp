#include "ipgeolocation/client.hpp"

#include <chrono>

#include "client_impl.hpp"

namespace ipgeolocation {

IpGeolocationClient::IpGeolocationClient(const IpGeolocationClientConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

IpGeolocationClient::~IpGeolocationClient() = default;

IpGeolocationClient::IpGeolocationClient(IpGeolocationClient&&) noexcept = default;

IpGeolocationClient& IpGeolocationClient::operator=(IpGeolocationClient&&) noexcept = default;

ApiResponse<IpGeolocationResponse> IpGeolocationClient::LookupIpGeolocation(
    const LookupIpGeolocationRequest& request) const {
    if (!impl_) {
        throw ClientClosedError("client is closed");
    }
    if (impl_->closed.load()) {
        throw ClientClosedError("client is closed");
    }
    if (!impl_->config.api_key.has_value() && !impl_->config.request_origin.has_value()) {
        throw ValidationError("single lookup requires apiKey or requestOrigin in client config");
    }

    const internal::NormalizedLookupRequest normalized = internal::NormalizeLookupRequest(request);
    if (normalized.output == ResponseFormat::kXml) {
        throw ValidationError("typed methods support JSON only");
    }

    const internal::HttpRequestData http_request = internal::BuildLookupHttpRequest(impl_->config, normalized);

    const auto started_at = std::chrono::steady_clock::now();
    const internal::HttpResponseData response =
        impl_->transport->Send(http_request, impl_->config.max_response_body_chars);
    const auto finished_at = std::chrono::steady_clock::now();
    const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finished_at - started_at).count();

    if (response.status_code / 100 != 2) {
        throw internal::ToApiError(response.status_code, response.body);
    }

    ApiResponse<IpGeolocationResponse> result;
    result.data = internal::ParseIpGeolocationResponse(response.body);
    result.metadata = internal::ToMetadata(response.status_code, duration_ms, response.headers);
    return result;
}

ApiResponse<std::string> IpGeolocationClient::LookupIpGeolocationRaw(
    const LookupIpGeolocationRequest& request) const {
    if (!impl_) {
        throw ClientClosedError("client is closed");
    }
    if (impl_->closed.load()) {
        throw ClientClosedError("client is closed");
    }
    if (!impl_->config.api_key.has_value() && !impl_->config.request_origin.has_value()) {
        throw ValidationError("single lookup requires apiKey or requestOrigin in client config");
    }

    const internal::NormalizedLookupRequest normalized = internal::NormalizeLookupRequest(request);
    const internal::HttpRequestData http_request = internal::BuildLookupHttpRequest(impl_->config, normalized);

    const auto started_at = std::chrono::steady_clock::now();
    const internal::HttpResponseData response =
        impl_->transport->Send(http_request, impl_->config.max_response_body_chars);
    const auto finished_at = std::chrono::steady_clock::now();
    const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finished_at - started_at).count();

    if (response.status_code / 100 != 2) {
        throw internal::ToApiError(response.status_code, response.body);
    }

    ApiResponse<std::string> result;
    result.data = response.body;
    result.metadata = internal::ToMetadata(response.status_code, duration_ms, response.headers);
    return result;
}

ApiResponse<std::vector<BulkLookupResult>> IpGeolocationClient::BulkLookupIpGeolocation(
    const BulkLookupIpGeolocationRequest& request) const {
    if (!impl_) {
        throw ClientClosedError("client is closed");
    }
    if (impl_->closed.load()) {
        throw ClientClosedError("client is closed");
    }
    if (!impl_->config.api_key.has_value()) {
        throw ValidationError("bulk lookup requires apiKey in client config");
    }

    const internal::NormalizedBulkLookupRequest normalized = internal::NormalizeBulkLookupRequest(request);
    if (normalized.output == ResponseFormat::kXml) {
        throw ValidationError("typed methods support JSON only");
    }

    const internal::HttpRequestData http_request = internal::BuildBulkHttpRequest(impl_->config, normalized);

    const auto started_at = std::chrono::steady_clock::now();
    const internal::HttpResponseData response =
        impl_->transport->Send(http_request, impl_->config.max_response_body_chars);
    const auto finished_at = std::chrono::steady_clock::now();
    const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finished_at - started_at).count();

    if (response.status_code / 100 != 2) {
        throw internal::ToApiError(response.status_code, response.body);
    }

    ApiResponse<std::vector<BulkLookupResult>> result;
    result.data = internal::ParseBulkLookupResults(response.body);
    result.metadata = internal::ToMetadata(response.status_code, duration_ms, response.headers);
    return result;
}

ApiResponse<std::string> IpGeolocationClient::BulkLookupIpGeolocationRaw(
    const BulkLookupIpGeolocationRequest& request) const {
    if (!impl_) {
        throw ClientClosedError("client is closed");
    }
    if (impl_->closed.load()) {
        throw ClientClosedError("client is closed");
    }
    if (!impl_->config.api_key.has_value()) {
        throw ValidationError("bulk lookup requires apiKey in client config");
    }

    const internal::NormalizedBulkLookupRequest normalized = internal::NormalizeBulkLookupRequest(request);
    const internal::HttpRequestData http_request = internal::BuildBulkHttpRequest(impl_->config, normalized);

    const auto started_at = std::chrono::steady_clock::now();
    const internal::HttpResponseData response =
        impl_->transport->Send(http_request, impl_->config.max_response_body_chars);
    const auto finished_at = std::chrono::steady_clock::now();
    const auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finished_at - started_at).count();

    if (response.status_code / 100 != 2) {
        throw internal::ToApiError(response.status_code, response.body);
    }

    ApiResponse<std::string> result;
    result.data = response.body;
    result.metadata = internal::ToMetadata(response.status_code, duration_ms, response.headers);
    return result;
}

void IpGeolocationClient::Close() noexcept {
    if (!impl_ || impl_->closed.exchange(true)) {
        return;
    }
    impl_->transport->Close();
}

bool IpGeolocationClient::closed() const noexcept {
    return impl_ == nullptr || impl_->closed.load();
}

std::string IpGeolocationClient::DefaultUserAgent() {
    return internal::DefaultUserAgent();
}

}  // namespace ipgeolocation
