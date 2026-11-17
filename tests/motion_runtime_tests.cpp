#include "cx/runtime.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void write(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << contents;
    if (!output) throw std::runtime_error("could not write test fixture: " + path.string());
}

void createTexture(const std::filesystem::path& root) {
    write(root / "assets" / "textures" / "stone.png", "placeholder");
}

void createWallRoom(const std::filesystem::path& root) {
    write(root / "assets" / "textures" / "wall.png", "placeholder");
    write(root / "assets" / "collision.json", R"JSON({
  "schema": "cx-collision", "version": 1, "default": "none",
  "textures": {"stone": "none", "wall": "solid"}
})JSON");
    std::string tiles = "[";
    for (int y = 0; y < 6; ++y) {
        if (y != 0) tiles += ",";
        tiles += "[";
        for (int x = 0; x < 8; ++x) {
            if (x != 0) tiles += ",";
            tiles += x == 3 ? "\"wall\"" : "\"stone\"";
        }
        tiles += "]";
    }
    tiles += "]";
    write(root / "rooms" / "wall.room.json",
        "{\"schema\":\"cx-room\",\"version\":1,\"name\":\"wall\","
        "\"width\":8,\"height\":6,\"tileSize\":32,\"tiles\":" + tiles +
        ",\"entrances\":{\"spawn\":{\"x\":0,\"y\":0}}}");
}

bool near(float actual, float expected, float epsilon = 0.02F) {
    return std::abs(actual - expected) <= epsilon;
}

void testLinearArcAndOneShotCompletion() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-linear-arc-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "boulder" with texture {$stone}
set entity "boulder" position to (0, 0)
on load:
    tween entity "boulder" to (120, 60) over 1 seconds easing linear with arc height 80
on tween complete for entity "boulder":
    add 1 to {completion_count}
    set {completion_entity} to {_tween_entity}
    set {completion_x} to {_tween_x}
    set {completion_y} to {_tween_y}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "linear tween and completion event should parse and initialize");
    for (int i = 0; i < 30; ++i) runtime.tick();
    const auto& halfway = runtime.sprites().at("boulder");
    expect(near(halfway.x, 60.0F), "linear tween should reach half its horizontal distance at half time");
    expect(near(halfway.y, -50.0F), "arc height should peak above the straight path at the temporal midpoint");
    expect(!runtime.getGlobal("completion_count").truthy(), "completion should not fire before the last tick");

    for (int i = 0; i < 30; ++i) runtime.tick();
    const auto& arrived = runtime.sprites().at("boulder");
    expect(near(arrived.x, 120.0F) && near(arrived.y, 60.0F), "tween must land exactly on its requested destination");
    expect(runtime.getGlobal("completion_count").numberOr(0.0) == 1.0, "completion handler should run once");
    expect(runtime.getGlobal("completion_entity").toString() == "boulder", "completion event should expose the completed entity name");
    expect(runtime.getGlobal("completion_x").numberOr(-1.0) == 120.0, "completion event should expose final x coordinate");
    expect(runtime.getGlobal("completion_y").numberOr(-1.0) == 60.0, "completion event should expose final y coordinate");
    for (int i = 0; i < 120; ++i) runtime.tick();
    expect(runtime.getGlobal("completion_count").numberOr(0.0) == 1.0, "completed tween must not dispatch a second event");
    expect(!runtime.hasErrors(), "valid linear tween scenario should not produce runtime errors");
    std::filesystem::remove_all(root);
}

void testVariableDrivenTweenOperands() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-expression-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "stone" with texture {$stone}
on load:
    set {target_x} to 80
    set {travel_seconds} to 2
    set {arc_pixels} to 40
    tween entity "stone" to ({target_x}, {target_x}) over {travel_seconds} seconds easing linear with arc height {arc_pixels}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "variable-driven tween operands should initialize");
    for (int i = 0; i < 60; ++i) runtime.tick();
    const auto& halfway = runtime.sprites().at("stone");
    expect(near(halfway.x, 40.0F) && near(halfway.y, 0.0F),
           "coordinate, duration and arc expressions should be evaluated when the tween starts");
    expect(!runtime.hasErrors(), "valid variable-driven tween operands should not produce errors");
    std::filesystem::remove_all(root);
}

void testInvalidTweenParametersDoNotReplaceActiveMotion() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-invalid-parameters-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "boulder" with texture {$stone}
on load:
    tween entity "boulder" to (120, 0) over 1 second easing linear
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "invalid-parameter scenario should initialize its valid tween");
    for (int i = 0; i < 10; ++i) runtime.tick();
    const float beforeInvalidCommand = runtime.sprites().at("boulder").x;
    runtime.executeInline("tween entity \"boulder\" to (300, 0) over 0 seconds easing linear");
    expect(runtime.hasErrors(), "a zero-duration tween should produce a runtime diagnostic");
    for (int i = 0; i < 10; ++i) runtime.tick();
    expect(runtime.sprites().at("boulder").x > beforeInvalidCommand,
           "a rejected zero-duration tween must leave the previous valid tween active");
    const float beforeInvalidArc = runtime.sprites().at("boulder").x;
    runtime.executeInline("tween entity \"boulder\" to (300, 0) over 1 second easing linear with arc height -1");
    expect(runtime.hasErrors(), "a negative arc height should produce a runtime diagnostic");
    for (int i = 0; i < 10; ++i) runtime.tick();
    expect(runtime.sprites().at("boulder").x > beforeInvalidArc,
           "a rejected negative arc must leave the previous valid tween active");
    for (int i = 0; i < 40; ++i) runtime.tick();
    expect(near(runtime.sprites().at("boulder").x, 120.0F),
           "the original valid tween should still arrive at its original destination");
    std::filesystem::remove_all(root);
}

void testEasingCurves() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-easing-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "in" with texture {$stone}
create entity "out" with texture {$stone}
create entity "both" with texture {$stone}
on load:
    tween entity "in" to (100, 0) over 1 second easing ease_in
    tween entity "out" to (100, 0) over 1 second easing ease_out
    tween entity "both" to (100, 0) over 1 second easing ease_in_out
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "easing tween scenario should initialize");
    for (int i = 0; i < 30; ++i) runtime.tick();
    expect(near(runtime.sprites().at("in").x, 25.0F), "ease_in should cover 25 percent at half time");
    expect(near(runtime.sprites().at("out").x, 75.0F), "ease_out should cover 75 percent at half time");
    expect(near(runtime.sprites().at("both").x, 50.0F), "ease_in_out should be symmetric at its midpoint");
    expect(!runtime.hasErrors(), "valid easing curves should not produce runtime errors");
    std::filesystem::remove_all(root);
}

void testReplacementAndCancellation() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-replace-cancel-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "boulder" with texture {$stone}
on load:
    tween entity "boulder" to (100, 0) over 1 second easing linear
on tween complete for entity "boulder":
    add 1 to {completion_count}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "replacement tween scenario should initialize");
    for (int i = 0; i < 30; ++i) runtime.tick();
    expect(near(runtime.sprites().at("boulder").x, 50.0F), "original tween should be halfway before replacement");
    runtime.executeInline("tween entity \"boulder\" to (150, 0) over 1 second easing linear");
    expect(!runtime.hasErrors(), "valid replacement tween should execute inline");
    for (int i = 0; i < 60; ++i) runtime.tick();
    expect(near(runtime.sprites().at("boulder").x, 150.0F), "replacement tween should finish at its own destination");
    expect(runtime.getGlobal("completion_count").numberOr(0.0) == 1.0, "replaced tween must not complete; replacement should complete once");

    runtime.executeInline("tween entity \"boulder\" to (300, 0) over 1 second easing linear");
    for (int i = 0; i < 10; ++i) runtime.tick();
    const float stoppedAt = runtime.sprites().at("boulder").x;
    runtime.executeInline("cancel tween for entity \"boulder\"");
    for (int i = 0; i < 60; ++i) runtime.tick();
    expect(near(runtime.sprites().at("boulder").x, stoppedAt), "cancellation should freeze the entity at its current interpolated position");
    expect(runtime.getGlobal("completion_count").numberOr(0.0) == 1.0, "cancellation should not fire completion");

    runtime.executeInline("tween entity \"boulder\" to (400, 0) over 1 second easing linear");
    for (int i = 0; i < 10; ++i) runtime.tick();
    runtime.executeInline("set entity \"boulder\" position to (23, 45)");
    for (int i = 0; i < 60; ++i) runtime.tick();
    expect(near(runtime.sprites().at("boulder").x, 23.0F) && near(runtime.sprites().at("boulder").y, 45.0F),
           "explicit position assignment should cancel the active tween");
    expect(runtime.getGlobal("completion_count").numberOr(0.0) == 1.0, "position assignment should not fire tween completion");
    expect(!runtime.hasErrors(), "valid cancellation scenario should not produce errors");
    std::filesystem::remove_all(root);
}

void testManualMovementOverridesTweenAndReplacementIdentity() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-manual-override-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "boulder" with texture {$stone}
on load:
    tween entity "boulder" to (100, 0) over 1 second easing linear
on tween complete for entity "boulder":
    add 1 to {completion_count}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "manual movement override scenario should initialize");
    for (int i = 0; i < 10; ++i) runtime.tick();
    runtime.executeInline("move entity \"boulder\" by (5, 0)");
    expect(!runtime.hasErrors(), "manual movement should execute");
    const float manuallyMoved = runtime.sprites().at("boulder").x;
    for (int i = 0; i < 60; ++i) runtime.tick();
    expect(near(runtime.sprites().at("boulder").x, manuallyMoved), "manual movement should cancel the active tween");
    expect(runtime.getGlobal("completion_count").numberOr(0.0) == 0.0, "manual override should cancel completion");

    runtime.executeInline("tween entity \"boulder\" to (300, 0) over 1 second easing linear");
    for (int i = 0; i < 10; ++i) runtime.tick();
    runtime.executeInline("delete entity \"boulder\"");
    runtime.executeInline("create entity \"boulder\" with texture {$stone}");
    runtime.executeInline("set entity \"boulder\" position to (17, 23)");
    for (int i = 0; i < 90; ++i) runtime.tick();
    expect(near(runtime.sprites().at("boulder").x, 17.0F) && near(runtime.sprites().at("boulder").y, 23.0F),
           "a replacement instance with the same name must not inherit the old tween");
    expect(runtime.getGlobal("completion_count").numberOr(0.0) == 0.0, "deleted instance must never dispatch its old completion event");
    expect(!runtime.hasErrors(), "replacement identity scenario should not produce errors");
    std::filesystem::remove_all(root);
}

void testCompletionCallbackIdentityGuard() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-callback-identity-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "boulder" with texture {$stone}
on load:
    tween entity "boulder" to (10, 0) over 0.02 seconds easing linear
on tween complete for entity "boulder":
    set {first_handler_ran} to true
    delete entity "boulder"
on tween complete for entity "boulder":
    set {stale_handler_ran} to true
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "completion callback identity scenario should initialize");
    for (int i = 0; i < 5; ++i) runtime.tick();
    expect(runtime.getGlobal("first_handler_ran").truthy(), "first completion handler should run");
    expect(!runtime.getGlobal("stale_handler_ran").truthy(), "later handlers must stop when the first deletes the completing instance");
    expect(!runtime.sprites().contains("boulder"), "first handler should be able to delete its entity safely");
    expect(!runtime.hasErrors(), "completion callback deletion should be safe");
    std::filesystem::remove_all(root);
}

void testCompletionLocalsRemainTheCapturedArrivalValues() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-completion-local-snapshot-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "boulder" with texture {$stone}
on load:
    tween entity "boulder" to (10, 20) over 0.02 seconds easing linear
on tween complete for entity "boulder":
    set {first_handler_x} to {_tween_x}
    set entity "boulder" position to (200, 300)
on tween complete for entity "boulder":
    set {second_handler_x} to {_tween_x}
    set {second_handler_y} to {_tween_y}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "completion-local snapshot scenario should initialize");
    for (int i = 0; i < 5; ++i) runtime.tick();
    expect(runtime.getGlobal("first_handler_x").numberOr(-1.0) == 10.0,
           "first handler should receive the original destination x");
    expect(runtime.getGlobal("second_handler_x").numberOr(-1.0) == 10.0 &&
           runtime.getGlobal("second_handler_y").numberOr(-1.0) == 20.0,
           "later handlers should receive captured arrival coordinates even if earlier handlers move the entity");
    expect(near(runtime.sprites().at("boulder").x, 200.0F) && near(runtime.sprites().at("boulder").y, 300.0F),
           "completion handlers should be allowed to start a new movement by assigning position");
    expect(!runtime.hasErrors(), "completion-local snapshot behavior should not produce errors");
    std::filesystem::remove_all(root);
}

void testRoomTransitionDiscardsTweenWithoutCompletion() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-room-context-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    const auto writeRoom = [&](const std::string& name) {
        write(root / "rooms" / (name + ".room.json"),
              "{\"schema\":\"cx-room\",\"version\":1,\"name\":\"" + name +
              "\",\"width\":2,\"height\":2,\"tileSize\":16,"
              "\"tiles\":[[null,null],[null,null]],\"entrances\":{\"spawn\":{\"x\":0,\"y\":0}}}");
    };
    writeRoom("hall");
    writeRoom("cave");
    write(root / "scripts" / "main.cx", R"CX(
load room "hall"
load room "cave"
create entity "boulder" with texture {$stone}
on load:
    enter room "hall" at {entrance::hall::spawn}
    tween entity "boulder" to (100, 0) over 1 second easing linear
on press key "B":
    enter room "cave" at {entrance::cave::spawn}
on tween complete for entity "boulder":
    set {stale_completion_ran} to true
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "room-context tween scenario should initialize");
    for (int i = 0; i < 10; ++i) runtime.tick();
    const float positionBeforeTransition = runtime.sprites().at("boulder").x;
    runtime.pressKey("B");
    expect(runtime.rooms().activeRoomName() == "cave", "test should enter the new room");
    for (int i = 0; i < 90; ++i) runtime.tick();
    expect(near(runtime.sprites().at("boulder").x, positionBeforeTransition),
           "old-room tween should be discarded when its room context changes");
    expect(!runtime.getGlobal("stale_completion_ran").truthy(),
           "a tween invalidated by a room transition must not emit completion");
    expect(!runtime.hasErrors(), "room-context tween cancellation should not produce errors");
    std::filesystem::remove_all(root);
}

void testTweenIgnorePreservesLegacyTilePassThrough() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-ignore-legacy-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    createWallRoom(root);
    write(root / "scripts" / "main.cx", R"CX(
load room "wall"
create entity "boulder" with texture {$stone}
set entity "boulder" position to (32, 64)
on load:
    enter room "wall" at {entrance::wall::spawn}
    tween entity "boulder" to (160, 128) over 1 second easing linear
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "legacy ignore-response fixture should load without an explicit response property");
    for (int tick = 0; tick < 60; ++tick) runtime.tick();
    const auto& boulder = runtime.sprites().at("boulder");
    expect(near(boulder.x, 160.0F) && near(boulder.y, 128.0F),
           "the default ignore response must preserve v0.10.9 tween pass-through across solid tiles");
    expect(!runtime.hasErrors(), "legacy pass-through behavior should remain error-free");
    std::filesystem::remove_all(root);
}

void testTweenStopResponseRespectsRoomBoundary() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-room-boundary-stop-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    createWallRoom(root);
    write(root / "scripts" / "main.cx", R"CX(
load room "wall"
create entity "boulder" with texture {$stone}
set entity "boulder" position to (192, 64)
set entity "boulder" tween collision response to "stop"
on load:
    enter room "wall" at {entrance::wall::spawn}
    tween entity "boulder" to (240, 64) over 1 second easing linear
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "room-boundary stop-response fixture should load");
    for (int tick = 0; tick < 60; ++tick) runtime.tick();
    expect(runtime.sprites().at("boulder").x <= 224.02F,
           "a collision-aware tween must not push its entity hitbox beyond the active room boundary");
    expect(!runtime.hasErrors(), "room-boundary tween cancellation should remain error-free");
    std::filesystem::remove_all(root);
}

void testTweenStopResponseSweepsAgainstSolidTiles() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-stop-collision-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    createWallRoom(root);
    write(root / "scripts" / "main.cx", R"CX(
load room "wall"
create entity "boulder" with texture {$stone}
set entity "boulder" position to (32, 64)
set entity "boulder" tween collision response to "stop"
on load:
    enter room "wall" at {entrance::wall::spawn}
    tween entity "boulder" to (160, 128) over 1 second easing linear
on tween complete for entity "boulder":
    add 1 to {completion_count}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "solid-tile stop-response fixture should load");
    for (int tick = 0; tick < 90; ++tick) runtime.tick();
    const auto& boulder = runtime.sprites().at("boulder");
    expect(boulder.x <= 64.02F,
           "stop response must halt before the boulder's hitbox penetrates the wall tile");
    expect(!runtime.getGlobal("completion_count").truthy(),
           "a blocked stop-response tween must cancel silently instead of reporting arrival");
    const float stoppedX = boulder.x;
    const float stoppedY = boulder.y;
    for (int tick = 0; tick < 30; ++tick) runtime.tick();
    expect(near(runtime.sprites().at("boulder").x, stoppedX) &&
           near(runtime.sprites().at("boulder").y, stoppedY),
           "a stopped tween must remain canceled on later ticks");
    expect(!runtime.hasErrors(), "valid stop-response collision should not produce runtime errors");
    std::filesystem::remove_all(root);
}

void testTweenSlideResponseMovesAlongSolidTileWall() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-slide-collision-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    createWallRoom(root);
    write(root / "scripts" / "main.cx", R"CX(
load room "wall"
create entity "boulder" with texture {$stone}
set entity "boulder" position to (32, 64)
set entity "boulder" tween collision response to "slide"
on load:
    enter room "wall" at {entrance::wall::spawn}
    tween entity "boulder" to (160, 128) over 1 second easing linear
on tween complete for entity "boulder":
    set {completed_x} to {_tween_x}
    set {completed_y} to {_tween_y}
    add 1 to {completion_count}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "solid-tile slide-response fixture should load");
    for (int tick = 0; tick < 60; ++tick) runtime.tick();
    const auto& boulder = runtime.sprites().at("boulder");
    expect(near(boulder.x, 64.0F) && near(boulder.y, 128.0F),
           "slide response should keep the blocked horizontal coordinate while completing free vertical travel");
    expect(runtime.getGlobal("completion_count").numberOr(0.0) == 1.0,
           "a sliding tween should dispatch one completion after its timeline ends");
    expect(near(static_cast<float>(runtime.getGlobal("completed_x").numberOr(-1.0)), 64.0F) &&
           near(static_cast<float>(runtime.getGlobal("completed_y").numberOr(-1.0)), 128.0F),
           "sliding completion locals must report the actual final position, not the blocked destination");
    expect(!runtime.hasErrors(), "valid slide-response collision should not produce runtime errors");
    std::filesystem::remove_all(root);
}

void testTweenEntityBlockersUseMutualCollisionFilters() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-entity-blocker-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "mover" with texture {$stone}
create entity "crate" with texture {$stone}
set entity "mover" position to (32, 32)
set entity "crate" position to (96, 32)
set entity "crate" collision layer to "props"
set entity "crate" collision mask to "actors"
set entity "crate" blocks movement to true
set entity "mover" collision mask to "props"
set entity "mover" tween collision response to "stop"
on load:
    tween entity "mover" to (160, 32) over 1 second easing linear
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "entity-blocker stop-response fixture should load");
    for (int tick = 0; tick < 90; ++tick) runtime.tick();
    expect(runtime.sprites().at("mover").x <= 64.02F,
           "an explicitly movement-blocking entity with mutually accepted channels should stop a tween");
    expect(runtime.sprites().at("crate").blocksMovement,
           "the blocks-movement property should be stored on the blocker entity");
    expect(!runtime.hasErrors(), "valid movement blocker settings should not produce runtime errors");
    std::filesystem::remove_all(root);

    const auto filteredRoot = std::filesystem::temp_directory_path() / "cx-motion-entity-blocker-filter-tests";
    std::filesystem::remove_all(filteredRoot);
    createTexture(filteredRoot);
    write(filteredRoot / "scripts" / "main.cx", R"CX(
create entity "mover" with texture {$stone}
create entity "crate" with texture {$stone}
set entity "mover" position to (32, 32)
set entity "crate" position to (96, 32)
set entity "crate" collision layer to "props"
set entity "crate" collision mask to "actors"
set entity "crate" blocks movement to true
set entity "mover" collision mask to "none"
set entity "mover" tween collision response to "stop"
on load:
    tween entity "mover" to (160, 32) over 1 second easing linear
)CX");
    cx::Runtime filteredRuntime(filteredRoot);
    expect(filteredRuntime.loadScripts(), "filtered entity-blocker fixture should load");
    for (int tick = 0; tick < 60; ++tick) filteredRuntime.tick();
    expect(near(filteredRuntime.sprites().at("mover").x, 160.0F),
           "a blocker excluded by the moving entity's collision mask must not stop its tween");
    std::filesystem::remove_all(filteredRoot);
}

void testTweenCollisionLeavesTriggerAndFocusTargetsNonBlocking() {
    const auto root = std::filesystem::temp_directory_path() / "cx-motion-trigger-focus-nonblocking-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "mover" with texture {$stone}
create entity "gate" with texture {$stone}
set entity "mover" position to (32, 32)
set entity "mover" collision mask to "triggers"
set entity "mover" interaction range to 200
set entity "mover" tween collision response to "slide"
set entity "gate" position to (96, 32)
set entity "gate" collision layer to "triggers"
set entity "gate" collision mask to "actors"
set entity "gate" interactable to true
on load:
    tween entity "mover" to (160, 32) over 1 second easing linear
on entity "mover" enter trigger "gate":
    add 1 to {trigger_enters}
on entity "mover" focus enter entity "gate":
    add 1 to {focus_enters}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "trigger/focus non-blocker fixture should load");
    for (int tick = 0; tick < 60; ++tick) runtime.tick();
    expect(near(runtime.sprites().at("mover").x, 160.0F),
           "a trigger/focus target without blocks-movement must not obstruct a tween");
    expect(runtime.getGlobal("trigger_enters").numberOr(0.0) == 1.0,
           "moving through the trigger should still dispatch its enter lifecycle event");
    expect(runtime.getGlobal("focus_enters").numberOr(0.0) == 1.0,
           "a nearby interactable trigger can also receive interaction focus without blocking motion");
    expect(!runtime.hasErrors(), "trigger/focus contacts during collision-aware motion should stay valid");
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        testLinearArcAndOneShotCompletion();
        testVariableDrivenTweenOperands();
        testInvalidTweenParametersDoNotReplaceActiveMotion();
        testEasingCurves();
        testReplacementAndCancellation();
        testManualMovementOverridesTweenAndReplacementIdentity();
        testCompletionCallbackIdentityGuard();
        testCompletionLocalsRemainTheCapturedArrivalValues();
        testRoomTransitionDiscardsTweenWithoutCompletion();
        testTweenIgnorePreservesLegacyTilePassThrough();
        testTweenStopResponseRespectsRoomBoundary();
        testTweenStopResponseSweepsAgainstSolidTiles();
        testTweenSlideResponseMovesAlongSolidTileWall();
        testTweenEntityBlockersUseMutualCollisionFilters();
        testTweenCollisionLeavesTriggerAndFocusTargetsNonBlocking();
        std::cout << "All entity tween and motion tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Entity motion test failure: " << error.what() << '\n';
        return 1;
    }
}
