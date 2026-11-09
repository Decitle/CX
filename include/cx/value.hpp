#pragma once

#include <string>
#include <string_view>
#include <variant>

namespace cx {

// The scripting language uses one Value type instead of making every command
// manage four unrelated C++ types. std::variant stores exactly one alternative:
// no value, true/false, a number, or text. The conversion helpers below define
// how those values behave when a script prints, compares, or calculates with them.
class Value {
public:
    using Storage = std::variant<std::monostate, bool, double, std::string>;

    Value() = default;
    Value(bool value) : value_(value) {}
    Value(int value) : value_(static_cast<double>(value)) {}
    Value(double value) : value_(value) {}
    Value(const char* value) : value_(std::string(value)) {}
    Value(std::string value) : value_(std::move(value)) {}

    [[nodiscard]] bool isNull() const noexcept;
    [[nodiscard]] bool isBool() const noexcept;
    [[nodiscard]] bool isNumber() const noexcept;
    [[nodiscard]] bool isString() const noexcept;
    [[nodiscard]] bool truthy() const noexcept;
    [[nodiscard]] double numberOr(double fallback = 0.0) const noexcept;
    [[nodiscard]] std::string toString() const;
    [[nodiscard]] const Storage& storage() const noexcept { return value_; }

    [[nodiscard]] static Value fromString(std::string_view value);

private:
    Storage value_{};
};

[[nodiscard]] std::string trim(std::string_view input);
[[nodiscard]] std::string lower(std::string_view input);
[[nodiscard]] std::string upper(std::string_view input);
[[nodiscard]] bool approximatelyEqual(double a, double b) noexcept;

} // namespace cx
