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
on load:
    set {counter} to 0
    repeat 3 times:
        add 1 to {counter}
        set {counted_loop_counter} to loop-counter
        set {counted_loop_iteration} to the loop iteration
        set {counted_loop_text} to "counter %loop counter%; iteration %the loop-iteration%"

    set {items::*} to "red", "green", "blue"
    set {inner_items::*} to "inner-a", "inner-b"
    for each item in {items::*}:
        add 1 to {counter}
        set {last_value} to loop-value
        set {list_loop_iteration} to loop-iteration
    loop over {items::*}:
        add 1 to {counter}
        set {last_index} to loop-index

    set {_item} to "original-item"
    for each {_item} in {items::*}:
        set {last_named_item} to {_item}
        for each {_item} in {inner_items::*}:
            set {last_inner_item} to {_item}
        set {outer_after_nested_loop} to {_item}
    set {restored_item} to {_item}

    set {_index} to "original-index"
    set {_value} to "original-value"
    for each key {_index} and value {_value} in {items::*}:
        set {last_named_key} to {_index}
        set {last_named_value} to {_value}
    set {restored_index} to {_index}
    set {restored_value} to {_value}

    set {_while_count} to 0
    while {_while_count} < 2:
        add 1 to {_while_count}
        set {while_loop_counter} to loop counter
        set {while_loop_iteration} to the loop-iteration

    set {_comma_key} to "old-key"
    set {_comma_value} to "old-value"
    for each {_comma_key}, {_comma_value} in {items::*}:
        set {comma_key} to {_comma_key}
        set {comma_value} to {_comma_value}
    set {restored_comma_key} to {_comma_key}
    set {restored_comma_value} to {_comma_value}
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
    if (!loaded)
        for (const auto& d : runtime.diagnostics()) std::cerr << d.message << '\n';
    const bool correct = loaded && !runtime.hasErrors() &&
        runtime.getGlobal("counter").numberOr(-1.0) == 9.0 &&
        runtime.getGlobal("counted_loop_counter").numberOr(-1.0) == 3.0 &&
        runtime.getGlobal("counted_loop_iteration").numberOr(-1.0) == 3.0 &&
        runtime.getGlobal("counted_loop_text").toString() == "counter 3; iteration 3" &&
        runtime.getGlobal("list_loop_iteration").numberOr(-1.0) == 3.0 &&
        runtime.getGlobal("while_loop_counter").numberOr(-1.0) == 2.0 &&
        runtime.getGlobal("while_loop_iteration").numberOr(-1.0) == 2.0 &&
        runtime.getGlobal("last_value").toString() == "blue" &&
        runtime.getGlobal("last_index").toString() == "3" &&
        runtime.getGlobal("last_named_item").toString() == "blue" &&
        runtime.getGlobal("last_inner_item").toString() == "inner-b" &&
        runtime.getGlobal("outer_after_nested_loop").toString() == "blue" &&
        runtime.getGlobal("restored_item").toString() == "original-item" &&
        runtime.getGlobal("last_named_key").toString() == "3" &&
        runtime.getGlobal("last_named_value").toString() == "blue" &&
        runtime.getGlobal("restored_index").toString() == "original-index" &&
        runtime.getGlobal("restored_value").toString() == "original-value" &&
        runtime.getGlobal("comma_key").toString() == "3" &&
        runtime.getGlobal("comma_value").toString() == "blue" &&
        runtime.getGlobal("restored_comma_key").toString() == "old-key" &&
        runtime.getGlobal("restored_comma_value").toString() == "old-value";
    std::filesystem::remove_all(root);
    if (!correct) {
        std::cerr << "Named list loops should bind the current value/key, preserve nested scopes, and restore previous local values.\n";
        return 1;
    }
    std::cout << "CX_LOOP_SYNTAX_TESTS_OK\n";
    return 0;
}
