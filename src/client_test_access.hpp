#pragma once

#include <memory>

#include "ipgeolocation/client.hpp"

namespace ipgeolocation::internal {

class HttpTransport;

class ClientTestAccess {
public:
    static IpGeolocationClient CreateClientWithTransport(
        const IpGeolocationClientConfig& config,
        std::unique_ptr<HttpTransport> transport);
};

}  // namespace ipgeolocation::internal
