#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>

namespace cx {

// Runtime objects are referenced by their script-visible name and monotonic
// instance ID. This prevents a stored variable from targeting a replacement
// object after the original object is deleted and recreated.
enum class ObjectKind : std::uint8_t { Sprite, Entity };
struct ObjectReference {
    ObjectKind kind{ObjectKind::Sprite};
    std::string name;
    std::uint64_t instanceId{0};
    friend bool operator==(const ObjectReference&, const ObjectReference&) = default;
};

// Values are typed objects that scripts assign to variables and return from
// expressions. Object references are runtime-only handles, not persistent IDs.
class Value {
public:
    using Storage = std::variant<std::monostate, bool, double, std::string, ObjectReference>;

    Value() = default;
    Value(bool value) : value_(value) {}
    Value(int value) : value_(static_cast<double>(value)) {}
    Value(double value) : value_(value) {}
    Value(const char* value) : value_(std::string(value)) {}
    Value(std::string value) : value_(std::move(value)) {}
    Value(ObjectReference value) : value_(std::move(value)) {}

    [[nodiscard]] bool isNull() const noexcept;
    [[nodiscard]] bool isBool() const noexcept;
    [[nodiscard]] bool isNumber() const noexcept;
    [[nodiscard]] bool isString() const noexcept;
    [[nodiscard]] bool isObjectReference() const noexcept;
    [[nodiscard]] const ObjectReference* objectReference() const noexcept;
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
