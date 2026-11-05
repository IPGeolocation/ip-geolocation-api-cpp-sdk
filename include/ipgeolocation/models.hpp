#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "ipgeolocation/visibility.hpp"

namespace ipgeolocation {

struct IPGEOLOCATION_API Location {
    std::optional<std::string> continent_code;
    std::optional<std::string> continent_name;
    std::optional<std::string> country_code2;
    std::optional<std::string> country_code3;
    std::optional<std::string> country_name;
    std::optional<std::string> country_name_official;
    std::optional<std::string> country_capital;
    std::optional<std::string> state_prov;
    std::optional<std::string> state_code;
    std::optional<std::string> district;
    std::optional<std::string> city;
    std::optional<std::string> locality;
    std::optional<std::string> accuracy_radius;
    std::optional<std::string> confidence;
    std::optional<std::string> dma_code;
    std::optional<std::string> zipcode;
    std::optional<std::string> latitude;
    std::optional<std::string> longitude;
    std::optional<bool> is_eu;
    std::optional<std::string> country_flag;
    std::optional<std::string> geoname_id;
    std::optional<std::string> country_emoji;
};

struct IPGEOLOCATION_API CountryMetadata {
    std::optional<std::string> calling_code;
    std::optional<std::string> tld;
    std::optional<std::vector<std::string>> languages;
};

struct IPGEOLOCATION_API Currency {
    std::optional<std::string> code;
    std::optional<std::string> name;
    std::optional<std::string> symbol;
};

struct IPGEOLOCATION_API Network {
    std::optional<std::string> connection_type;
    std::optional<std::string> route;
    std::optional<bool> is_anycast;
    std::optional<bool> is_cdn{};
    std::optional<std::string> cdn_provider_name{};
};

struct IPGEOLOCATION_API Asn {
    std::optional<std::string> as_number;
    std::optional<std::string> organization;
    std::optional<std::string> country;
    std::optional<std::string> type;
    std::optional<std::string> domain;
    std::optional<std::string> date_allocated;
    std::optional<std::string> rir;
};

struct IPGEOLOCATION_API Company {
    std::optional<std::string> name;
    std::optional<std::string> type;
    std::optional<std::string> domain;
};

struct IPGEOLOCATION_API DstTransition {
    std::optional<std::string> utc_time;
    std::optional<std::string> duration;
    std::optional<bool> gap;
    std::optional<std::string> date_time_after;
    std::optional<std::string> date_time_before;
    std::optional<bool> overlap;
};

struct IPGEOLOCATION_API TimeZoneInfo {
    std::optional<std::string> name;
    std::optional<double> offset;
    std::optional<double> offset_with_dst;
    std::optional<std::string> current_time;
    std::optional<double> current_time_unix;
    std::optional<std::string> current_tz_abbreviation;
    std::optional<std::string> current_tz_full_name;
    std::optional<std::string> standard_tz_abbreviation;
    std::optional<std::string> standard_tz_full_name;
    std::optional<bool> is_dst;
    std::optional<double> dst_savings;
    std::optional<bool> dst_exists;
    std::optional<std::string> dst_tz_abbreviation;
    std::optional<std::string> dst_tz_full_name;
    std::optional<DstTransition> dst_start;
    std::optional<DstTransition> dst_end;
};

struct IPGEOLOCATION_API Security {
    std::optional<double> threat_score;
    std::optional<bool> is_tor;
    std::optional<bool> is_proxy;
    std::optional<std::vector<std::string>> proxy_provider_names;
    std::optional<double> proxy_confidence_score;
    std::optional<std::string> proxy_last_seen;
    std::optional<bool> is_residential_proxy;
    std::optional<bool> is_vpn;
    std::optional<std::vector<std::string>> vpn_provider_names;
    std::optional<double> vpn_confidence_score;
    std::optional<std::string> vpn_last_seen;
    std::optional<bool> is_relay;
    std::optional<std::string> relay_provider_name;
    std::optional<bool> is_anonymous;
    std::optional<bool> is_known_attacker;
    std::optional<bool> is_bot;
    std::optional<bool> is_spam;
    std::optional<bool> is_cloud_provider;
    std::optional<std::string> cloud_provider_name;
    std::optional<bool> is_known_good_bot{};
    std::optional<bool> is_corporate_gateway{};
    std::optional<double> proxy_days_seen_30d{};
    std::optional<double> proxy_days_seen_7d{};
    std::optional<double> proxy_provider_count_30d{};
    std::optional<double> bot_confidence_score{};
    std::optional<std::string> bot_operator_name{};
    std::optional<std::string> bot_type{};
    std::optional<std::string> bot_last_seen{};
    std::optional<std::string> corporate_gateway_type{};
    std::optional<std::string> corporate_gateway_provider_name{};
};

struct IPGEOLOCATION_API Abuse {
    std::optional<std::string> route;
    std::optional<std::string> country;
    std::optional<std::string> name;
    std::optional<std::string> organization;
    std::optional<std::string> kind;
    std::optional<std::string> address;
    std::optional<std::vector<std::string>> emails;
    std::optional<std::vector<std::string>> phone_numbers;
};

struct IPGEOLOCATION_API UserAgentDevice {
    std::optional<std::string> name;
    std::optional<std::string> type;
    std::optional<std::string> brand;
    std::optional<std::string> cpu;
};

struct IPGEOLOCATION_API UserAgentEngine {
    std::optional<std::string> name;
    std::optional<std::string> type;
    std::optional<std::string> version;
    std::optional<std::string> version_major;
};

struct IPGEOLOCATION_API UserAgentOperatingSystem {
    std::optional<std::string> name;
    std::optional<std::string> type;
    std::optional<std::string> version;
    std::optional<std::string> version_major;
    std::optional<std::string> build;
};

struct IPGEOLOCATION_API UserAgent {
    std::optional<std::string> user_agent_string;
    std::optional<std::string> name;
    std::optional<std::string> type;
    std::optional<std::string> version;
    std::optional<std::string> version_major;
    std::optional<UserAgentDevice> device;
    std::optional<UserAgentEngine> engine;
    std::optional<UserAgentOperatingSystem> operating_system;
};

struct IPGEOLOCATION_API IpGeolocationResponse {
    std::optional<std::string> ip;
    std::optional<std::string> hostname;
    std::optional<std::string> domain;
    std::optional<Location> location;
    std::optional<CountryMetadata> country_metadata;
    std::optional<Currency> currency;
    std::optional<Network> network;
    std::optional<Asn> asn;
    std::optional<Company> company;
    std::optional<TimeZoneInfo> time_zone;
    std::optional<Security> security;
    std::optional<UserAgent> user_agent;
    std::optional<Abuse> abuse;
};

struct IPGEOLOCATION_API BulkLookupError {
    std::optional<std::string> message;
};

struct IPGEOLOCATION_API BulkLookupResult {
    std::optional<IpGeolocationResponse> data;
    std::optional<BulkLookupError> error;

    bool is_success() const noexcept {
        return data.has_value() && !error.has_value();
    }
};

namespace internal {

template <typename Model>
struct BindingMembers;

template <>
struct BindingMembers<Network> {
    inline static constexpr auto fields =
        std::make_tuple(&Network::connection_type, &Network::route, &Network::is_anycast);
};

template <>
struct BindingMembers<Security> {
    inline static constexpr auto fields = std::make_tuple(
        &Security::threat_score, &Security::is_tor, &Security::is_proxy, &Security::proxy_provider_names,
        &Security::proxy_confidence_score, &Security::proxy_last_seen, &Security::is_residential_proxy,
        &Security::is_vpn, &Security::vpn_provider_names, &Security::vpn_confidence_score, &Security::vpn_last_seen,
        &Security::is_relay, &Security::relay_provider_name, &Security::is_anonymous, &Security::is_known_attacker,
        &Security::is_bot, &Security::is_spam, &Security::is_cloud_provider, &Security::cloud_provider_name);
};

}

template <std::size_t I, typename Model,
          std::enable_if_t<std::is_same_v<std::decay_t<Model>, Network> ||
                           std::is_same_v<std::decay_t<Model>, Security>, int> = 0>
constexpr decltype(auto) get(Model&& value) noexcept {
    constexpr auto field = std::get<I>(internal::BindingMembers<std::decay_t<Model>>::fields);
    return (std::forward<Model>(value).*field);
}

}  // namespace ipgeolocation

namespace std {

template <>
struct tuple_size<ipgeolocation::Network>
    : tuple_size<decltype(ipgeolocation::internal::BindingMembers<ipgeolocation::Network>::fields)> {};

template <>
struct tuple_size<ipgeolocation::Security>
    : tuple_size<decltype(ipgeolocation::internal::BindingMembers<ipgeolocation::Security>::fields)> {};

template <size_t I>
struct tuple_element<I, ipgeolocation::Network> {
    using type = remove_reference_t<decltype(ipgeolocation::get<I>(declval<ipgeolocation::Network&>()))>;
};

template <size_t I>
struct tuple_element<I, ipgeolocation::Security> {
    using type = remove_reference_t<decltype(ipgeolocation::get<I>(declval<ipgeolocation::Security&>()))>;
};

}
