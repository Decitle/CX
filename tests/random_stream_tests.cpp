#include "cx/runtime.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>

namespace {
int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void write(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << contents;
    if (!output) throw std::runtime_error("could not write test fixture: " + path.string());
}

std::filesystem::path fixture(std::string name, const std::string& script) {
    const auto root = std::filesystem::temp_directory_path() / ("cx-random-stream-" + name);
    std::filesystem::remove_all(root);
    write(root / "scripts" / "main.cx", script);
    return root;
}

void testSplitMixGoldenSequenceAndReproducibility() {
    const auto script = R"CX(
seed random stream "gold" with 0
set {float_1} to random_float("gold")
set {int_1} to random_int("gold", -5, 5)
set {int_2} to random_int("gold", -5, 5)
)CX";
    const auto rootA = fixture("golden-a", script);
    const auto rootB = fixture("golden-b", script);
    cx::Runtime a(rootA);
    cx::Runtime b(rootB);
    expect(a.loadScripts(), "golden-vector runtime A should load");
    expect(b.loadScripts(), "golden-vector runtime B should load");

    const double expectedFloat = 0.8833108082136426;
    expect(std::abs(a.getGlobal("float_1").numberOr(-1.0) - expectedFloat) < 1e-15,
           "SplitMix64 random_float golden vector should be stable");
    expect(a.getGlobal("int_1").numberOr(99.0) == 5.0 &&
           a.getGlobal("int_2").numberOr(99.0) == -4.0,
           "unbiased inclusive random_int sequence should match its SplitMix64 golden vector");
    expect(a.getGlobal("float_1").numberOr(-1.0) == b.getGlobal("float_1").numberOr(-2.0) &&
           a.getGlobal("int_1").numberOr(99.0) == b.getGlobal("int_1").numberOr(98.0) &&
           a.getGlobal("int_2").numberOr(99.0) == b.getGlobal("int_2").numberOr(98.0),
           "separate runtimes with the same seed must produce identical sequences");
    expect(!a.hasErrors() && !b.hasErrors(), "valid random functions should not emit diagnostics");
    std::filesystem::remove_all(rootA);
    std::filesystem::remove_all(rootB);
}

void testOnLoadCanSeedAndUseStreams() {
    const auto root = fixture("on-load", R"CX(
on load:
    seed random stream "startup" with 4815
    set {startup_first} to random_float("startup")
)CX");
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "on load should seed and use its named random stream");
    expect(std::abs(runtime.getGlobal("startup_first").numberOr(-1.0) - 0.9138626110969524) < 1e-15,
           "on load must seed the stream before its first deterministic draw");
    expect(!runtime.hasErrors(), "on load random stream initialization should not emit diagnostics");
    runtime.executeInline("set {startup_second} to random_float(\"startup\")");
    expect(std::abs(runtime.getGlobal("startup_second").numberOr(-1.0) - 0.05412876104711306) < 1e-15,
           "the on-load stream's advanced state must be committed to the live runtime");
    std::filesystem::remove_all(root);
}

void testNamedStreamsAreIndependentAndReseedingIsExplicit() {
    const auto root = fixture("independence", R"CX(
seed random stream " alpha " with 12345
seed random stream "beta" with 777
set {alpha_first} to random_int("alpha", 0, 1000)
set {beta_draw} to random_float("beta")
set {alpha_second} to random_int("alpha", 0, 1000)
seed random stream "alpha" with 12345
set {alpha_restarted} to random_int("ALPHA", 0, 1000)
)CX");
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "independent stream fixture should load");
    expect(runtime.getGlobal("alpha_first").numberOr(-1.0) == 586.0,
           "alpha first bounded draw should match deterministic SplitMix output");
    expect(runtime.getGlobal("alpha_second").numberOr(-1.0) == 339.0,
           "a beta draw must not consume or perturb alpha state");
    expect(runtime.getGlobal("alpha_restarted").numberOr(-1.0) == 586.0,
           "re-seeding a named stream should restart its sequence");
    expect(runtime.getGlobal("beta_draw").numberOr(-1.0) >= 0.0 &&
           runtime.getGlobal("beta_draw").numberOr(2.0) < 1.0,
           "random_float should produce a value in the half-open interval [0, 1)");
    expect(!runtime.hasErrors(), "independent streams should not generate diagnostics");
    std::filesystem::remove_all(root);
}

void testChoicePreservesValueTypesAndSingletonDoesNotAdvance() {
    const auto root = fixture("choice", R"CX(
seed random stream "loot" with 0
set {picked} to random_choice("loot", "first", true, 2)
set {after_choice} to random_float("loot")
seed random stream "single" with 0
set {only} to random_choice("single", 42)
set {single_next} to random_float("single")
)CX");
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "random choice fixture should load");
    const cx::Value pickedValue = runtime.getGlobal("picked");
    const auto& pickedStorage = pickedValue.storage();
    const auto* pickedBool = std::get_if<bool>(&pickedStorage);
    expect(pickedBool != nullptr && *pickedBool,
           "random_choice should preserve a selected boolean instead of converting it to text or number");
    const double secondSplitMixFloat = static_cast<double>(0x6e789e6aa1b965f4ULL >> 11U) * (1.0 / 9007199254740992.0);
    expect(std::abs(runtime.getGlobal("after_choice").numberOr(-1.0) - secondSplitMixFloat) < 1e-15,
           "a multi-option random_choice should consume exactly one bounded draw when no rejection occurs");
    expect(runtime.getGlobal("only").numberOr(-1.0) == 42.0,
           "a singleton random_choice should return its only option");
    expect(std::abs(runtime.getGlobal("single_next").numberOr(-1.0) - 0.8833108082136426) < 1e-15,
           "a singleton random_choice should not advance its stream");
    expect(!runtime.hasErrors(), "valid random choices should not emit diagnostics");
    std::filesystem::remove_all(root);
}


void testChoiceEvaluatesOnlySelectedOption() {
    const auto root = fixture("lazy-choice", R"CX(
seed random stream "choice" with 0
seed random stream "side" with 0
set {picked} to random_choice("choice", random_int("missing", 1, 2), 42)
set {side_after} to random_float("side")
)CX");
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "an unselected invalid choice expression must not be evaluated");
    expect(runtime.getGlobal("picked").numberOr(-1.0) == 42.0,
           "the deterministic second option should be returned");
    expect(std::abs(runtime.getGlobal("side_after").numberOr(-1.0) - 0.8833108082136426) < 1e-15,
           "evaluating a choice must not consume a random stream used only by an unselected option");
    expect(!runtime.hasErrors(), "unselected invalid choice expressions must not emit diagnostics");
    std::filesystem::remove_all(root);

    const auto selected = fixture("selected-choice-error", R"CX(
seed random stream "choice" with 0
set {picked} to random_choice("choice", "safe", random_float("missing"))
)CX");
    cx::Runtime selectedRuntime(selected);
    expect(!selectedRuntime.loadScripts(), "an invalid selected option must still be evaluated and rejected");
    expect(selectedRuntime.hasErrors(), "a selected option failure must produce a diagnostic");
    std::filesystem::remove_all(selected);
}

void testIntegerSingletonAndSafeRangeBoundaries() {
    const auto root = fixture("integer-boundaries", R"CX(
seed random stream "single" with 0
set {fixed} to random_int("single", 8, 8)
set {next} to random_float("single")
seed random stream "wide" with -9007199254740991
set {wide} to random_int("wide", -9007199254740991, 9007199254740991)
)CX");
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "safe integer boundary fixture should load");
    expect(runtime.getGlobal("fixed").numberOr(-1.0) == 8.0,
           "an inclusive random_int range with equal endpoints should return that endpoint");
    const double secondFloat = static_cast<double>(0x6e789e6aa1b965f4ULL >> 11U) * (1.0 / 9007199254740992.0);
    expect(std::abs(runtime.getGlobal("next").numberOr(-1.0) - secondFloat) < 1e-15,
           "a successful singleton random_int call consumes exactly one stream draw");
    const double wide = runtime.getGlobal("wide").numberOr(-9007199254740992.0);
    expect(std::isfinite(wide) && std::trunc(wide) == wide &&
           wide >= -9007199254740991.0 && wide <= 9007199254740991.0,
           "random_int supports the documented inclusive safe-integer endpoints");
    expect(!runtime.hasErrors(), "valid safe-range draws should not produce diagnostics");
    std::filesystem::remove_all(root);
}

void testInvalidSeedValuesAndStreamNamesAreRejected() {
    const std::vector<std::string> invalidScripts = {
        "seed random stream \"fraction\" with 1.5\n",
        "seed random stream \"unsafe\" with 9007199254740992\n",
        "seed random stream \"text\" with \"123\"\n",
        "seed random stream \"\" with 1\n",
        "seed random stream \"" + std::string(65, 'x') + "\" with 1\n"
    };
    for (std::size_t i = 0; i < invalidScripts.size(); ++i) {
        const auto root = fixture("invalid-seed-" + std::to_string(i), invalidScripts[i]);
        cx::Runtime runtime(root);
        expect(!runtime.loadScripts(), "invalid seed/name case " + std::to_string(i) + " should be rejected");
        expect(runtime.hasErrors(), "invalid seed/name case should have an actionable diagnostic");
        std::filesystem::remove_all(root);
    }
}

void testUnseededStreamAndInvalidRangesFailClearly() {
    const auto root = fixture("invalid", R"CX(
seed random stream "ready" with 7
set {bad_range} to random_int("ready", 10, 1)
)CX");
    cx::Runtime runtime(root);
    expect(!runtime.loadScripts(), "reversed random_int bounds should reject startup transactionally");
    expect(runtime.hasErrors(), "invalid random ranges should report a diagnostic");
    std::filesystem::remove_all(root);

    const auto unseeded = fixture("unseeded", R"CX(
set {bad} to random_float("not_seeded")
)CX");
    cx::Runtime missing(unseeded);
    expect(!missing.loadScripts(), "an unseeded stream must not be initialized implicitly");
    expect(missing.hasErrors(), "using an unseeded stream should report an actionable diagnostic");
    std::filesystem::remove_all(unseeded);

    const auto fraction = fixture("fraction", R"CX(
seed random stream "fraction" with 4
set {bad} to random_int("fraction", 1.5, 10)
)CX");
    cx::Runtime fractional(fraction);
    expect(!fractional.loadScripts(), "fractional random_int bounds must be rejected");
    std::filesystem::remove_all(fraction);
}

void testFailedReloadKeepsLiveRandomStreamState() {
    const auto root = fixture("reload", R"CX(
seed random stream "loot" with 12345
set {first} to random_int("loot", 0, 1000)
)CX");
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "valid random runtime should load before failed reload test");

    write(root / "scripts" / "main.cx", R"CX(
seed random stream "loot" with 9
set {candidate_failure} to random_float("missing_stream")
)CX");
    expect(!runtime.loadScripts(false), "failed candidate startup must not commit candidate random stream state");
    runtime.executeInline("set {next_after_failed_reload} to random_int(\"loot\", 0, 1000)");
    expect(runtime.getGlobal("first").numberOr(-1.0) == 586.0,
           "failed reload must preserve the last-good program globals");
    expect(runtime.getGlobal("next_after_failed_reload").numberOr(-1.0) == 339.0,
           "failed reload must preserve the exact live random stream position");
    std::filesystem::remove_all(root);
}

void testSuccessfulReloadRebuildsStreamsFromScriptSeed() {
    const auto root = fixture("successful-reload", R"CX(
seed random stream "world" with 8675309
set {first_roll} to random_int("world", 1, 1000000)
)CX");
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "random stream reset fixture should load initially");
    const auto first = runtime.getGlobal("first_roll").numberOr(-1.0);
    runtime.executeInline("set {advanced} to random_int(\"world\", 1, 1000000)");
    expect(runtime.loadScripts(false), "successful reload should commit its newly seeded candidate stream");
    expect(runtime.getGlobal("first_roll").numberOr(-2.0) == first,
           "successful reload should reconstruct the same stream sequence from the script seed");
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    testSplitMixGoldenSequenceAndReproducibility();
    testOnLoadCanSeedAndUseStreams();
    testNamedStreamsAreIndependentAndReseedingIsExplicit();
    testChoicePreservesValueTypesAndSingletonDoesNotAdvance();
    testChoiceEvaluatesOnlySelectedOption();
    testIntegerSingletonAndSafeRangeBoundaries();
    testInvalidSeedValuesAndStreamNamesAreRejected();
    testUnseededStreamAndInvalidRangesFailClearly();
    testFailedReloadKeepsLiveRandomStreamState();
    testSuccessfulReloadRebuildsStreamsFromScriptSeed();
    if (failures != 0) {
        std::cerr << "CX_RANDOM_STREAM_TESTS_FAILED failures=" << failures << '\n';
        return 1;
    }
    std::cout << "CX_RANDOM_STREAM_TESTS_OK\n";
    return 0;
}
