#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

namespace ipgeolocation::test_support {

inline std::string Trim(const std::string& value) {
    std::size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }

    std::size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return value.substr(start, end - start);
}

inline std::optional<std::string> EnvValue(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }

    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    return trimmed;
}

inline bool EnvFlag(const char* name) {
    const auto value = EnvValue(name);
    if (!value.has_value()) {
        return false;
    }

    std::string normalized = *value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    return normalized == "1" || normalized == "true" || normalized == "yes";
}

inline void Expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename ExceptionType>
inline void ExpectThrows(const std::function<void()>& callback, const std::string& expected_message) {
    try {
        callback();
    } catch (const ExceptionType& error) {
        Expect(error.what() == expected_message,
               "expected error message '" + expected_message + "', got '" + error.what() + "'");
        return;
    }

    throw std::runtime_error("expected exception was not thrown");
}

inline void ExpectNear(double actual, double expected, double epsilon, const std::string& message) {
    if (std::fabs(actual - expected) > epsilon) {
        throw std::runtime_error(message);
    }
}

}  // namespace ipgeolocation::test_support
