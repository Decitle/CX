#include "cx/runtime.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void write(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << contents;
    if (!output) throw std::runtime_error("could not write lifecycle fixture: " + path.string());
}

class TemporaryProject {
public:
    TemporaryProject() : root_(std::filesystem::temp_directory_path() / "cx-room-lifecycle-tests") {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        for (const auto* texture : {"stone.png", "orb.png", "beam.png"})
            write(root_ / "assets" / "textures" / texture, "placeholder");
        createRoom("cave");
        createRoom("arena");
        write(root_ / "scripts" / "main.cx", R"CX(
load room "cave"
load room "arena"
create entity "boss" with texture {$stone}
set entity "boss" position to (32, 32)
create entity "player" with texture {$stone}
set entity "player" position to (520, 32)
create light "world_lamp" at (1, 1)

canvas "HUD":
    create panel "hud_backdrop" at (0, 0) size (120, 40) color (0, 0, 0, 180)

on load:
    enter room "cave" at {entrance::cave::spawn}
    create light "cave_lamp" at (2, 2)
    create beam "cave_beam" from (40, 40) to (200, 40) using {$beam}
    set beam "cave_beam" endpoint_light to true
    create beam "cave_timed" from (40, 80) to (200, 80) using {$beam}
    set beam "cave_timed" lifetime to 0.2
    open canvas "HUD"
    fire projectile from "boss" toward "player" with texture {$orb} speed 4 damage 1

every 5 milliseconds:
    add 1 to {periodic_runs}

on tick:
    add 1 to {tick_runs}

on press key "B":
    enter room "arena" at {entrance::arena::spawn}

# Program-level key handlers are global, not owned by the active room. This
# second handler must still run after the first one changes the active room.
on press key "B":
    add 1 to {global_key_handlers_after_transition}
)CX");
    }

    ~TemporaryProject() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    const std::filesystem::path& root() const noexcept { return root_; }

private:
    void createRoom(const std::string& name) {
        write(root_ / "rooms" / (name + ".room.json"),
              "{\"schema\":\"cx-room\",\"version\":1,\"name\":\"" + name +
              "\",\"width\":8,\"height\":6,\"tileSize\":32,"
              "\"tiles\":[[null,null,null,null,null,null,null,null],"
              "[null,null,null,null,null,null,null,null],"
              "[null,null,null,null,null,null,null,null],"
              "[null,null,null,null,null,null,null,null],"
              "[null,null,null,null,null,null,null,null],"
              "[null,null,null,null,null,null,null,null]],"
              "\"entrances\":{\"spawn\":{\"x\":0,\"y\":0}}}");
    }

    std::filesystem::path root_;
};

std::map<std::string, cx::SpriteObject>::const_iterator findProjectile(const cx::Runtime& runtime) {
    return std::find_if(runtime.sprites().begin(), runtime.sprites().end(),
        [](const auto& entry) { return entry.second.projectile; });
}

void testRoomTransitionPreservesWorldStateAndScopesRoomEffects() {
    TemporaryProject project;
    cx::Runtime runtime(project.root());
    expect(runtime.loadScripts(), "lifecycle fixture should load and initialize");
    expect(runtime.rooms().activeRoomName() == "cave", "on load should activate cave");

    const auto bossIdentity = runtime.sprites().at("boss").runtimeInstanceId;
    const auto playerIdentity = runtime.sprites().at("player").runtimeInstanceId;
    auto projectile = findProjectile(runtime);
    expect(projectile != runtime.sprites().end(), "on load should create one projectile");
    const std::string projectileName = projectile->first;
    const float projectileStartX = projectile->second.x;

    expect(runtime.lights().at("world_lamp").roomName.empty(),
           "lights created without an active room should be global");
    expect(runtime.lights().at("cave_lamp").roomName == "cave",
           "lights created in an active room should retain that room name");
    expect(runtime.beams().at("cave_beam").roomName == "cave" &&
           runtime.beams().at("cave_beam").endpointLightEnabled,
           "a room beam and its independent endpoint light should retain room scope");
    expect(runtime.openCanvases().contains("hud"), "on load should open the HUD canvas");

    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    runtime.tick();
    const double periodicBeforeTransition = runtime.getGlobal("periodic_runs").numberOr(0.0);
    const double tickBeforeTransition = runtime.getGlobal("tick_runs").numberOr(0.0);
    expect(periodicBeforeTransition >= 1.0, "periodic handler should have fired before the room change");
    expect(tickBeforeTransition == 1.0, "on tick should fire once per runtime tick");

    runtime.pressKey("B");
    expect(runtime.rooms().activeRoomName() == "arena", "B should enter the arena");
    expect(runtime.getGlobal("global_key_handlers_after_transition").numberOr(0.0) == 1.0,
           "a later program-level key handler should still run after an earlier handler changes rooms");
    expect(runtime.rooms().loadedRoom("cave") == nullptr,
           "switching to another room should unload the previous active room");
    expect(runtime.rooms().loadedRoom("arena") != nullptr,
           "the destination room should be loaded and active");

    // Entity and projectile ownership is currently runtime-global, not room-bound.
    expect(runtime.sprites().contains("boss") && runtime.sprites().contains("player"),
           "ordinary entities should survive an active-room transition");
    expect(runtime.sprites().at("boss").runtimeInstanceId == bossIdentity &&
           runtime.sprites().at("player").runtimeInstanceId == playerIdentity,
           "room changes must not replace the identities of surviving entities");
    projectile = runtime.sprites().find(projectileName);
    expect(projectile != runtime.sprites().end() && projectile->second.projectile,
           "a live projectile should survive a room transition");
    expect(runtime.lights().contains("world_lamp") && runtime.lights().contains("cave_lamp") &&
           runtime.beams().contains("cave_beam"),
           "room changes should not delete registered lights or beams");
    expect(runtime.lights().at("cave_lamp").roomName == "cave" &&
           runtime.beams().at("cave_beam").roomName == "cave",
           "room effects should remain tagged to their creation room rather than becoming global");
    expect(runtime.openCanvases().contains("hud"),
           "open canvases should remain open after room changes");

    const float projectileAfterTransitionX = projectile->second.x;
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    runtime.tick();
    projectile = runtime.sprites().find(projectileName);
    expect(projectile != runtime.sprites().end() &&
           projectile->second.projectileAgeSeconds > 0.0F &&
           projectile->second.x > projectileAfterTransitionX &&
           projectile->second.x > projectileStartX,
           "projectiles should continue aging and moving after a room transition");
    expect(runtime.getGlobal("periodic_runs").numberOr(0.0) > periodicBeforeTransition,
           "periodic handlers should continue across room transitions");
    expect(runtime.getGlobal("tick_runs").numberOr(0.0) == tickBeforeTransition + 1.0,
           "on tick handlers should continue in the destination room");

    runtime.executeInline("reload room \"arena\"");
    expect(!runtime.hasErrors(), "reloading the active room should succeed");
    expect(runtime.rooms().activeRoomName() == "arena" && runtime.sprites().contains("boss") &&
           runtime.sprites().contains("player") && runtime.beams().contains("cave_beam"),
           "active-room reload should retain world entities and effects");
    expect(runtime.openCanvases().contains("hud"),
           "active-room reload should not close global canvas overlays");

    runtime.executeInline("unload room \"arena\"");
    expect(!runtime.hasErrors(), "unloading the active room should succeed");
    expect(runtime.rooms().activeRoomName().empty(), "unloading the active room should leave no active room");
    expect(runtime.sprites().contains("boss") && runtime.sprites().contains("player") &&
           runtime.sprites().find(projectileName) != runtime.sprites().end(),
           "unloading the active room should not delete runtime-global entities or projectiles");
    expect(runtime.lights().contains("cave_lamp") && runtime.beams().contains("cave_beam") &&
           runtime.openCanvases().contains("hud"),
           "unloading the active room should not implicitly clear effects or canvas state");

    // Beam lifetime is measured in runtime ticks, not visible-room time. A room-tagged
    // timed beam can expire while its room is not active. Periodic handlers also keep
    // their wall-clock schedule while no room is active.
    const double periodicBeforeNoRoomTicks = runtime.getGlobal("periodic_runs").numberOr(0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    runtime.tick();
    expect(runtime.getGlobal("periodic_runs").numberOr(0.0) > periodicBeforeNoRoomTicks,
           "periodic handlers should continue when no room is active");
    for (int i = 0; i < 20; ++i) runtime.tick();
    expect(!runtime.beams().contains("cave_timed"),
           "a timed room beam should keep aging and expire while hidden in another room or no room");
    expect(runtime.beams().contains("cave_beam") && runtime.lights().contains("cave_lamp"),
           "persistent room-scoped effects should remain registered until explicitly removed");
    expect(runtime.getGlobal("periodic_runs").numberOr(0.0) > periodicBeforeTransition,
           "periodic handlers should remain scheduled after active-room unload");
    expect(!runtime.hasErrors(), "the complete room lifecycle audit fixture should not produce runtime errors");
}

} // namespace

int main() {
    try {
        testRoomTransitionPreservesWorldStateAndScopesRoomEffects();
        std::cout << "All room lifecycle tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Room lifecycle test failure: " << error.what() << '\n';
        return 1;
    }
}
