#include "cx/runtime.hpp"

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
    std::ofstream output(path, std::ios::binary);
    output << contents;
    if (!output) throw std::runtime_error("could not write fixture: " + path.string());
}
std::filesystem::path makeProject(const std::string& name, const std::string& script) {
    const auto root = std::filesystem::temp_directory_path() / ("cx-object-reference-" + name);
    std::filesystem::remove_all(root);
    write(root / "assets" / "textures" / "object.png", "placeholder texture data");
    write(root / "scripts" / "main.cx", script);
    return root;
}
void testObjectVariablesAndEnglishProperties() {
    const auto root = makeProject("properties", R"CX(
on load:
    create sprite "hero" with texture {$object}
    create entity "npc" with texture {$object}
    set {_object} to sprite "hero"
    set {_entity} to entity "npc"
    set x-coordinate of {_object} to 15
    set y-coordinate of sprite {_object} to 25
    set position of object {_object} to (30, 40)
    set size of object {_object} to (24, 28)
    set sprite "hero"'s x-coordinate to 35
    set rotation of sprite {_object} to 90
    set sprite "hero"'s rotation to 45
    set rotation of entity {_entity} to 180
    set speed of entity {_entity} to 125
    set kind of entity {_entity} to "boss"
    set the team of {_entity} to "hostile"
    set {read_kind} to kind of entity {_entity}
    set {read_team} to the team of {_entity}
    set health of entity {_entity} to 8
    set max health of entity {_entity} to 12
    set {_target_health} to 10
    set the health of {_entity} to {_target_health}
    set max health of entity {_entity} to 9
    set {read_health_clamped} to health of entity {_entity}
    set max-health of entity {_entity} to 14
    set the health of {_entity} to {_target_health}
    set entity {_entity}'s health to 6
    set {read_health} to health of entity {_entity}
    set {read_max_health} to the max health of {_entity}
    set {read_health_global} to {entity::npc::health}
    set {read_max_health_global} to {entity::npc::max_health}
    set {_hide} to false
    set visibility of sprite {_object} to {_hide}
    set {read_x} to x-coordinate of sprite "hero"
    set {read_y} to y-coordinate of {_object}
    set {read_rotation} to rotation of {_object}
    set {read_speed} to speed of entity {_entity}
    set {read_visibility} to visibility of {_object}
    set {read_width} to width of {_object}
    set {read_height} to height of sprite {_object}
    set {read_texture} to texture of sprite "hero"
    if {_object} is sprite "hero":
        set {object_identity_matches} to true
)CX");
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "Skript-like object-handle script should load and execute");
    expect(!runtime.hasErrors(), "valid object handles and property setters should not report errors");
    if (runtime.sprites().contains("hero")) {
        const auto& hero = runtime.sprites().at("hero");
        expect(hero.x == 35.0F && hero.y == 40.0F, "canonical position and possessive x-coordinate setters should share runtime behavior");
        expect(hero.width == 24.0F && hero.height == 28.0F, "size setter should update both dimensions");
        expect(hero.rotationDegrees == 45.0F, "possessive rotation setter should update the live sprite property");
        expect(!hero.visible, "visibility should accept a boolean expression");
    } else expect(false, "hero sprite should exist");
    if (runtime.sprites().contains("npc")) {
        const auto& npc = runtime.sprites().at("npc");
        expect(npc.rotationDegrees == 180.0F, "entity handle should support rotation");
        expect(npc.moveSpeedPixelsPerSecond == 125.0F, "entity speed should remain accessible through a handle");
        expect(npc.health == 6 && npc.maxHealth == 14, "possessive entity-handle health setter should preserve health/max-health rules");
    } else expect(false, "npc entity should exist");
    expect(runtime.getGlobal("read_x").numberOr(-1.0) == 35.0, "x-coordinate getter should read the current value after a possessive setter");
    expect(runtime.getGlobal("read_y").numberOr(-1.0) == 40.0, "property getters should work with a local object handle");
    expect(runtime.getGlobal("read_rotation").numberOr(-1.0) == 45.0, "rotation getter should read the value assigned by the possessive form");
    expect(runtime.getGlobal("read_speed").numberOr(-1.0) == 125.0, "speed getter should work for entity handles");
    expect(!runtime.getGlobal("read_visibility").truthy(), "visibility getter should return the live boolean property");
    expect(runtime.getGlobal("read_width").numberOr(-1.0) == 24.0, "width getter should expose the live horizontal size");
    expect(runtime.getGlobal("read_height").numberOr(-1.0) == 28.0, "height getter should expose the live vertical size");
    expect(runtime.getGlobal("read_texture").toString() == runtime.sprites().at("hero").textureId,
           "texture getter should return the active texture identifier");
    expect(runtime.getGlobal("read_kind").toString() == "boss",
           "kind getter should return the entity's registered classification");
    expect(runtime.getGlobal("read_team").toString() == "hostile",
           "team getter should return the entity's registered team");
    expect(runtime.getGlobal("read_health_clamped").numberOr(-1.0) == 9.0,
           "lowering max health should clamp current health to the new maximum");
    expect(runtime.getGlobal("read_health").numberOr(-1.0) == 6.0, "health getter should observe the possessive entity-handle setter");
           "health getter should read the live entity health value");
    expect(runtime.getGlobal("read_max_health").numberOr(-1.0) == 14.0,
           "max health getter should read the live entity maximum health");
    expect(runtime.getGlobal("read_health_global").numberOr(-1.0) == 10.0,
           "entity health property writes should keep the public health variable synchronized");
    expect(runtime.getGlobal("read_max_health_global").numberOr(-1.0) == 14.0,
           "entity max-health property writes should keep the public max-health variable synchronized");
    expect(runtime.getGlobal("object_identity_matches").truthy(), "expressions resolving to the same object should compare by identity");
    std::filesystem::remove_all(root);
}
void testClassificationPropertiesRejectSprites() {
    const auto root = makeProject("sprite-classification", R"CX(
on load:
    create sprite "hero" with texture {$object}
    set {bad_kind} to kind of sprite "hero"
)CX");
    cx::Runtime runtime(root);
    expect(!runtime.loadScripts() && runtime.hasErrors(), "sprite objects must not expose entity kind properties");
    bool found = false;
    for (const auto& diagnostic : runtime.diagnostics())
        if (diagnostic.message.find("only available on entities") != std::string::npos) found = true;
    expect(found, "reading kind from a sprite should explain that it belongs to entities");
    std::filesystem::remove_all(root);
}
void testClassificationSettersRejectSprites() {
    const auto root = makeProject("sprite-classification-setter", R"CX(
on load:
    create sprite "hero" with texture {$object}
    set team of sprite "hero" to "hostile"
)CX");
    cx::Runtime runtime(root);
    expect(!runtime.loadScripts() && runtime.hasErrors(), "sprite objects must reject entity team setters");
    bool found = false;
    for (const auto& diagnostic : runtime.diagnostics())
        if (diagnostic.message.find("only available on entities") != std::string::npos) found = true;
    expect(found, "setting team on a sprite should explain that it belongs to entities");
    std::filesystem::remove_all(root);
}
void testHealthPropertiesRejectSprites() {
    const auto root = makeProject("sprite-health", R"CX(
on load:
    create sprite "hero" with texture {$object}
    set {bad_health} to health of sprite "hero"
)CX");
    cx::Runtime runtime(root);
    expect(!runtime.loadScripts() && runtime.hasErrors(), "sprite objects must not expose entity health properties");
    bool found = false;
    for (const auto& diagnostic : runtime.diagnostics())
        if (diagnostic.message.find("only available on entities") != std::string::npos) found = true;
    expect(found, "reading health from a sprite should explain that health belongs to entities");
    std::filesystem::remove_all(root);
}
void testHealthSettersRejectSprites() {
    const auto root = makeProject("sprite-health-setter", R"CX(
on load:
    create sprite "hero" with texture {$object}
    set health of sprite "hero" to 5
)CX");
    cx::Runtime runtime(root);
    expect(!runtime.loadScripts() && runtime.hasErrors(), "sprite objects must reject entity health setters");
    bool found = false;
    for (const auto& diagnostic : runtime.diagnostics())
        if (diagnostic.message.find("only available on entities") != std::string::npos) found = true;
    expect(found, "setting health on a sprite should explain that health belongs to entities");
    std::filesystem::remove_all(root);
}
void testStaleHandleCannotTargetReplacement() {
    const auto root = makeProject("stale", R"CX(
on load:
    create sprite "hero" with texture {$object}
    set {_old_object} to sprite "hero"
    delete sprite "hero"
    create sprite "hero" with texture {$object}
    set x-coordinate of {_old_object} to 99
)CX");
    cx::Runtime runtime(root);
    const bool loaded = runtime.loadScripts();
    expect(!loaded && runtime.hasErrors(), "stale references should be rejected after same-name replacement");
    bool found = false;
    for (const auto& diagnostic : runtime.diagnostics())
        if (diagnostic.message.find("no longer valid") != std::string::npos) found = true;
    expect(found, "stale-reference diagnostic should identify removal or replacement");
    expect(runtime.sprites().empty(), "failed bootstrap must not publish partially initialized object state");
    std::filesystem::remove_all(root);
}
void testPersistentGlobalsRejectObjectHandles() {
    const auto root = makeProject("persistent", R"CX(
on load:
    create sprite "hero" with texture {$object}
    set {persistent_object} to sprite "hero"
)CX");
    cx::Runtime runtime(root);
    expect(!runtime.loadScripts() && runtime.hasErrors(), "persistent object references should be rejected");
    bool found = false;
    for (const auto& diagnostic : runtime.diagnostics())
        if (diagnostic.message.find("cannot be stored in persistent global") != std::string::npos) found = true;
    expect(found, "persistent-reference diagnostic should explain runtime-only storage");
    std::filesystem::remove_all(root);
}
}
int main() {
    testObjectVariablesAndEnglishProperties();
    testClassificationPropertiesRejectSprites();
    testClassificationSettersRejectSprites();
    testHealthPropertiesRejectSprites();
    testHealthSettersRejectSprites();
    testStaleHandleCannotTargetReplacement();
    testPersistentGlobalsRejectObjectHandles();
    if (failures != 0) {
        std::cerr << "CX_OBJECT_REFERENCE_TESTS_FAILED failures=" << failures << '\n';
        return 1;
    }
    std::cout << "CX_OBJECT_REFERENCE_TESTS_OK\n";
    return 0;
}
