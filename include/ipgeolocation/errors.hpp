#pragma once

#include <stdexcept>
#include <string>

#include "ipgeolocation/visibility.hpp"

namespace ipgeolocation {

class IPGEOLOCATION_API IpGeolocationError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    ~IpGeolocationError() override = default;
};

class IPGEOLOCATION_API ValidationError : public IpGeolocationError {
public:
    using IpGeolocationError::IpGeolocationError;
};

class IPGEOLOCATION_API ClientClosedError : public IpGeolocationError {
public:
    using IpGeolocationError::IpGeolocationError;
};

class IPGEOLOCATION_API TransportError : public IpGeolocationError {
public:
    using IpGeolocationError::IpGeolocationError;
};

class IPGEOLOCATION_API SerializationError : public IpGeolocationError {
public:
    using IpGeolocationError::IpGeolocationError;
};

class IPGEOLOCATION_API RequestTimeoutError : public TransportError {
public:
    using TransportError::TransportError;
};

class IPGEOLOCATION_API ApiError : public IpGeolocationError {
public:
    ApiError(int status_code, std::string message, std::string response_body = "");

    int status_code() const noexcept;
    const std::string& response_body() const noexcept;

private:
    int status_code_;
    std::string response_body_;
};

}  // namespace ipgeolocation
