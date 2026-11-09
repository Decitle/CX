#include "cx/value.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace cx {

bool Value::isNull() const noexcept { return std::holds_alternative<std::monostate>(value_); }
bool Value::isBool() const noexcept { return std::holds_alternative<bool>(value_); }
bool Value::isNumber() const noexcept { return std::holds_alternative<double>(value_); }
bool Value::isString() const noexcept { return std::holds_alternative<std::string>(value_); }

// Truthiness is the shared rule used by conditions and logical operators. It is intentionally
// explicit: an empty string and the strings "false", "no", "0", and "none" are false; most
// other non-empty strings are true. Keep the docs/tests aligned with this behavior.
bool Value::truthy() const noexcept {
    if (const auto* b = std::get_if<bool>(&value_)) return *b;
    if (const auto* n = std::get_if<double>(&value_)) return *n != 0.0 && !std::isnan(*n);
    if (const auto* s = std::get_if<std::string>(&value_)) {
        const auto lowered = lower(*s);
        return !s->empty() && lowered != "false" && lowered != "no" && lowered != "0" && lowered != "none";
    }
    return false;
}

double Value::numberOr(double fallback) const noexcept {
    if (const auto* n = std::get_if<double>(&value_)) return *n;
    if (const auto* b = std::get_if<bool>(&value_)) return *b ? 1.0 : 0.0;
    if (const auto* s = std::get_if<std::string>(&value_)) {
        double parsed{};
        const char* begin = s->data();
        const char* end = begin + s->size();
        const auto result = std::from_chars(begin, end, parsed);
        if (result.ec == std::errc{} && result.ptr == end) return parsed;
    }
    return fallback;
}

std::string Value::toString() const {
    if (std::holds_alternative<std::monostate>(value_)) return "none";
    if (const auto* b = std::get_if<bool>(&value_)) return *b ? "true" : "false";
    if (const auto* n = std::get_if<double>(&value_)) {
        if (std::isfinite(*n) && std::floor(*n) == *n && std::abs(*n) < 9.0e15) {
            return std::to_string(static_cast<long long>(*n));
        }
        std::ostringstream out;
        out << std::setprecision(12) << *n;
        return out.str();
    }
    return std::get<std::string>(value_);
}

// Convert a text value supplied by the runtime (for example a console assignment) into the
// most useful primitive type. A word that is not a recognized boolean/null or complete number
// stays text; it is not silently converted based on a partial numeric prefix.
Value Value::fromString(std::string_view input) {
    const auto cleaned = trim(input);
    const auto lowered = lower(cleaned);
    if (lowered == "true") return Value(true);
    if (lowered == "false") return Value(false);
    if (lowered == "none" || lowered == "null") return Value{};

    double parsed{};
    const char* begin = cleaned.data();
    const char* end = begin + cleaned.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (!cleaned.empty() && result.ec == std::errc{} && result.ptr == end) return Value(parsed);
    return Value(cleaned);
}

std::string trim(std::string_view input) {
    const auto first = input.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = input.find_last_not_of(" \t\r\n");
    return std::string(input.substr(first, last - first + 1));
}

std::string lower(std::string_view input) {
    std::string result(input);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string upper(std::string_view input) {
    std::string result(input);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return result;
}

bool approximatelyEqual(double a, double b) noexcept {
    const double scale = std::max({1.0, std::abs(a), std::abs(b)});
    return std::abs(a - b) <= 1.0e-9 * scale;
}

} // namespace cx
