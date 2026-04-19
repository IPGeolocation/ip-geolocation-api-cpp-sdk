#include "client_test_access.hpp"

#include <memory>

#include "client_impl.hpp"
#include "internal.hpp"

namespace ipgeolocation {

IpGeolocationClient::IpGeolocationClient(
    const IpGeolocationClientConfig& config,
    std::unique_ptr<internal::HttpTransport> transport)
    : impl_(std::make_unique<Impl>(config, std::move(transport))) {}

}  // namespace ipgeolocation

namespace ipgeolocation::internal {

IpGeolocationClient ClientTestAccess::CreateClientWithTransport(
    const IpGeolocationClientConfig& config,
    std::unique_ptr<HttpTransport> transport) {
    return IpGeolocationClient(config, std::move(transport));
}

}  // namespace ipgeolocation::internal
