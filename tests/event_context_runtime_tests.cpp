#include "cx/runtime.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {
int failures = 0;

void expect(bool condition, const std::string& message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void writeText(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << content;
}
}

int main() {
    const auto root = std::filesystem::temp_directory_path() / "cx-event-context-file-attach-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");

    writeText(root / "drop.txt", "event payload fixture");
    writeText(root / "scripts" / "main.cx", R"CX(
set {attached_path} to "not supplied"
set {drop_x} to -1
set {drop_y} to -1
set {drop_button} to "not supplied"
set {drop_count} to 0
set {resumed_path} to "not resumed"
set {resumed_x} to -1

on file attach:
    set {attached_path} to {_attached_file}
    set {drop_x} to {_mouse_x}
    set {drop_y} to {_mouse_y}
    set {drop_button} to {_button_under_cursor}
    add 1 to {drop_count}
    wait 1 milliseconds
    set {resumed_path} to {_attached_file}
    set {resumed_x} to {_mouse_x}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "file-attachment event fixture should load");

    // The public host API resolves a relative path against the project root before
    // dispatching the event. With no canvas under the cursor, the button ID is empty.
    runtime.attachFile("drop.txt", 12.5F, 37.25F);

    const auto expectedPath = std::filesystem::absolute(root / "drop.txt").lexically_normal().string();
    expect(runtime.getGlobal("attached_path").toString() == expectedPath,
           "file-attachment context should contain the resolved absolute file path");
    expect(runtime.getGlobal("drop_x").numberOr(-1.0) == 12.5,
           "file-attachment context should contain the cursor x coordinate");
    expect(runtime.getGlobal("drop_y").numberOr(-1.0) == 37.25,
           "file-attachment context should contain the cursor y coordinate");
    expect(runtime.getGlobal("drop_button").toString().empty(),
           "file-attachment context should provide empty text when no canvas button is under the cursor");
    expect(runtime.getGlobal("drop_count").numberOr(0.0) == 1.0,
           "one accepted file attachment should dispatch exactly one event");
    expect(runtime.getGlobal("resumed_path").toString() == "not resumed",
           "a handler that reaches wait should suspend before its later statements");

    // A suspended event continuation must retain its original event-local payload.
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    runtime.tick();
    expect(runtime.getGlobal("resumed_path").toString() == expectedPath,
           "event-local file path should remain available after the handler resumes");
    expect(runtime.getGlobal("resumed_x").numberOr(-1.0) == 12.5,
           "event-local cursor coordinates should remain available after the handler resumes");

    expect(!runtime.hasErrors(), "valid file-attachment context should not produce runtime diagnostics");
    std::filesystem::remove_all(root);

    if (failures != 0) {
        std::cerr << "CX_EVENT_CONTEXT_RUNTIME_TESTS_FAILED failures=" << failures << '\n';
        return 1;
    }
    std::cout << "CX_EVENT_CONTEXT_RUNTIME_TESTS_OK\n";
    return 0;
}
