# IPGeolocation C++ SDK

Official C++ SDK for the IPGeolocation.io IP Location API.

Look up IPv4, IPv6, and domains with `/v3/ipgeo` and `/v3/ipgeo-bulk`. Get geolocation, company, ASN, timezone, network, hostname, abuse, user-agent, currency, and security data from one API.

- C++17 SDK built on `libcurl`
- Typed responses plus raw JSON and XML methods
- Single include header at `ipgeolocation/ipgeolocation.hpp`

## Table of Contents

- [Install](#install)
- [Quick Start](#quick-start)
- [At a Glance](#at-a-glance)
- [Get Your API Key](#get-your-api-key)
- [Authentication](#authentication)
- [Plan Behavior](#plan-behavior)
- [Client Configuration](#client-configuration)
- [Available Methods](#available-methods)
- [Request Options](#request-options)
- [Examples](#examples)
- [Response Metadata](#response-metadata)
- [Errors](#errors)
- [Troubleshooting](#troubleshooting)
- [Frequently Asked Questions](#frequently-asked-questions)
- [Links](#links)

## Install

Use the SDK from CMake in one of these ways.

The SDK requires `libcurl` with HTTP(S) support. The provided CMake build locates it with `find_package(CURL REQUIRED)`.

### Install with `vcpkg`

Install the package from the public `vcpkg` registry:

```bash
vcpkg install ipgeolocation-cpp-sdk
```

Then link it from your CMake project:

```cmake
find_package(ipgeolocation CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE ipgeolocation::ipgeolocation)
```

If your local `vcpkg` checkout predates the merged port, update `vcpkg` first. The IPGeolocation custom registry at <https://github.com/IPGeolocation/vcpkg-registry> can be used as a fallback while your local baseline catches up.

### Use with `add_subdirectory`

Add the SDK repo next to your project and link the exported target:

```cmake
add_subdirectory(ip-geolocation-api-cpp-sdk)
target_link_libraries(your_target PRIVATE ipgeolocation::ipgeolocation)
```

### Install and use with `find_package`

Install the SDK into a prefix and consume it with `find_package`:

```bash
cmake -S ip-geolocation-api-cpp-sdk -B build
cmake --build build
cmake --install build --prefix /your/install/prefix
```

Then link it from another CMake project:

```cmake
find_package(ipgeolocation CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE ipgeolocation::ipgeolocation)
```

If your install prefix is not on CMake's default search path, set `CMAKE_PREFIX_PATH` when you configure the consuming project.

Public headers live under the `ipgeolocation` include directory:

```cpp
#include <ipgeolocation/ipgeolocation.hpp>
```

CMake target: `ipgeolocation::ipgeolocation`
GitHub repository: <https://github.com/IPGeolocation/ip-geolocation-api-cpp-sdk>

## Quick Start

```cpp
#include <cstdlib>
#include <iostream>
#include <ipgeolocation/ipgeolocation.hpp>

int main() {
    const char* api_key = std::getenv("IPGEO_API_KEY");
    if (api_key == nullptr) {
        throw std::runtime_error("set IPGEO_API_KEY first");
    }

    ipgeolocation::IpGeolocationClientConfig config;
    config.api_key = api_key;

    ipgeolocation::IpGeolocationClient client(config);

    ipgeolocation::LookupIpGeolocationRequest request;
    request.ip = "8.8.8.8";

    auto response = client.LookupIpGeolocation(request);

    if (response.data.ip.has_value()) {
        std::cout << *response.data.ip << '\n'; // 8.8.8.8
    }
    if (response.data.location.has_value() && response.data.location->country_name.has_value()) {
        std::cout << *response.data.location->country_name << '\n'; // United States
    }
    if (response.data.location.has_value() && response.data.location->city.has_value()) {
        std::cout << *response.data.location->city << '\n'; // Mountain View
    }
    if (response.data.time_zone.has_value() && response.data.time_zone->name.has_value()) {
        std::cout << *response.data.time_zone->name << '\n'; // America/Los_Angeles
    }
    std::cout << response.metadata.status_code << '\n'; // 200
    if (response.metadata.credits_charged.has_value()) {
        std::cout << *response.metadata.credits_charged << '\n'; // 1
    }
}
```

Leave `request.ip` unset to resolve the caller IP.

## At a Glance

| Item | Value |
|------|-------|
| CMake target | `ipgeolocation::ipgeolocation` |
| Include header | `ipgeolocation/ipgeolocation.hpp` |
| Namespace | `ipgeolocation` |
| Language standard | C++17 |
| Native dependency | `libcurl` with HTTP(S) support |
| Supported Endpoints | `/v3/ipgeo`, `/v3/ipgeo-bulk` |
| Supported Inputs | IPv4, IPv6, domain |
| Main Data Returned | Geolocation, company, ASN, timezone, network, hostname, abuse, user-agent, currency, security |
| Authentication | API key, request-origin auth for `/v3/ipgeo` only |
| Response Formats | Structured JSON, raw JSON, raw XML |
| Bulk Limit | Up to 50,000 IPs or domains per request |
| Transport | `libcurl` |

## Get Your API Key

Create an IPGeolocation account and copy an API key from your dashboard.

1. Sign up: <https://app.ipgeolocation.io/signup>
2. Verify your email if prompted
3. Sign in: <https://app.ipgeolocation.io/login>
4. Open your dashboard: <https://app.ipgeolocation.io/dashboard>
5. Copy an API key from the `API Keys` section

For server-side C++ code, keep the API key in an environment variable or secret manager. For browser-based single lookups on paid plans, use request-origin auth instead of exposing an API key in frontend code.

## Authentication

### API Key

```cpp
ipgeolocation::IpGeolocationClientConfig config;
if (const char* api_key = std::getenv("IPGEO_API_KEY"); api_key != nullptr) {
    config.api_key = api_key;
}

ipgeolocation::IpGeolocationClient client(config);
```

### Request-Origin Auth

```cpp
ipgeolocation::IpGeolocationClientConfig config;
config.request_origin = "https://app.example.com";

ipgeolocation::IpGeolocationClient client(config);
```

`request_origin` must be an absolute `http` or `https` origin with no path, query string, fragment, or userinfo.

> [!IMPORTANT]
> Request-origin auth does not work with `/v3/ipgeo-bulk`. Bulk lookup always requires `api_key`.

> [!NOTE]
> If you set both `api_key` and `request_origin`, single lookup still uses the API key. The API key is sent as the `apiKey` query parameter, so avoid logging full request URLs.

## Plan Behavior

Feature availability depends on your plan and request parameters.

| Capability | Free | Paid |
|------------|------|------|
| Single IPv4 and IPv6 lookup | Supported | Supported |
| Domain lookup | Not supported | Supported |
| Bulk lookup | Not supported | Supported |
| Non-English `lang` | Not supported | Supported |
| Request-origin auth | Not supported | Supported for `/v3/ipgeo` only |
| Optional modules via `include` | Not supported | Supported |
| `include = {"*"}` | Base response only | All plan-available modules |

Paid plans still need `include` for optional modules. `fields` and `excludes` only trim the response. They do not turn modules on or unlock paid data.

## Client Configuration

| Field | Type | Default | Notes |
|-------|------|---------|-------|
| `api_key` | `std::optional<std::string>` | unset | Required for bulk lookup. Optional for single lookup if `request_origin` is set. |
| `request_origin` | `std::optional<std::string>` | unset | Must be an absolute `http` or `https` origin with no path, query string, fragment, or userinfo. |
| `base_url` | `std::string` | `https://api.ipgeolocation.io` | Override the API base URL. |
| `connect_timeout` | `std::chrono::milliseconds` | `10000ms` | Time to open the connection. Must be greater than zero and must be less than or equal to `read_timeout`. |
| `read_timeout` | `std::chrono::milliseconds` | `30000ms` | Time to wait while reading the response body. Must be greater than zero and must be greater than or equal to `connect_timeout`. |
| `max_response_body_chars` | `std::size_t` | `33554432` | Maximum response body size the SDK will buffer before failing the request. Must be greater than zero. |

Config values are validated when the client is created. Request values are validated before each request is sent.

Request methods are safe to call concurrently on a shared client instance. `Close()` is idempotent, but call it only when no other thread is actively issuing requests.

Typed JSON parsing rejects payloads nested deeper than `256` levels.

The transport buffers up to `max_response_body_chars` bytes of response body data before failing with `response body exceeded maxResponseBodyChars`.

## Available Methods

| Method | Returns | Notes |
|--------|---------|-------|
| `LookupIpGeolocation(request = {})` | `ApiResponse<IpGeolocationResponse>` | Single lookup. Typed JSON response. |
| `LookupIpGeolocationRaw(request = {})` | `ApiResponse<std::string>` | Single lookup. Raw JSON or XML string. |
| `BulkLookupIpGeolocation(request)` | `ApiResponse<std::vector<BulkLookupResult>>` | Bulk lookup. Typed JSON response. |
| `BulkLookupIpGeolocationRaw(request)` | `ApiResponse<std::string>` | Bulk lookup. Raw JSON or XML string. |
| `Close()` | `void` | Closes the client. Closed clients cannot be reused. |
| `DefaultUserAgent()` | `std::string` | Returns the SDK default outbound `User-Agent` header value. |

> [!NOTE]
> Typed methods support JSON only. Use the raw methods when you need XML output.

## Request Options

| Field | Applies To | Notes |
|-------|------------|-------|
| `ip` | Single lookup | IPv4, IPv6, or domain. Leave it unset for caller IP lookup. |
| `ips` | Bulk lookup | Collection of 1 to 50,000 IPs or domains. |
| `lang` | Single and bulk | One of `Language::kEn`, `kDe`, `kRu`, `kJa`, `kFr`, `kCn`, `kEs`, `kCs`, `kIt`, `kKo`, `kFa`, or `kPt`. |
| `include` | Single and bulk | Collection of module names such as `security`, `abuse`, `user_agent`, `hostname`, `liveHostname`, `hostnameFallbackLive`, `geo_accuracy`, `dma_code`, or `*`. |
| `fields` | Single and bulk | Collection of field paths to keep, for example `location.country_name` or `security.threat_score`. |
| `excludes` | Single and bulk | Collection of field paths to remove from the response. |
| `user_agent` | Single and bulk | Overrides the outbound `User-Agent` header. If you also pass a `User-Agent` header in `headers`, `user_agent` wins. |
| `headers` | Single and bulk | Extra request headers as `std::map<std::string, std::string>`. |
| `output` | Single and bulk | `ResponseFormat::kJson` or `ResponseFormat::kXml`. |

## Examples

The examples below assume you already have a configured client in scope:

```cpp
ipgeolocation::IpGeolocationClientConfig config;
if (const char* api_key = std::getenv("IPGEO_API_KEY"); api_key != nullptr) {
    config.api_key = api_key;
}

ipgeolocation::IpGeolocationClient client(config);
```

### Caller IP

Leave `ip` unset to look up the public IP of the machine making the request.

```cpp
auto response = client.LookupIpGeolocation();
if (response.data.ip.has_value()) {
    std::cout << *response.data.ip << '\n';
}
```

### Domain Lookup

Domain lookup is a paid-plan feature.

```cpp
ipgeolocation::LookupIpGeolocationRequest request;
request.ip = "ipgeolocation.io";

auto response = client.LookupIpGeolocation(request);
if (response.data.domain.has_value()) {
    std::cout << *response.data.domain << '\n';
}
if (response.data.location.has_value() && response.data.location->country_name.has_value()) {
    std::cout << *response.data.location->country_name << '\n';
}
```

### Security and Abuse

```cpp
ipgeolocation::LookupIpGeolocationRequest request;
request.ip = "8.8.8.8";
request.include = {"security", "abuse"};

auto response = client.LookupIpGeolocation(request);
if (response.data.security.has_value() && response.data.security->is_proxy.has_value()) {
    std::cout << (*response.data.security->is_proxy ? "true" : "false") << '\n';
}
if (response.data.abuse.has_value() && response.data.abuse->country.has_value()) {
    std::cout << *response.data.abuse->country << '\n';
}
```

### Filtered Response

```cpp
ipgeolocation::LookupIpGeolocationRequest request;
request.ip = "8.8.8.8";
request.include = {"security"};
request.fields = {"location.country_name", "security.threat_score", "security.is_vpn"};
request.excludes = {"currency"};

auto response = client.LookupIpGeolocation(request);
if (response.data.location.has_value() && response.data.location->country_name.has_value()) {
    std::cout << *response.data.location->country_name << '\n';
}
if (response.data.security.has_value() && response.data.security->threat_score.has_value()) {
    std::cout << *response.data.security->threat_score << '\n';
}
```

### Raw XML

```cpp
ipgeolocation::LookupIpGeolocationRequest request;
request.ip = "8.8.8.8";
request.output = ipgeolocation::ResponseFormat::kXml;

auto response = client.LookupIpGeolocationRaw(request);
std::cout << response.data << '\n';
```

### Bulk Lookup

```cpp
ipgeolocation::BulkLookupIpGeolocationRequest request;
request.ips = {"8.8.8.8", "1.1.1.1", "ipgeolocation.io"};

auto response = client.BulkLookupIpGeolocation(request);
for (const auto& result : response.data) {
    if (result.data.has_value() && result.data->ip.has_value()) {
        std::cout << *result.data->ip << '\n';
        continue;
    }
    if (result.error.has_value() && result.error->message.has_value()) {
        std::cout << *result.error->message << '\n';
    }
}
```

### Raw Bulk JSON

```cpp
ipgeolocation::BulkLookupIpGeolocationRequest request;
request.ips = {"8.8.8.8", "1.1.1.1"};

auto response = client.BulkLookupIpGeolocationRaw(request);
std::cout << response.data << '\n';
```

### User-Agent Parsing

To parse a visitor user-agent string, pass `include = {"user_agent"}` and send the visitor string in the request `User-Agent` header.

```cpp
ipgeolocation::LookupIpGeolocationRequest request;
request.ip = "8.8.8.8";
request.include = {"user_agent"};
request.headers = {
    {"User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36"},
};

auto response = client.LookupIpGeolocation(request);
if (response.data.user_agent.has_value() && response.data.user_agent->name.has_value()) {
    std::cout << *response.data.user_agent->name << '\n';
}
```

> [!NOTE]
> The `user_agent` request field overrides the SDK default outbound `User-Agent` header. It takes precedence over `headers["User-Agent"]`.
> `response.data.user_agent` is different. That field is the parsed visitor user-agent data returned by the API.

## Response Metadata

Every method returns:

- `data`: the typed response model or raw response body
- `metadata.status_code`: HTTP status code
- `metadata.duration_ms`: request duration measured by the SDK
- `metadata.credits_charged`: parsed from `X-Credits-Charged` when present
- `metadata.successful_records`: parsed from `X-Successful-Record` or `X-Successful-Records` when present
- `metadata.raw_headers`: all response headers captured by the SDK

Example:

```cpp
std::cout << response.metadata.status_code << '\n';
std::cout << response.metadata.duration_ms << '\n';
if (auto header = response.metadata.raw_headers.find("content-type");
    header != response.metadata.raw_headers.end() && !header->second.empty()) {
    std::cout << header->second.front() << '\n';
}
```

Header names are preserved in the case the server returned them. Response header names from `api.ipgeolocation.io` are lowercase (for example `content-type`, `x-credits-charged`).

## Errors

The SDK throws exceptions derived from `IpGeolocationError`:

- `ValidationError`: invalid config, invalid request, blank headers, bad origin, or missing auth
- `ClientClosedError`: method called after `Close()`
- `TransportError`: `libcurl` transport failure
- `RequestTimeoutError`: connection or read timeout
- `SerializationError`: invalid JSON in typed methods
- `ApiError`: non-2xx API response, including HTTP status code and response body

`ApiError` exposes `status_code()` and `response_body()`. Its `what()` message is taken from the API's JSON error body when present. If the body is not parseable JSON, the message falls back to the raw body or a status-based string.

Example:

```cpp
try {
    auto response = client.LookupIpGeolocation(request);
} catch (const ipgeolocation::ValidationError& error) {
    std::cerr << "validation: " << error.what() << '\n';
} catch (const ipgeolocation::RequestTimeoutError& error) {
    std::cerr << "timeout: " << error.what() << '\n';
} catch (const ipgeolocation::TransportError& error) {
    std::cerr << "transport: " << error.what() << '\n';
} catch (const ipgeolocation::SerializationError& error) {
    std::cerr << "serialization: " << error.what() << '\n';
} catch (const ipgeolocation::ApiError& error) {
    std::cerr << error.status_code() << ' ' << error.what() << '\n';
}
```

`RequestTimeoutError` derives from `TransportError`, so catch `RequestTimeoutError` first if you need to distinguish it.

## Troubleshooting

### `bulk lookup requires apiKey in client config`

Bulk lookup does not support request-origin auth. Set `api_key` in the client config.

### `requestOrigin must not include a path`

Use an origin only, such as `https://app.example.com`. Do not include `/path`, query strings, fragments, or userinfo.

### `headers must not contain CR or LF`

Header names and values are validated before the request is sent. Trim user input before you pass it to the SDK.

### `typed methods support JSON only`

Use `LookupIpGeolocationRaw(...)` or `BulkLookupIpGeolocationRaw(...)` when you need XML output.

### `response body exceeded maxResponseBodyChars`

Increase `max_response_body_chars` in the client config if you expect unusually large raw responses. The default cap is `32 MiB`. Typed JSON parsing still rejects payloads nested deeper than `256` levels.

### `connectTimeout must be <= readTimeout`

`connect_timeout` must be less than or equal to `read_timeout`. Lower `connect_timeout` or raise `read_timeout` so the config validates.

### Omitted response fields

Response fields are `std::optional` so omitted fields stay omitted. Check `has_value()` before dereferencing, or rely on `value_or(...)` when a default is safe.

## Frequently Asked Questions

<details>
<summary>Can I use this SDK without an API key?</summary>

Only for single lookup with paid-plan request-origin auth. Bulk lookup always requires an API key.

</details>

<details>
<summary>Can I request XML and still get typed models?</summary>

No. Typed methods only support JSON. Use `LookupIpGeolocationRaw` or `BulkLookupIpGeolocationRaw` for XML.

</details>

<details>
<summary>Does domain lookup work on the free plan?</summary>

No. Domain lookup is a paid-plan feature.

</details>

<details>
<summary>Why are so many response fields <code>std::optional</code>?</summary>

Optional fields let the SDK preserve omitted API fields instead of inventing empty values for data the API did not send.

</details>

<details>
<summary>Can I use the SDK without an IP address?</summary>

Yes. Leave `ip` unset on single lookup to resolve the caller IP.

</details>

<details>
<summary>What does <code>Close()</code> do?</summary>

It marks the client closed and releases the internal transport state. Closed clients cannot be reused.

</details>

## Links

- Homepage: <https://ipgeolocation.io>
- IP Location API product page: <https://ipgeolocation.io/ip-location-api.html>
- Documentation: <https://ipgeolocation.io/documentation/ip-location-api.html>
- GitHub repository: <https://github.com/IPGeolocation/ip-geolocation-api-cpp-sdk>
