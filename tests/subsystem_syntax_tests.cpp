#include "cx/script.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
int failures = 0;
void expect(bool condition, const std::string& message) {
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}
cx::ScriptProgram parse(const std::string& source) {
    return cx::ScriptParser{}.parseText(source, "<subsystem-syntax-corpus>");
}
void expectRejected(const std::string& source, const std::string& label) {
    const auto program = parse(source);
    expect(!program.diagnostics.empty(), "invalid syntax case '" + label + "' must be rejected");
}
void testNaturalFormsNormalizeToExistingSemantics() {
    const auto program = parse(R"CX(
on load:
    create wave "tone" duration 0.25 sample rate 44100
    set beam "shaft" endpoint light radius to 180
    tween entity "boulder" to (20, 40) over 0.9 seconds easing ease in out
on projectile hit entity "warden":
    set {last_hit_damage} to {_hit_damage}
on entity "player" enter trigger "door":
    set {last_actor} to {_trigger_actor}
canvas "Natural":
    create text "Paused" at (300, 180) font size 24
    create button "play" at grid (127, 1) anchor top right size (180, 44) label "Play"
)CX");
    expect(program.diagnostics.empty(), "natural subsystem syntax should parse");
    expect(program.handlers.size() == 3, "load, projectile-hit, and trigger-enter handlers should be retained");
    if (program.handlers.size() == 3) {
        const auto& load = program.handlers[0];
        expect(load.type == cx::EventType::Load, "first handler should remain the load event");
        expect(load.body.size() == 3, "load handler should contain wave, beam, and tween statements");
        if (load.body.size() == 3) {
            expect(load.body[0].type == cx::StatementType::CreateWave, "natural sample rate should create a wave");
            expect(load.body[0].property == "44100", "sample rate should preserve its numeric value");
            expect(load.body[1].type == cx::StatementType::SetBeam, "natural endpoint-light property should set a beam");
            expect(load.body[1].property == "endpoint_light_radius", "spaced beam property should normalize to compatibility key");
            expect(load.body[2].type == cx::StatementType::TweenEntity, "natural easing phrase should create a tween");
            expect(load.body[2].property == "ease_in_out", "spaced easing should normalize to compatibility key");
        }
        expect(program.handlers[1].type == cx::EventType::ProjectileHit &&
               program.handlers[1].selector == "warden",
               "projectile-hit header should require and retain its target selector");
        expect(program.handlers[2].type == cx::EventType::TriggerEnter &&
               program.handlers[2].selector == "player" &&
               program.handlers[2].secondarySelector == "door",
               "trigger header should retain its explicit actor/trigger pair");
    }
    expect(program.canvases.size() == 1, "natural canvas should be retained");
    if (program.canvases.size() == 1 && program.canvases[0].elements.size() == 2) {
        const auto& text = program.canvases[0].elements[0];
        const auto& button = program.canvases[0].elements[1];
        expect(text.type == cx::CanvasElementType::Text && text.fontSize == 24,
               "font size should accept natural spacing and preserve its value");
        expect(button.type == cx::CanvasElementType::Button && button.anchor == "top_right",
               "spaced anchors should normalize to the existing anchor key");
    } else expect(false, "natural canvas should contain two elements");
}
void testUnderscoredCompatibilityAliases() {
    const auto program = parse(R"CX(
on load:
    create wave "tone" duration 0.25 sample_rate 22050
    set beam "shaft" endpoint_light_radius to 90
    tween entity "boulder" to (20, 40) over 0.9 seconds easing ease_in_out
canvas "Legacy":
    create text "Paused" at (300, 180) font_size 18
    create button "play" at grid (127, 1) anchor top_right size (180, 44) label "Play"
)CX");
    expect(program.diagnostics.empty(), "underscored compatibility spellings should remain accepted");
    expect(program.handlers.size() == 1 && program.handlers[0].body.size() == 3,
           "legacy audio, beam, and tween statements should be retained");
    if (program.handlers.size() == 1 && program.handlers[0].body.size() == 3) {
        expect(program.handlers[0].body[0].property == "22050", "sample_rate should preserve its value");
        expect(program.handlers[0].body[1].property == "endpoint_light_radius", "endpoint_light_radius should retain its key");
        expect(program.handlers[0].body[2].property == "ease_in_out", "ease_in_out should retain its key");
    }
    expect(program.canvases.size() == 1 && program.canvases[0].elements.size() == 2,
           "legacy canvas forms should remain accepted");
    if (program.canvases.size() == 1 && program.canvases[0].elements.size() == 2) {
        expect(program.canvases[0].elements[0].fontSize == 18, "font_size should preserve its value");
        expect(program.canvases[0].elements[1].anchor == "top_right", "top_right anchor should remain accepted");
    }
}
void testNegativeAmbiguousAndMissingContextForms() {
    const std::vector<std::pair<std::string, std::string>> rejected = {
        {R"CX(canvas "Bad":
    create text "Paused" at (0, 0) font size "large"
)CX", "font size with a text value"},
        {R"CX(canvas "Bad":
    create button "play" at grid (127, 1) anchor top diagonal size (180, 44) label "Play"
)CX", "unknown canvas anchor"},
        {R"CX(on load:
    create wave "tone" duration 0.25 sample rate fast
)CX", "non-numeric sample rate"},
        {R"CX(on load:
    tween entity "boulder" to (20, 40) over 0.9 seconds easing ease around
)CX", "unknown easing phrase"},
        {R"CX(on load:
    set beam "shaft" endpoint glow radius to 180
)CX", "unknown beam property"},
        {R"CX(on projectile hit:
    set {damage} to {_hit_damage}
)CX", "projectile-hit without target selector"},
        {R"CX(on entity "player" enter trigger:
    set {actor} to {_trigger_actor}
)CX", "trigger event without trigger selector"},
        {R"CX(on projectile hit entity "   ":
    set {damage} to {_hit_damage}
)CX", "projectile-hit with an empty quoted selector"},
        {R"CX(on tween complete for entity "   ":
    set {name} to {_tween_entity}
)CX", "tween completion with an empty quoted selector"},
        {R"CX(on animation complete "   " for entity "player":
    set {clip} to {_animation_name}
)CX", "animation completion with an empty clip selector"},
        {R"CX(on entity "   " focus enter entity "chest":
    set {focus} to {_focus_entity}
)CX", "focus event with an empty actor selector"},
        {R"CX(on entity "player" focus exit entity "   ":
    set {focus} to {_focus_entity}
)CX", "focus event with an empty target selector"},
        {R"CX(on entity "player" exit trigger "   ":
    set {actor} to {_trigger_actor}
)CX", "trigger event with an empty quoted selector"},
        {R"CX(when key "   " is pressed:
    set {pressed} to true
)CX", "key event with an empty quoted selector"},
        {R"CX(on button click "   ":
    set {clicked} to true
)CX", "button event with an empty quoted selector"},
        {R"CX(canvas "Bad":
    create button "play" at grid (127, 1) anchor top right bottom left size (180, 44) label "Play"
)CX", "ambiguous multiple-anchor phrase"}
    };
    for (const auto& item : rejected) expectRejected(item.first, item.second);
}
}
int main() {
    testNaturalFormsNormalizeToExistingSemantics();
    testUnderscoredCompatibilityAliases();
    testNegativeAmbiguousAndMissingContextForms();
    if (failures != 0) {
        std::cerr << "CX_SUBSYSTEM_SYNTAX_TESTS_FAILED failures=" << failures << '\n';
        return 1;
    }
    std::cout << "CX_SUBSYSTEM_SYNTAX_TESTS_OK\n";
    return 0;
}
