#pragma once

#include <atomic>
#include <memory>
#include <utility>

#include "ipgeolocation/client.hpp"
#include "internal.hpp"

namespace ipgeolocation {

class IpGeolocationClient::Impl {
public:
    explicit Impl(const IpGeolocationClientConfig& raw_config)
        : config(internal::NormalizeConfig(raw_config)),
          transport(internal::CreateDefaultTransport()) {}

    Impl(
        const IpGeolocationClientConfig& raw_config,
        std::unique_ptr<internal::HttpTransport> injected_transport)
        : config(internal::NormalizeConfig(raw_config)),
          transport(std::move(injected_transport)) {}

    internal::NormalizedConfig config;
    std::unique_ptr<internal::HttpTransport> transport;
    mutable std::atomic<bool> closed{false};
};

}  // namespace ipgeolocation
