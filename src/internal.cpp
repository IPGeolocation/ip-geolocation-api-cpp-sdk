#include "internal.hpp"

#include <algorithm>
#include <regex>
#include <set>
#include <sstream>

#include "json.hpp"
#include "ipgeolocation/version.hpp"

namespace ipgeolocation {

ApiError::ApiError(int status_code, std::string message, std::string response_body)
    : IpGeolocationError(std::move(message)),
      status_code_(status_code),
      response_body_(std::move(response_body)) {}

int ApiError::status_code() const noexcept {
    return status_code_;
}

const std::string& ApiError::response_body() const noexcept {
    return response_body_;
}

}  // namespace ipgeolocation

namespace ipgeolocation::internal {
namespace {

bool IsAsciiWhitespace(char value) {
    switch (value) {
        case ' ':
        case '\n':
        case '\r':
        case '\t':
        case '\f':
        case '\v':
            return true;
        default:
            return false;
    }
}

std::string Trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && IsAsciiWhitespace(value[start])) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && IsAsciiWhitespace(value[end - 1])) {
        --end;
    }

    return value.substr(start, end - start);
}

bool ContainsCrOrLf(const std::string& value) {
    return value.find('\r') != std::string::npos || value.find('\n') != std::string::npos;
}

bool ContainsAsciiWhitespace(const std::string& value) {
    for (char character : value) {
        if (IsAsciiWhitespace(character)) {
            return true;
        }
    }
    return false;
}

bool IsUnreservedQueryByte(unsigned char value) {
    return (value >= 'A' && value <= 'Z') ||
           (value >= 'a' && value <= 'z') ||
           (value >= '0' && value <= '9') ||
           value == '-' || value == '_' || value == '.' || value == '~';
}

bool IsAsciiDigit(unsigned char value) {
    return value >= '0' && value <= '9';
}

bool IsAsciiAlpha(unsigned char value) {
    return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z');
}

bool IsAsciiAlphaNumeric(unsigned char value) {
    return IsAsciiAlpha(value) || IsAsciiDigit(value);
}

bool IsAsciiHexDigit(unsigned char value) {
    return IsAsciiDigit(value) ||
           (value >= 'A' && value <= 'F') ||
           (value >= 'a' && value <= 'f');
}

char ToLowerAscii(unsigned char value) {
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return static_cast<char>(value);
}

bool EqualsAsciiCaseInsensitive(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (ToLowerAscii(static_cast<unsigned char>(left[index])) !=
            ToLowerAscii(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }

    return true;
}

bool IsDigitsOnly(const std::string& value) {
    return !value.empty() &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return IsAsciiDigit(character);
           });
}

bool IsValidPortString(const std::string& value) {
    if (!IsDigitsOnly(value)) {
        return false;
    }

    try {
        const int port = std::stoi(value);
        return port >= 1 && port <= 65535;
    } catch (...) {
        return false;
    }
}

bool IsValidDnsOrIpv4Host(const std::string& host) {
    if (host.empty() || ContainsAsciiWhitespace(host)) {
        return false;
    }

    std::size_t label_start = 0;
    while (label_start < host.size()) {
        const std::size_t label_end = host.find('.', label_start);
        const std::string label = host.substr(label_start, label_end - label_start);
        if (label.empty() || label.front() == '-' || label.back() == '-') {
            return false;
        }
        if (!std::all_of(label.begin(), label.end(), [](unsigned char character) {
                return IsAsciiAlphaNumeric(character) || character == '-';
            })) {
            return false;
        }

        if (label_end == std::string::npos) {
            break;
        }
        label_start = label_end + 1;
    }

    return true;
}

bool IsValidBracketedIpv6Host(const std::string& host) {
    if (host.empty() || ContainsAsciiWhitespace(host)) {
        return false;
    }
    if (host.find(':') == std::string::npos) {
        return false;
    }

    return std::all_of(host.begin(), host.end(), [](unsigned char character) {
        return IsAsciiHexDigit(character) || character == ':' || character == '.';
    });
}

bool IsValidAuthority(const std::string& authority) {
    if (authority.empty() || ContainsAsciiWhitespace(authority)) {
        return false;
    }

    if (authority.front() == '[') {
        const std::size_t closing = authority.find(']');
        if (closing == std::string::npos) {
            return false;
        }

        if (!IsValidBracketedIpv6Host(authority.substr(1, closing - 1))) {
            return false;
        }

        if (closing + 1 == authority.size()) {
            return true;
        }
        if (authority[closing + 1] != ':') {
            return false;
        }
        return IsValidPortString(authority.substr(closing + 2));
    }

    if (authority.find('[') != std::string::npos || authority.find(']') != std::string::npos) {
        return false;
    }

    std::string host = authority;
    const std::size_t colon = authority.rfind(':');
    if (colon != std::string::npos) {
        if (authority.find(':') != colon) {
            return false;
        }
        host = authority.substr(0, colon);
        if (!IsValidPortString(authority.substr(colon + 1))) {
            return false;
        }
    }

    return IsValidDnsOrIpv4Host(host);
}

bool LooksLikeSuccessfulBulkItem(const JsonValue::Object& object) {
    static constexpr const char* kSuccessMarkers[] = {
        "ip",
        "hostname",
        "domain",
        "location",
        "country_metadata",
        "currency",
        "network",
        "asn",
        "company",
        "time_zone",
        "security",
        "abuse",
        "user_agent",
    };

    return std::any_of(std::begin(kSuccessMarkers), std::end(kSuccessMarkers), [&object](const char* name) {
        return object.find(name) != object.end();
    });
}

std::string NormalizeOptionalNonBlankString(
    const std::optional<std::string>& value,
    const char* field_name,
    bool reject_crlf) {
    if (!value.has_value()) {
        return "";
    }

    const std::string normalized = Trim(*value);
    if (normalized.empty()) {
        throw ValidationError(std::string(field_name) + " must not be blank");
    }

    if (reject_crlf && ContainsCrOrLf(normalized)) {
        throw ValidationError(std::string(field_name) + " must not contain CR or LF");
    }

    return normalized;
}

std::string NormalizeBaseUrl(const std::string& raw_value) {
    std::string value = Trim(raw_value);
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }

    if (value.empty()) {
        throw ValidationError("baseUrl must not be blank");
    }
    if (ContainsCrOrLf(value)) {
        throw ValidationError("baseUrl must not contain CR or LF");
    }

    const std::size_t scheme_separator = value.find("://");
    if (scheme_separator != std::string::npos) {
        const std::size_t authority_start = scheme_separator + 3;
        const std::size_t authority_end = value.find_first_of("/?#", authority_start);
        const std::string authority = value.substr(authority_start, authority_end - authority_start);
        if (authority.find('@') != std::string::npos) {
            throw ValidationError("baseUrl must not include userinfo");
        }
    }

    static const std::regex pattern(R"(^(https?)://([^/?#@]+)(/[^?#]*)?(\?[^#]*)?(#.*)?$)",
                                    std::regex::icase);
    std::smatch match;
    if (!std::regex_match(value, match, pattern)) {
        throw ValidationError("baseUrl must be an absolute http or https URL");
    }

    const std::string authority = match[2].str();
    const std::string query = match[4].matched ? match[4].str() : "";
    const std::string fragment = match[5].matched ? match[5].str() : "";

    if (!IsValidAuthority(authority)) {
        throw ValidationError("baseUrl must include a valid host");
    }
    if (!query.empty() || !fragment.empty()) {
        throw ValidationError("baseUrl must not include query or fragment");
    }

    return value;
}

std::string NormalizeRequestOrigin(const std::optional<std::string>& raw_value) {
    if (!raw_value.has_value()) {
        return "";
    }

    const std::string value = NormalizeOptionalNonBlankString(raw_value, "requestOrigin", true);
    const std::size_t scheme_separator = value.find("://");
    if (scheme_separator != std::string::npos) {
        const std::size_t authority_start = scheme_separator + 3;
        const std::size_t authority_end = value.find_first_of("/?#", authority_start);
        const std::string authority = value.substr(authority_start, authority_end - authority_start);
        if (authority.find('@') != std::string::npos) {
            throw ValidationError("requestOrigin must not include userinfo");
        }
    }

    static const std::regex pattern(R"(^(https?)://([^/?#@]+)(/[^?#]*)?(\?[^#]*)?(#.*)?$)",
                                    std::regex::icase);
    std::smatch match;
    if (!std::regex_match(value, match, pattern)) {
        throw ValidationError("requestOrigin must be an absolute http or https origin");
    }

    const std::string scheme = match[1].str();
    const std::string authority = match[2].str();
    const std::string path = match[3].matched ? match[3].str() : "";
    const std::string query = match[4].matched ? match[4].str() : "";
    const std::string fragment = match[5].matched ? match[5].str() : "";

    if (!IsValidAuthority(authority)) {
        throw ValidationError("requestOrigin must include a valid host");
    }
    if (!query.empty() || !fragment.empty()) {
        throw ValidationError("requestOrigin must not include query or fragment");
    }
    if (!path.empty() && path != "/") {
        throw ValidationError("requestOrigin must not include a path");
    }

    return scheme + "://" + authority;
}

std::vector<std::string> NormalizeStringList(const std::vector<std::string>& values, const char* field_name) {
    if (values.empty()) {
        return {};
    }

    std::set<std::string> seen;
    std::vector<std::string> normalized;
    normalized.reserve(values.size());

    for (const std::string& raw_value : values) {
        const std::string value = Trim(raw_value);
        if (value.empty()) {
            throw ValidationError(std::string(field_name) + " must not contain blank values");
        }
        if (ContainsCrOrLf(value)) {
            throw ValidationError(std::string(field_name) + " must not contain CR or LF");
        }
        if (seen.insert(value).second) {
            normalized.push_back(value);
        }
    }

    return normalized;
}

std::optional<std::string> NormalizeLookupIp(const std::optional<std::string>& raw_value) {
    if (!raw_value.has_value()) {
        return std::nullopt;
    }

    const std::string value = Trim(*raw_value);
    if (value.empty()) {
        return std::nullopt;
    }
    if (ContainsCrOrLf(value)) {
        throw ValidationError("ip must not contain CR or LF");
    }

    return value;
}

std::vector<std::string> NormalizeIpList(const std::vector<std::string>& values) {
    if (values.empty()) {
        throw ValidationError("ips must contain at least one IP address or domain");
    }
    if (values.size() > 50000) {
        throw ValidationError("ips must not contain more than 50000 entries");
    }

    std::vector<std::string> normalized;
    normalized.reserve(values.size());
    for (const std::string& raw_value : values) {
        const std::string value = Trim(raw_value);
        if (value.empty()) {
            throw ValidationError("ips must not contain blank values");
        }
        if (ContainsCrOrLf(value)) {
            throw ValidationError("ip must not contain CR or LF");
        }
        normalized.push_back(value);
    }

    return normalized;
}

std::optional<std::string> NormalizeLanguage(const std::optional<Language>& raw_value) {
    if (!raw_value.has_value()) {
        return std::nullopt;
    }

    return LanguageCode(*raw_value);
}

std::map<std::string, std::string> NormalizeHeaders(const std::map<std::string, std::string>& headers) {
    std::map<std::string, std::string> normalized;
    for (const auto& [raw_name, raw_value] : headers) {
        const std::string name = Trim(raw_name);
        const std::string value = Trim(raw_value);
        if (name.empty()) {
            throw ValidationError("headers must not contain blank names");
        }
        if (value.empty()) {
            throw ValidationError("headers must not contain blank values");
        }
        if (ContainsCrOrLf(name) || ContainsCrOrLf(value)) {
            throw ValidationError("headers must not contain CR or LF");
        }
        normalized[name] = value;
    }
    return normalized;
}

std::string HeaderValueCaseInsensitive(
    const std::map<std::string, std::string>& headers,
    const std::string& header_name) {
    for (const auto& [name, value] : headers) {
        if (EqualsAsciiCaseInsensitive(name, header_name)) {
            return value;
        }
    }
    return "";
}

void SetHeaderCaseInsensitive(
    std::map<std::string, std::string>& headers,
    const std::string& header_name,
    const std::string& value) {
    for (auto iterator = headers.begin(); iterator != headers.end(); ) {
        if (EqualsAsciiCaseInsensitive(iterator->first, header_name)) {
            iterator = headers.erase(iterator);
            continue;
        }
        ++iterator;
    }

    headers[header_name] = value;
}

std::string UrlEncode(const std::string& value) {
    static constexpr char kHexDigits[] = "0123456789ABCDEF";

    std::string encoded;
    encoded.reserve(value.size() * 3);
    for (unsigned char byte : value) {
        if (IsUnreservedQueryByte(byte)) {
            encoded.push_back(static_cast<char>(byte));
            continue;
        }

        encoded.push_back('%');
        encoded.push_back(kHexDigits[(byte >> 4) & 0x0F]);
        encoded.push_back(kHexDigits[byte & 0x0F]);
    }

    return encoded;
}

std::string JoinCsv(const std::vector<std::string>& values) {
    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            stream << ',';
        }
        stream << values[index];
    }
    return stream.str();
}

std::string BuildQueryString(const std::vector<std::pair<std::string, std::string>>& params) {
    std::ostringstream stream;
    bool has_any = false;
    for (const auto& [key, value] : params) {
        if (value.empty()) {
            continue;
        }
        stream << (has_any ? '&' : '?') << UrlEncode(key) << '=' << UrlEncode(value);
        has_any = true;
    }
    return stream.str();
}

std::optional<int> ParseHeaderInt(
    const std::map<std::string, std::vector<std::string>>& headers,
    const std::vector<std::string>& names) {
    for (const std::string& wanted_name : names) {
        for (const auto& [name, values] : headers) {
            if (!EqualsAsciiCaseInsensitive(name, wanted_name) || values.empty()) {
                continue;
            }

            try {
                return std::stoi(values.front());
            } catch (...) {
                return std::nullopt;
            }
        }
    }

    return std::nullopt;
}

std::string GenericApiErrorMessage(int status_code) {
    switch (status_code) {
        case 400:
            return "bad request";
        case 401:
            return "unauthorized";
        case 403:
            return "forbidden";
        case 404:
            return "not found";
        case 405:
            return "method not allowed";
        case 413:
            return "content too large";
        case 415:
            return "unsupported media type";
        case 423:
            return "locked";
        case 429:
            return "too many requests";
        case 499:
            return "client closed request";
        case 500:
            return "internal server error";
        default:
            return "api request failed";
    }
}

const JsonValue* GetMember(
    const JsonValue::Object& object,
    std::initializer_list<const char*> names) {
    for (const char* name : names) {
        const auto iterator = object.find(name);
        if (iterator != object.end()) {
            return &iterator->second;
        }
    }
    return nullptr;
}

std::optional<std::string> ReadString(const JsonValue* value) {
    if (value == nullptr || value->is_null() || !value->is_string()) {
        return std::nullopt;
    }

    return value->string_value();
}

std::optional<double> ReadNumber(const JsonValue* value) {
    if (value == nullptr || value->is_null() || !value->is_number()) {
        return std::nullopt;
    }

    return value->number_value();
}

std::optional<bool> ReadBool(const JsonValue* value) {
    if (value == nullptr || value->is_null() || !value->is_bool()) {
        return std::nullopt;
    }

    return value->bool_value();
}

std::optional<std::vector<std::string>> ReadStringArray(const JsonValue* value) {
    if (value == nullptr || value->is_null()) {
        return std::nullopt;
    }
    if (!value->is_array()) {
        return std::nullopt;
    }

    std::vector<std::string> items;
    items.reserve(value->array_value().size());
    for (const JsonValue& item : value->array_value()) {
        if (!item.is_string()) {
            continue;
        }
        items.push_back(item.string_value());
    }
    return items;
}

template <typename T, typename Parser>
std::optional<T> ReadObject(const JsonValue* value, Parser parser) {
    if (value == nullptr || value->is_null()) {
        return std::nullopt;
    }
    if (!value->is_object()) {
        return std::nullopt;
    }
    return parser(value->object_value());
}

Location ParseLocation(const JsonValue::Object& object) {
    Location value;
    value.continent_code = ReadString(GetMember(object, {"continent_code"}));
    value.continent_name = ReadString(GetMember(object, {"continent_name"}));
    value.country_code2 = ReadString(GetMember(object, {"country_code2"}));
    value.country_code3 = ReadString(GetMember(object, {"country_code3"}));
    value.country_name = ReadString(GetMember(object, {"country_name"}));
    value.country_name_official = ReadString(GetMember(object, {"country_name_official"}));
    value.country_capital = ReadString(GetMember(object, {"country_capital"}));
    value.state_prov = ReadString(GetMember(object, {"state_prov"}));
    value.state_code = ReadString(GetMember(object, {"state_code"}));
    value.district = ReadString(GetMember(object, {"district"}));
    value.city = ReadString(GetMember(object, {"city"}));
    value.locality = ReadString(GetMember(object, {"locality"}));
    value.accuracy_radius = ReadString(GetMember(object, {"accuracy_radius"}));
    value.confidence = ReadString(GetMember(object, {"confidence"}));
    value.dma_code = ReadString(GetMember(object, {"dma_code"}));
    value.zipcode = ReadString(GetMember(object, {"zipcode"}));
    value.latitude = ReadString(GetMember(object, {"latitude"}));
    value.longitude = ReadString(GetMember(object, {"longitude"}));
    value.is_eu = ReadBool(GetMember(object, {"is_eu"}));
    value.country_flag = ReadString(GetMember(object, {"country_flag"}));
    value.geoname_id = ReadString(GetMember(object, {"geoname_id"}));
    value.country_emoji = ReadString(GetMember(object, {"country_emoji"}));
    return value;
}

CountryMetadata ParseCountryMetadata(const JsonValue::Object& object) {
    CountryMetadata value;
    value.calling_code = ReadString(GetMember(object, {"calling_code"}));
    value.tld = ReadString(GetMember(object, {"tld"}));
    value.languages = ReadStringArray(GetMember(object, {"languages"}));
    return value;
}

Currency ParseCurrency(const JsonValue::Object& object) {
    Currency value;
    value.code = ReadString(GetMember(object, {"code"}));
    value.name = ReadString(GetMember(object, {"name"}));
    value.symbol = ReadString(GetMember(object, {"symbol"}));
    return value;
}

Network ParseNetwork(const JsonValue::Object& object) {
    Network value;
    value.connection_type = ReadString(GetMember(object, {"connection_type"}));
    value.route = ReadString(GetMember(object, {"route"}));
    value.is_anycast = ReadBool(GetMember(object, {"is_anycast"}));
    return value;
}

Asn ParseAsn(const JsonValue::Object& object) {
    Asn value;
    value.as_number = ReadString(GetMember(object, {"as_number"}));
    value.organization = ReadString(GetMember(object, {"organization"}));
    value.country = ReadString(GetMember(object, {"country"}));
    value.type = ReadString(GetMember(object, {"type"}));
    value.domain = ReadString(GetMember(object, {"domain"}));
    value.date_allocated = ReadString(GetMember(object, {"date_allocated"}));
    value.rir = ReadString(GetMember(object, {"rir"}));
    return value;
}

Company ParseCompany(const JsonValue::Object& object) {
    Company value;
    value.name = ReadString(GetMember(object, {"name"}));
    value.type = ReadString(GetMember(object, {"type"}));
    value.domain = ReadString(GetMember(object, {"domain"}));
    return value;
}

DstTransition ParseDstTransition(const JsonValue::Object& object) {
    DstTransition value;
    value.utc_time = ReadString(GetMember(object, {"utc_time"}));
    value.duration = ReadString(GetMember(object, {"duration"}));
    value.gap = ReadBool(GetMember(object, {"gap"}));
    value.date_time_after = ReadString(GetMember(object, {"date_time_after"}));
    value.date_time_before = ReadString(GetMember(object, {"date_time_before"}));
    value.overlap = ReadBool(GetMember(object, {"overlap"}));
    return value;
}

TimeZoneInfo ParseTimeZoneInfo(const JsonValue::Object& object) {
    TimeZoneInfo value;
    value.name = ReadString(GetMember(object, {"name"}));
    value.offset = ReadNumber(GetMember(object, {"offset"}));
    value.offset_with_dst = ReadNumber(GetMember(object, {"offset_with_dst"}));
    value.current_time = ReadString(GetMember(object, {"current_time"}));
    value.current_time_unix = ReadNumber(GetMember(object, {"current_time_unix"}));
    value.current_tz_abbreviation = ReadString(GetMember(object, {"current_tz_abbreviation", "current_timezone_abbreviation"}));
    value.current_tz_full_name = ReadString(GetMember(object, {"current_tz_full_name", "current_timezone_name"}));
    value.standard_tz_abbreviation = ReadString(GetMember(object, {"standard_tz_abbreviation", "timezone_abbreviation"}));
    value.standard_tz_full_name = ReadString(GetMember(object, {"standard_tz_full_name", "timezone_name"}));
    value.is_dst = ReadBool(GetMember(object, {"is_dst"}));
    value.dst_savings = ReadNumber(GetMember(object, {"dst_savings"}));
    value.dst_exists = ReadBool(GetMember(object, {"dst_exists"}));
    value.dst_tz_abbreviation = ReadString(GetMember(object, {"dst_tz_abbreviation", "dst_timezone_abbreviation"}));
    value.dst_tz_full_name = ReadString(GetMember(object, {"dst_tz_full_name", "dst_timezone_name"}));
    value.dst_start = ReadObject<DstTransition>(GetMember(object, {"dst_start"}), ParseDstTransition);
    value.dst_end = ReadObject<DstTransition>(GetMember(object, {"dst_end"}), ParseDstTransition);
    return value;
}

Security ParseSecurity(const JsonValue::Object& object) {
    Security value;
    value.threat_score = ReadNumber(GetMember(object, {"threat_score"}));
    value.is_tor = ReadBool(GetMember(object, {"is_tor"}));
    value.is_proxy = ReadBool(GetMember(object, {"is_proxy"}));
    value.proxy_provider_names = ReadStringArray(GetMember(object, {"proxy_provider_names"}));
    value.proxy_confidence_score = ReadNumber(GetMember(object, {"proxy_confidence_score"}));
    value.proxy_last_seen = ReadString(GetMember(object, {"proxy_last_seen"}));
    value.is_residential_proxy = ReadBool(GetMember(object, {"is_residential_proxy"}));
    value.is_vpn = ReadBool(GetMember(object, {"is_vpn"}));
    value.vpn_provider_names = ReadStringArray(GetMember(object, {"vpn_provider_names"}));
    value.vpn_confidence_score = ReadNumber(GetMember(object, {"vpn_confidence_score"}));
    value.vpn_last_seen = ReadString(GetMember(object, {"vpn_last_seen"}));
    value.is_relay = ReadBool(GetMember(object, {"is_relay"}));
    value.relay_provider_name = ReadString(GetMember(object, {"relay_provider_name"}));
    value.is_anonymous = ReadBool(GetMember(object, {"is_anonymous"}));
    value.is_known_attacker = ReadBool(GetMember(object, {"is_known_attacker"}));
    value.is_bot = ReadBool(GetMember(object, {"is_bot"}));
    value.is_spam = ReadBool(GetMember(object, {"is_spam"}));
    value.is_cloud_provider = ReadBool(GetMember(object, {"is_cloud_provider"}));
    value.cloud_provider_name = ReadString(GetMember(object, {"cloud_provider_name"}));
    return value;
}

Abuse ParseAbuse(const JsonValue::Object& object) {
    Abuse value;
    value.route = ReadString(GetMember(object, {"route"}));
    value.country = ReadString(GetMember(object, {"country"}));
    value.name = ReadString(GetMember(object, {"name"}));
    value.organization = ReadString(GetMember(object, {"organization"}));
    value.kind = ReadString(GetMember(object, {"kind"}));
    value.address = ReadString(GetMember(object, {"address"}));
    value.emails = ReadStringArray(GetMember(object, {"emails"}));
    value.phone_numbers = ReadStringArray(GetMember(object, {"phone_numbers"}));
    return value;
}

UserAgentDevice ParseUserAgentDevice(const JsonValue::Object& object) {
    UserAgentDevice value;
    value.name = ReadString(GetMember(object, {"name"}));
    value.type = ReadString(GetMember(object, {"type"}));
    value.brand = ReadString(GetMember(object, {"brand"}));
    value.cpu = ReadString(GetMember(object, {"cpu"}));
    return value;
}

UserAgentEngine ParseUserAgentEngine(const JsonValue::Object& object) {
    UserAgentEngine value;
    value.name = ReadString(GetMember(object, {"name"}));
    value.type = ReadString(GetMember(object, {"type"}));
    value.version = ReadString(GetMember(object, {"version"}));
    value.version_major = ReadString(GetMember(object, {"version_major"}));
    return value;
}

UserAgentOperatingSystem ParseUserAgentOperatingSystem(const JsonValue::Object& object) {
    UserAgentOperatingSystem value;
    value.name = ReadString(GetMember(object, {"name"}));
    value.type = ReadString(GetMember(object, {"type"}));
    value.version = ReadString(GetMember(object, {"version"}));
    value.version_major = ReadString(GetMember(object, {"version_major"}));
    value.build = ReadString(GetMember(object, {"build"}));
    return value;
}

UserAgent ParseUserAgent(const JsonValue::Object& object) {
    UserAgent value;
    value.user_agent_string = ReadString(GetMember(object, {"user_agent_string"}));
    value.name = ReadString(GetMember(object, {"name"}));
    value.type = ReadString(GetMember(object, {"type"}));
    value.version = ReadString(GetMember(object, {"version"}));
    value.version_major = ReadString(GetMember(object, {"version_major"}));
    value.device = ReadObject<UserAgentDevice>(GetMember(object, {"device"}), ParseUserAgentDevice);
    value.engine = ReadObject<UserAgentEngine>(GetMember(object, {"engine"}), ParseUserAgentEngine);
    value.operating_system = ReadObject<UserAgentOperatingSystem>(
        GetMember(object, {"operating_system"}), ParseUserAgentOperatingSystem);
    return value;
}

IpGeolocationResponse ParseResponseObject(const JsonValue::Object& object) {
    IpGeolocationResponse response;
    response.ip = ReadString(GetMember(object, {"ip"}));
    response.hostname = ReadString(GetMember(object, {"hostname"}));
    response.domain = ReadString(GetMember(object, {"domain"}));
    response.location = ReadObject<Location>(GetMember(object, {"location"}), ParseLocation);
    response.country_metadata = ReadObject<CountryMetadata>(
        GetMember(object, {"country_metadata"}), ParseCountryMetadata);
    response.currency = ReadObject<Currency>(GetMember(object, {"currency"}), ParseCurrency);
    response.network = ReadObject<Network>(GetMember(object, {"network"}), ParseNetwork);
    response.asn = ReadObject<Asn>(GetMember(object, {"asn"}), ParseAsn);
    response.company = ReadObject<Company>(GetMember(object, {"company"}), ParseCompany);
    response.time_zone = ReadObject<TimeZoneInfo>(GetMember(object, {"time_zone"}), ParseTimeZoneInfo);
    response.security = ReadObject<Security>(GetMember(object, {"security"}), ParseSecurity);
    response.user_agent = ReadObject<UserAgent>(GetMember(object, {"user_agent"}), ParseUserAgent);
    response.abuse = ReadObject<Abuse>(GetMember(object, {"abuse"}), ParseAbuse);
    return response;
}

std::optional<std::string> ExtractMessageFromErrorObject(const JsonValue::Object& object) {
    if (const auto direct = ReadString(GetMember(object, {"message"})); direct.has_value()) {
        return direct;
    }
    if (const JsonValue* nested_error = GetMember(object, {"error"}); nested_error != nullptr && nested_error->is_object()) {
        if (const auto nested = ReadString(GetMember(nested_error->object_value(), {"message"})); nested.has_value()) {
            return nested;
        }
    }
    if (const JsonValue* nested_detail = GetMember(object, {"detail"}); nested_detail != nullptr && nested_detail->is_object()) {
        if (const auto nested = ReadString(GetMember(nested_detail->object_value(), {"message"})); nested.has_value()) {
            return nested;
        }
    }
    return std::nullopt;
}

}  // namespace

NormalizedConfig NormalizeConfig(const IpGeolocationClientConfig& config) {
    const std::string api_key = NormalizeOptionalNonBlankString(config.api_key, "apiKey", true);
    const std::string request_origin = NormalizeRequestOrigin(config.request_origin);
    const std::string base_url = NormalizeBaseUrl(config.base_url);

    if (config.connect_timeout.count() <= 0) {
        throw ValidationError("connectTimeout must be greater than zero");
    }
    if (config.read_timeout.count() <= 0) {
        throw ValidationError("readTimeout must be greater than zero");
    }
    if (config.connect_timeout > config.read_timeout) {
        throw ValidationError("connectTimeout must be <= readTimeout");
    }
    if (config.max_response_body_chars == 0) {
        throw ValidationError("maxResponseBodyChars must be greater than zero");
    }

    NormalizedConfig normalized;
    if (!api_key.empty()) {
        normalized.api_key = api_key;
    }
    if (!request_origin.empty()) {
        normalized.request_origin = request_origin;
    }
    normalized.base_url = base_url;
    normalized.connect_timeout = config.connect_timeout;
    normalized.read_timeout = config.read_timeout;
    normalized.max_response_body_chars = config.max_response_body_chars;
    return normalized;
}

NormalizedLookupRequest NormalizeLookupRequest(const LookupIpGeolocationRequest& request) {
    NormalizedLookupRequest normalized;
    normalized.ip = NormalizeLookupIp(request.ip);
    normalized.lang = NormalizeLanguage(request.lang);
    normalized.include = NormalizeStringList(request.include, "include");
    normalized.fields = NormalizeStringList(request.fields, "fields");
    normalized.excludes = NormalizeStringList(request.excludes, "excludes");

    const std::string user_agent = NormalizeOptionalNonBlankString(request.user_agent, "userAgent", true);
    if (!user_agent.empty()) {
        normalized.user_agent = user_agent;
    }

    normalized.headers = NormalizeHeaders(request.headers);
    normalized.output = request.output;
    return normalized;
}

NormalizedBulkLookupRequest NormalizeBulkLookupRequest(const BulkLookupIpGeolocationRequest& request) {
    NormalizedBulkLookupRequest normalized;
    normalized.ips = NormalizeIpList(request.ips);
    normalized.lang = NormalizeLanguage(request.lang);
    normalized.include = NormalizeStringList(request.include, "include");
    normalized.fields = NormalizeStringList(request.fields, "fields");
    normalized.excludes = NormalizeStringList(request.excludes, "excludes");

    const std::string user_agent = NormalizeOptionalNonBlankString(request.user_agent, "userAgent", true);
    if (!user_agent.empty()) {
        normalized.user_agent = user_agent;
    }

    normalized.headers = NormalizeHeaders(request.headers);
    normalized.output = request.output;
    return normalized;
}

std::string DefaultUserAgent() {
    return std::string("ipgeolocation-cpp-sdk/") + VERSION;
}

std::string WireValue(ResponseFormat format) {
    return format == ResponseFormat::kXml ? "xml" : "json";
}

std::string LanguageCode(Language language) {
    switch (language) {
        case Language::kEn:
            return "en";
        case Language::kDe:
            return "de";
        case Language::kRu:
            return "ru";
        case Language::kJa:
            return "ja";
        case Language::kFr:
            return "fr";
        case Language::kCn:
            return "cn";
        case Language::kEs:
            return "es";
        case Language::kCs:
            return "cs";
        case Language::kIt:
            return "it";
        case Language::kKo:
            return "ko";
        case Language::kFa:
            return "fa";
        case Language::kPt:
            return "pt";
    }

    return "en";
}

std::string AcceptHeaderFor(ResponseFormat format) {
    return format == ResponseFormat::kXml ? "application/xml" : "application/json";
}

std::string ResolveUserAgentHeader(
    const std::optional<std::string>& request_user_agent,
    const std::map<std::string, std::string>& headers) {
    if (request_user_agent.has_value()) {
        return *request_user_agent;
    }

    const std::string header_user_agent = HeaderValueCaseInsensitive(headers, "User-Agent");
    if (!header_user_agent.empty()) {
        return header_user_agent;
    }

    return DefaultUserAgent();
}

std::unique_ptr<HttpTransport> CreateDefaultTransport() {
    return std::make_unique<CurlHttpTransport>();
}

HttpRequestData BuildLookupHttpRequest(
    const NormalizedConfig& config,
    const NormalizedLookupRequest& request) {
    HttpRequestData data;
    data.url = config.base_url + "/v3/ipgeo" + BuildQueryString({
        {"apiKey", config.api_key.value_or("")},
        {"ip", request.ip.value_or("")},
        {"lang", request.lang.value_or("")},
        {"include", JoinCsv(request.include)},
        {"fields", JoinCsv(request.fields)},
        {"excludes", JoinCsv(request.excludes)},
        {"output", WireValue(request.output)},
    });
    data.method = "GET";
    data.headers = request.headers;
    SetHeaderCaseInsensitive(data.headers, "User-Agent", ResolveUserAgentHeader(request.user_agent, request.headers));
    SetHeaderCaseInsensitive(data.headers, "Accept", AcceptHeaderFor(request.output));
    if (config.request_origin.has_value()) {
        SetHeaderCaseInsensitive(data.headers, "Origin", *config.request_origin);
    }
    data.connect_timeout = config.connect_timeout;
    data.read_timeout = config.read_timeout;
    return data;
}

HttpRequestData BuildBulkHttpRequest(
    const NormalizedConfig& config,
    const NormalizedBulkLookupRequest& request) {
    HttpRequestData data;
    data.url = config.base_url + "/v3/ipgeo-bulk" + BuildQueryString({
        {"apiKey", config.api_key.value_or("")},
        {"lang", request.lang.value_or("")},
        {"include", JoinCsv(request.include)},
        {"fields", JoinCsv(request.fields)},
        {"excludes", JoinCsv(request.excludes)},
        {"output", WireValue(request.output)},
    });
    data.method = "POST";
    data.body = BuildBulkRequestBody(request.ips);
    data.headers = request.headers;
    SetHeaderCaseInsensitive(data.headers, "User-Agent", ResolveUserAgentHeader(request.user_agent, request.headers));
    SetHeaderCaseInsensitive(data.headers, "Accept", AcceptHeaderFor(request.output));
    SetHeaderCaseInsensitive(data.headers, "Content-Type", "application/json");
    if (config.request_origin.has_value()) {
        SetHeaderCaseInsensitive(data.headers, "Origin", *config.request_origin);
    }
    data.connect_timeout = config.connect_timeout;
    data.read_timeout = config.read_timeout;
    return data;
}

ApiResponseMetadata ToMetadata(
    int status_code,
    long long duration_ms,
    const std::map<std::string, std::vector<std::string>>& headers) {
    ApiResponseMetadata metadata;
    metadata.status_code = status_code;
    metadata.duration_ms = duration_ms;
    metadata.raw_headers = headers;
    metadata.credits_charged = ParseHeaderInt(headers, {"X-Credits-Charged"});
    metadata.successful_records = ParseHeaderInt(headers, {"X-Successful-Record", "X-Successful-Records"});
    return metadata;
}

IpGeolocationResponse ParseIpGeolocationResponse(const std::string& body) {
    const JsonValue root = ParseJson(body);
    if (!root.is_object()) {
        throw SerializationError("Failed to deserialize API response: expected an object payload");
    }
    return ParseResponseObject(root.object_value());
}

std::vector<BulkLookupResult> ParseBulkLookupResults(const std::string& body) {
    const JsonValue root = ParseJson(body);
    if (!root.is_array()) {
        throw SerializationError("Failed to deserialize bulk response: expected an array payload");
    }

    std::vector<BulkLookupResult> results;
    results.reserve(root.array_value().size());

    for (const JsonValue& item : root.array_value()) {
        if (!item.is_object()) {
            throw SerializationError("Failed to deserialize bulk response: expected object items");
        }

        const JsonValue::Object& object = item.object_value();
        const bool has_error_key = object.find("error") != object.end();
        const bool looks_like_success = LooksLikeSuccessfulBulkItem(object);
        if (const auto error_message = ExtractMessageFromErrorObject(object);
            has_error_key || (error_message.has_value() && !looks_like_success)) {
            BulkLookupResult result;
            result.error = BulkLookupError{
                error_message.value_or("bulk item returned an error without a parsable message")};
            results.push_back(std::move(result));
            continue;
        }

        if (const JsonValue* data_value = GetMember(object, {"data"});
            data_value != nullptr && data_value->is_object() && object.size() == 1) {
            throw SerializationError("Failed to deserialize bulk response: unexpected wrapped success item");
        }

        BulkLookupResult result;
        result.data = ParseResponseObject(object);
        results.push_back(std::move(result));
    }

    return results;
}

ApiError ToApiError(int status_code, const std::string& body) {
    std::string message = ExtractApiMessage(body);
    if (message.empty()) {
        message = GenericApiErrorMessage(status_code);
    }

    return ApiError(status_code, message, body);
}

std::string ExtractApiMessage(const std::string& body) {
    const std::string trimmed = Trim(body);
    if (trimmed.empty()) {
        return "";
    }

    try {
        const JsonValue root = ParseJson(trimmed);
        if (root.is_string()) {
            return root.string_value();
        }
        if (root.is_object()) {
            if (const auto message = ExtractMessageFromErrorObject(root.object_value()); message.has_value()) {
                return *message;
            }
        }
    } catch (const SerializationError&) {
    }

    return trimmed;
}

std::string EscapeJsonString(const std::string& value) {
    std::ostringstream stream;
    for (const char current : value) {
        const unsigned char byte = static_cast<unsigned char>(current);
        switch (current) {
            case '"':
                stream << "\\\"";
                break;
            case '\\':
                stream << "\\\\";
                break;
            case '\b':
                stream << "\\b";
                break;
            case '\f':
                stream << "\\f";
                break;
            case '\n':
                stream << "\\n";
                break;
            case '\r':
                stream << "\\r";
                break;
            case '\t':
                stream << "\\t";
                break;
            default:
                if (byte < 0x20) {
                    static const char hex_digits[] = "0123456789ABCDEF";
                    stream << "\\u00"
                           << hex_digits[(byte >> 4) & 0x0F]
                           << hex_digits[byte & 0x0F];
                } else {
                    stream << current;
                }
                break;
        }
    }
    return stream.str();
}

std::string BuildBulkRequestBody(const std::vector<std::string>& ips) {
    std::ostringstream stream;
    stream << "{\"ips\":[";
    for (std::size_t index = 0; index < ips.size(); ++index) {
        if (index != 0) {
            stream << ',';
        }
        stream << '"' << EscapeJsonString(ips[index]) << '"';
    }
    stream << "]}";
    return stream.str();
}

}  // namespace ipgeolocation::internal
