#include "cx/diagnostics.hpp"
#include "cx/runtime.hpp"
#include "cx/script.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <stdexcept>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void createTexturePlaceholders(const std::filesystem::path& root) {
    std::filesystem::create_directories(root / "assets" / "textures");
    for (const auto& name : {"boss.png", "player.png", "orb.png", "stone.png"}) {
        std::ofstream(root / "assets" / "textures" / name, std::ios::binary).put('x');
    }
}

void testProjectilePatternSyntaxAndRuntime() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-patterns";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "textures");
    for (const auto& name : {"boss.png", "player.png", "orb.png"}) {
        std::ofstream(root / "assets" / "textures" / name, std::ios::binary).put('x');
    }
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (200, 32)
on load:
    fire projectile pattern radial from "boss" toward "player" with texture {$orb} count 4 speed 4 damage 1
    fire projectile pattern fan from "boss" toward "player" with texture {$orb} count 3 spread 60 speed 6 damage 1
)CX";
    }

    cx::ScriptParser parser;
    const auto invalid = parser.parseText(
        "fire projectile pattern radial from \"boss\" toward \"player\" with texture {$orb} count 65 speed 4 damage 1\n",
        "<pattern-limit>");
    expect(cx::hasErrors(invalid.diagnostics), "a pattern above the 64-projectile limit should be a syntax error");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "radial and fan patterns should parse and run: " +
           (runtime.diagnostics().empty() ? std::string{} : runtime.diagnostics().front().message));

    std::vector<float> radialAngles;
    std::vector<float> fanAngles;
    for (const auto& [name, sprite] : runtime.sprites()) {
        (void)name;
        if (!sprite.projectile) continue;
        const float angle = std::atan2(sprite.projectileVy, sprite.projectileVx);
        if (std::abs(sprite.projectileSpeedTilesPerSecond - 4.0F) < 0.001F) radialAngles.push_back(angle);
        if (std::abs(sprite.projectileSpeedTilesPerSecond - 6.0F) < 0.001F) fanAngles.push_back(angle);
    }
    expect(radialAngles.size() == 4, "radial pattern should create exactly four projectiles");
    expect(fanAngles.size() == 3, "fan pattern should create exactly three projectiles");
    std::sort(radialAngles.begin(), radialAngles.end());
    const float pi = std::acos(-1.0F);
    expect(std::abs(radialAngles[0] + pi) < 0.001F, "radial pattern should include a leftward projectile");
    expect(std::abs(radialAngles[1] + pi * 0.5F) < 0.001F, "radial pattern should include an upward projectile");
    expect(std::abs(radialAngles[2]) < 0.001F, "aim-aligned radial pattern should include a rightward projectile");
    expect(std::abs(radialAngles[3] - pi * 0.5F) < 0.001F, "radial pattern should include a downward projectile");
    expect(std::abs(fanAngles.front() + pi / 6.0F) < 0.001F,
           "fan pattern should start 30 degrees left of the aim direction");
    expect(std::abs(fanAngles[1]) < 0.001F, "three-shot fan should include the aim direction");
    expect(std::abs(fanAngles.back() - pi / 6.0F) < 0.001F,
           "fan pattern should end 30 degrees right of the aim direction");
    expect(!runtime.hasErrors(), "valid projectile patterns should not emit runtime errors");
    std::filesystem::remove_all(root);
}

void testGeneratedProjectileNamesAvoidUserSpritesAndKeepCountAccurate() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-name-collision";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (220, 32)
create sprite "projectile_1" with texture {$stone}
on load:
    fire projectile from "boss" toward "player" with texture {$orb} speed 4 damage 1
    fire projectile from "boss" toward "player" with texture {$orb} speed 4 damage 1
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "projectile-name collision fixture should load");
    const auto actualProjectileCount = [&runtime]() {
        return static_cast<std::size_t>(std::count_if(
            runtime.sprites().begin(), runtime.sprites().end(),
            [](const auto& item) { return item.second.projectile; }));
    };
    expect(actualProjectileCount() == 2,
           "both shots must be inserted even when the first generated name is already owned by a user sprite");
    expect(runtime.sprites().contains("projectile_1") && !runtime.sprites().at("projectile_1").projectile &&
           runtime.sprites().at("projectile_1").textureId == "stone",
           "a user sprite that happens to use a generated-projectile name must not be overwritten");
    expect(runtime.sprites().contains("projectile_2") && runtime.sprites().at("projectile_2").projectile &&
           runtime.sprites().contains("projectile_3") && runtime.sprites().at("projectile_3").projectile,
           "generated names should skip occupied IDs and remain unique");

    runtime.executeInline("delete sprite \"projectile_2\"");
    expect(actualProjectileCount() == 1,
           "deleting a projectile must release exactly one active-projectile slot");
    runtime.executeInline("fire projectile from \"boss\" toward \"player\" with texture {$orb} speed 4 damage 1");
    expect(actualProjectileCount() == 2 && runtime.sprites().contains("projectile_4"),
           "a later shot must reuse the released capacity while retaining monotonic unique IDs");
    expect(!runtime.hasErrors(), "projectile-name collision handling should not create runtime errors");
    std::filesystem::remove_all(root);
}

void testFastProjectileCannotTunnelThroughTarget() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-sweep";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "textures");
    for (const auto& name : {"boss.png", "player.png", "orb.png"}) {
        std::ofstream(root / "assets" / "textures" / name, std::ios::binary).put('x');
    }
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (106, 32)
set entity "player" health to 2
fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "fast-projectile fixture should load");
    runtime.tick();
    expect(runtime.sprites().at("player").health == 2,
           "the projectile should not hit before its swept bounds reach the target");
    runtime.tick();
    expect(runtime.sprites().at("player").health == 1,
           "a fast projectile should register the target crossed between tick endpoints");
    expect(std::none_of(runtime.sprites().begin(), runtime.sprites().end(),
                        [](const auto& pair) { return pair.second.projectile; }),
           "a projectile should be removed after its swept hit");
    std::filesystem::remove_all(root);
}



void testInvisibleTargetStillCollides() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-invisible-projectile-target";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (106, 32)
set entity "player" health to 2
set entity "player" visible to false
fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "invisible-target fixture should load");
    runtime.tick();
    runtime.tick();
    expect(runtime.sprites().at("player").health == 1,
           "an invisible entity with collision enabled should still receive projectile hits");
    std::filesystem::remove_all(root);
}

void testSameFrameHitsResolveInOrderAndSkipDefeatedTarget() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-hit-order";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (106, 32)
set entity "player" health to 2
set {hit_count} to 0
on load:
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
on projectile hit entity "player":
    add 1 to {hit_count}
    if {hit_count} <= 1:
        set {health_after_first_hit} to {entity::player::health}
    if {entity::player::health} <= 0:
        delete entity "player"
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "same-frame-hit fixture should load");
    runtime.tick();
    runtime.tick();
    expect(runtime.getGlobal("health_after_first_hit").numberOr(-1.0) == 1.0,
           "the first callback should observe health after its own hit, not after all same-frame hits");
    expect(runtime.getGlobal("hit_count").numberOr(0.0) == 2.0,
           "two hits should resolve in order; the third stale impact must not replay callbacks after defeat");
    expect(!runtime.sprites().contains("player"),
           "the lethal-hit callback should be able to delete the defeated target safely");
    expect(!runtime.hasErrors(), "ordered hit callbacks should not create stale-target diagnostics");
    std::filesystem::remove_all(root);
}

void testQueuedHitsDoNotTransferToReplacementEntityWithSameName() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-target-replacement";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (106, 32)
set entity "player" health to 10
set {hit_count} to 0
on load:
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
on projectile hit entity "player":
    add 1 to {hit_count}
    if {hit_count} <= 1:
        delete entity "player"
        create entity "player" with texture {$player}
        set entity "player" position to (106, 32)
        set entity "player" health to 10
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "replacement-target fixture should load");
    runtime.tick();
    runtime.tick();
    expect(runtime.getGlobal("hit_count").numberOr(0.0) == 1.0,
           "a queued collision against the old entity instance must not fire against its replacement");
    expect(runtime.sprites().contains("player") && runtime.sprites().at("player").health == 10,
           "recreating an entity under the same name must not inherit already-queued damage");
    expect(!runtime.hasErrors(), "discarding a stale queued impact should not raise an error");
    std::filesystem::remove_all(root);
}

void testInFlightProjectileDoesNotRetargetReplacementEntityWithSameName() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-inflight-target-replacement";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (220, 32)
set entity "player" health to 10
on load:
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "in-flight target replacement fixture should load");
    runtime.tick(); // Move a little, but do not yet reach the distant original target.
    expect(std::any_of(runtime.sprites().begin(), runtime.sprites().end(),
                       [](const auto& pair) { return pair.second.projectile; }),
           "the projectile should remain alive while its original target is still present");

    runtime.executeInline("delete entity \"player\"");
    runtime.executeInline("create entity \"player\" with texture {$player}");
    runtime.executeInline("set entity \"player\" position to (106, 32)");
    runtime.executeInline("set entity \"player\" health to 10");
    expect(runtime.sprites().at("player").health == 10,
           "the replacement target should begin with the explicitly assigned health");

    runtime.tick();
    expect(runtime.sprites().at("player").health == 10,
           "an in-flight projectile must not reacquire a replacement entity by its reused name");
    expect(std::none_of(runtime.sprites().begin(), runtime.sprites().end(),
                        [](const auto& pair) { return pair.second.projectile; }),
           "a projectile whose bound target instance vanished should be discarded and release its capacity");
    expect(!runtime.hasErrors(), "target replacement should discard the projectile without runtime errors");
    std::filesystem::remove_all(root);
}

void writeEmptyRoom(const std::filesystem::path& root, const std::string& name) {
    std::filesystem::create_directories(root / "rooms");
    std::ofstream room(root / "rooms" / (name + ".room.json"));
    constexpr int width = 30;
    constexpr int height = 6;
    room << "{\n  \"schema\": \"cx-room\", \"version\": 1, \"name\": \"" << name
         << "\", \"width\": " << width << ", \"height\": " << height
         << ", \"tileSize\": 32,\n  \"tiles\": [\n";
    for (int y = 0; y < height; ++y) {
        room << "    [";
        for (int x = 0; x < width; ++x) {
            if (x != 0) room << ", ";
            room << "\"\"";
        }
        room << "]" << (y + 1 == height ? "\n" : ",\n");
    }
    room << "  ],\n  \"entrances\": {\"spawn\": {\"x\": 1, \"y\": 1}}\n}\n";
}

void testQueuedImpactIsSkippedWhenCallbackMovesTargetAway() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-target-moved-away";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (106, 32)
set entity "player" health to 10
set {hit_count} to 0
on load:
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
on projectile hit entity "player":
    add 1 to {hit_count}
    if {hit_count} is 1:
        set entity "player" position to (400, 32)
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "moving-target-away fixture should load");
    runtime.tick();
    runtime.tick();
    expect(runtime.getGlobal("hit_count").numberOr(0.0) == 1.0,
           "a queued impact must be skipped when callback mutation moves its target away from that impact's sampled contact bounds");
    expect(runtime.sprites().at("player").health == 9,
           "only the first contact should damage a target moved away by its callback");
    expect(runtime.sprites().at("player").x == 400.0F,
           "the callback's target movement should remain committed");
    expect(!runtime.hasErrors(), "skipping a now-stale contact should not produce a runtime diagnostic");
    std::filesystem::remove_all(root);
}

void testQueuedImpactStillResolvesWhenMovedTargetOverlapsContact() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-target-moved-within-contact";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (106, 32)
set entity "player" health to 10
set {hit_count} to 0
on load:
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
on projectile hit entity "player":
    add 1 to {hit_count}
    if {hit_count} is 1:
        set entity "player" position to (107, 32)
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "overlapping-moved-target fixture should load");
    runtime.tick();
    runtime.tick();
    expect(runtime.getGlobal("hit_count").numberOr(0.0) == 2.0,
           "a queued impact should still resolve if its captured contact bounds overlap the target's current hitbox");
    expect(runtime.sprites().at("player").health == 8,
           "both contacts should damage the target while it still overlaps their sampled contact bounds");
    expect(runtime.sprites().at("player").x == 107.0F,
           "the small callback movement should remain committed");
    expect(!runtime.hasErrors(), "valid contact revalidation should not produce a runtime diagnostic");
    std::filesystem::remove_all(root);
}

void testQueuedImpactIsSkippedWhenCallbackDisablesTargetCollision() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-target-collision-disabled";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (106, 32)
set entity "player" health to 10
set {hit_count} to 0
on load:
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
on projectile hit entity "player":
    add 1 to {hit_count}
    if {hit_count} is 1:
        set entity "player" collision to false
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "collision-disabled-target fixture should load");
    runtime.tick();
    runtime.tick();
    expect(runtime.getGlobal("hit_count").numberOr(0.0) == 1.0,
           "a queued impact must be skipped when an earlier callback disables target collision");
    expect(runtime.sprites().at("player").health == 9,
           "only the first contact should damage a target after its collision is disabled");
    expect(!runtime.sprites().at("player").collisionEnabled,
           "the callback's collision policy change should remain committed");
    expect(!runtime.hasErrors(), "skipping a collision-disabled queued target should not produce a runtime diagnostic");
    std::filesystem::remove_all(root);
}

void testQueuedImpactIsSkippedWhenCallbackMovesTargetHitboxAway() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-target-hitbox-moved-away";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (106, 32)
set entity "player" health to 10
set {hit_count} to 0
on load:
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
on projectile hit entity "player":
    add 1 to {hit_count}
    if {hit_count} is 1:
        set entity "player" hitbox to (15, 12) offset (400, 400)
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "hitbox-moved-target fixture should load");
    runtime.tick();
    runtime.tick();
    expect(runtime.getGlobal("hit_count").numberOr(0.0) == 1.0,
           "a queued impact must be skipped when callback mutation moves the target hitbox away from its sprite");
    expect(runtime.sprites().at("player").health == 9,
           "only the first contact should damage a target whose hitbox has moved away");
    expect(runtime.sprites().at("player").hitboxOffsetX == 400.0F &&
           runtime.sprites().at("player").hitboxOffsetY == 400.0F,
           "the callback's hitbox update should remain committed");
    expect(!runtime.hasErrors(), "skipping an impact outside the updated hitbox should not produce a runtime diagnostic");
    std::filesystem::remove_all(root);
}

void testQueuedImpactsAreCanceledWhenRoomContextChanges(const std::string& caseName,
                                                         const std::string& roomOperation,
                                                         const std::string& expectedActiveRoom,
                                                         double expectedFirstHandlerCount = 1.0,
                                                         double expectedSecondHandlerCount = 0.0,
                                                         int expectedPlayerHealth = 9,
                                                         double expectedCompletedHandlerCount = 1.0,
                                                         bool preloadSanctum = false) {
    const auto root = std::filesystem::temp_directory_path() /
        ("cx-engine-test-projectile-room-context-" + caseName);
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    writeEmptyRoom(root, "arena");
    writeEmptyRoom(root, "sanctum");
    {
        std::ofstream script(root / "scripts" / "main.cx");
        if (preloadSanctum) script << "load room \"sanctum\"\n";
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (106, 32)
set entity "player" health to 10
set {first_hit_handler_count} to 0
set {second_hit_handler_count} to 0
set {completed_handler_count} to 0
set {arena_spawn} to an entrance point at (1, 1)
set {sanctum_spawn} to an entrance point at (2, 2)
on load:
    enter room "arena" at {arena_spawn}
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
on projectile hit entity "player":
    add 1 to {first_hit_handler_count}
    if {first_hit_handler_count} is 1:
)CX";
        script << "        " << roomOperation << "\n";
        script << R"CX(    add 1 to {completed_handler_count}
on projectile hit entity "player":
    add 1 to {second_hit_handler_count}
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), caseName + ": room-context fixture should load");
    expect(runtime.rooms().activeRoomName() == "arena",
           caseName + ": arena should be active before projectile impacts");
    runtime.tick();
    runtime.tick();

    expect(runtime.getGlobal("first_hit_handler_count").numberOr(0.0) == expectedFirstHandlerCount,
           caseName + ": the first matching handler count should follow the context contract");
    expect(runtime.getGlobal("second_hit_handler_count").numberOr(0.0) == expectedSecondHandlerCount,
           caseName + ": additional handlers must run only when the active context remains valid");
    expect(runtime.getGlobal("completed_handler_count").numberOr(0.0) == expectedCompletedHandlerCount,
           caseName + ": a handler that changes room context must still finish its remaining statements");
    expect(runtime.sprites().at("player").health == expectedPlayerHealth,
           caseName + ": queued impacts must follow the active-room context contract");
    expect(runtime.rooms().activeRoomName() == expectedActiveRoom,
           caseName + ": the intended room operation should have completed successfully");
    expect(!runtime.hasErrors(), caseName + ": successful room-context invalidation must not report runtime errors");
    std::filesystem::remove_all(root);
}

void testQueuedImpactsAreCanceledOnRoomEntry() {
    testQueuedImpactsAreCanceledWhenRoomContextChanges(
        "enter", "enter room \"sanctum\" at {sanctum_spawn}", "sanctum");
}

void testQueuedImpactsAreCanceledOnActiveRoomReload() {
    testQueuedImpactsAreCanceledWhenRoomContextChanges(
        "reload", "reload room \"arena\"", "arena");
}

void testQueuedImpactsAreCanceledOnActiveRoomUnload() {
    testQueuedImpactsAreCanceledWhenRoomContextChanges(
        "unload", "unload room \"arena\"", "");
}

void testQueuedImpactsRemainValidWhenNonActiveRoomReloads() {
    testQueuedImpactsAreCanceledWhenRoomContextChanges(
        "non-active-reload", "reload room \"sanctum\"", "arena", 2.0, 2.0, 8, 2.0, true);
}

void testActiveProjectileLimitRejectsWholeVolley() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-cap";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (200, 32)
on press key "SPACE":
    loop 65 times:
        fire projectile pattern radial from "boss" toward "player" with texture {$orb} count 64 speed 4 damage 1
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "projectile-cap fixture should load before the keypress");
    runtime.pressKey("SPACE");

    const auto projectileCount = static_cast<std::size_t>(std::count_if(
        runtime.sprites().begin(), runtime.sprites().end(),
        [](const auto& pair) { return pair.second.projectile; }));
    expect(projectileCount == 4096,
           "an attack that reaches the global cap should not create any part of an additional volley");
    expect(runtime.hasErrors(), "the runtime should clearly report when a script exceeds the global projectile cap");
    bool sawCapDiagnostic = false;
    for (const auto& diagnostic : runtime.diagnostics()) {
        if (diagnostic.message.find("active-projectile safety limit") != std::string::npos) {
            sawCapDiagnostic = true;
            break;
        }
    }
    expect(sawCapDiagnostic, "the projectile cap should include an actionable diagnostic");
    std::filesystem::remove_all(root);
}

void testProjectileCapWarningDoesNotRepeatEveryTick() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-cap-warning";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (200, 32)
on load:
    loop 63 times:
        fire projectile pattern radial from "boss" toward "player" with texture {$orb} count 64 speed 1 damage 1
    loop 18 times:
        fire projectile from "boss" toward "player" with texture {$orb} speed 1 damage 1
on tick:
    fire projectile pattern radial from "boss" toward "player" with texture {$orb} count 64 speed 1 damage 1
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "cap-warning fixture should load at a safe initial projectile count");
    const auto countCapDiagnostics = [&runtime]() {
        return static_cast<std::size_t>(std::count_if(
            runtime.diagnostics().begin(), runtime.diagnostics().end(), [](const auto& diagnostic) {
                return diagnostic.message.find("projectile pattern would exceed the active-projectile safety limit") != std::string::npos;
            }));
    };
    runtime.tick();
    expect(countCapDiagnostics() == 1,
           "a pattern that cannot fit below the cap should emit one warning on the first tick");
    runtime.tick();
    expect(countCapDiagnostics() == 1,
           "the same rejected pattern should not flood diagnostics on every tick while capacity remains insufficient");
    std::filesystem::remove_all(root);
}

void testSameTickImpactsResolveBySampledImpactTime() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-chronological-hit-order";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "source_far" with texture {$boss}
set entity "source_far" position to (140, 32)
create entity "source_middle" with texture {$boss}
set entity "source_middle" position to (155, 32)
create entity "source_near" with texture {$boss}
set entity "source_near" position to (170, 32)
create entity "player" with texture {$player}
set entity "player" position to (200, 32)
set entity "player" health to 10
set {hit_count} to 0
on load:
    fire projectile from "source_far" toward "player" with texture {$orb} speed 100 damage 1
    fire projectile from "source_middle" toward "player" with texture {$orb} speed 100 damage 1
    fire projectile from "source_near" toward "player" with texture {$orb} speed 100 damage 1
on projectile hit entity "player":
    add 1 to {hit_count}
    if {hit_count} is 1:
        set {first_hit_entity} to {_hit_entity}
        set {first_hit_projectile} to {_hit_projectile}
        set {first_hit_damage} to {_hit_damage}
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "chronological-hit fixture should load");
    runtime.tick();
    expect(runtime.getGlobal("hit_count").numberOr(0.0) == 3.0,
           "all three projectiles should cross the target during the same tick");
    expect(runtime.getGlobal("first_hit_entity").toString() == "player",
           "projectile-hit callbacks should expose the impacted entity name");
    expect(runtime.getGlobal("first_hit_projectile").toString() == "projectile_3",
           "the closest projectile should resolve first even though it was spawned last");
    expect(runtime.getGlobal("first_hit_damage").numberOr(-1.0) == 1.0,
           "projectile-hit callbacks should expose the damage applied by that impact");
    expect(runtime.sprites().at("player").health == 7,
           "each accepted same-tick impact should apply exactly one damage");
    std::filesystem::remove_all(root);
}

void testProjectileGetsItsFinalLifetimeMovementStep() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-final-lifetime-step";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create entity "boss" with texture {$boss}
set entity "boss" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (16032, 32)
set entity "player" health to 2
fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "final-lifetime-step fixture should load");
    for (int tick = 0; tick < 299; ++tick) runtime.tick();
    expect(runtime.sprites().at("player").health == 2,
           "the target should not be hit before the projectile's final lifetime tick");
    expect(std::any_of(runtime.sprites().begin(), runtime.sprites().end(),
                       [](const auto& pair) { return pair.second.projectile; }),
           "the projectile should still exist before the lifetime limit");

    runtime.tick();
    expect(runtime.sprites().at("player").health == 1,
           "the final partial/full movement step should still detect a hit before expiry");
    expect(std::none_of(runtime.sprites().begin(), runtime.sprites().end(),
                        [](const auto& pair) { return pair.second.projectile; }),
           "a projectile should be removed after its lifetime-ending impact");
    std::filesystem::remove_all(root);
}

void testFastProjectileCannotTunnelThroughSolidTile() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-projectile-wall-sweep";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    std::filesystem::create_directories(root / "rooms");
    {
        std::ofstream room(root / "rooms" / "test.room.json");
        room << R"JSON({
  "schema":"cx-room","version":1,"name":"test","width":10,"height":4,"tileSize":32,
  "tiles":[
    [null,null,null,null,null,null,null,null,null,null],
    [null,null,"stone",null,null,null,null,null,null,null],
    [null,null,null,null,null,null,null,null,null,null],
    [null,null,null,null,null,null,null,null,null,null]
  ],
  "entrances":{"spawn":{"x":1,"y":1}}
})JSON";
    }
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
load room "test"
on load:
    enter room "test" at {entrance::test::spawn}
    create entity "boss" with texture {$boss}
    set entity "boss" position to (32, 32)
    create entity "player" with texture {$player}
    set entity "player" position to (160, 32)
    set entity "player" health to 2
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "fast-projectile wall fixture should load");
    runtime.tick();
    expect(runtime.sprites().at("player").health == 2,
           "a fast projectile should be removed by the wall before it reaches the target");
    expect(std::none_of(runtime.sprites().begin(), runtime.sprites().end(),
                        [](const auto& pair) { return pair.second.projectile; }),
           "wall collision should remove the projectile even when the step endpoint lies beyond the wall");
    std::filesystem::remove_all(root);
}


void testTargetHitBeforeWallUsesEarlierSampledContact() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-target-before-wall";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    std::filesystem::create_directories(root / "rooms");
    {
        std::ofstream room(root / "rooms" / "test.room.json");
        room << R"JSON({
  "schema":"cx-room","version":1,"name":"test","width":8,"height":4,"tileSize":32,
  "tiles":[
    [null,null,null,null,null,null,null,null],
    [null,null,null,null,null,"stone",null,null],
    [null,null,null,null,null,null,null,null],
    [null,null,null,null,null,null,null,null]
  ],
  "entrances":{"spawn":{"x":1,"y":1}}
})JSON";
    }
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
load room "test"
on load:
    enter room "test" at {entrance::test::spawn}
    create entity "boss" with texture {$boss}
    set entity "boss" position to (32, 32)
    create entity "player" with texture {$player}
    set entity "player" position to (96, 32)
    set entity "player" health to 2
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "target-before-wall fixture should load");
    runtime.tick();
    expect(runtime.sprites().at("player").health == 1,
           "a target reached at an earlier sampled position should be hit before a wall farther down the path");
    expect(std::none_of(runtime.sprites().begin(), runtime.sprites().end(),
                        [](const auto& pair) { return pair.second.projectile; }),
           "the projectile should be consumed by the earlier target hit");
    expect(!runtime.hasErrors(), "target-before-wall collision should not produce runtime diagnostics");
    std::filesystem::remove_all(root);
}

void testSolidWallWinsWhenTargetOverlapsSameWallSample() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-wall-target-tie";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    createTexturePlaceholders(root);
    std::filesystem::create_directories(root / "rooms");
    {
        std::ofstream room(root / "rooms" / "test.room.json");
        room << R"JSON({
  "schema":"cx-room","version":1,"name":"test","width":8,"height":4,"tileSize":32,
  "tiles":[
    [null,null,null,null,null,null,null,null],
    [null,null,"stone",null,null,null,null,null],
    [null,null,null,null,null,null,null,null],
    [null,null,null,null,null,null,null,null]
  ],
  "entrances":{"spawn":{"x":1,"y":1}}
})JSON";
    }
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
load room "test"
on load:
    enter room "test" at {entrance::test::spawn}
    create entity "boss" with texture {$boss}
    set entity "boss" position to (32, 32)
    create entity "player" with texture {$player}
    set entity "player" position to (64, 32)
    set entity "player" health to 2
    fire projectile from "boss" toward "player" with texture {$orb} speed 100 damage 1
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "wall-target tie fixture should load");
    runtime.tick();
    expect(runtime.sprites().at("player").health == 2,
           "a solid wall should occlude a target when both first overlap at the same sampled position");
    expect(std::none_of(runtime.sprites().begin(), runtime.sprites().end(),
                        [](const auto& pair) { return pair.second.projectile; }),
           "the wall should consume the projectile at the tie sample");
    expect(!runtime.hasErrors(), "wall-target precedence should not produce runtime diagnostics");
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        testProjectilePatternSyntaxAndRuntime();
        testGeneratedProjectileNamesAvoidUserSpritesAndKeepCountAccurate();
        testFastProjectileCannotTunnelThroughTarget();
        testInvisibleTargetStillCollides();
        testSameFrameHitsResolveInOrderAndSkipDefeatedTarget();
        testQueuedHitsDoNotTransferToReplacementEntityWithSameName();
        testQueuedImpactIsSkippedWhenCallbackMovesTargetAway();
        testQueuedImpactStillResolvesWhenMovedTargetOverlapsContact();
        testQueuedImpactIsSkippedWhenCallbackDisablesTargetCollision();
        testQueuedImpactIsSkippedWhenCallbackMovesTargetHitboxAway();
        testQueuedImpactsAreCanceledOnRoomEntry();
        testQueuedImpactsAreCanceledOnActiveRoomReload();
        testQueuedImpactsAreCanceledOnActiveRoomUnload();
        testQueuedImpactsRemainValidWhenNonActiveRoomReloads();
        testInFlightProjectileDoesNotRetargetReplacementEntityWithSameName();
        testActiveProjectileLimitRejectsWholeVolley();
        testProjectileCapWarningDoesNotRepeatEveryTick();
        testSameTickImpactsResolveBySampledImpactTime();
        testProjectileGetsItsFinalLifetimeMovementStep();
        testFastProjectileCannotTunnelThroughSolidTile();
        testTargetHitBeforeWallUsesEarlierSampledContact();
        testSolidWallWinsWhenTargetOverlapsSameWallSample();
    } catch (const std::exception& exception) {
        std::cerr << "PROJECTILE TEST FAILED: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All projectile runtime tests passed.\n";
    return 0;
}
