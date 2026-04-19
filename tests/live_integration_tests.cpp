#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "ipgeolocation/ipgeolocation.hpp"
#include "test_support.hpp"

namespace {

using ipgeolocation::test_support::EnvFlag;
using ipgeolocation::test_support::EnvValue;
using ipgeolocation::test_support::Expect;
using ipgeolocation::test_support::ExpectThrows;

ipgeolocation::IpGeolocationClientConfig ApiKeyConfig(const std::string& api_key) {
    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = api_key;
    return config;
}

void AssertFreePlanIncludeRejected(
    const std::string& free_key,
    const std::string& include_value) {
    ipgeolocation::IpGeolocationClient client(ApiKeyConfig(free_key));

    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "8.8.8.8";
    request.include = {include_value};

    try {
        static_cast<void>(client.LookupIpGeolocation(request));
    } catch (const ipgeolocation::ApiError& error) {
        Expect(error.status_code() == 401, "free-plan include rejection should return status 401");
        return;
    }

    throw std::runtime_error("expected free-plan include rejection");
}

void TestFreePlanBaseLookupWorks(const std::string& free_key) {
    ipgeolocation::IpGeolocationClient client(ApiKeyConfig(free_key));

    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "8.8.8.8";

    const auto response = client.LookupIpGeolocation(request);
    Expect(response.metadata.status_code == 200, "free-plan lookup should succeed");
    Expect(response.data.ip.has_value() && *response.data.ip == "8.8.8.8", "free-plan lookup should return 8.8.8.8");
    Expect(response.metadata.credits_charged.has_value() && *response.metadata.credits_charged >= 1,
           "free-plan lookup should report at least one charged credit");
    Expect(response.data.time_zone.has_value(), "free-plan lookup should include time zone");
    Expect(response.data.time_zone->current_tz_abbreviation.has_value() &&
               !response.data.time_zone->current_tz_abbreviation->empty(),
           "free-plan lookup should include a current time zone abbreviation");
    Expect(response.data.time_zone->current_tz_full_name.has_value() &&
               !response.data.time_zone->current_tz_full_name->empty(),
           "free-plan lookup should include a current time zone name");
}

void TestFreePlanSecurityModuleIsRejected(const std::string& free_key) {
    AssertFreePlanIncludeRejected(free_key, "security");
}

void TestFreePlanIncludeStarReturnsBaseResponseOnly(const std::string& free_key) {
    ipgeolocation::IpGeolocationClient client(ApiKeyConfig(free_key));

    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "8.8.8.8";
    request.include = {"*"};

    const auto response = client.LookupIpGeolocation(request);
    Expect(response.data.ip.has_value() && *response.data.ip == "8.8.8.8", "include=* should still return base lookup data");
    Expect(!response.data.security.has_value(), "free-plan include=* should not unlock security");
    Expect(!response.data.abuse.has_value(), "free-plan include=* should not unlock abuse");
    Expect(!response.data.user_agent.has_value(), "free-plan include=* should not unlock user-agent parsing");
}

void TestFreePlanBulkIsRejected(const std::string& free_key) {
    ipgeolocation::IpGeolocationClient client(ApiKeyConfig(free_key));

    ipgeolocation::BulkLookupIpGeolocationRequest request;
    request.ips = {"8.8.8.8"};

    try {
        static_cast<void>(client.BulkLookupIpGeolocation(request));
    } catch (const ipgeolocation::ApiError& error) {
        Expect(error.status_code() == 401, "free-plan bulk should return status 401");
        return;
    }

    throw std::runtime_error("expected free-plan bulk rejection");
}

void TestPaidPlanSecurityAndAbuseWorks(const std::string& paid_key) {
    ipgeolocation::IpGeolocationClient client(ApiKeyConfig(paid_key));

    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "8.8.8.8";
    request.include = {"security", "abuse"};

    const auto response = client.LookupIpGeolocation(request);
    Expect(response.metadata.status_code == 200, "paid-plan security lookup should succeed");
    Expect(response.data.security.has_value(), "paid-plan lookup should include security");
    Expect(response.data.abuse.has_value(), "paid-plan lookup should include abuse");
    Expect(response.metadata.credits_charged.has_value() && *response.metadata.credits_charged >= 1,
           "paid-plan security lookup should charge at least one credit");
}

void TestPaidPlanBulkMixedReturnsSuccessAndErrorItems(const std::string& paid_key) {
    ipgeolocation::IpGeolocationClient client(ApiKeyConfig(paid_key));

    ipgeolocation::BulkLookupIpGeolocationRequest request;
    request.ips = {"8.8.8.8", "invalid-ip"};

    const auto response = client.BulkLookupIpGeolocation(request);
    Expect(response.metadata.status_code == 200, "paid-plan bulk should succeed");
    Expect(response.data.size() == 2, "paid-plan bulk should return two items");
    Expect(response.data[0].is_success(), "first bulk item should be successful");
    Expect(response.data[0].data.has_value() && response.data[0].data->ip.has_value() &&
               *response.data[0].data->ip == "8.8.8.8",
           "first bulk item should parse the good IP");
    Expect(response.data[1].error.has_value() && response.data[1].error->message.has_value() &&
               !response.data[1].error->message->empty(),
           "second bulk item should expose error.message");
}

void TestPaidPlanXmlOutputIsRejectedForTypedMethod(const std::string& paid_key) {
    ipgeolocation::IpGeolocationClient client(ApiKeyConfig(paid_key));

    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "8.8.8.8";
    request.output = ipgeolocation::ResponseFormat::kXml;

    ExpectThrows<ipgeolocation::ValidationError>(
        [&client, &request]() {
            static_cast<void>(client.LookupIpGeolocation(request));
        },
        "typed methods support JSON only");
}

void TestPaidPlanIncludeUserAgentReflectsRequestFieldOverride(const std::string& paid_key) {
    ipgeolocation::IpGeolocationClient client(ApiKeyConfig(paid_key));

    ipgeolocation::LookupIpGeolocationRequest default_request;
    default_request.ip = "8.8.8.8";
    default_request.include = {"user_agent"};

    const auto default_response = client.LookupIpGeolocation(default_request);
    Expect(default_response.data.user_agent.has_value() &&
               default_response.data.user_agent->user_agent_string.has_value() &&
               !default_response.data.user_agent->user_agent_string->empty(),
           "default user-agent lookup should return a parsed user-agent string");

    ipgeolocation::LookupIpGeolocationRequest override_request = default_request;
    override_request.user_agent = "python-requests/2.32.5";

    const auto override_response = client.LookupIpGeolocation(override_request);
    Expect(override_response.data.user_agent.has_value() &&
               override_response.data.user_agent->user_agent_string.has_value(),
           "overridden user-agent lookup should return a parsed user-agent string");
    Expect(*override_response.data.user_agent->user_agent_string == "python-requests/2.32.5",
           "request user_agent field should win on live requests");
    Expect(*override_response.data.user_agent->user_agent_string !=
               *default_response.data.user_agent->user_agent_string,
           "request user_agent field should produce a different parsed value");
}

void TestPaidPlanRawMethodsReturnXmlWhenRequested(const std::string& paid_key) {
    ipgeolocation::IpGeolocationClient client(ApiKeyConfig(paid_key));

    ipgeolocation::LookupIpGeolocationRequest single_request;
    single_request.ip = "8.8.8.8";
    single_request.output = ipgeolocation::ResponseFormat::kXml;

    const auto single = client.LookupIpGeolocationRaw(single_request);
    Expect(single.metadata.status_code == 200, "raw single XML lookup should succeed");
    Expect(single.data.find('<') != std::string::npos, "raw single XML lookup should return XML");

    ipgeolocation::BulkLookupIpGeolocationRequest bulk_request;
    bulk_request.ips = {"8.8.8.8", "invalid-ip"};
    bulk_request.output = ipgeolocation::ResponseFormat::kXml;

    const auto bulk = client.BulkLookupIpGeolocationRaw(bulk_request);
    Expect(bulk.metadata.status_code == 200, "raw bulk XML lookup should succeed");
    Expect(bulk.data.find('<') != std::string::npos, "raw bulk XML lookup should return XML");
}

}  // namespace

int main() {
    if (!EnvFlag("IPGEO_RUN_LIVE_TESTS")) {
        std::cout << "[SKIP] Set IPGEO_RUN_LIVE_TESTS=true to enable C++ live integration tests.\n";
        return 77;
    }

    const auto free_key = EnvValue("IPGEO_FREE_KEY");
    const auto paid_key = EnvValue("IPGEO_PAID_KEY");
    if (!free_key.has_value() || !paid_key.has_value()) {
        std::cout << "[SKIP] Set IPGEO_FREE_KEY and IPGEO_PAID_KEY to run C++ live integration tests.\n";
        return 77;
    }

    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"TestFreePlanBaseLookupWorks", [free_key]() { TestFreePlanBaseLookupWorks(*free_key); }},
        {"TestFreePlanSecurityModuleIsRejected", [free_key]() { TestFreePlanSecurityModuleIsRejected(*free_key); }},
        {"TestFreePlanIncludeStarReturnsBaseResponseOnly",
         [free_key]() { TestFreePlanIncludeStarReturnsBaseResponseOnly(*free_key); }},
        {"TestFreePlanBulkIsRejected", [free_key]() { TestFreePlanBulkIsRejected(*free_key); }},
        {"TestPaidPlanSecurityAndAbuseWorks", [paid_key]() { TestPaidPlanSecurityAndAbuseWorks(*paid_key); }},
        {"TestPaidPlanBulkMixedReturnsSuccessAndErrorItems",
         [paid_key]() { TestPaidPlanBulkMixedReturnsSuccessAndErrorItems(*paid_key); }},
        {"TestPaidPlanXmlOutputIsRejectedForTypedMethod",
         [paid_key]() { TestPaidPlanXmlOutputIsRejectedForTypedMethod(*paid_key); }},
        {"TestPaidPlanIncludeUserAgentReflectsRequestFieldOverride",
         [paid_key]() { TestPaidPlanIncludeUserAgentReflectsRequestFieldOverride(*paid_key); }},
        {"TestPaidPlanRawMethodsReturnXmlWhenRequested",
         [paid_key]() { TestPaidPlanRawMethodsReturnXmlWhenRequested(*paid_key); }},
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

    std::cout << "All C++ live integration tests passed.\n";
    return 0;
}
