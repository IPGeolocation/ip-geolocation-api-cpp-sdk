#pragma once

#include <memory>
#include <string>

#include "ipgeolocation/api_response.hpp"
#include "ipgeolocation/config.hpp"
#include "ipgeolocation/models.hpp"
#include "ipgeolocation/requests.hpp"
#include "ipgeolocation/visibility.hpp"

namespace ipgeolocation {

namespace internal {
class ClientTestAccess;
class HttpTransport;
}

// IpGeolocationClient supports concurrent request calls on a shared instance.
// Close() is idempotent, but it should not be raced against active requests.
class IpGeolocationClient {
public:
    IPGEOLOCATION_API explicit IpGeolocationClient(const IpGeolocationClientConfig& config);
    IPGEOLOCATION_API ~IpGeolocationClient();

    IPGEOLOCATION_API IpGeolocationClient(IpGeolocationClient&&) noexcept;
    IPGEOLOCATION_API IpGeolocationClient& operator=(IpGeolocationClient&&) noexcept;

    IpGeolocationClient(const IpGeolocationClient&) = delete;
    IpGeolocationClient& operator=(const IpGeolocationClient&) = delete;

    IPGEOLOCATION_API ApiResponse<IpGeolocationResponse> LookupIpGeolocation(
        const LookupIpGeolocationRequest& request = {}) const;
    IPGEOLOCATION_API ApiResponse<std::string> LookupIpGeolocationRaw(
        const LookupIpGeolocationRequest& request = {}) const;
    IPGEOLOCATION_API ApiResponse<std::vector<BulkLookupResult>> BulkLookupIpGeolocation(
        const BulkLookupIpGeolocationRequest& request) const;
    IPGEOLOCATION_API ApiResponse<std::string> BulkLookupIpGeolocationRaw(
        const BulkLookupIpGeolocationRequest& request) const;

    IPGEOLOCATION_API void Close() noexcept;
    IPGEOLOCATION_API bool closed() const noexcept;

    IPGEOLOCATION_API static std::string DefaultUserAgent();

private:
    friend class internal::ClientTestAccess;

    class Impl;
    IPGEOLOCATION_HIDDEN explicit IpGeolocationClient(
        const IpGeolocationClientConfig& config,
        std::unique_ptr<internal::HttpTransport> transport);
    std::unique_ptr<Impl> impl_;
};

}  // namespace ipgeolocation
