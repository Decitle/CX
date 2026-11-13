#include "cx/script.hpp"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void expect(bool condition, const std::string& message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}
}

int main() {
    const std::string source = R"CX(
on load:
    set {load_seen} to true
on startup:
    set {startup_seen} to true
on project start:
    set {project_start_seen} to true
on tick:
    set {tick_seen} to true
on every tick:
    set {every_tick_seen} to true
each tick:
    set {each_tick_seen} to true
every 250 milliseconds:
    set {periodic_seen} to true
on press key "SPACE":
    set {space_seen} to true
on key "ENTER" pressed:
    set {enter_seen} to true
when key "ESC" is pressed:
    set {escape_seen} to true
on button click "resume":
    set {resume_seen} to true
on button "quit" clicked:
    set {quit_seen} to true
when button "settings" is clicked:
    set {settings_seen} to true
on file attached:
    set {file_seen} to true
)CX";
    const auto program = cx::ScriptParser{}.parseText(source);
    expect(program.diagnostics.empty(), "all documented event aliases should parse without diagnostics");
    expect(program.handlers.size() == 14, "all event declarations should produce one handler each");
    if (program.handlers.size() == 14) {
        expect(program.handlers[0].type == cx::EventType::Load, "on load remains valid");
        expect(program.handlers[1].type == cx::EventType::Load, "on startup maps to load");
        expect(program.handlers[2].type == cx::EventType::Load, "on project start maps to load");
        expect(program.handlers[3].type == cx::EventType::Tick, "on tick remains valid");
        expect(program.handlers[4].type == cx::EventType::Tick, "on every tick maps to tick");
        expect(program.handlers[5].type == cx::EventType::Tick, "each tick maps to tick");
        expect(program.handlers[6].type == cx::EventType::Periodic, "periodic schedules retain their own event type");
        expect(program.handlers[7].selector == "SPACE", "legacy key syntax remains valid");
        expect(program.handlers[8].selector == "ENTER", "key pressed syntax maps to key press");
        expect(program.handlers[9].selector == "ESCAPE", "when escape is pressed canonicalizes ESC");
        expect(program.handlers[10].selector == "resume", "button click syntax maps to button click");
        expect(program.handlers[11].selector == "quit", "button clicked syntax maps to button click");
        expect(program.handlers[12].selector == "settings", "when button clicked maps to button click");
        expect(program.handlers[13].type == cx::EventType::FileAttach, "file attached maps to file attach");
    }
    if (failures != 0) {
        std::cerr << "CX_EVENT_SYNTAX_TESTS_FAILED failures=" << failures << '\n';
        return 1;
    }
    std::cout << "CX_EVENT_SYNTAX_TESTS_OK\n";
    return 0;
}
