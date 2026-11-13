#include "cx/runtime.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;

void expect(bool condition, const std::string& message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void write(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << contents;
    if (!output) throw std::runtime_error("could not write test fixture: " + path.string());
}

std::filesystem::path fixture(const std::string& name, const std::string& script) {
    const auto root = std::filesystem::temp_directory_path() / ("cx-language-expression-" + name);
    std::filesystem::remove_all(root);
    write(root / "scripts" / "main.cx", script);
    return root;
}

void testSkriptStyleRandomNumberGrammar() {
    const auto root = fixture("natural-random", R"CX(
seed random stream "loot" with 0
set {between_int} to a random integer between 1 and 10
set {from_int} to random integer from 10 to 1
set {between_number} to a random number between 2.7 and -1.5
set {named_stream} to a random integer between 1 and 10 using random stream "loot"
set {items::*} to "red", "blue", 42
set {random_item} to a random element out of {items::*}
set {message} to "You rolled %random integer from 1 to 6%!"
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "Skript-style random expressions should use the automatic default stream without explicit setup");
    const double betweenInt = runtime.getGlobal("between_int").numberOr(-1.0);
    const double fromInt = runtime.getGlobal("from_int").numberOr(-1.0);
    const double namedStream = runtime.getGlobal("named_stream").numberOr(-1.0);
    const double betweenNumber = runtime.getGlobal("between_number").numberOr(-100.0);
    expect(betweenInt >= 1.0 && betweenInt <= 10.0 && std::trunc(betweenInt) == betweenInt,
           "a random integer between returns an inclusive whole-number result");
    expect(fromInt >= 1.0 && fromInt <= 10.0 && std::trunc(fromInt) == fromInt,
           "random integer from ... to ... accepts reversed endpoints like Skript");
    expect(namedStream >= 1.0 && namedStream <= 10.0 && std::trunc(namedStream) == namedStream,
           "natural random expressions can opt into a named deterministic stream");
    expect(betweenNumber >= -1.5 && betweenNumber < 2.7,
           "a random number between accepts reversed decimal endpoints");
    const auto randomItem = runtime.getGlobal("random_item");
    expect(randomItem.isString() || (randomItem.isNumber() && randomItem.numberOr(-1.0) == 42.0),
           "random element out of a list returns one original value without stringifying its type");
    const std::string message = runtime.getGlobal("message").toString();
    expect(message.starts_with("You rolled ") && message.ends_with("!") && message.size() > 12,
           "Skript-style percent interpolation evaluates expressions inside text");
    expect(!runtime.hasErrors(), "valid natural random expressions should not emit diagnostics");
    std::filesystem::remove_all(root);
}

void testNaturalMathAndSkriptDegreeTrigonometry() {
    const auto root = fixture("math", R"CX(
set {absolute} to the absolute value of -12.5
set {square_root} to the square root of 81
set {floor} to the floor of -1.2
set {ceiling} to the ceiling of 1.2
set {rounded} to round 2.345 to 2 decimal places
set {square} to the square of 9
set {sin_degrees} to sin(90)
set {cos_degrees} to cos(180)
set {tan_degrees} to tan(45)
set {sin_radians} to sin_radians(1.5707963267948966)
set {arcsin_degrees} to asin(1)
set {atan2_degrees} to atan2(1, 1)
set {log_default_base} to log(1000)
set {log_custom_base} to log(81, 3)
set {natural_log} to ln(exp(1))
set {root} to root(3, -8)
set {root_skript_order} to root(2, 4)
set {factorial_five} to factorial(5)
set {combinations_five_two} to combinations(5, 2)
set {permutations_five_two} to permutations(5, 2)
set {hex_text} to toBase(32, 16)
set {hex_value} to fromBase("20", 16)
set {formatted_number} to formatNumber(1234567.891, "#,##0.00")
set {joined_text} to concat("score: ", 42, " pts")
set {positive_mod} to mod(-1, 10)
set {mod_zero_is_nan} to isNaN(mod(1, 0))
set {log_zero_is_nan} to isNaN(log(0))
set {log_zero_is_finite} to isFinite(log(0))
set {overflow_is_finite} to isFinite(factorial(171))
set {values::*} to 1, 2, 3, 4
set {mean_values} to mean({values::*})
set {median_values} to median({values::*})
set {average_values} to the average of {values::*}
set {sum_values} to sum({values::*})
set {product_values} to product({values::*})
set {bad_root} to sqrt(-1)
set {bad_root_is_nan} to isNaN({bad_root})
set {finite_number} to isFinite(42)
set {power_right_associative} to 2 ^ 3 ^ 2
set {power_precedence} to 2 + 3 * 4 ^ 2
set {negative_product} to 2 * -3
set {negative_exponent} to 2 ^ -2
set {minimum} to min(5, -2, 9, 4)
set {maximum} to max(5, -2, 9, 4)
set {clamped} to clamp(15, 0, 10)
set {interpolated} to lerp(10, 20, 0.25)
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "natural and function-form math expressions should load");
    const auto number = [&](const char* name) { return runtime.getGlobal(name).numberOr(std::numeric_limits<double>::quiet_NaN()); };
    expect(std::abs(number("absolute") - 12.5) < 1e-12, "absolute value should work in natural English form");
    expect(std::abs(number("square_root") - 9.0) < 1e-12, "square root should work in natural English form");
    expect(std::abs(number("floor") + 2.0) < 1e-12, "floor should round negative values downward");
    expect(std::abs(number("ceiling") - 2.0) < 1e-12, "ceiling should round upward");
    expect(std::abs(number("rounded") - 2.35) < 1e-12, "round with decimal-place precision should work");
    expect(std::abs(number("square") - 81.0) < 1e-12, "square should be a convenient power helper");
    expect(std::abs(number("sin_degrees") - 1.0) < 1e-12, "Skript-compatible sin uses degrees");
    expect(std::abs(number("cos_degrees") + 1.0) < 1e-12, "Skript-compatible cos uses degrees");
    expect(std::abs(number("tan_degrees") - 1.0) < 1e-12, "Skript-compatible tan uses degrees");
    expect(std::abs(number("sin_radians") - 1.0) < 1e-12, "explicit radians aliases preserve technical radians use");
    expect(std::abs(number("arcsin_degrees") - 90.0) < 1e-12, "inverse trigonometry returns degrees");
    expect(std::abs(number("atan2_degrees") - 45.0) < 1e-12, "atan2 should return screen/game-friendly degrees");
    expect(std::abs(number("log_default_base") - 3.0) < 1e-12, "log defaults to base ten like Skript");
    expect(std::abs(number("log_custom_base") - 4.0) < 1e-12, "log accepts a custom base");
    expect(std::abs(number("natural_log") - 1.0) < 1e-12, "natural logarithm and exponential should be inverses");
    expect(std::abs(number("root") + 2.0) < 1e-12, "odd roots of negative values should remain real");
    expect(std::abs(number("root_skript_order") - 2.0) < 1e-12, "root accepts the Skript argument order: degree, value");
    expect(std::abs(number("factorial_five") - 120.0) < 1e-12, "factorial should return exact small factorials");
    expect(std::abs(number("combinations_five_two") - 10.0) < 1e-12, "combinations counts unordered selections");
    expect(std::abs(number("permutations_five_two") - 20.0) < 1e-12, "permutations counts ordered selections");
    expect(runtime.getGlobal("hex_text").toString() == "20", "toBase converts a decimal integer to hexadecimal text");
    expect(std::abs(number("hex_value") - 32.0) < 1e-12, "fromBase converts hexadecimal text to a decimal number");
    expect(runtime.getGlobal("formatted_number").toString() == "1,234,567.89", "formatNumber groups thousands and rounds decimal places");
    expect(runtime.getGlobal("joined_text").toString() == "score: 42 pts", "concat joins text and numeric values");
    expect(std::abs(number("positive_mod") - 9.0) < 1e-12, "mod always returns a positive remainder like Skript");
    expect(runtime.getGlobal("mod_zero_is_nan").truthy(), "mod with a zero divisor returns NaN instead of aborting execution");
    expect(!runtime.getGlobal("log_zero_is_nan").truthy() && !runtime.getGlobal("log_zero_is_finite").truthy(),
           "log of zero is negative infinity, not NaN");
    expect(!runtime.getGlobal("overflow_is_finite").truthy(), "factorial overflow remains Infinity and can be inspected");
    expect(std::abs(number("mean_values") - 2.5) < 1e-12, "mean accepts a list variable");
    expect(std::abs(number("median_values") - 2.5) < 1e-12, "median accepts a list variable and averages its middle pair");
    expect(std::abs(number("average_values") - 2.5) < 1e-12, "natural average form accepts a list variable");
    expect(std::abs(number("sum_values") - 10.0) < 1e-12, "sum accepts a list variable");
    expect(std::abs(number("product_values") - 24.0) < 1e-12, "product accepts a list variable");
    expect(runtime.getGlobal("bad_root_is_nan").truthy(), "sqrt of a negative number follows Skript NaN behavior");
    expect(runtime.getGlobal("finite_number").truthy(), "isFinite recognizes finite values");
    expect(std::abs(number("power_right_associative") - 512.0) < 1e-12, "power operator should be right-associative");
    expect(std::abs(number("power_precedence") - 50.0) < 1e-12, "power should bind more tightly than multiplication and addition");
    expect(std::abs(number("negative_product") + 6.0) < 1e-12, "unary negative values should work after multiplication");
    expect(std::abs(number("negative_exponent") - 0.25) < 1e-12, "negative exponents should be parsed as unary values");
    expect(std::abs(number("minimum") + 2.0) < 1e-12, "min should accept multiple numeric arguments");
    expect(std::abs(number("maximum") - 9.0) < 1e-12, "max should accept multiple numeric arguments");
    expect(std::abs(number("clamped") - 10.0) < 1e-12, "clamp should constrain a value to the requested range");
    expect(std::abs(number("interpolated") - 12.5) < 1e-12, "lerp should interpolate between endpoints");
    expect(!runtime.hasErrors(), "valid math expressions should not emit diagnostics");
    std::filesystem::remove_all(root);
}

void testNaturalComparisonAndListConditions() {
    const auto root = fixture("condition-language", R"CX(
set {score} to 8
set {items::*} to "blue", "green", 3
if {score} greater than 7:
    set {plain_greater_than} to true
if {score} is higher than 7:
    set {higher_than} to true
if {score} bigger than 7:
    set {bigger_than} to true
if {score} is larger than 7:
    set {larger_than} to true
if {score} are more than 7:
    set {are_more_than} to true
if {score} is lower than 9:
    set {lower_than} to true
if {score} smaller than 9:
    set {smaller_than} to true
if {score} is smaller than 9:
    set {is_smaller_than} to true
if {score} are lower than 9:
    set {are_lower_than} to true
if {score} is at least 8:
    set {at_least} to true
if {score} at most 8:
    set {at_most} to true
if {score} is greater than or equal to 8:
    set {inclusive_greater} to true
if {score} is not equal to 9:
    set {not_equal} to true
if {score} is between 5 and 10:
    set {between} to true
if {score} is not between 9 and 12:
    set {not_between} to true
if {score} is even:
    set {even} to true
if 7 is odd:
    set {odd} to true
if -3 is negative:
    set {negative} to true
if "hello" is text:
    set {text_type} to true
if {score} is a number:
    set {number_type} to true
if "" is empty:
    set {empty_string} to true
if "blue" is in {items::*}:
    set {list_membership} to true
if {items::*} contains "green":
    set {list_contains} to true
if {items::*} does not contain "red":
    set {list_not_contains} to true
)CX");
    cx::Runtime runtime(root);
    const bool loaded = runtime.loadScripts();
    if (!loaded) {
        for (const auto& diagnostic : runtime.diagnostics())
            std::cerr << "CONDITION FIXTURE DIAGNOSTIC: " << diagnostic.message << '\\n';
    }
    expect(loaded, "Skript-style comparison and membership conditions should load");
    expect(!runtime.hasErrors(), "valid English comparison and list conditions should not diagnose");
    expect(runtime.getGlobal("plain_greater_than").truthy(), "plain greater-than wording should work");
    expect(runtime.getGlobal("higher_than").truthy(), "higher-than is a greater-than alias");
    expect(runtime.getGlobal("bigger_than").truthy(), "bigger-than is a greater-than alias");
    expect(runtime.getGlobal("larger_than").truthy(), "larger-than is a greater-than alias");
    expect(runtime.getGlobal("are_more_than").truthy(), "are-more-than is a greater-than alias");
    expect(runtime.getGlobal("lower_than").truthy(), "lower-than is a less-than alias");
    expect(runtime.getGlobal("smaller_than").truthy(), "smaller-than is a less-than alias");
    expect(runtime.getGlobal("is_smaller_than").truthy(), "is-smaller-than is a less-than alias");
    expect(runtime.getGlobal("are_lower_than").truthy(), "are-lower-than is a less-than alias");
    expect(runtime.getGlobal("at_least").truthy(), "at least should map to greater-than-or-equal");
    expect(runtime.getGlobal("at_most").truthy(), "at most should map to less-than-or-equal");
    expect(runtime.getGlobal("inclusive_greater").truthy(), "is greater than or equal to should be parsed as one operator");
    expect(runtime.getGlobal("not_equal").truthy(), "is not equal to should compare unequal values");
    expect(runtime.getGlobal("between").truthy(), "is between should include values inside the numeric range");
    expect(runtime.getGlobal("not_between").truthy(), "is not between should negate the inclusive range check");
    expect(runtime.getGlobal("even").truthy(), "is even should accept whole even numbers");
    expect(runtime.getGlobal("odd").truthy(), "is odd should accept whole odd numbers");
    expect(runtime.getGlobal("negative").truthy(), "is negative should recognize negative values");
    expect(runtime.getGlobal("text_type").truthy(), "is text should test the string value type");
    expect(runtime.getGlobal("number_type").truthy(), "is a number should test the numeric value type");
    expect(runtime.getGlobal("empty_string").truthy(), "is empty should recognize empty strings");
    expect(runtime.getGlobal("list_membership").truthy(), "is in should test typed values against a list");
    expect(runtime.getGlobal("list_contains").truthy(), "list contains should find a typed list value");
    expect(runtime.getGlobal("list_not_contains").truthy(), "does not contain should negate list membership");
    std::filesystem::remove_all(root);
}

void testMathDomainResultsCanBeCheckedAsNaN() {
    const auto root = fixture("math-domain", R"CX(
on load:
    set {_bad_root} to sqrt(-1)
    set {_bad_log} to ln(-1)
    set {_bad_inverse} to asin(2)
    set {root_is_nan} to isNaN({_bad_root})
    set {log_is_nan} to isNaN({_bad_log})
    set {inverse_is_nan} to isNaN({_bad_inverse})
    set {normal_is_finite} to isFinite(3.5)
    set {log_zero_is_not_nan} to isNaN(log(0))
    set {log_zero_is_infinite} to isFinite(log(0))
)CX");
    cx::Runtime runtime(root);
    const bool loaded = runtime.loadScripts();
    if (!loaded) {
        for (const auto& diagnostic : runtime.diagnostics())
            std::cerr << "MATH FIXTURE DIAGNOSTIC: " << diagnostic.message << '\n';
    }
    expect(loaded, "out-of-domain math results should remain numeric NaN values instead of aborting script execution");
    expect(!runtime.hasErrors(), "valid NaN and infinity math results should not emit runtime diagnostics");
    expect(runtime.getGlobal("root_is_nan").truthy(), "isNaN should detect sqrt of a negative value");
    expect(runtime.getGlobal("log_is_nan").truthy(), "isNaN should detect negative logarithm inputs");
    expect(runtime.getGlobal("inverse_is_nan").truthy(), "inverse trig outside its real domain should return NaN");
    expect(runtime.getGlobal("normal_is_finite").truthy(), "isFinite should distinguish ordinary finite numbers");
    expect(!runtime.getGlobal("log_zero_is_not_nan").truthy(), "log(0) should be negative infinity, not NaN");
    expect(!runtime.getGlobal("log_zero_is_infinite").truthy(), "isFinite should detect the negative infinity result");
    std::filesystem::remove_all(root);
}
}

int main() {
    testSkriptStyleRandomNumberGrammar();
    testNaturalMathAndSkriptDegreeTrigonometry();
    testNaturalComparisonAndListConditions();
    testMathDomainResultsCanBeCheckedAsNaN();
    if (failures != 0) {
        std::cerr << "CX_LANGUAGE_EXPRESSION_TESTS_FAILED failures=" << failures << '\n';
        return 1;
    }
    std::cout << "CX_LANGUAGE_EXPRESSION_TESTS_OK\n";
    return 0;
}
