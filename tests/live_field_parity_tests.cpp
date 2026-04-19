#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "internal.hpp"
#include "json.hpp"
#include "ipgeolocation/ipgeolocation.hpp"
#include "test_support.hpp"

namespace {

using ipgeolocation::test_support::EnvFlag;
using ipgeolocation::test_support::EnvValue;
using ipgeolocation::test_support::Expect;

using JsonValue = ipgeolocation::internal::JsonValue;

template <typename T>
void AddOptionalField(
    JsonValue::Object& object,
    const char* name,
    const std::optional<T>& value);

JsonValue ToJsonValue(const std::string& value) {
    return JsonValue::String(value);
}

JsonValue ToJsonValue(bool value) {
    return JsonValue::Bool(value);
}

JsonValue ToJsonValue(double value) {
    return JsonValue::Number(value);
}

JsonValue ToJsonValue(const std::vector<std::string>& values) {
    JsonValue::Array array;
    array.reserve(values.size());
    for (const auto& value : values) {
        array.push_back(JsonValue::String(value));
    }
    return JsonValue::ArrayValue(std::move(array));
}

JsonValue ToJsonValue(const ipgeolocation::Location& value) {
    JsonValue::Object object;
    AddOptionalField(object, "continent_code", value.continent_code);
    AddOptionalField(object, "continent_name", value.continent_name);
    AddOptionalField(object, "country_code2", value.country_code2);
    AddOptionalField(object, "country_code3", value.country_code3);
    AddOptionalField(object, "country_name", value.country_name);
    AddOptionalField(object, "country_name_official", value.country_name_official);
    AddOptionalField(object, "country_capital", value.country_capital);
    AddOptionalField(object, "state_prov", value.state_prov);
    AddOptionalField(object, "state_code", value.state_code);
    AddOptionalField(object, "district", value.district);
    AddOptionalField(object, "city", value.city);
    AddOptionalField(object, "locality", value.locality);
    AddOptionalField(object, "accuracy_radius", value.accuracy_radius);
    AddOptionalField(object, "confidence", value.confidence);
    AddOptionalField(object, "dma_code", value.dma_code);
    AddOptionalField(object, "zipcode", value.zipcode);
    AddOptionalField(object, "latitude", value.latitude);
    AddOptionalField(object, "longitude", value.longitude);
    AddOptionalField(object, "is_eu", value.is_eu);
    AddOptionalField(object, "country_flag", value.country_flag);
    AddOptionalField(object, "geoname_id", value.geoname_id);
    AddOptionalField(object, "country_emoji", value.country_emoji);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::CountryMetadata& value) {
    JsonValue::Object object;
    AddOptionalField(object, "calling_code", value.calling_code);
    AddOptionalField(object, "tld", value.tld);
    AddOptionalField(object, "languages", value.languages);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::Currency& value) {
    JsonValue::Object object;
    AddOptionalField(object, "code", value.code);
    AddOptionalField(object, "name", value.name);
    AddOptionalField(object, "symbol", value.symbol);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::Network& value) {
    JsonValue::Object object;
    AddOptionalField(object, "connection_type", value.connection_type);
    AddOptionalField(object, "route", value.route);
    AddOptionalField(object, "is_anycast", value.is_anycast);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::Asn& value) {
    JsonValue::Object object;
    AddOptionalField(object, "as_number", value.as_number);
    AddOptionalField(object, "organization", value.organization);
    AddOptionalField(object, "country", value.country);
    AddOptionalField(object, "type", value.type);
    AddOptionalField(object, "domain", value.domain);
    AddOptionalField(object, "date_allocated", value.date_allocated);
    AddOptionalField(object, "rir", value.rir);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::Company& value) {
    JsonValue::Object object;
    AddOptionalField(object, "name", value.name);
    AddOptionalField(object, "type", value.type);
    AddOptionalField(object, "domain", value.domain);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::DstTransition& value) {
    JsonValue::Object object;
    AddOptionalField(object, "utc_time", value.utc_time);
    AddOptionalField(object, "duration", value.duration);
    AddOptionalField(object, "gap", value.gap);
    AddOptionalField(object, "date_time_after", value.date_time_after);
    AddOptionalField(object, "date_time_before", value.date_time_before);
    AddOptionalField(object, "overlap", value.overlap);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::TimeZoneInfo& value) {
    JsonValue::Object object;
    AddOptionalField(object, "name", value.name);
    AddOptionalField(object, "offset", value.offset);
    AddOptionalField(object, "offset_with_dst", value.offset_with_dst);
    AddOptionalField(object, "current_time", value.current_time);
    AddOptionalField(object, "current_time_unix", value.current_time_unix);
    AddOptionalField(object, "current_tz_abbreviation", value.current_tz_abbreviation);
    AddOptionalField(object, "current_tz_full_name", value.current_tz_full_name);
    AddOptionalField(object, "standard_tz_abbreviation", value.standard_tz_abbreviation);
    AddOptionalField(object, "standard_tz_full_name", value.standard_tz_full_name);
    AddOptionalField(object, "is_dst", value.is_dst);
    AddOptionalField(object, "dst_savings", value.dst_savings);
    AddOptionalField(object, "dst_exists", value.dst_exists);
    AddOptionalField(object, "dst_tz_abbreviation", value.dst_tz_abbreviation);
    AddOptionalField(object, "dst_tz_full_name", value.dst_tz_full_name);
    AddOptionalField(object, "dst_start", value.dst_start);
    AddOptionalField(object, "dst_end", value.dst_end);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::Security& value) {
    JsonValue::Object object;
    AddOptionalField(object, "threat_score", value.threat_score);
    AddOptionalField(object, "is_tor", value.is_tor);
    AddOptionalField(object, "is_proxy", value.is_proxy);
    AddOptionalField(object, "proxy_provider_names", value.proxy_provider_names);
    AddOptionalField(object, "proxy_confidence_score", value.proxy_confidence_score);
    AddOptionalField(object, "proxy_last_seen", value.proxy_last_seen);
    AddOptionalField(object, "is_residential_proxy", value.is_residential_proxy);
    AddOptionalField(object, "is_vpn", value.is_vpn);
    AddOptionalField(object, "vpn_provider_names", value.vpn_provider_names);
    AddOptionalField(object, "vpn_confidence_score", value.vpn_confidence_score);
    AddOptionalField(object, "vpn_last_seen", value.vpn_last_seen);
    AddOptionalField(object, "is_relay", value.is_relay);
    AddOptionalField(object, "relay_provider_name", value.relay_provider_name);
    AddOptionalField(object, "is_anonymous", value.is_anonymous);
    AddOptionalField(object, "is_known_attacker", value.is_known_attacker);
    AddOptionalField(object, "is_bot", value.is_bot);
    AddOptionalField(object, "is_spam", value.is_spam);
    AddOptionalField(object, "is_cloud_provider", value.is_cloud_provider);
    AddOptionalField(object, "cloud_provider_name", value.cloud_provider_name);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::Abuse& value) {
    JsonValue::Object object;
    AddOptionalField(object, "route", value.route);
    AddOptionalField(object, "country", value.country);
    AddOptionalField(object, "name", value.name);
    AddOptionalField(object, "organization", value.organization);
    AddOptionalField(object, "kind", value.kind);
    AddOptionalField(object, "address", value.address);
    AddOptionalField(object, "emails", value.emails);
    AddOptionalField(object, "phone_numbers", value.phone_numbers);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::UserAgentDevice& value) {
    JsonValue::Object object;
    AddOptionalField(object, "name", value.name);
    AddOptionalField(object, "type", value.type);
    AddOptionalField(object, "brand", value.brand);
    AddOptionalField(object, "cpu", value.cpu);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::UserAgentEngine& value) {
    JsonValue::Object object;
    AddOptionalField(object, "name", value.name);
    AddOptionalField(object, "type", value.type);
    AddOptionalField(object, "version", value.version);
    AddOptionalField(object, "version_major", value.version_major);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::UserAgentOperatingSystem& value) {
    JsonValue::Object object;
    AddOptionalField(object, "name", value.name);
    AddOptionalField(object, "type", value.type);
    AddOptionalField(object, "version", value.version);
    AddOptionalField(object, "version_major", value.version_major);
    AddOptionalField(object, "build", value.build);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::UserAgent& value) {
    JsonValue::Object object;
    AddOptionalField(object, "user_agent_string", value.user_agent_string);
    AddOptionalField(object, "name", value.name);
    AddOptionalField(object, "type", value.type);
    AddOptionalField(object, "version", value.version);
    AddOptionalField(object, "version_major", value.version_major);
    AddOptionalField(object, "device", value.device);
    AddOptionalField(object, "engine", value.engine);
    AddOptionalField(object, "operating_system", value.operating_system);
    return JsonValue::ObjectValue(std::move(object));
}

JsonValue ToJsonValue(const ipgeolocation::IpGeolocationResponse& value) {
    JsonValue::Object object;
    AddOptionalField(object, "ip", value.ip);
    AddOptionalField(object, "hostname", value.hostname);
    AddOptionalField(object, "domain", value.domain);
    AddOptionalField(object, "location", value.location);
    AddOptionalField(object, "country_metadata", value.country_metadata);
    AddOptionalField(object, "currency", value.currency);
    AddOptionalField(object, "network", value.network);
    AddOptionalField(object, "asn", value.asn);
    AddOptionalField(object, "company", value.company);
    AddOptionalField(object, "time_zone", value.time_zone);
    AddOptionalField(object, "security", value.security);
    AddOptionalField(object, "user_agent", value.user_agent);
    AddOptionalField(object, "abuse", value.abuse);
    return JsonValue::ObjectValue(std::move(object));
}

template <typename T>
void AddOptionalField(
    JsonValue::Object& object,
    const char* name,
    const std::optional<T>& value) {
    if (!value.has_value()) {
        return;
    }
    object[name] = ToJsonValue(*value);
}

bool IsLiveClockField(const std::string& path) {
    const auto ends_with = [&path](const char* suffix) {
        const std::string suffix_string(suffix);
        return path.size() >= suffix_string.size() &&
               path.compare(path.size() - suffix_string.size(), suffix_string.size(), suffix_string) == 0;
    };

    return ends_with(".time_zone.current_time") ||
           ends_with(".time_zone.current_time_unix");
}

const JsonValue* FindMember(
    const JsonValue::Object& object,
    const std::string& key) {
    const auto iterator = object.find(key);
    if (iterator == object.end()) {
        return nullptr;
    }
    return &iterator->second;
}

std::optional<std::string> ExtractBulkErrorMessage(const JsonValue& value) {
    if (!value.is_object()) {
        return std::nullopt;
    }

    if (const JsonValue* message = FindMember(value.object_value(), "message");
        message != nullptr && message->is_string()) {
        return message->string_value();
    }

    if (const JsonValue* nested_error = FindMember(value.object_value(), "error");
        nested_error != nullptr && nested_error->is_object()) {
        if (const JsonValue* message = FindMember(nested_error->object_value(), "message");
            message != nullptr && message->is_string()) {
            return message->string_value();
        }
    }

    return std::nullopt;
}

void AssertJsonSubset(const JsonValue& raw, const JsonValue* typed, const std::string& path) {
    if (raw.is_null()) {
        Expect(typed == nullptr || typed->is_null(), path + " should be null or absent");
        return;
    }

    Expect(typed != nullptr, path + " should exist in typed response");

    if (IsLiveClockField(path)) {
        Expect(!typed->is_null(), path + " should be present in typed response");
        return;
    }

    if (raw.is_object()) {
        Expect(typed->is_object(), path + " should be an object");
        for (const auto& [key, raw_value] : raw.object_value()) {
            const JsonValue* typed_value = FindMember(typed->object_value(), key);
            AssertJsonSubset(raw_value, typed_value, path + "." + key);
        }
        return;
    }

    if (raw.is_array()) {
        Expect(typed->is_array(), path + " should be an array");
        Expect(typed->array_value().size() == raw.array_value().size(), path + " size should match");
        for (std::size_t index = 0; index < raw.array_value().size(); ++index) {
            AssertJsonSubset(raw.array_value()[index], &typed->array_value()[index], path + "[" + std::to_string(index) + "]");
        }
        return;
    }

    if (raw.is_number()) {
        Expect(typed->is_number(), path + " should be numeric");
        Expect(std::fabs(typed->number_value() - raw.number_value()) <= 1e-9, path + " numeric value should match");
        return;
    }

    if (raw.is_bool()) {
        Expect(typed->is_bool(), path + " should be boolean");
        Expect(typed->bool_value() == raw.bool_value(), path + " boolean value should match");
        return;
    }

    Expect(raw.is_string(), path + " should be a string");
    Expect(typed->is_string(), path + " should be a string");
    Expect(typed->string_value() == raw.string_value(), path + " string value should match");
}

void AssertSingleLookupParity(
    const std::string& paid_key,
    const ipgeolocation::LookupIpGeolocationRequest& request) {
    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = paid_key;

    ipgeolocation::IpGeolocationClient client(config);
    const auto raw = client.LookupIpGeolocationRaw(request);
    const auto typed = client.LookupIpGeolocation(request);

    const JsonValue raw_value = ipgeolocation::internal::ParseJson(raw.data);
    const JsonValue typed_value = ToJsonValue(typed.data);
    AssertJsonSubset(raw_value, &typed_value, "$");
}

void TestIncludeStarResponseMatchesTypedModel(const std::string& paid_key) {
    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "8.8.8.8";
    request.include = {"*"};
    AssertSingleLookupParity(paid_key, request);
}

void TestGeoAccuracyAndDmaResponseMatchesTypedModel(const std::string& paid_key) {
    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "8.8.8.8";
    request.include = {"geo_accuracy", "dma_code"};
    AssertSingleLookupParity(paid_key, request);
}

void TestDomainLookupResponseMatchesTypedModel(const std::string& paid_key) {
    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "ipgeolocation.io";
    request.include = {"hostnameFallbackLive"};
    AssertSingleLookupParity(paid_key, request);
}

void TestSecurityAbuseAndUserAgentResponseMatchesTypedModel(const std::string& paid_key) {
    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "8.8.8.8";
    request.include = {"security", "abuse", "user_agent"};
    request.user_agent =
        "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_11_2) AppleWebKit/601.3.9 "
        "(KHTML, like Gecko) Version/9.0.2 Safari/601.3.9";
    AssertSingleLookupParity(paid_key, request);
}

void TestBulkMixedResponseMatchesTypedModel(const std::string& paid_key) {
    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = paid_key;

    ipgeolocation::IpGeolocationClient client(config);

    ipgeolocation::BulkLookupIpGeolocationRequest request;
    request.ips = {"8.8.8.8", "invalid-ip", "1.1.1.1"};

    const auto raw = client.BulkLookupIpGeolocationRaw(request);
    const auto typed = client.BulkLookupIpGeolocation(request);

    const JsonValue raw_value = ipgeolocation::internal::ParseJson(raw.data);
    Expect(raw_value.is_array(), "bulk raw response should be a JSON array");
    Expect(typed.data.size() == raw_value.array_value().size(), "typed bulk response should keep raw item count");

    for (std::size_t index = 0; index < raw_value.array_value().size(); ++index) {
        const JsonValue& raw_item = raw_value.array_value()[index];
        const auto& typed_item = typed.data[index];

        if (typed_item.is_success()) {
            Expect(typed_item.data.has_value(), "successful bulk item should expose data");

            const JsonValue* raw_payload = &raw_item;
            if (raw_item.is_object()) {
                if (const JsonValue* nested = FindMember(raw_item.object_value(), "data");
                    nested != nullptr) {
                    raw_payload = nested;
                }
            }

            const JsonValue typed_payload = ToJsonValue(*typed_item.data);
            AssertJsonSubset(*raw_payload, &typed_payload, "$[" + std::to_string(index) + "]");
            continue;
        }

        Expect(typed_item.error.has_value() && typed_item.error->message.has_value(),
               "error bulk item should expose error.message");
        const auto raw_message = ExtractBulkErrorMessage(raw_item);
        Expect(raw_message.has_value(), "raw bulk error item should expose a message");
        Expect(*raw_message == *typed_item.error->message,
               "raw bulk error message should match typed error.message");
    }
}

}  // namespace

int main() {
    if (!EnvFlag("IPGEO_RUN_LIVE_HARDENING")) {
        std::cout << "[SKIP] Set IPGEO_RUN_LIVE_HARDENING=true to enable C++ live field parity tests.\n";
        return 77;
    }

    const auto paid_key = EnvValue("IPGEO_PAID_KEY");
    if (!paid_key.has_value()) {
        std::cout << "[SKIP] Set IPGEO_PAID_KEY to run C++ live field parity tests.\n";
        return 77;
    }

    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"TestIncludeStarResponseMatchesTypedModel",
         [paid_key]() { TestIncludeStarResponseMatchesTypedModel(*paid_key); }},
        {"TestGeoAccuracyAndDmaResponseMatchesTypedModel",
         [paid_key]() { TestGeoAccuracyAndDmaResponseMatchesTypedModel(*paid_key); }},
        {"TestDomainLookupResponseMatchesTypedModel",
         [paid_key]() { TestDomainLookupResponseMatchesTypedModel(*paid_key); }},
        {"TestSecurityAbuseAndUserAgentResponseMatchesTypedModel",
         [paid_key]() { TestSecurityAbuseAndUserAgentResponseMatchesTypedModel(*paid_key); }},
        {"TestBulkMixedResponseMatchesTypedModel",
         [paid_key]() { TestBulkMixedResponseMatchesTypedModel(*paid_key); }},
    };

    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return 1;
        }
    }

    std::cout << "All C++ live field parity tests passed.\n";
    return 0;
}
