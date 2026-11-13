#include "cx/runtime.hpp"

#include "cx/diagnostics.hpp"
#include "cx/value.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <numeric>
#include <functional>
#include <initializer_list>
#include <optional>
#include <regex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cx {
namespace {

// Expression evaluation is kept separate from statement execution so the core
// interpreter is easier to navigate. These helpers are intentionally private to
// this translation unit; the public language contract lives in docs/SYNTAX.md.
std::string unquote(std::string text) {
    text = trim(text);
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') return text;
    std::string out;
    out.reserve(text.size() - 2);
    bool escaped = false;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            switch (c) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                default: out.push_back(c); break;
            }
            escaped = false;
        } else if (c == '\\') escaped = true;
        else out.push_back(c);
    }
    if (escaped) out.push_back('\\');
    return out;
}
bool startsWithInsensitive(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && lower(text.substr(0, prefix.size())) == lower(prefix);
}
bool isQuoted(std::string_view value) {
    return value.size() >= 2 && value.front() == '"' && value.back() == '"';
}

// SplitMix64 is a small, fully specified integer generator. Its output does not
// depend on a standard-library distribution implementation or platform RNG. Each
// named stream stores only its own 64-bit state.
std::uint64_t nextSplitMix64(std::uint64_t& state) noexcept {
    state += 0x9e3779b97f4a7c15ULL;
    std::uint64_t value = state;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

// Rejection sampling avoids the modulo bias of `draw % bound`. A bound of one
// still consumes one draw so a successful random_int call always advances its stream.
std::uint64_t boundedRandom(std::uint64_t& state, std::uint64_t bound) noexcept {
    const std::uint64_t threshold = (std::uint64_t{0} - bound) % bound;
    while (true) {
        const std::uint64_t value = nextSplitMix64(state);
        if (value >= threshold) return value % bound;
    }
}
std::size_t findMatchingBrace(std::string_view text, std::size_t opening) {
    if (opening >= text.size() || text[opening] != '{') return std::string_view::npos;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (std::size_t i = opening; i < text.size(); ++i) {
        const char c = text[i];
        if (escaped) { escaped = false; continue; }
        if (c == '\\' && inString) { escaped = true; continue; }
        if (c == '"') { inString = !inString; continue; }
        if (inString) continue;
        if (c == '{') ++depth;
        else if (c == '}' && --depth == 0) return i;
    }
    return std::string_view::npos;
}
struct ArithmeticOperator { std::size_t position{}; char token{}; };
std::optional<ArithmeticOperator> findArithmeticOperator(std::string_view text) {
    bool quoted = false, escaped = false;
    int braces = 0, parentheses = 0;
    std::optional<ArithmeticOperator> addSub, mulDiv, power;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (escaped) { escaped = false; continue; }
        if (c == '\\' && quoted) { escaped = true; continue; }
        if (c == '"') { quoted = !quoted; continue; }
        if (quoted) continue;
        if (c == '{') { ++braces; continue; }
        if (c == '}' && braces > 0) { --braces; continue; }
        if (braces > 0) continue;
        if (c == '(') { ++parentheses; continue; }
        if (c == ')' && parentheses > 0) { --parentheses; continue; }
        if (parentheses > 0) continue;
        if (c != '+' && c != '-' && c != '*' && c != '/' && c != '%' && c != '^') continue;
        if (i == 0) continue; // unary sign
        // Look through whitespace when deciding whether + or - is unary.
        // This keeps expressions such as `2 * -3` and `2 ^ -2` intact.
        std::size_t previousIndex = i;
        while (previousIndex > 0 && std::isspace(static_cast<unsigned char>(text[previousIndex - 1])))
            --previousIndex;
        if (previousIndex == 0) continue;
        const char previous = text[previousIndex - 1];
        if (std::isspace(static_cast<unsigned char>(text[i - 1]))) {
            std::size_t j = i + 1;
            while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) ++j;
            if (j == text.size()) continue;
        }
        if (previous == '+' || previous == '-' || previous == '*' || previous == '/' || previous == '%' || previous == '^' || previous == '(') continue;
        if (c == '+' || c == '-') addSub = ArithmeticOperator{i, c};
        else if (c == '^') {
            // Exponentiation is right-associative: 2 ^ 3 ^ 2 means 2 ^ (3 ^ 2).
            if (!power) power = ArithmeticOperator{i, c};
        } else mulDiv = ArithmeticOperator{i, c};
    }
    return addSub ? addSub : (mulDiv ? mulDiv : power);
}
bool validVariableName(std::string_view name) {
    // This validation runs on assignment and interpolation, including inside on tick.
    // A direct character scan avoids regex overhead on this hot path.
    if (name.empty()) return false;
    const auto first = static_cast<unsigned char>(name.front());
    if (!(std::isalpha(first) || first == '_' || first == '$' || first == '^' || first == '@')) return false;
    for (const char raw : name.substr(1)) {
        const auto ch = static_cast<unsigned char>(raw);
        if (!(std::isalnum(ch) || ch == '_' || ch == ':')) return false;
    }
    return true;
}
bool sameValue(const Value& a, const Value& b) {
    if (a.isObjectReference() || b.isObjectReference()) {
        const auto* left = a.objectReference();
        const auto* right = b.objectReference();
        return left && right && *left == *right;
    }
    if (a.isNull() || b.isNull()) return a.isNull() && b.isNull();
    if (a.isNumber() || b.isNumber()) {
        const double av = a.numberOr(std::numeric_limits<double>::quiet_NaN());
        const double bv = b.numberOr(std::numeric_limits<double>::quiet_NaN());
        if (std::isfinite(av) && std::isfinite(bv)) return approximatelyEqual(av, bv);
    }
    return lower(a.toString()) == lower(b.toString());
}
struct OperatorMatch {
    std::size_t position{};
    std::size_t length{};
    std::string op;
};

std::optional<OperatorMatch> findOperator(std::string_view expression,
                                          const std::vector<std::pair<std::string, std::string>>& operators) {
    bool inString = false;
    bool escaped = false;
    int braceDepth = 0;
    int parenDepth = 0;
    const auto lowered = lower(expression);
    for (std::size_t i = 0; i < expression.size(); ++i) {
        const char c = expression[i];
        if (escaped) { escaped = false; continue; }
        if (c == '\\' && inString) { escaped = true; continue; }
        if (c == '"') { inString = !inString; continue; }
        if (inString) continue;
        if (c == '{') { ++braceDepth; continue; }
        if (c == '}' && braceDepth > 0) { --braceDepth; continue; }
        if (braceDepth > 0) continue;
        if (c == '(') { ++parenDepth; continue; }
        if (c == ')' && parenDepth > 0) { --parenDepth; continue; }
        if (parenDepth > 0) continue;
        for (const auto& [token, normalized] : operators) {
            if (i + token.size() > lowered.size() || lowered.compare(i, token.size(), token) != 0) continue;
            // Word operators include their own surrounding spaces (for example ` is not `),
            // so those spaces already provide token boundaries. Checking the character after
            // the full token would accidentally reject every operator whose token ends in space.
            return OperatorMatch{i, token.size(), normalized};
        }
    }
    return std::nullopt;
}
std::string stripOuterParentheses(std::string expression) {
    expression = trim(expression);
    while (expression.size() >= 2 && expression.front() == '(' && expression.back() == ')') {
        bool inString = false, escaped = false;
        int depth = 0;
        bool fullWrap = true;
        for (std::size_t i = 0; i < expression.size(); ++i) {
            const char c = expression[i];
            if (escaped) { escaped = false; continue; }
            if (c == '\\' && inString) { escaped = true; continue; }
            if (c == '"') { inString = !inString; continue; }
            if (inString) continue;
            if (c == '(') ++depth;
            if (c == ')') --depth;
            if (depth == 0 && i + 1 < expression.size()) { fullWrap = false; break; }
        }
        if (!fullWrap) break;
        expression = trim(std::string_view(expression).substr(1, expression.size() - 2));
    }
    return expression;
}

} // namespace

Value Runtime::evaluateValue(std::string_view rawExpression, const ExecutionContext& context, std::string& error) {
    const std::string expression = trim(rawExpression);
    if (expression.empty()) { error = "A value is missing after the command."; return {}; }
    if (isQuoted(expression)) {
        std::string value = unquote(expression);
        std::string interpolated;
        interpolated.reserve(value.size());
        auto interpolateReference = [&](std::string_view rawReference) -> std::optional<std::string> {
            const std::string plain = lower(trim(rawReference));
            if (plain == "loop-number" && !context.loopNumbers.empty()) return std::to_string(context.loopNumbers.back());
            if (plain == "loop-value" && !context.loopValues.empty()) return context.loopValues.back().toString();
            if (plain == "loop-index" && !context.loopIndexes.empty()) return context.loopIndexes.back();
            const std::string name = resolveVariableName(canonicalVariableName(rawReference), context);
            if (!validVariableName(name)) {
                error = "Invalid variable reference inside text: `" + std::string(rawReference) + "`.";
                return std::string{};
            }
            if (name.starts_with("$")) {
                const auto* texture = rooms_.textures().find(std::string_view(name).substr(1));
                if (!texture) {
                    error = "Unknown texture variable `{ " + name + " }`. No matching image was found in assets/textures/.";
                    return std::string{};
                }
                return texture->id;
            }
            if (name.starts_with("@")) {
                const auto* audioPath = audio_.findAssetPath(std::string_view(name).substr(1));
                if (!audioPath) {
                    error = "Unknown audio variable `{ " + name + " }`. No matching file was found in assets/audio/.";
                    return std::string{};
                }
                return audioPath->generic_string();
            }
            return getVariable(name, context).toString();
        };
        for (std::size_t i = 0; i < value.size();) {
            // Skript-style interpolation: "%{score}%" and "%loop-number%".
            if (value[i] == '%') {
                const auto endPercent = value.find('%', i + 1);
                if (endPercent != std::string::npos) {
                    const std::string_view token(value.data() + i + 1, endPercent - i - 1);
                    bool recognized = false;
                    std::optional<std::string> replacement;
                    if ((token.size() >= 2 && token.front() == '{' && token.back() == '}') ||
                        lower(token) == "loop-number" || lower(token) == "loop-value" || lower(token) == "loop-index") {
                        replacement = interpolateReference(token);
                        recognized = replacement.has_value();
                    } else if (!token.empty()) {
                        const Value embeddedExpression = evaluateValue(token, context, error);
                        if (!error.empty()) return {};
                        replacement = embeddedExpression.toString();
                        recognized = true;
                    }
                    if (recognized) {
                        interpolated += *replacement;
                        i = endPercent + 1;
                        if (!error.empty()) return {};
                        continue;
                    }
                }
            }
            if (value[i] == '{') {
                const auto end = findMatchingBrace(value, i);
                if (end != std::string::npos) {
                    const std::string rawName = canonicalVariableName(std::string_view(value).substr(i, end - i + 1));
                    const std::string name = resolveVariableName(rawName, context);
                    if (!validVariableName(name)) {
                        error = "Invalid variable reference inside text: `{" + name + "}`.";
                        return {};
                    }
                    if (name.starts_with("$")) {
                        const auto* texture = rooms_.textures().find(std::string_view(name).substr(1));
                        if (!texture) {
                            error = "Unknown texture variable `{ " + name + " }`. No matching image was found in assets/textures/.";
                            return {};
                        }
                        interpolated += texture->id;
                    } else if (name.starts_with("@")) {
                        const auto* audioPath = audio_.findAssetPath(std::string_view(name).substr(1));
                        if (!audioPath) {
                            error = "Unknown audio variable `{ " + name + " }`. No matching file was found in assets/audio/.";
                            return {};
                        }
                        interpolated += audioPath->generic_string();
                    } else {
                        interpolated += getVariable(name, context).toString();
                    }
                    i = end + 1;
                    continue;
                }
            }
            interpolated.push_back(value[i++]);
        }
        return Value(std::move(interpolated));
    }
    if (expression.size() >= 2 && expression.front() == '{' && findMatchingBrace(expression, 0) == expression.size() - 1) {
        const std::string name = resolveVariableName(canonicalVariableName(expression), context);
        if (!validVariableName(name)) { error = "Invalid variable name: `" + expression + "`."; return {}; }
        if (name.starts_with("$")) {
            const auto* texture = rooms_.textures().find(std::string_view(name).substr(1));
            if (!texture) {
                error = "Unknown texture variable `{ " + name + " }`. No matching image was found in assets/textures/.";
                return {};
            }
            return Value(texture->id);
        }
        if (name.starts_with("@")) {
            const auto* audioPath = audio_.findAssetPath(std::string_view(name).substr(1));
            if (!audioPath) {
                error = "Unknown audio variable `{ " + name + " }`. No matching file was found in assets/audio/.";
                return {};
            }
            return Value(audioPath->generic_string());
        }
        return getVariable(name, context);
    }
    const auto lowered = lower(expression);
    if (lowered == "loop-number" && !context.loopNumbers.empty()) return Value(static_cast<double>(context.loopNumbers.back()));
    if (lowered == "loop-value" && !context.loopValues.empty()) return context.loopValues.back();
    if (lowered == "loop-index" && !context.loopIndexes.empty()) return Value(context.loopIndexes.back());
    if (lowered == "true") return Value(true);
    if (lowered == "false") return Value(false);
    if (lowered == "none" || lowered == "null") return Value{};
    // Object properties read with the same English structure used by setters:
    // "set x-coordinate of {_player} to 15" pairs with
    // "x-coordinate of {_player}" in an expression or condition.
    static const std::regex objectPropertyPattern(
        R"PROPERTY(^\s*(?:the\s+)?(x-coordinate|y-coordinate|x|y|rotation|speed|visible|visibility)\s+of\s+(?:(sprite|entity|object)\s+)?(.+?)\s*$)PROPERTY",
        std::regex::icase);
    std::smatch propertyMatch;
    if (std::regex_match(expression, propertyMatch, objectPropertyPattern)) {
        const std::string property = lower(trim(propertyMatch[1].str()));
        const std::string requestedKind = propertyMatch[2].matched ? lower(trim(propertyMatch[2].str())) : "object";
        const std::string operand = trim(propertyMatch[3].str());
        const Value handleValue = evaluateValue(requestedKind + " " + operand, context, error);
        if (!error.empty()) return {};
        const auto* handle = handleValue.objectReference();
        if (!handle) { error = "That property needs a sprite or entity reference."; return {}; }
        const auto found = sprites_.find(handle->name);
        if (found == sprites_.end() || found->second.runtimeInstanceId != handle->instanceId) {
            error = "That object reference is no longer valid because the object was removed or replaced.";
            return {};
        }
        if ((requestedKind == "sprite" && handle->kind != ObjectKind::Sprite) ||
            (requestedKind == "entity" && handle->kind != ObjectKind::Entity)) {
            error = "That object reference is not a " + requestedKind + ".";
            return {};
        }
        const auto& object = found->second;
        if (property == "x" || property == "x-coordinate") return Value(static_cast<double>(object.x));
        if (property == "y" || property == "y-coordinate") return Value(static_cast<double>(object.y));
        if (property == "rotation") return Value(static_cast<double>(object.rotationDegrees));
        if (property == "speed") return Value(static_cast<double>(object.moveSpeedPixelsPerSecond));
        return Value(object.visible);
    }
    static const std::regex objectReferencePattern(R"OBJECT(^\s*(sprite|entity|object)\s+(.+?)\s*$)OBJECT", std::regex::icase);
    std::smatch objectMatch;
    if (std::regex_match(expression, objectMatch, objectReferencePattern)) {
        const std::string requestedKind = lower(objectMatch[1].str());
        const std::string operand = trim(objectMatch[2].str());
        std::string objectName;
        ObjectKind objectKind = ObjectKind::Sprite;
        std::uint64_t instanceId = 0;
        if (isQuoted(operand)) {
            objectName = lower(trim(unquote(operand)));
            const auto found = sprites_.find(objectName);
            if (found == sprites_.end()) { error = "There is no object named \"" + objectName + "\"."; return {}; }
            const bool isEntity = found->second.entity;
            if ((requestedKind == "sprite" && isEntity) || (requestedKind == "entity" && !isEntity)) {
                error = "Object \"" + objectName + "\" is not a " + requestedKind + "."; return {};
            }
            objectKind = isEntity ? ObjectKind::Entity : ObjectKind::Sprite;
            instanceId = found->second.runtimeInstanceId;
        } else {
            Value operandValue = evaluateValue(operand, context, error);
            if (!error.empty()) return {};
            const auto* reference = operandValue.objectReference();
            if (!reference) {
                error = "A sprite or entity variable must contain an object reference. Set it from `sprite \"name\"` or `entity \"name\"` first."; return {};
            }
            if ((requestedKind == "sprite" && reference->kind != ObjectKind::Sprite) ||
                (requestedKind == "entity" && reference->kind != ObjectKind::Entity)) {
                error = "That object reference is not a " + requestedKind + "."; return {};
            }
            const auto found = sprites_.find(reference->name);
            if (found == sprites_.end() || found->second.runtimeInstanceId != reference->instanceId) {
                error = "That object reference is no longer valid because the object was removed or replaced."; return {};
            }
            objectName = reference->name;
            objectKind = reference->kind;
            instanceId = reference->instanceId;
        }
        return Value(ObjectReference{objectKind, objectName, instanceId});
    }
    double number{};
    try {
        std::size_t consumed = 0;
        number = std::stod(expression, &consumed);
        if (consumed == expression.size() && std::isfinite(number)) return Value(number);
    } catch (...) { /* Plain words are accepted as string values. */ }
    // English-first numeric expressions are intentionally parsed before arithmetic and
    // function-call syntax. The familiar words are the primary interface; function calls
    // remain available as a compact/advanced spelling.
    {
        const std::string loweredExpression = lower(expression);
        struct NaturalMathForm { std::string prefix; std::string function; };
        static const std::array<NaturalMathForm, 26> naturalMathForms{{
            {"the absolute value of ", "abs"}, {"absolute value of ", "abs"},
            {"the square root of ", "sqrt"}, {"square root of ", "sqrt"},
            {"the floor of ", "floor"}, {"floor of ", "floor"},
            {"the ceiling of ", "ceil"}, {"ceiling of ", "ceil"},
            {"the sine of ", "sin"}, {"sine of ", "sin"},
            {"the cosine of ", "cos"}, {"cosine of ", "cos"},
            {"the average of ", "mean"}, {"average of ", "mean"},
            {"the mean of ", "mean"}, {"mean of ", "mean"},
            {"the median of ", "median"}, {"median of ", "median"},
            {"the sum of ", "sum"}, {"sum of ", "sum"},
            {"the product of ", "product"}, {"product of ", "product"},
            {"the minimum of ", "min"}, {"minimum of ", "min"},
            {"the maximum of ", "max"}, {"maximum of ", "max"}
        }};
        for (const auto& form : naturalMathForms) {
            if (startsWithInsensitive(loweredExpression, form.prefix)) {
                const auto operand = trim(std::string_view(expression).substr(form.prefix.size()));
                if (operand.empty()) { error = "The " + form.function + " expression needs a number."; return {}; }
                return evaluateValue(form.function + "(" + operand + ")", context, error);
            }
        }
        static const std::array<NaturalMathForm, 4> otherNaturalMathForms{{
            {"the tangent of ", "tan"}, {"tangent of ", "tan"},
            {"the square of ", "square"}, {"square of ", "square"}
        }};
        for (const auto& form : otherNaturalMathForms) {
            if (startsWithInsensitive(loweredExpression, form.prefix)) {
                const auto operand = trim(std::string_view(expression).substr(form.prefix.size()));
                if (operand.empty()) { error = "The " + form.function + " expression needs a number."; return {}; }
                return evaluateValue(form.function + "(" + operand + ")", context, error);
            }
        }
        static const std::regex roundNaturalPattern(
            R"ROUND(^round\s+(.+?)\s+to\s+(-?\d+)\s+decimal\s+places?$)ROUND", std::regex::icase);
        std::smatch roundMatch;
        if (std::regex_match(expression, roundMatch, roundNaturalPattern))
            return evaluateValue("round(" + roundMatch[1].str() + ", " + roundMatch[2].str() + ")", context, error);
        if (startsWithInsensitive(loweredExpression, "round "))
            return evaluateValue("round(" + trim(std::string_view(expression).substr(6)) + ")", context, error);

        // Skript-style random expressions are natural language; explicit named streams
        // are optional and retain their deterministic behavior.
        std::string randomText = expression;
        std::string streamExpression = "\"default\"";
        if (const auto streamClause = findOperator(randomText, {{" using random stream ", "using"}})) {
            streamExpression = trim(std::string_view(randomText).substr(streamClause->position + streamClause->length));
            randomText = trim(std::string_view(randomText).substr(0, streamClause->position));
            if (streamExpression.empty()) { error = "The random stream name is missing after using random stream."; return {}; }
        }
        auto findNaturalPrefix = [](std::string_view candidate, std::initializer_list<std::string_view> prefixes) -> std::optional<std::size_t> {
            for (const auto prefix : prefixes) if (startsWithInsensitive(candidate, prefix)) return prefix.size();
            return std::nullopt;
        };
        const auto loweredRandom = lower(randomText);
        if (const auto prefix = findNaturalPrefix(randomText, {
                "a random integer between ", "random integer between ",
                "a random integer from ", "random integer from ",
                "a random number between ", "random number between ",
                "a random number from ", "random number from "})) {
            const std::string header = lower(std::string_view(randomText).substr(0, *prefix));
            const bool integerResult = header.find("integer") != std::string::npos;
            const bool betweenForm = header.find("between") != std::string::npos;
            const std::string_view range(std::string_view(randomText).substr(*prefix));
            const auto separator = findOperator(range, {{betweenForm ? " and " : " to ", betweenForm ? "and" : "to"}});
            if (!separator) {
                error = betweenForm ? "A between random expression needs and between its two bounds."
                                     : "A from random expression needs to between its two bounds.";
                return {};
            }
            const std::string leftText = trim(range.substr(0, separator->position));
            const std::string rightText = trim(range.substr(separator->position + separator->length));
            if (leftText.empty() || rightText.empty()) { error = "Both bounds of a random expression are required."; return {}; }
            Value leftValue = evaluateValue(leftText, context, error);
            if (!error.empty()) return {};
            Value rightValue = evaluateValue(rightText, context, error);
            if (!error.empty()) return {};
            const double left = leftValue.numberOr(std::numeric_limits<double>::quiet_NaN());
            const double right = rightValue.numberOr(std::numeric_limits<double>::quiet_NaN());
            if (!leftValue.isNumber() || !rightValue.isNumber() || !std::isfinite(left) || !std::isfinite(right)) {
                error = "Random number bounds must both be finite numbers.";
                return {};
            }
            auto streamValue = evaluateValue(streamExpression, context, error);
            if (!error.empty()) return {};
            if (!streamValue.isString()) { error = "A random stream name must be text."; return {}; }
            const auto streamName = lower(trim(streamValue.toString()));
            const auto streamIt = randomStreams_.find(streamName);
            if (streamIt == randomStreams_.end()) {
                error = "Random stream " + streamName + " has not been seeded. Seed it before using it.";
                return {};
            }
            auto& state = streamIt->second.state;
            const double minimum = std::min(left, right);
            const double maximum = std::max(left, right);
            if (integerResult) {
                constexpr double maximumSafeInteger = 9007199254740991.0;
                if (std::trunc(minimum) != minimum || std::trunc(maximum) != maximum ||
                    std::abs(minimum) > maximumSafeInteger || std::abs(maximum) > maximumSafeInteger) {
                    error = "A random integer needs whole-number bounds within the safe integer range.";
                    return {};
                }
                const auto low = static_cast<std::int64_t>(minimum);
                const auto high = static_cast<std::int64_t>(maximum);
                const auto width = static_cast<std::uint64_t>(high - low) + 1ULL;
                return Value(static_cast<double>(low + static_cast<std::int64_t>(boundedRandom(state, width))));
            }
            const std::uint64_t bits = nextSplitMix64(state) >> 11U;
            const double unit = static_cast<double>(bits) * (1.0 / 9007199254740992.0);
            return Value(std::lerp(minimum, maximum, unit));
        }
        if (loweredRandom.find("random integer") != std::string::npos ||
            loweredRandom.find("random number") != std::string::npos) {
            error = "I could not parse that random expression. Use a random integer between 1 and 10 or a random number from 0 to 1.";
            return {};
        }

        // Random element out of a CX list follows Skript's collection expression shape.
        // Selected values keep their original number, text, or boolean type.
        if (const auto prefix = findNaturalPrefix(randomText, {
                "a random element out of ", "random element out of ",
                "a random element of ", "random element of "})) {
            const auto source = trim(std::string_view(randomText).substr(*prefix));
            if (source.size() < 4 || source.front() != '{' || source.back() != '}' || source.find("::*") == std::string::npos) {
                error = "A random element currently needs a list variable, such as {inventory::*}.";
                return {};
            }
            auto entries = listEntries(canonicalVariableName(source), context);
            if (entries.empty()) return Value{};
            auto streamValue = evaluateValue(streamExpression, context, error);
            if (!error.empty()) return {};
            if (!streamValue.isString()) { error = "A random stream name must be text."; return {}; }
            const auto streamName = lower(trim(streamValue.toString()));
            const auto streamIt = randomStreams_.find(streamName);
            if (streamIt == randomStreams_.end()) { error = "Random stream " + streamName + " has not been seeded."; return {}; }
            const std::size_t selected = entries.size() == 1U ? 0U : static_cast<std::size_t>(boundedRandom(streamIt->second.state, entries.size()));
            return entries[selected].value;
        }
    }

    // Arithmetic expressions support normal precedence and parentheses without requiring a special command.
    const std::string strippedExpression = stripOuterParentheses(expression);
    if (strippedExpression != expression) return evaluateValue(strippedExpression, context, error);
    if (const auto arithmetic = findArithmeticOperator(expression)) {
        const Value left = evaluateValue(std::string_view(expression).substr(0, arithmetic->position), context, error);
        if (!error.empty()) return {};
        const Value right = evaluateValue(std::string_view(expression).substr(arithmetic->position + 1), context, error);
        if (!error.empty()) return {};
        if (arithmetic->token == '+' && (left.isString() || right.isString()))
            return Value(left.toString() + right.toString());
        const double a = left.numberOr(std::numeric_limits<double>::quiet_NaN());
        const double b = right.numberOr(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(a) || !std::isfinite(b)) {
            error = "Arithmetic operators need numbers unless `+` is joining text.";
            return {};
        }
        if ((arithmetic->token == '/' || arithmetic->token == '%') && b == 0.0) {
            error = "Division by zero is not allowed.";
            return {};
        }
        switch (arithmetic->token) {
            case '+': return Value(a + b);
            case '-': return Value(a - b);
            case '*': return Value(a * b);
            case '/': return Value(a / b);
            case '%': return Value(std::fmod(a, b));
            case '^': return Value(std::pow(a, b));
            default: break;
        }
    }
    // Small pure math/noise functions make procedural worlds and audio parameters scriptable.
    const auto openParen = expression.find('(');
    if (openParen != std::string::npos && expression.back() == ')') {
        const std::string function = lower(trim(std::string_view(expression).substr(0, openParen)));
        const auto argsText = std::string_view(expression).substr(openParen + 1, expression.size() - openParen - 2);
        std::vector<std::string> rawArgs;
        std::size_t argStart = 0; int nesting = 0; bool quoted = false; bool escaped = false;
        for (std::size_t i = 0; i <= argsText.size(); ++i) {
            if (i < argsText.size()) {
                const char c = argsText[i];
                if (escaped) { escaped = false; continue; }
                if (c == '\\' && quoted) { escaped = true; continue; }
                if (c == '"') { quoted = !quoted; continue; }
                if (!quoted) { if (c == '(') ++nesting; else if (c == ')') --nesting; }
                if (i < argsText.size() && (c != ',' || quoted || nesting != 0)) continue;
            }
            rawArgs.push_back(trim(argsText.substr(argStart, i - argStart)));
            argStart = i + 1;
        }
        if (const auto customFunction = program_.functions.find(function); customFunction != program_.functions.end()) {
            if (functionCallDepth_ >= 64) { error = "Function recursion exceeded 64 nested calls."; return {}; }
            const auto& definition = customFunction->second;
            if (rawArgs.size() == 1 && rawArgs.front().empty() && definition.parameters.empty()) rawArgs.clear();
            if (rawArgs.size() != definition.parameters.size()) {
                error = "Function `" + function + "` expects " + std::to_string(definition.parameters.size()) +
                    " argument(s), but received " + std::to_string(rawArgs.size()) + ".";
                return {};
            }
            ExecutionContext functionContext;
            functionContext.allowSuspend = false; // Functions return synchronously; waiting is intentionally prohibited.
            for (std::size_t i = 0; i < rawArgs.size(); ++i) {
                Value argument = evaluateValue(rawArgs[i], context, error);
                if (!error.empty()) return {};
                functionContext.locals["_" + definition.parameters[i]] = std::move(argument);
            }
            ++functionCallDepth_;
            functionContext.frames.push_back({&definition.body, 0, 1, 1, false, false, nullptr, false, {}});
            const std::size_t diagnosticsBefore = diagnostics_.size();
            runExecution(functionContext);
            --functionCallDepth_;
            if (functionContext.suspended) {
                error = "A function cannot suspend with `wait`; use waits in events instead.";
                return {};
            }
            if (diagnostics_.size() > diagnosticsBefore && !diagnostics_.back().warning) {
                error = "Function `" + function + "` failed while running; see the terminal diagnostic above.";
                return {};
            }
            return functionContext.returnValue;
        }
        if (function == "isnan" || function == "isfinite") {
            if (rawArgs.size() != 1 || rawArgs.front().empty()) {
                error = function + " expects exactly one numeric argument.";
                return {};
            }
            const Value checked = evaluateValue(rawArgs.front(), context, error);
            if (!error.empty()) return {};
            if (!checked.isNumber()) {
                error = function + " expects a number.";
                return {};
            }
            const double value = checked.numberOr();
            return Value(function == "isnan" ? std::isnan(value) : std::isfinite(value));
        }
        auto resolveRandomStream = [&](std::string_view rawName) -> RandomStreamState* {
            std::string streamError;
            const Value streamValue = evaluateValue(rawName, context, streamError);
            if (!streamError.empty()) { error = streamError; return nullptr; }
            if (!streamValue.isString()) {
                error = "A random stream argument must be text. Use a name such as \"encounters\".";
                return nullptr;
            }
            const std::string streamName = lower(trim(streamValue.toString()));
            if (streamName.empty()) { error = "A random stream name cannot be empty."; return nullptr; }
            const auto found = randomStreams_.find(streamName);
            if (found == randomStreams_.end()) {
                error = "Random stream `" + streamName + "` has not been seeded.";
                return nullptr;
            }
            return &found->second;
        };
        if (function == "random_float") {
            if (rawArgs.size() != 1 || rawArgs.front().empty()) {
                error = "random_float(\"stream\") expects one named, seeded stream.";
                return {};
            }
            auto* stream = resolveRandomStream(rawArgs[0]);
            if (!stream) return {};
            // The high 53 bits map exactly to the representable [0, 1) double grid.
            const std::uint64_t bits = nextSplitMix64(stream->state) >> 11U;
            return Value(static_cast<double>(bits) * (1.0 / 9007199254740992.0));
        }
        if (function == "random_int") {
            if (rawArgs.size() != 3 || rawArgs[0].empty() || rawArgs[1].empty() || rawArgs[2].empty()) {
                error = "random_int(\"stream\", minimum, maximum) expects three arguments.";
                return {};
            }
            auto* stream = resolveRandomStream(rawArgs[0]);
            if (!stream) return {};
            Value lowerValue = evaluateValue(rawArgs[1], context, error);
            if (!error.empty()) return {};
            Value upperValue = evaluateValue(rawArgs[2], context, error);
            if (!error.empty()) return {};
            constexpr double maximumSafeInteger = 9007199254740991.0;
            const double lowerNumber = lowerValue.numberOr(std::numeric_limits<double>::quiet_NaN());
            const double upperNumber = upperValue.numberOr(std::numeric_limits<double>::quiet_NaN());
            const auto validInteger = [&](const Value& value, double numericValue) {
                return value.isNumber() && std::isfinite(numericValue) && std::trunc(numericValue) == numericValue &&
                       std::abs(numericValue) <= maximumSafeInteger;
            };
            if (!validInteger(lowerValue, lowerNumber) || !validInteger(upperValue, upperNumber)) {
                error = "random_int bounds must be whole numbers within the safe integer range (-9007199254740991 to 9007199254740991).";
                return {};
            }
            const auto minimum = static_cast<std::int64_t>(lowerNumber);
            const auto maximum = static_cast<std::int64_t>(upperNumber);
            if (minimum > maximum) { error = "random_int minimum must not exceed its maximum."; return {}; }
            const auto width = static_cast<std::uint64_t>(maximum - minimum) + 1ULL;
            const auto offset = boundedRandom(stream->state, width);
            return Value(static_cast<double>(minimum + static_cast<std::int64_t>(offset)));
        }
        if (function == "random_choice") {
            if (rawArgs.size() < 2 || rawArgs[0].empty()) {
                error = "random_choice(\"stream\", option1, option2, ...) needs a stream and at least one option.";
                return {};
            }
            if (rawArgs.size() > 257) { error = "random_choice supports at most 256 options."; return {}; }
            auto* stream = resolveRandomStream(rawArgs[0]);
            if (!stream) return {};
            // Select the source expression before evaluating it. Options are lazy:
            // unselected expressions must not produce diagnostics or consume other
            // random streams as a side effect of a choice they did not win.
            for (std::size_t i = 1; i < rawArgs.size(); ++i) {
                if (rawArgs[i].empty()) { error = "random_choice contains an empty option."; return {}; }
            }
            const std::size_t optionCount = rawArgs.size() - 1;
            const std::size_t selected = optionCount == 1
                ? 0U
                : static_cast<std::size_t>(boundedRandom(stream->state, optionCount));
            return evaluateValue(rawArgs[selected + 1], context, error);
        }

        if (function == "concat") {
            std::string combined;
            for (const auto& rawArg : rawArgs) {
                if (rawArg.empty()) continue;
                const Value value = evaluateValue(rawArg, context, error);
                if (!error.empty()) return {};
                combined += value.toString();
            }
            return Value(std::move(combined));
        }
        if (function == "frombase") {
            if (rawArgs.size() != 2) {
                error = "fromBase(text, base) expects exactly two arguments.";
                return {};
            }
            const Value textValue = evaluateValue(rawArgs[0], context, error);
            if (!error.empty()) return {};
            const Value baseValue = evaluateValue(rawArgs[1], context, error);
            if (!error.empty()) return {};
            const double baseNumber = baseValue.numberOr(std::numeric_limits<double>::quiet_NaN());
            if (!textValue.isString() || !std::isfinite(baseNumber) || std::trunc(baseNumber) != baseNumber ||
                baseNumber < 2.0 || baseNumber > 36.0) {
                error = "fromBase needs text and a whole-number base from 2 to 36.";
                return {};
            }
            const std::string digits = trim(textValue.toString());
            if (digits.empty()) return {};
            bool negative = false;
            std::size_t position = 0;
            if (digits.front() == '-' || digits.front() == '+') {
                negative = digits.front() == '-';
                position = 1;
            }
            if (position == digits.size()) return {};
            std::uint64_t magnitude = 0;
            const auto base = static_cast<std::uint32_t>(baseNumber);
            constexpr std::uint64_t maximumSafeInteger = 9007199254740991ULL;
            for (; position < digits.size(); ++position) {
                const unsigned char ch = static_cast<unsigned char>(digits[position]);
                unsigned int digit = 99;
                if (ch >= '0' && ch <= '9') digit = ch - '0';
                else if (ch >= 'a' && ch <= 'z') digit = 10U + ch - 'a';
                else if (ch >= 'A' && ch <= 'Z') digit = 10U + ch - 'A';
                if (digit >= base || magnitude > (maximumSafeInteger - digit) / base) return {};
                magnitude = magnitude * base + digit;
            }
            return Value(static_cast<double>(magnitude) * (negative ? -1.0 : 1.0));
        }
        if (function == "formatnumber") {
            if (rawArgs.empty() || rawArgs.size() > 2) {
                error = "formatNumber(number[, format]) expects one or two arguments.";
                return {};
            }
            const Value numberValue = evaluateValue(rawArgs[0], context, error);
            if (!error.empty()) return {};
            if (!numberValue.isNumber()) {
                error = "formatNumber expects a numeric first argument.";
                return {};
            }
            const double number = numberValue.numberOr();
            std::string pattern;
            if (rawArgs.size() == 2) {
                const Value patternValue = evaluateValue(rawArgs[1], context, error);
                if (!error.empty()) return {};
                if (!patternValue.isString()) return {};
                pattern = patternValue.toString();
            } else {
                pattern = std::isfinite(number) && std::trunc(number) != number ? "###,###.##" : "###,###";
            }
            if (std::isnan(number)) return Value("NaN");
            if (std::isinf(number)) return Value(number < 0.0 ? "-Infinity" : "Infinity");
            const auto decimalPoint = pattern.find('.');
            if (decimalPoint != std::string::npos && pattern.find('.', decimalPoint + 1) != std::string::npos) return {};
            const std::string integerPattern = pattern.substr(0, decimalPoint);
            const std::string fractionPattern = decimalPoint == std::string::npos ? std::string{} : pattern.substr(decimalPoint + 1);
            if (integerPattern.empty() || (decimalPoint != std::string::npos && fractionPattern.empty())) return {};
            const auto validInteger = std::all_of(integerPattern.begin(), integerPattern.end(),
                [](char ch) { return ch == '#' || ch == '0' || ch == ','; });
            const auto validFraction = std::all_of(fractionPattern.begin(), fractionPattern.end(),
                [](char ch) { return ch == '#' || ch == '0'; });
            if (!validInteger || !validFraction) return {};
            const bool grouping = integerPattern.find(',') != std::string::npos;
            if (grouping) {
                std::size_t previousComma = std::string::npos;
                for (std::size_t i = 0; i < integerPattern.size(); ++i) {
                    if (integerPattern[i] != ',') continue;
                    const std::size_t groupLength = i - (previousComma == std::string::npos ? 0U : previousComma + 1U);
                    if ((previousComma == std::string::npos && (groupLength < 1U || groupLength > 3U)) ||
                        (previousComma != std::string::npos && groupLength != 3U) ||
                        i + 1U == integerPattern.size()) return {};
                    previousComma = i;
                }
                if (integerPattern.size() - previousComma - 1U != 3U) return {};
            }
            const auto minimumIntegerDigits = static_cast<std::size_t>(std::count(integerPattern.begin(), integerPattern.end(), '0'));
            const auto minimumFractionDigits = static_cast<std::size_t>(std::count(fractionPattern.begin(), fractionPattern.end(), '0'));
            const auto maximumFractionDigits = fractionPattern.size();
            if (maximumFractionDigits > 12U) return {};
            std::ostringstream stream;
            stream.imbue(std::locale::classic());
            stream << std::fixed << std::setprecision(static_cast<int>(maximumFractionDigits)) << std::abs(number);
            std::string rendered = stream.str();
            const auto point = rendered.find('.');
            std::string integerPart = rendered.substr(0, point);
            std::string fractionalPart = point == std::string::npos ? std::string{} : rendered.substr(point + 1);
            while (fractionalPart.size() > minimumFractionDigits && !fractionalPart.empty() && fractionalPart.back() == '0')
                fractionalPart.pop_back();
            while (integerPart.size() < minimumIntegerDigits) integerPart.insert(integerPart.begin(), '0');
            if (grouping && integerPart.size() > 3U) {
                for (std::ptrdiff_t pos = static_cast<std::ptrdiff_t>(integerPart.size()) - 3; pos > 0; pos -= 3)
                    integerPart.insert(static_cast<std::size_t>(pos), 1, ',');
            }
            std::string result = (std::signbit(number) ? "-" : "") + integerPart;
            if (!fractionalPart.empty()) result += "." + fractionalPart;
            return Value(std::move(result));
        }

        std::vector<double> args;
        for (const auto& rawArg : rawArgs) {
            if (rawArg.empty()) { error = "Function `" + function + "` has an empty argument."; return {}; }
            // Skript's collection-oriented math functions accept a list variable as a
            // single argument. Expand that collection here so the function implementations
            // below operate on one consistent numeric sequence.
            if (rawArg.front() == '{' && rawArg.back() == '}' && rawArg.find("::*") != std::string::npos) {
                const auto entries = listEntries(canonicalVariableName(rawArg), context);
                for (const auto& entry : entries) {
                    if (!entry.value.isNumber()) {
                        error = "Function `" + function + "` expects every list item to be a number.";
                        return {};
                    }
                    args.push_back(entry.value.numberOr());
                }
                continue;
            }
            std::string argError;
            const Value arg = evaluateValue(rawArg, context, argError);
            if (!argError.empty()) { error = argError; return {}; }
            const double argumentNumber = arg.numberOr(std::numeric_limits<double>::quiet_NaN());
            if (!arg.isNumber()) { error = "Function `" + function + "` expects numeric arguments."; return {}; }
            args.push_back(argumentNumber);
        }
        auto require = [&](std::size_t count) { if (args.size() != count) { error = "Function `" + function + "` expects " + std::to_string(count) + " argument(s)."; return false; } return true; };
        constexpr double degreesToRadians = 3.1415926535897932384626433832795 / 180.0;
        constexpr double radiansToDegrees = 180.0 / 3.1415926535897932384626433832795;
        if (function == "sin" || function == "cos" || function == "tan") {
            if (!require(1)) return {};
            const double radians = args[0] * degreesToRadians;
            if (function == "sin") return Value(std::sin(radians));
            if (function == "cos") return Value(std::cos(radians));
            return Value(std::tan(radians));
        }
        if (function == "sin_radians" || function == "cos_radians" || function == "tan_radians") {
            if (!require(1)) return {};
            if (function == "sin_radians") return Value(std::sin(args[0]));
            if (function == "cos_radians") return Value(std::cos(args[0]));
            return Value(std::tan(args[0]));
        }
        if (function == "asin" || function == "acos" || function == "atan") {
            if (!require(1)) return {};
            if (function == "asin") return Value(std::asin(args[0]) * radiansToDegrees);
            if (function == "acos") return Value(std::acos(args[0]) * radiansToDegrees);
            return Value(std::atan(args[0]) * radiansToDegrees);
        }
        if (function == "atan2") {
            if (!require(2)) return {};
            if (args[0] == 0.0 && args[1] == 0.0) { error = "atan2 is undefined when both coordinates are zero."; return {}; }
            return Value(std::atan2(args[0], args[1]) * radiansToDegrees);
        }
        if (function == "abs" || function == "absolute") { if (!require(1)) return {}; return Value(std::abs(args[0])); }
        if (function == "sqrt") {
            if (!require(1)) return {};
            // Match Skript's numeric semantics: invalid real roots produce NaN, which
            // authors can explicitly test with isNaN(...), rather than aborting a script.
            return Value(std::sqrt(args[0]));
        }
        if (function == "square") { if (!require(1)) return {}; return Value(args[0] * args[0]); }
        if (function == "floor") { if (!require(1)) return {}; return Value(std::floor(args[0])); }
        if (function == "ceil" || function == "ceiling") { if (!require(1)) return {}; return Value(std::ceil(args[0])); }
        if (function == "round") {
            if (args.empty() || args.size() > 2) { error = "round(number[, decimal_places]) expects one or two arguments."; return {}; }
            const double places = args.size() == 2 ? args[1] : 0.0;
            if (std::trunc(places) != places || places < -12.0 || places > 12.0) {
                error = "round decimal_places must be a whole number from -12 to 12.";
                return {};
            }
            const double scale = std::pow(10.0, places);
            if (!std::isfinite(args[0])) return Value(args[0]);
            const double scaled = args[0] * scale;
            if (!std::isfinite(scaled)) return Value(scaled);
            return Value(std::round(scaled) / scale);
        }
        if (function == "exp") { if (!require(1)) return {}; return Value(std::exp(args[0])); }
        if (function == "ln") {
            if (!require(1)) return {};
            return Value(std::log(args[0]));
        }
        if (function == "log" || function == "log10") {
            if (args.empty() || args.size() > 2) { error = "log(number[, base]) expects one or two arguments."; return {}; }
            const double base = args.size() == 2 ? args[1] : 10.0;
            if (args[0] < 0.0 || base <= 0.0 || base == 1.0)
                return Value(std::numeric_limits<double>::quiet_NaN());
            return Value(std::log(args[0]) / std::log(base));
        }
        if (function == "root") {
            if (!require(2)) return {};
            const double degree = args[0], radicand = args[1];
            if (degree == 0.0) return Value(std::numeric_limits<double>::quiet_NaN());
            if (radicand < 0.0 && std::trunc(degree) == degree && std::fmod(std::abs(degree), 2.0) == 1.0)
                return Value(-std::pow(-radicand, 1.0 / degree));
            if (radicand < 0.0) return Value(std::numeric_limits<double>::quiet_NaN());
            return Value(std::pow(radicand, 1.0 / degree));
        }
        if (function == "pow") {
            if (!require(2)) return {};
            return Value(std::pow(args[0], args[1]));
        }
        if (function == "factorial") {
            if (!require(1)) return {};
            if (args[0] < 0.0 || std::trunc(args[0]) != args[0])
                return Value(std::numeric_limits<double>::quiet_NaN());
            return Value(std::tgamma(args[0] + 1.0));
        }
        if (function == "combinations" || function == "permutations") {
            if (!require(2)) return {};
            constexpr double maxSafeInteger = 9007199254740991.0;
            const double n = args[0], selected = args[1];
            if (!std::isfinite(n) || !std::isfinite(selected) || n < 0.0 || selected < 0.0 ||
                std::trunc(n) != n || std::trunc(selected) != selected ||
                n > maxSafeInteger || selected > maxSafeInteger)
                return Value(std::numeric_limits<double>::quiet_NaN());
            if (selected > n) return Value(0.0);
            const double logarithm = function == "combinations"
                ? std::lgamma(n + 1.0) - std::lgamma(selected + 1.0) - std::lgamma(n - selected + 1.0)
                : std::lgamma(n + 1.0) - std::lgamma(n - selected + 1.0);
            if (logarithm > std::log(std::numeric_limits<double>::max()))
                return Value(std::numeric_limits<double>::infinity());
            return Value(std::exp(logarithm));
        }
        if (function == "toBase") {
            if (!require(2)) return {};
            constexpr double maxSafeInteger = 9007199254740991.0;
            if (!std::isfinite(args[0]) || std::trunc(args[0]) != args[0] ||
                std::abs(args[0]) > maxSafeInteger || !std::isfinite(args[1]) ||
                std::trunc(args[1]) != args[1] || args[1] < 2.0 || args[1] > 36.0)
                return {};
            static constexpr char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
            const auto base = static_cast<std::uint32_t>(args[1]);
            const auto signedValue = static_cast<std::int64_t>(args[0]);
            const bool negative = signedValue < 0;
            std::uint64_t magnitude = negative
                ? static_cast<std::uint64_t>(-(signedValue + 1)) + 1ULL
                : static_cast<std::uint64_t>(signedValue);
            std::string result;
            do {
                result.push_back(digits[magnitude % base]);
                magnitude /= base;
            } while (magnitude != 0);
            if (negative) result.push_back('-');
            std::reverse(result.begin(), result.end());
            return Value(std::move(result));
        }
        if (function == "min" || function == "max" || function == "mean" || function == "median" ||
            function == "sum" || function == "product") {
            if (args.empty()) { error = function + " expects at least one number."; return {}; }
            if (function == "min") return Value(*std::min_element(args.begin(), args.end()));
            if (function == "max") return Value(*std::max_element(args.begin(), args.end()));
            if (function == "sum" || function == "mean") {
                const double total = std::accumulate(args.begin(), args.end(), 0.0);
                return Value(function == "mean" ? total / static_cast<double>(args.size()) : total);
            }
            if (function == "product") {
                return Value(std::accumulate(args.begin(), args.end(), 1.0, std::multiplies<double>{}));
            }
            std::vector<double> ordered = args;
            std::sort(ordered.begin(), ordered.end());
            const std::size_t middle = ordered.size() / 2U;
            if (ordered.size() % 2U != 0U) return Value(ordered[middle]);
            return Value((ordered[middle - 1U] + ordered[middle]) * 0.5);
        }
        if (function == "sine") {
            if (args.size() != 2 && args.size() != 3) { error = "sine(hz, seconds[, phase_radians]) expects 2 or 3 arguments."; return {}; }
            const double phase = args.size() == 3 ? args[2] : 0.0;
            return Value(std::sin(6.283185307179586 * args[0] * args[1] + phase));
        }
        if (function == "noise2d") {
            if (args.size() != 2 && args.size() != 3) { error = "noise2d(x, y[, seed]) expects 2 or 3 arguments."; return {}; }
            const auto seed = args.size() == 3 ? static_cast<std::uint32_t>(static_cast<std::int64_t>(args[2])) : 0U;
            return Value(AudioSystem::noise2d(args[0], args[1], seed));
        }
        if (function == "fbm2d") {
            if (args.size() < 2 || args.size() > 4) { error = "fbm2d(x, y[, seed[, octaves]]) expects 2 to 4 arguments."; return {}; }
            const auto seed = args.size() >= 3 ? static_cast<std::uint32_t>(static_cast<std::int64_t>(args[2])) : 0U;
            const auto octaves = args.size() >= 4 ? static_cast<unsigned int>(std::clamp(args[3], 1.0, 12.0)) : 4U;
            return Value(AudioSystem::fbm2d(args[0], args[1], seed, octaves));
        }
        if (function == "mod") {
            if (!require(2)) return {};
            if (args[1] == 0.0) return Value(std::numeric_limits<double>::quiet_NaN());
            const double divisor = std::abs(args[1]);
            double remainder = std::fmod(args[0], divisor);
            if (remainder < 0.0) remainder += divisor;
            return Value(remainder == 0.0 ? 0.0 : remainder);
        }
        if (function == "clamp") {
            if (!require(3)) return {};
            if (args[1] > args[2]) { error = "clamp minimum must not exceed its maximum."; return {}; }
            return Value(std::clamp(args[0], args[1], args[2]));
        }
        if (function == "lerp") { if (!require(3)) return {}; return Value(std::lerp(args[0], args[1], args[2])); }
        error = "Unknown function `" + function + "`. Built-ins: abs, sqrt, floor, ceil, round, exp, ln, log, root, pow, factorial, combinations, permutations, toBase, fromBase, formatNumber, concat, min, max, mean, median, sum, product, mod, isNaN, isFinite, sin, cos, tan, asin, acos, atan, atan2, sin_radians, cos_radians, tan_radians, sine, noise2d, fbm2d, random_float, random_int, random_choice, clamp, lerp. You can define reusable script functions with `function name(parameters):`.";
        return {};
    }
    if (expression.find('{') != std::string::npos || expression.find('}') != std::string::npos) {
        error = "Variables must be written as {name}, and text containing variables must be inside quotes.";
        return {};
    }
    return Value(expression);
}

bool Runtime::evaluateCondition(std::string_view rawExpression, const ExecutionContext& context,
                                bool& result, std::string& error) {
    const std::string expression = stripOuterParentheses(std::string(rawExpression));
    if (expression.empty()) { error = "The condition is empty."; return false; }

    static const std::regex keyDownPattern(R"KEY(^key\s+"?([A-Za-z0-9_]+)"?\s+is\s+(not\s+)?down$)KEY", std::regex::icase);
    std::smatch keyDownMatch;
    if (std::regex_match(expression, keyDownMatch, keyDownPattern)) {
        result = keyDown(keyDownMatch[1].str());
        if (keyDownMatch[2].matched) result = !result;
        return true;
    }

    static const std::regex mouseOverButtonPattern(R"MOUSE(^mouse\s+cursor\s+is\s+over\s+button\s+"([^"\n]+)"$)MOUSE", std::regex::icase);
    std::smatch mouseMatch;
    if (std::regex_match(expression, mouseMatch, mouseOverButtonPattern)) {
        const auto xIt = context.locals.find("_mouse_x");
        const auto yIt = context.locals.find("_mouse_y");
        if (xIt == context.locals.end() || yIt == context.locals.end()) {
            error = "`mouse cursor is over button` is only available during mouse/file-attachment events.";
            return false;
        }
        const double mx = xIt->second.numberOr(-1.0), my = yIt->second.numberOr(-1.0);
        const std::string wanted = lower(mouseMatch[1].str());
        result = false;
        for (const auto& canvasName : openCanvases_) {
            const auto* canvas = findCanvas(canvasName);
            if (!canvas) continue;
            for (const auto& element : resolveCanvasElements(*canvas, viewportWidth_, viewportHeight_)) {
                if (element.type == CanvasElementType::Button && lower(element.id) == wanted &&
                    mx >= element.x && my >= element.y && mx <= element.x + element.width && my <= element.y + element.height) {
                    result = true;
                    return true;
                }
            }
        }
        return true;
    }

    static const std::regex entityTagPattern(R"TAG(^\s*entity\s+"([^"\n]+)"\s+has\s+tag\s+"([^"\n]+)"\s*$)TAG", std::regex::icase);
    std::smatch entityQueryMatch;
    if (std::regex_match(expression, entityQueryMatch, entityTagPattern)) {
        const auto entity = sprites_.find(lower(trim(entityQueryMatch[1].str())));
        const std::uint32_t tagId = findEntityLabel(entityQueryMatch[2].str());
        result = entity != sprites_.end() && entity->second.entity && tagId != 0U &&
            std::find(entity->second.tagIds.begin(), entity->second.tagIds.end(), tagId) != entity->second.tagIds.end();
        return true;
    }

    static const std::regex entityClassificationPattern(R"CLASS(^\s*entity\s+"([^"\n]+)"\s+(kind|team)\s+is\s+"([^"\n]+)"\s*$)CLASS", std::regex::icase);
    if (std::regex_match(expression, entityQueryMatch, entityClassificationPattern)) {
        const auto entity = sprites_.find(lower(trim(entityQueryMatch[1].str())));
        const std::string property = lower(trim(entityQueryMatch[2].str()));
        const std::uint32_t wantedId = findEntityLabel(entityQueryMatch[3].str());
        result = false;
        if (entity != sprites_.end() && entity->second.entity && wantedId != 0U) {
            result = property == "kind" ? entity->second.kindId == wantedId : entity->second.teamId == wantedId;
        }
        return true;
    }

    static const std::regex proximityPattern(
        R"PROXIMITY(^\s*entity\s+"([^"\n]+)"\s+is\s+within\s+(.+?)\s+pixels\s+of\s+entity\s+"([^"\n]+)"\s*$)PROXIMITY",
        std::regex::icase);
    if (std::regex_match(expression, entityQueryMatch, proximityPattern)) {
        std::string distanceError;
        const Value distanceValue = evaluateValue(entityQueryMatch[2].str(), context, distanceError);
        if (!distanceError.empty()) { error = distanceError; return false; }
        const double radius = distanceValue.numberOr(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(radius) || radius < 0.0) {
            error = "A proximity distance must be a finite, non-negative number of pixels.";
            return false;
        }
        const auto actor = sprites_.find(lower(trim(entityQueryMatch[1].str())));
        const auto target = sprites_.find(lower(trim(entityQueryMatch[3].str())));
        result = false;
        if (actor == sprites_.end() || target == sprites_.end() ||
            !actor->second.entity || !target->second.entity) return true;
        const auto actorCenter = spriteHitboxCenter(actor->second);
        const auto targetCenter = spriteHitboxCenter(target->second);
        const double dx = actorCenter[0] - targetCenter[0];
        const double dy = actorCenter[1] - targetCenter[1];
        result = dx * dx + dy * dy <= radius * radius;
        return true;
    }

    static const std::regex focusedOnPattern(R"FOCUSED(^\s*entity\s+"([^"\n]+)"\s+is\s+focused\s+on\s+entity\s+"([^"\n]+)"\s*$)FOCUSED", std::regex::icase);
    std::smatch focusedOnMatch;
    if (std::regex_match(expression, focusedOnMatch, focusedOnPattern)) {
        result = focusedEntity(focusedOnMatch[1].str()) == lower(trim(focusedOnMatch[2].str()));
        return true;
    }

    static const std::regex overlapPattern(R"OVERLAP(^\s*(?:entity|sprite)\s+"([^"\n]+)"\s+(?:overlaps|collides\s+with)\s+(?:entity|sprite)\s+"([^"\n]+)"\s*$)OVERLAP", std::regex::icase);
    std::smatch overlapMatch;
    if (std::regex_match(expression, overlapMatch, overlapPattern)) {
        result = spritesOverlap(overlapMatch[1].str(), overlapMatch[2].str());
        return true;
    }

    // Range, parity, sign, type, and emptiness conditions are more specific than
    // the generic `is` comparison and must therefore be evaluated first.
    static const std::regex betweenConditionPattern(
        R"BETWEEN(^(.+?)\s+is\s+(not\s+)?between\s+(.+)$)BETWEEN", std::regex::icase);
    std::smatch betweenMatch;
    if (std::regex_match(expression, betweenMatch, betweenConditionPattern)) {
        const std::string range = trim(betweenMatch[3].str());
        const auto separator = findOperator(range, {{" and ", "and"}});
        if (!separator) { error = "A between condition needs a lower and upper bound separated by `and`."; return false; }
        const Value subject = evaluateValue(trim(betweenMatch[1].str()), context, error);
        if (!error.empty()) return false;
        const Value lowerBound = evaluateValue(trim(std::string_view(range).substr(0, separator->position)), context, error);
        if (!error.empty()) return false;
        const Value upperBound = evaluateValue(trim(std::string_view(range).substr(separator->position + separator->length)), context, error);
        if (!error.empty()) return false;
        if (!subject.isNumber() || !lowerBound.isNumber() || !upperBound.isNumber()) {
            error = "A between condition needs three numeric values.";
            return false;
        }
        const double value = subject.numberOr(), low = lowerBound.numberOr(), high = upperBound.numberOr();
        const bool inside = std::isfinite(value) && std::isfinite(low) && std::isfinite(high) &&
            value >= std::min(low, high) && value <= std::max(low, high);
        result = betweenMatch[2].matched ? !inside : inside;
        return true;
    }

    static const std::regex valuePredicatePattern(
        R"PREDICATE(^(.+?)\s+is\s+(not\s+)?(even|odd|positive|negative|zero|a\s+number|number|a\s+text|text|string|a\s+boolean|boolean|empty|a\s+list|list)$)PREDICATE",
        std::regex::icase);
    std::smatch predicateMatch;
    if (std::regex_match(expression, predicateMatch, valuePredicatePattern)) {
        const std::string subjectExpression = trim(predicateMatch[1].str());
        const std::string predicate = lower(trim(predicateMatch[3].str()));
        const bool isListExpression = subjectExpression.size() >= 4 &&
            subjectExpression.front() == '{' && subjectExpression.back() == '}' &&
            subjectExpression.find("::*") != std::string::npos;
        Value subject;
        if (!isListExpression) {
            subject = evaluateValue(subjectExpression, context, error);
            if (!error.empty()) return false;
        }
        bool matches = false;
        if (predicate == "even" || predicate == "odd" || predicate == "positive" ||
            predicate == "negative" || predicate == "zero" || predicate == "number" || predicate == "a number") {
            if (subject.isNumber()) {
                const double number = subject.numberOr();
                if (predicate == "even") matches = std::isfinite(number) && std::trunc(number) == number && std::fmod(std::abs(number), 2.0) == 0.0;
                else if (predicate == "odd") matches = std::isfinite(number) && std::trunc(number) == number && std::fmod(std::abs(number), 2.0) == 1.0;
                else if (predicate == "positive") matches = number > 0.0;
                else if (predicate == "negative") matches = number < 0.0;
                else if (predicate == "zero") matches = number == 0.0;
                else matches = true;
            }
        } else if (predicate == "text" || predicate == "a text" || predicate == "string") {
            matches = subject.isString();
        } else if (predicate == "boolean" || predicate == "a boolean") {
            matches = subject.isBool();
        } else if (predicate == "list" || predicate == "a list") {
            matches = isListExpression;
        } else if (predicate == "empty") {
            matches = isListExpression ? listEntries(canonicalVariableName(subjectExpression), context).empty()
                                       : subject.isNull() || (subject.isString() && subject.toString().empty());
        }
        result = predicateMatch[2].matched ? !matches : matches;
        return true;
    }

    // Skript-style presence checks must run before the generic `is not` comparison.
    static const std::regex setStatePattern(R"SET(^(.+?)\s+is\s+(not\s+)?set$)SET", std::regex::icase);
    std::smatch setStateMatch;
    if (std::regex_match(expression, setStateMatch, setStatePattern)) {
        std::string valueError;
        const Value value = evaluateValue(trim(setStateMatch[1].str()), context, valueError);
        if (!valueError.empty()) { error = valueError; return false; }
        const bool isSet = !value.isNull();
        const bool negated = setStateMatch[2].matched;
        result = negated ? !isSet : isSet;
        return true;
    }

    static const std::vector<std::pair<std::string, std::string>> orOps = {{" or ", "or"}};
    if (const auto op = findOperator(expression, orOps)) {
        bool left = false, right = false;
        if (!evaluateCondition(std::string_view(expression).substr(0, op->position), context, left, error)) return false;
        if (left) { result = true; return true; }
        if (!evaluateCondition(std::string_view(expression).substr(op->position + op->length), context, right, error)) return false;
        result = left || right;
        return true;
    }
    static const std::vector<std::pair<std::string, std::string>> andOps = {{" and ", "and"}};
    if (const auto op = findOperator(expression, andOps)) {
        bool left = false, right = false;
        if (!evaluateCondition(std::string_view(expression).substr(0, op->position), context, left, error)) return false;
        if (!left) { result = false; return true; }
        if (!evaluateCondition(std::string_view(expression).substr(op->position + op->length), context, right, error)) return false;
        result = left && right;
        return true;
    }
    if (startsWithInsensitive(expression, "not ")) {
        bool inner = false;
        if (!evaluateCondition(std::string_view(expression).substr(4), context, inner, error)) return false;
        result = !inner;
        return true;
    }

    static const std::vector<std::pair<std::string, std::string>> compareOps = {
        // More specific multi-word operators come first. The same operation also has
        // concise forms so a script can read naturally without forcing one exact phrase.
        {" is greater than or equal to ", ">="}, {" is less than or equal to ", "<="},
        {" greater than or equal to ", ">="}, {" less than or equal to ", "<="},
        {" is not equal to ", "!="}, {" does not equal ", "!="}, {" is not in ", "not_in"},
        {" is greater than ", ">"}, {" is less than ", "<"}, {" is more than ", ">"},
        {" is at least ", ">="}, {" is at most ", "<="}, {" is above ", ">"}, {" is below ", "<"},
        {" greater than ", ">"}, {" less than ", "<"}, {" more than ", ">"},
        {" at least ", ">="}, {" at most ", "<="}, {" above ", ">"}, {" below ", "<"},
        {" is equal to ", "=="}, {" equal to ", "=="}, {" is in ", "in"}, {" in ", "in"},
        {" is not ", "!="}, {" does not contain ", "not_contains"}, {" contains ", "contains"},
        {" equals ", "=="}, {" >= ", ">="}, {" <= ", "<="}, {" != ", "!="},
        {" == ", "=="}, {" > ", ">"}, {" < ", "<"}, {" is ", "=="}
    };
    if (const auto op = findOperator(expression, compareOps)) {
        const std::string_view leftText = std::string_view(expression).substr(0, op->position);
        const std::string_view rightText = std::string_view(expression).substr(op->position + op->length);
        const auto isListExpression = [](std::string_view text) {
            const std::string value = trim(text);
            return value.size() >= 4 && value.front() == '{' && value.back() == '}' &&
                   value.find("::*") != std::string::npos;
        };
        // A list wildcard is a collection expression, not a scalar Value. Do not
        // pass it through evaluateValue before the collection membership branch.
        const bool rightIsMembershipList = (op->op == "in" || op->op == "not_in") && isListExpression(rightText);
        const bool leftIsMembershipList = (op->op == "contains" || op->op == "not_contains") && isListExpression(leftText);
        std::string leftError, rightError;
        const Value left = leftIsMembershipList ? Value{} : evaluateValue(leftText, context, leftError);
        const Value right = rightIsMembershipList ? Value{} : evaluateValue(rightText, context, rightError);
        if (!leftError.empty()) { error = leftError; return false; }
        if (!rightError.empty()) { error = rightError; return false; }
        const double leftNumber = left.numberOr(std::numeric_limits<double>::quiet_NaN());
        const double rightNumber = right.numberOr(std::numeric_limits<double>::quiet_NaN());
        auto listContains = [&](std::string_view listExpression, const Value& wanted) {
            const auto entries = listEntries(canonicalVariableName(listExpression), context);
            return std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
                return sameValue(entry.value, wanted);
            });
        };
        if (op->op == "==") result = sameValue(left, right);
        else if (op->op == "!=") result = !sameValue(left, right);
        else if (op->op == "in" || op->op == "not_in") {
            const bool found = isListExpression(std::string_view(expression).substr(op->position + op->length))
                ? listContains(std::string_view(expression).substr(op->position + op->length), left)
                : lower(right.toString()).find(lower(left.toString())) != std::string::npos;
            result = op->op == "not_in" ? !found : found;
        } else if (op->op == "contains" || op->op == "not_contains") {
            const bool found = isListExpression(std::string_view(expression).substr(0, op->position))
                ? listContains(std::string_view(expression).substr(0, op->position), right)
                : lower(left.toString()).find(lower(right.toString())) != std::string::npos;
            result = op->op == "not_contains" ? !found : found;
        }
        else if (op->op == ">") {
            if (std::isfinite(leftNumber) && std::isfinite(rightNumber)) result = leftNumber > rightNumber;
            else result = lower(left.toString()) > lower(right.toString());
        } else if (op->op == "<") {
            if (std::isfinite(leftNumber) && std::isfinite(rightNumber)) result = leftNumber < rightNumber;
            else result = lower(left.toString()) < lower(right.toString());
        } else if (op->op == ">=") {
            if (std::isfinite(leftNumber) && std::isfinite(rightNumber)) result = leftNumber >= rightNumber;
            else result = lower(left.toString()) >= lower(right.toString());
        } else if (op->op == "<=") {
            if (std::isfinite(leftNumber) && std::isfinite(rightNumber)) result = leftNumber <= rightNumber;
            else result = lower(left.toString()) <= lower(right.toString());
        }
        return true;
    }

    // Common malformed comparisons are caught at runtime too, so they are understandable even when entered in the console.
    const auto lowered = lower(expression);
    if (lowered.find(" greater than ") != std::string::npos || lowered.find(" less than ") != std::string::npos ||
        lowered.find(" at least ") != std::string::npos || lowered.find(" at most ") != std::string::npos ||
        lowered.find(" equals ") != std::string::npos) {
        error = "Unknown comparison operator. Try a natural form such as `is greater than`, `at least`, `contains`, or `is in`, or use `>`, `<`, `>=`, `<=`.";
        return false;
    }
    Value value = evaluateValue(expression, context, error);
    if (!error.empty()) return false;
    result = value.truthy();
    return true;
}

// Strip the user's braces/formatting and apply the variable scope rules in one place.
// `{_name}` is looked up in this execution's locals, `{^name}` in process-only memory,
// and ordinary names in persistent globals. Centralizing this avoids each command inventing
// its own interpretation of variable lifetime.

} // namespace cx
