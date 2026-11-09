#include "cx/runtime.hpp"

#include "cx/diagnostics.hpp"
#include "cx/value.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
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
    std::optional<ArithmeticOperator> addSub, mulDiv;
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
        if (c != '+' && c != '-' && c != '*' && c != '/' && c != '%') continue;
        if (i == 0) continue; // unary sign
        const char previous = text[i - 1];
        if (std::isspace(static_cast<unsigned char>(previous))) {
            std::size_t j = i + 1;
            while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) ++j;
            if (j == text.size()) continue;
        }
        if (previous == '+' || previous == '-' || previous == '*' || previous == '/' || previous == '%' || previous == '(') continue;
        if (c == '+' || c == '-') addSub = ArithmeticOperator{i, c};
        else mulDiv = ArithmeticOperator{i, c};
    }
    return addSub ? addSub : mulDiv;
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
    double number{};
    try {
        std::size_t consumed = 0;
        number = std::stod(expression, &consumed);
        if (consumed == expression.size() && std::isfinite(number)) return Value(number);
    } catch (...) { /* Plain words are accepted as string values. */ }
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

        std::vector<double> args;
        for (const auto& rawArg : rawArgs) {
            if (rawArg.empty()) { error = "Function `" + function + "` has an empty argument."; return {}; }
            std::string argError;
            const Value arg = evaluateValue(rawArg, context, argError);
            if (!argError.empty()) { error = argError; return {}; }
            const double argumentNumber = arg.numberOr(std::numeric_limits<double>::quiet_NaN());
            if (!std::isfinite(argumentNumber)) { error = "Function `" + function + "` expects numeric arguments."; return {}; }
            args.push_back(argumentNumber);
        }
        auto require = [&](std::size_t count) { if (args.size() != count) { error = "Function `" + function + "` expects " + std::to_string(count) + " argument(s)."; return false; } return true; };
        if (function == "sin" || function == "cos" || function == "tan") {
            if (!require(1)) return {};
            if (function == "sin") return Value(std::sin(args[0]));
            if (function == "cos") return Value(std::cos(args[0]));
            return Value(std::tan(args[0]));
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
        if (function == "clamp") { if (!require(3)) return {}; return Value(std::clamp(args[0], args[1], args[2])); }
        if (function == "lerp") { if (!require(3)) return {}; return Value(args[0] + (args[1] - args[0]) * args[2]); }
        error = "Unknown function `" + function + "`. Built-ins: sin, cos, tan, sine, noise2d, fbm2d, random_float, random_int, random_choice, clamp, lerp. You can define reusable script functions with `function name(parameters):`.";
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
        {" greater than or equal to ", ">="}, {" less than or equal to ", "<="},
        {" is not equal to ", "!="}, {" is greater than ", ">"}, {" is less than ", "<"},
        {" is equal to ", "=="}, {" is not ", "!="}, {" contains ", "contains"},
        {" equals ", "=="}, {" >= ", ">="}, {" <= ", "<="}, {" != ", "!="},
        {" == ", "=="}, {" > ", ">"}, {" < ", "<"}, {" is ", "=="}
    };
    if (const auto op = findOperator(expression, compareOps)) {
        std::string leftError, rightError;
        const Value left = evaluateValue(std::string_view(expression).substr(0, op->position), context, leftError);
        const Value right = evaluateValue(std::string_view(expression).substr(op->position + op->length), context, rightError);
        if (!leftError.empty()) { error = leftError; return false; }
        if (!rightError.empty()) { error = rightError; return false; }
        const double leftNumber = left.numberOr(std::numeric_limits<double>::quiet_NaN());
        const double rightNumber = right.numberOr(std::numeric_limits<double>::quiet_NaN());
        if (op->op == "==") result = sameValue(left, right);
        else if (op->op == "!=") result = !sameValue(left, right);
        else if (op->op == "contains") result = lower(left.toString()).find(lower(right.toString())) != std::string::npos;
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
        lowered.find(" equals ") != std::string::npos) {
        error = "Unknown comparison operator. CX uses `>`, `<`, `>=`, `<=`, `is`, and `is not`.";
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
