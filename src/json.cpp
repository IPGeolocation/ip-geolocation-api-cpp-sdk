#include "json.hpp"

#include <charconv>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <locale.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "ipgeolocation/errors.hpp"

namespace ipgeolocation::internal {

JsonValue::JsonValue()
    : type_(Type::kNull),
      bool_value_(false),
      number_value_(0.0) {}

JsonValue JsonValue::Null() {
    return JsonValue();
}

JsonValue JsonValue::Bool(bool value) {
    JsonValue result;
    result.type_ = Type::kBool;
    result.bool_value_ = value;
    return result;
}

JsonValue JsonValue::Number(double value) {
    JsonValue result;
    result.type_ = Type::kNumber;
    result.number_value_ = value;
    return result;
}

JsonValue JsonValue::String(std::string value) {
    JsonValue result;
    result.type_ = Type::kString;
    result.string_value_ = std::move(value);
    return result;
}

JsonValue JsonValue::ObjectValue(Object value) {
    JsonValue result;
    result.type_ = Type::kObject;
    result.object_value_ = std::move(value);
    return result;
}

JsonValue JsonValue::ArrayValue(Array value) {
    JsonValue result;
    result.type_ = Type::kArray;
    result.array_value_ = std::move(value);
    return result;
}

JsonValue::Type JsonValue::type() const noexcept {
    return type_;
}

bool JsonValue::is_null() const noexcept {
    return type_ == Type::kNull;
}

bool JsonValue::is_bool() const noexcept {
    return type_ == Type::kBool;
}

bool JsonValue::is_number() const noexcept {
    return type_ == Type::kNumber;
}

bool JsonValue::is_string() const noexcept {
    return type_ == Type::kString;
}

bool JsonValue::is_object() const noexcept {
    return type_ == Type::kObject;
}

bool JsonValue::is_array() const noexcept {
    return type_ == Type::kArray;
}

bool JsonValue::bool_value() const {
    return bool_value_;
}

double JsonValue::number_value() const {
    return number_value_;
}

const std::string& JsonValue::string_value() const {
    return string_value_;
}

const JsonValue::Object& JsonValue::object_value() const {
    return object_value_;
}

const JsonValue::Array& JsonValue::array_value() const {
    return array_value_;
}

namespace {

constexpr std::size_t kMaxJsonNestingDepth = 256;

bool IsAsciiDigit(char value) {
    return value >= '0' && value <= '9';
}

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

double ParseLocaleIndependentDouble(std::string_view input) {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    double value = 0.0;
    const char* begin = input.data();
    const char* end = begin + input.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec == std::errc() && result.ptr == end) {
        return value;
    }
#endif

    const std::string owned(input);

#if defined(_WIN32)
    _locale_t c_locale = _create_locale(LC_NUMERIC, "C");
    if (c_locale == nullptr) {
        throw SerializationError("Failed to parse JSON: invalid number");
    }

    char* parse_end = nullptr;
    errno = 0;
    const double value = _strtod_l(owned.c_str(), &parse_end, c_locale);
    _free_locale(c_locale);
#else
    locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", nullptr);
    if (c_locale == nullptr) {
        throw SerializationError("Failed to parse JSON: invalid number");
    }

    char* parse_end = nullptr;
    errno = 0;
    const double value = strtod_l(owned.c_str(), &parse_end, c_locale);
    freelocale(c_locale);
#endif

    if (parse_end != owned.c_str() + owned.size() || errno == ERANGE) {
        throw SerializationError("Failed to parse JSON: invalid number");
    }

    return value;
}

class JsonParser {
public:
    explicit JsonParser(const std::string& input) : input_(input) {}

    JsonValue Parse() {
        SkipWhitespace();
        const JsonValue value = ParseValue(0);
        SkipWhitespace();
        if (!AtEnd()) {
            Fail("unexpected trailing characters");
        }
        return value;
    }

private:
    JsonValue ParseValue(std::size_t depth) {
        if (AtEnd()) {
            Fail("unexpected end of input");
        }

        const char current = input_[index_];
        switch (current) {
            case 'n':
                return ParseNull();
            case 't':
            case 'f':
                return ParseBool();
            case '"':
                return JsonValue::String(ParseString());
            case '[':
                return ParseArray(depth);
            case '{':
                return ParseObject(depth);
            default:
                if (current == '-' || IsAsciiDigit(current)) {
                    return JsonValue::Number(ParseNumber());
                }
                Fail("unexpected character");
        }
    }

    JsonValue ParseNull() {
        ExpectKeyword("null");
        return JsonValue::Null();
    }

    JsonValue ParseBool() {
        if (MatchKeyword("true")) {
            return JsonValue::Bool(true);
        }
        if (MatchKeyword("false")) {
            return JsonValue::Bool(false);
        }
        Fail("invalid boolean");
    }

    JsonValue ParseObject(std::size_t depth) {
        if (depth >= kMaxJsonNestingDepth) {
            Fail("maximum nesting depth exceeded");
        }

        Expect('{');
        JsonValue::Object object;
        SkipWhitespace();
        if (Consume('}')) {
            return JsonValue::ObjectValue(std::move(object));
        }

        while (true) {
            SkipWhitespace();
            if (!Consume('"')) {
                Fail("expected string object key");
            }
            --index_;
            const std::string key = ParseString();
            SkipWhitespace();
            Expect(':');
            SkipWhitespace();
            object.insert_or_assign(key, ParseValue(depth + 1));
            SkipWhitespace();
            if (Consume('}')) {
                break;
            }
            Expect(',');
            SkipWhitespace();
        }

        return JsonValue::ObjectValue(std::move(object));
    }

    JsonValue ParseArray(std::size_t depth) {
        if (depth >= kMaxJsonNestingDepth) {
            Fail("maximum nesting depth exceeded");
        }

        Expect('[');
        JsonValue::Array array;
        SkipWhitespace();
        if (Consume(']')) {
            return JsonValue::ArrayValue(std::move(array));
        }

        while (true) {
            SkipWhitespace();
            array.push_back(ParseValue(depth + 1));
            SkipWhitespace();
            if (Consume(']')) {
                break;
            }
            Expect(',');
            SkipWhitespace();
        }

        return JsonValue::ArrayValue(std::move(array));
    }

    std::string ParseString() {
        Expect('"');
        std::string result;
        while (!AtEnd()) {
            const char current = input_[index_++];
            if (current == '"') {
                return result;
            }
            if (current != '\\') {
                result.push_back(current);
                continue;
            }

            if (AtEnd()) {
                Fail("unexpected end of escape sequence");
            }

            const char escaped = input_[index_++];
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    result.push_back(escaped);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case 'u':
                    AppendUnicodeEscape(result);
                    break;
                default:
                    Fail("invalid escape sequence");
            }
        }

        Fail("unterminated string");
    }

    double ParseNumber() {
        const std::size_t start = index_;

        if (Peek() == '-') {
            ++index_;
        }

        if (Peek() == '0') {
            ++index_;
        } else {
            ConsumeDigits();
        }

        if (Peek() == '.') {
            ++index_;
            ConsumeDigits();
        }

        if (Peek() == 'e' || Peek() == 'E') {
            ++index_;
            if (Peek() == '+' || Peek() == '-') {
                ++index_;
            }
            ConsumeDigits();
        }

        try {
            return ParseLocaleIndependentDouble(
                std::string_view(input_).substr(start, index_ - start));
        } catch (const SerializationError&) {
            throw;
        } catch (...) {
            Fail("invalid number");
        }
    }

    char Peek() const {
        if (AtEnd()) {
            return '\0';
        }
        return input_[index_];
    }

    void ConsumeDigits() {
        const std::size_t start = index_;
        while (!AtEnd() && IsAsciiDigit(input_[index_])) {
            ++index_;
        }
        if (start == index_) {
            Fail("expected digits");
        }
    }

    std::uint32_t ParseHexCodePoint() {
        if (index_ + 4 > input_.size()) {
            Fail("incomplete unicode escape");
        }

        std::uint32_t code_point = 0;
        for (int count = 0; count < 4; ++count) {
            const char current = input_[index_++];
            code_point <<= 4;
            if (current >= '0' && current <= '9') {
                code_point |= static_cast<std::uint32_t>(current - '0');
            } else if (current >= 'a' && current <= 'f') {
                code_point |= static_cast<std::uint32_t>(10 + current - 'a');
            } else if (current >= 'A' && current <= 'F') {
                code_point |= static_cast<std::uint32_t>(10 + current - 'A');
            } else {
                Fail("invalid unicode escape");
            }
        }
        return code_point;
    }

    void AppendUnicodeEscape(std::string& target) {
        const std::uint32_t first_code_unit = ParseHexCodePoint();
        if (first_code_unit >= 0xD800 && first_code_unit <= 0xDBFF) {
            if (AtEnd() || input_[index_] != '\\') {
                Fail("invalid unicode surrogate pair");
            }
            ++index_;
            if (AtEnd() || input_[index_] != 'u') {
                Fail("invalid unicode surrogate pair");
            }
            ++index_;

            const std::uint32_t second_code_unit = ParseHexCodePoint();
            if (second_code_unit < 0xDC00 || second_code_unit > 0xDFFF) {
                Fail("invalid unicode surrogate pair");
            }

            const std::uint32_t code_point =
                0x10000u + (((first_code_unit - 0xD800u) << 10) | (second_code_unit - 0xDC00u));
            AppendUnicodeCodePoint(target, code_point);
            return;
        }

        if (first_code_unit >= 0xDC00 && first_code_unit <= 0xDFFF) {
            Fail("invalid unicode surrogate pair");
        }

        AppendUnicodeCodePoint(target, first_code_unit);
    }

    void AppendUnicodeCodePoint(std::string& target, std::uint32_t code_point) {
        if (code_point <= 0x7F) {
            target.push_back(static_cast<char>(code_point));
            return;
        }
        if (code_point <= 0x7FF) {
            target.push_back(static_cast<char>(0xC0 | ((code_point >> 6) & 0x1F)));
            target.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
            return;
        }
        if (code_point <= 0xFFFF) {
            target.push_back(static_cast<char>(0xE0 | ((code_point >> 12) & 0x0F)));
            target.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            target.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
            return;
        }
        if (code_point <= 0x10FFFF) {
            target.push_back(static_cast<char>(0xF0 | ((code_point >> 18) & 0x07)));
            target.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            target.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            target.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
            return;
        }
        Fail("invalid unicode code point");
    }

    void SkipWhitespace() {
        while (!AtEnd() && IsAsciiWhitespace(input_[index_])) {
            ++index_;
        }
    }

    bool AtEnd() const {
        return index_ >= input_.size();
    }

    bool Consume(char expected) {
        if (Peek() != expected) {
            return false;
        }
        ++index_;
        return true;
    }

    void Expect(char expected) {
        if (!Consume(expected)) {
            Fail(std::string("expected '") + expected + "'");
        }
    }

    bool MatchKeyword(const char* keyword) {
        const std::size_t length = std::char_traits<char>::length(keyword);
        if (input_.compare(index_, length, keyword) == 0) {
            index_ += length;
            return true;
        }
        return false;
    }

    void ExpectKeyword(const char* keyword) {
        if (!MatchKeyword(keyword)) {
            Fail(std::string("expected '") + keyword + "'");
        }
    }

    [[noreturn]] void Fail(const std::string& message) const {
        throw SerializationError("Failed to parse JSON: " + message);
    }

    const std::string& input_;
    std::size_t index_ = 0;
};

}  // namespace

JsonValue ParseJson(const std::string& input) {
    JsonParser parser(input);
    return parser.Parse();
}

}  // namespace ipgeolocation::internal
