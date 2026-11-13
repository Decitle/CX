#include "cx/runtime.hpp"
#include "cx/script.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void write(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << contents;
    if (!output) throw std::runtime_error("could not write loop test fixture");
}
}

int main() {
    const std::string script = R"CX(
set {counter} to 0
repeat 3 times:
    add 1 to {counter}
set {items::*} to "red", "green", "blue"
for each item in {items::*}:
    add 1 to {counter}
    set {last_value} to loop-value
loop over {items::*}:
    add 1 to {counter}
    set {last_index} to loop-index
)CX";
    const auto parsed = cx::ScriptParser{}.parseText(script);
    if (!parsed.diagnostics.empty()) {
        for (const auto& d : parsed.diagnostics)
            std::cerr << d.message << " " << d.suggestion << '\n';
        return 1;
    }
    const auto root = std::filesystem::temp_directory_path() / "cx-loop-syntax";
    std::filesystem::remove_all(root);
    write(root / "scripts" / "main.cx", script);
    cx::Runtime runtime(root);
    const bool loaded = runtime.loadScripts();
    if (!loaded) for (const auto& d : runtime.diagnostics()) std::cerr << d.message << '\n';
    const bool correct = loaded && !runtime.hasErrors() &&
        runtime.getGlobal("counter").numberOr(-1.0) == 9.0 &&
        runtime.getGlobal("last_value").toString() == "blue" &&
        runtime.getGlobal("last_index").toString() == "3";
    std::filesystem::remove_all(root);
    if (!correct) {
        std::cerr << "Expected repeat and list loop aliases to preserve counted/list loop behavior.\n";
        return 1;
    }
    std::cout << "CX_LOOP_SYNTAX_TESTS_OK\n";
    return 0;
}
