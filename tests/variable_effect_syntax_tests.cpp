#include "cx/runtime.hpp"
#include "cx/script.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
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
    std::ofstream out(path, std::ios::binary);
    out << contents;
    if (!out) throw std::runtime_error("could not write script fixture");
}
}

int main() {
    const std::string script = R"CX(
set {score} to 4
increase {score} by 3
decrease {score} by 2
set {items::*} to "potion", "key", "potion", 2
remove "potion" from {items::*}
remove all "potion" from {items::*}
add "gem" to {items::*}
)CX";
    const auto parsed = cx::ScriptParser{}.parseText(script);
    expect(parsed.diagnostics.empty(), "Skript-style variable effects should parse without diagnostics");
    expect(parsed.bootstrap.size() == 7, "every variable effect should produce one statement");
    if (parsed.bootstrap.size() == 7) {
        expect(parsed.bootstrap[1].type == cx::StatementType::Add, "increase maps to the numeric/list add effect");
        expect(parsed.bootstrap[2].type == cx::StatementType::Subtract, "decrease maps to the numeric/list subtract effect");
        expect(parsed.bootstrap[4].type == cx::StatementType::Subtract, "remove maps to the list subtraction effect");
        expect(parsed.bootstrap[5].property == "all", "remove all preserves the all-matches intent");
        expect(parsed.bootstrap[6].type == cx::StatementType::Add, "add to list remains supported");
    }

    const auto root = std::filesystem::temp_directory_path() / "cx-variable-effect-syntax";
    std::filesystem::remove_all(root);
    write(root / "scripts" / "main.cx", script);
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "the variable effect fixture should execute");
    expect(!runtime.hasErrors(), "valid variable effects should not produce runtime errors");
    expect(runtime.getGlobal("score").numberOr(-1.0) == 5.0, "increase/decrease should produce the expected numeric result");
    expect(runtime.variableReport().find("potion") == std::string::npos,
           "remove all should remove every matching item from the resulting list report");
    expect(runtime.variableReport().find("gem") != std::string::npos,
           "add should append a value to the list after removal");
    std::filesystem::remove_all(root);

    if (failures != 0) {
        std::cerr << "CX_VARIABLE_EFFECT_SYNTAX_TESTS_FAILED failures=" << failures << '\n';
        return 1;
    }
    std::cout << "CX_VARIABLE_EFFECT_SYNTAX_TESTS_OK\n";
    return 0;
}
