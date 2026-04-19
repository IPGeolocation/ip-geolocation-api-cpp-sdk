#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ipgeolocation/language.hpp"
#include "ipgeolocation/response_format.hpp"
#include "ipgeolocation/visibility.hpp"

namespace ipgeolocation {

struct IPGEOLOCATION_API LookupIpGeolocationRequest {
    std::optional<std::string> ip;
    std::optional<Language> lang;
    std::vector<std::string> include;
    std::vector<std::string> fields;
    std::vector<std::string> excludes;
    std::optional<std::string> user_agent;
    std::map<std::string, std::string> headers;
    ResponseFormat output = ResponseFormat::kJson;
};

struct IPGEOLOCATION_API BulkLookupIpGeolocationRequest {
    std::vector<std::string> ips;
    std::optional<Language> lang;
    std::vector<std::string> include;
    std::vector<std::string> fields;
    std::vector<std::string> excludes;
    std::optional<std::string> user_agent;
    std::map<std::string, std::string> headers;
    ResponseFormat output = ResponseFormat::kJson;
};

}  // namespace ipgeolocation
