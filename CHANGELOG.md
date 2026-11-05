# Changelog

## 1.0.1

- Add Arabic (`Language::kAr`) for single and bulk lookups.
- Include CDN, proxy, bot and corporate gateway details in typed lookup results.
- Hide API keys in request errors and individual bulk errors.
- Keep the HTTP status when an error response is too large or ends early.
- Reject invalid HTTP headers, malformed JSON text and extra results in typed bulk lookups.
- Fix very long timeouts on Windows and use HTTP/1.1 with libcurl versions affected by the HTTP/2 upload issue.

## 1.0.0

- First C++ SDK release for the IPGeolocation.io IP Location API.
- Added typed and raw support for:
  - `/v3/ipgeo`
  - `/v3/ipgeo-bulk`
- Added request and config validation, response metadata, error mapping, and CMake package export support.
- Added unit tests, live integration tests, live field-parity tests, and a local coverage gate.
