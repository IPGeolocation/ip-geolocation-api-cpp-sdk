#pragma once

#include <map>
#include <string>
#include <vector>

namespace ipgeolocation::internal {

class JsonValue {
public:
    enum class Type {
        kNull,
        kBool,
        kNumber,
        kString,
        kObject,
        kArray,
    };

    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;

    JsonValue();

    static JsonValue Null();
    static JsonValue Bool(bool value);
    static JsonValue Number(double value);
    static JsonValue String(std::string value);
    static JsonValue ObjectValue(Object value);
    static JsonValue ArrayValue(Array value);

    Type type() const noexcept;
    bool is_null() const noexcept;
    bool is_bool() const noexcept;
    bool is_number() const noexcept;
    bool is_string() const noexcept;
    bool is_object() const noexcept;
    bool is_array() const noexcept;

    bool bool_value() const;
    double number_value() const;
    const std::string& string_value() const;
    const Object& object_value() const;
    const Array& array_value() const;

private:
    Type type_;
    bool bool_value_;
    double number_value_;
    std::string string_value_;
    Object object_value_;
    Array array_value_;
};

JsonValue ParseJson(const std::string& input);

}  // namespace ipgeolocation::internal
