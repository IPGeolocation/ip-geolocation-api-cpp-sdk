#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ipgeolocation/visibility.hpp"

namespace ipgeolocation {

struct IPGEOLOCATION_API ApiResponseMetadata {
    int status_code = 0;
    long long duration_ms = 0;
    std::optional<int> credits_charged;
    std::optional<int> successful_records;
    std::map<std::string, std::vector<std::string>> raw_headers;
};

template <typename T>
struct IPGEOLOCATION_API ApiResponse {
    T data;
    ApiResponseMetadata metadata;
};

}  // namespace ipgeolocation
