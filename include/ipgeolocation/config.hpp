#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>

#include "ipgeolocation/visibility.hpp"

namespace ipgeolocation {

struct IPGEOLOCATION_API IpGeolocationClientConfig {
    std::optional<std::string> api_key;
    std::optional<std::string> request_origin;
    std::string base_url = "https://api.ipgeolocation.io";
    std::chrono::milliseconds connect_timeout{10000};
    std::chrono::milliseconds read_timeout{30000};
    std::size_t max_response_body_chars = 32u * 1024u * 1024u;
};

}  // namespace ipgeolocation
