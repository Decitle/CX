#include "cx/script.hpp"

#include <algorithm>
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
on start:
    set {start_seen} to true
on startup:
    set {startup_seen} to true
on project start:
    set {project_start_seen} to true
on project load:
    set {project_load_seen} to true
on initialize:
    set {initialize_seen} to true

on tick:
    set {tick_seen} to true
every tick:
    set {every_tick_seen} to true
on every tick:
    set {on_every_tick_seen} to true
each tick:
    set {each_tick_seen} to true
on each tick:
    set {on_each_tick_seen} to true
every 250 milliseconds:
    set {periodic_seen} to true
on every 1 second:
    set {periodic_seconds_seen} to true

on file attach:
    set {file_attach_seen} to true
on file attached:
    set {file_attached_seen} to true

on projectile hit entity " WARDEN ":
    set {projectile_hit_seen} to true
on tween complete for entity " Boulder ":
    set {tween_complete_seen} to true
on animation complete " Wake " for entity " Player ":
    set {animation_complete_seen} to true
on entity " Player " focus enter entity " Chest ":
    set {focus_enter_seen} to true
on entity " Player " focus exit entity " Chest ":
    set {focus_exit_seen} to true
on entity " Player " enter trigger " Door ":
    set {trigger_enter_seen} to true
on entity " Player " exit trigger " Door ":
    set {trigger_exit_seen} to true

on press key "SPACE":
    set {space_seen} to true
on key press "A":
    set {a_seen} to true
on key "RETURN" pressed:
    set {enter_seen} to true
when key "ESC" is pressed:
    set {escape_seen} to true
on press escape:
    set {escape_alias_seen} to true
on press key escape:
    set {escape_key_alias_seen} to true
when escape is pressed:
    set {escape_when_alias_seen} to true

on click button "resume":
    set {resume_seen} to true
on button click "settings":
    set {settings_seen} to true
on button "quit" click:
    set {quit_seen} to true
when button "save" is clicked:
    set {save_seen} to true
)CX";

    const auto program = cx::ScriptParser{}.parseText(source);
    expect(program.diagnostics.empty(), "all canonical event headers and documented aliases should parse without diagnostics");
    expect(program.handlers.size() == 34, "every event declaration should produce one handler");

    const auto countType = [&](cx::EventType type) {
        return static_cast<std::size_t>(std::count_if(
            program.handlers.begin(), program.handlers.end(),
            [type](const cx::EventHandler& handler) { return handler.type == type; }));
    };
    const auto hasEvent = [&](cx::EventType type, const std::string& selector,
                              const std::string& secondarySelector = std::string()) {
        return std::any_of(program.handlers.begin(), program.handlers.end(),
            [&](const cx::EventHandler& handler) {
                return handler.type == type && handler.selector == selector &&
                       handler.secondarySelector == secondarySelector;
            });
    };

    expect(countType(cx::EventType::Load) == 6, "all startup aliases should map to the load event");
    expect(countType(cx::EventType::Tick) == 5, "all fixed-step tick aliases should map to tick");
    expect(countType(cx::EventType::Periodic) == 2, "duration-based schedules should retain the periodic event type");
    expect(countType(cx::EventType::FileAttach) == 2, "both file-attachment headers should map to file attach");

    expect(hasEvent(cx::EventType::ProjectileHit, "warden"), "projectile-hit selectors should trim and lowercase entity names");
    expect(hasEvent(cx::EventType::TweenComplete, "boulder"), "tween completion selectors should trim and lowercase entity names");
    expect(hasEvent(cx::EventType::AnimationComplete, "player", "wake"),
           "animation completion should normalize the entity and clip selectors independently");
    expect(hasEvent(cx::EventType::FocusEnter, "player", "chest") &&
           hasEvent(cx::EventType::FocusExit, "player", "chest"),
           "focus transition headers should preserve their explicit actor/target pair");
    expect(hasEvent(cx::EventType::TriggerEnter, "player", "door") &&
           hasEvent(cx::EventType::TriggerExit, "player", "door"),
           "trigger transition headers should preserve their explicit actor/trigger pair");

    expect(hasEvent(cx::EventType::KeyPress, "SPACE"), "press-key syntax should remain valid");
    expect(hasEvent(cx::EventType::KeyPress, "A"), "key-press word order should remain valid");
    expect(hasEvent(cx::EventType::KeyPress, "ENTER"), "RETURN should canonicalize to ENTER");
    expect(countType(cx::EventType::KeyPress) == 7, "all documented special escape forms should map to key press");
    expect(hasEvent(cx::EventType::KeyPress, "ESCAPE"), "ESC and the escape aliases should canonicalize consistently");

    expect(hasEvent(cx::EventType::ButtonClick, "resume"), "click-button syntax should remain valid");
    expect(hasEvent(cx::EventType::ButtonClick, "settings"), "button-click syntax should remain valid");
    expect(countType(cx::EventType::ButtonClick) == 5, "all documented button-click aliases should map to button click");
    expect(hasEvent(cx::EventType::ButtonClick, "quit"), "button click/clicked syntax should remain valid");
    expect(hasEvent(cx::EventType::ButtonClick, "save"), "when-button-clicked syntax should remain valid");

    if (failures != 0) {
        std::cerr << "CX_EVENT_SYNTAX_TESTS_FAILED failures=" << failures << '\n';
        return 1;
    }
    std::cout << "CX_EVENT_SYNTAX_TESTS_OK\n";
    return 0;
}
