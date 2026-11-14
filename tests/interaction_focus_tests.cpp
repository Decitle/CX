#include "cx/runtime.hpp"

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

void createTextures(const std::filesystem::path& root) {
    for (const auto* name : {"player.png", "chest.png", "crate.png", "altar.png"})
        write(root / "assets" / "textures" / name, "placeholder texture data");
}

void testNearestInclusiveAndDeterministicTieSelection() {
    const auto root = std::filesystem::temp_directory_path() / "cx-interaction-nearest-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player" with texture {$player}
create entity "zeta" with texture {$crate}
create entity "alpha" with texture {$altar}
set entity "player" position to (0, 0)
set entity "zeta" position to (80, 0)
set entity "alpha" position to (0, 80)
set entity "player" interaction range to 80
set entity "zeta" interactable to true
set entity "alpha" interactable to true
on entity "player" focus enter entity "alpha":
    add 1 to {alpha_enters}
    set {focus_actor} to {_focus_actor}
    set {focus_entity} to {_focus_entity}
    set {focused_entity_alias} to {_focused_entity}
on entity "player" focus enter entity "zeta":
    add 1 to {zeta_enters}
on entity "player" focus exit entity "alpha":
    add 1 to {alpha_exits}
on entity "player" focus enter entity "zeta":
    if {alpha_exits} is 1:
        set {switch_order_ok} to true
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "nearest interaction-focus fixture should parse and load");
    expect(runtime.focusedEntity("player").empty(), "focus begins empty before the first simulation tick");
    runtime.tick();
    expect(runtime.focusedEntity("player") == "alpha",
           "exactly in-range equal-distance candidates should resolve to the lexically smaller entity name");
    expect(runtime.getGlobal("alpha_enters").numberOr(0.0) == 1.0,
           "the selected nearest target should emit one focus-enter event");
    expect(runtime.getGlobal("zeta_enters").numberOr(0.0) == 0.0,
           "an unselected equal-distance candidate must not emit focus-enter");
    expect(runtime.getGlobal("focus_actor").toString() == "player" &&
           runtime.getGlobal("focus_entity").toString() == "alpha" &&
           runtime.getGlobal("focused_entity_alias").toString() == "alpha",
           "focus handlers should receive the actor, target, and documented compatibility alias");
    expect(!runtime.hasErrors(), "valid focus setup should not emit diagnostics");

    runtime.executeInline("set entity \"alpha\" interactable to false");
    runtime.tick();
    expect(runtime.focusedEntity("player") == "zeta",
           "disabling the focused target should switch to the next eligible candidate");
    expect(runtime.getGlobal("alpha_exits").numberOr(0.0) == 1.0,
           "switching focus should emit one exit for the old target");
    expect(runtime.getGlobal("zeta_enters").numberOr(0.0) == 1.0,
           "switching focus should emit one enter for the replacement target");
    expect(runtime.getGlobal("switch_order_ok").truthy(),
           "focus exit should be dispatched before the replacement focus enter");
    std::filesystem::remove_all(root);
}

void testMutualChannelsHitboxCentersAndCollisionIndependence() {
    const auto root = std::filesystem::temp_directory_path() / "cx-interaction-mask-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player" with texture {$player}
create entity "crate" with texture {$crate}
set entity "player" position to (0, 0)
set entity "player" hitbox to (10, 10) offset (80, 0)
set entity "player" interaction range to 40
set entity "crate" position to (100, 0)
set entity "crate" interactable to true
set entity "crate" collision layer to "props"
set entity "crate" collision mask to "actors"
set entity "player" collision mask to "actors"
set entity "crate" collision to false
set sprite "crate" visible to false
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "interaction channel fixture should parse and load");
    runtime.tick();
    expect(runtime.focusedEntity("player").empty(),
           "a one-sided mask mismatch must block focus even when the target is in range");

    runtime.executeInline("set entity \"player\" collision mask to \"props\"");
    runtime.tick();
    expect(runtime.focusedEntity("player") == "crate",
           "focus should use mutually accepted channels and shared hitbox-center geometry");
    expect(!runtime.sprites().at("crate").collisionEnabled && !runtime.sprites().at("crate").visible,
           "fixture should prove the target is both invisible and physically collision-disabled");

    runtime.executeInline("set entity \"crate\" collision mask to \"none\"");
    runtime.tick();
    expect(runtime.focusedEntity("player").empty(),
           "target-side masks must also allow the actor's layer");

    runtime.executeInline("set entity \"crate\" collision mask to \"actors\"");
    runtime.executeInline("set entity \"player\" interaction range to 30.9");
    runtime.tick();
    expect(runtime.focusedEntity("player").empty(),
           "a candidate just outside the inclusive radius must not receive focus");
    expect(!runtime.hasErrors(), "valid focus masks and distance changes should not emit diagnostics");
    std::filesystem::remove_all(root);
}

void testFocusIsIndependentPerActor() {
    const auto root = std::filesystem::temp_directory_path() / "cx-interaction-two-actors-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player_a" with texture {$player}
create entity "player_b" with texture {$player}
create entity "chest_a" with texture {$chest}
create entity "chest_b" with texture {$crate}
set entity "player_a" position to (0, 0)
set entity "player_b" position to (500, 0)
set entity "player_a" interaction range to 100
set entity "player_b" interaction range to 100
set entity "chest_a" position to (50, 0)
set entity "chest_b" position to (550, 0)
set entity "chest_a" interactable to true
set entity "chest_b" interactable to true
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "two-actor focus fixture should load");
    runtime.tick();
    expect(runtime.focusedEntity("player_a") == "chest_a", "first actor should focus its nearest target");
    expect(runtime.focusedEntity("player_b") == "chest_b", "second actor should keep an independent focus target");
    runtime.executeInline("set entity \"chest_a\" interactable to false");
    runtime.tick();
    expect(runtime.focusedEntity("player_a").empty(), "only the actor that lost its target should lose focus");
    expect(runtime.focusedEntity("player_b") == "chest_b", "unrelated actor focus should remain stable");
    std::filesystem::remove_all(root);
}

void testTargetReplacementDuringFocusCallbackInvalidatesLaterHandlers() {
    const auto root = std::filesystem::temp_directory_path() / "cx-interaction-target-replacement-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
set {replaced_once} to false
create entity "player" with texture {$player}
create entity "chest" with texture {$chest}
set entity "player" interaction range to 100
set entity "chest" interactable to true
on entity "player" focus enter entity "chest":
    add 1 to {first_enter_handlers}
    if {replaced_once} is false:
        set {replaced_once} to true
        delete entity "chest"
        create entity "chest" with texture {$crate}
        set entity "chest" interactable to true
on entity "player" focus enter entity "chest":
    add 1 to {second_enter_handlers}
on entity "player" focus exit entity "chest":
    add 1 to {exit_handlers}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "focus target replacement fixture should load");
    const auto originalTargetId = runtime.sprites().at("chest").runtimeInstanceId;
    runtime.tick();
    expect(runtime.sprites().at("chest").runtimeInstanceId != originalTargetId,
           "first focus callback should replace the same-named target instance");
    expect(runtime.getGlobal("first_enter_handlers").numberOr(0.0) == 1.0,
           "first focus-enter handler should run");
    expect(runtime.getGlobal("second_enter_handlers").numberOr(0.0) == 0.0,
           "later enter handlers must be suppressed after the target instance is replaced");
    expect(runtime.focusedEntity("player").empty(),
           "the public focus query must not expose a replacement target using the old instance identity");

    runtime.tick();
    expect(runtime.getGlobal("exit_handlers").numberOr(0.0) == 0.0,
           "a same-name replacement present before exit dispatch must not be passed to stale exit handlers");
    expect(runtime.getGlobal("second_enter_handlers").numberOr(0.0) == 1.0,
           "the replacement should receive a fresh enter event as its own instance");
    expect(runtime.focusedEntity("player") == "chest",
           "the replacement should acquire focus through a new identity-bound enter event");
    expect(!runtime.hasErrors(), "same-name target replacement should not produce stale-object diagnostics");
    std::filesystem::remove_all(root);
}

void testExitCallbackTargetReplacementInvalidatesLaterHandlers() {
    const auto root = std::filesystem::temp_directory_path() / "cx-interaction-exit-replacement-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
set {exit_replaced_once} to false
create entity "player" with texture {$player}
create entity "chest" with texture {$chest}
set entity "player" interaction range to 100
set entity "chest" interactable to true
on entity "player" focus exit entity "chest":
    add 1 to {first_exit_handlers}
    if {exit_replaced_once} is false:
        set {exit_replaced_once} to true
        delete entity "chest"
        create entity "chest" with texture {$crate}
        set entity "chest" interactable to false
on entity "player" focus exit entity "chest":
    add 1 to {second_exit_handlers}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "focus exit replacement fixture should load");
    runtime.tick();
    expect(runtime.focusedEntity("player") == "chest", "the original target should acquire focus");
    runtime.executeInline("set entity \"player\" interaction range to 0");
    runtime.tick();
    expect(runtime.getGlobal("first_exit_handlers").numberOr(0.0) == 1.0,
           "the first exit handler should run for the original target identity");
    expect(runtime.getGlobal("second_exit_handlers").numberOr(0.0) == 0.0,
           "later exit handlers must stop when an earlier handler replaces the target");
    expect(runtime.focusedEntity("player").empty(),
           "the replacement must not inherit the previous target's focus");
    runtime.executeInline("set entity \"chest\" interactable to true");
    runtime.executeInline("set entity \"player\" interaction range to 100");
    runtime.tick();
    expect(runtime.focusedEntity("player") == "chest",
           "the replacement may acquire focus later as an independent instance");
    expect(!runtime.hasErrors(), "exit callback target replacement should not produce diagnostics");
    std::filesystem::remove_all(root);
}

void testDeletedTargetCanStillReportExitByName() {
    const auto root = std::filesystem::temp_directory_path() / "cx-interaction-deleted-target-exit-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player" with texture {$player}
create entity "chest" with texture {$chest}
set entity "player" interaction range to 100
set entity "chest" interactable to true
on entity "player" focus exit entity "chest":
    add 1 to {first_exit}
on entity "player" focus exit entity "chest":
    add 1 to {second_exit}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "deleted-target exit fixture should load");
    runtime.tick();
    expect(runtime.focusedEntity("player") == "chest", "target should acquire focus before deletion");
    runtime.executeInline("delete entity \"chest\"");
    runtime.tick();
    expect(runtime.getGlobal("first_exit").numberOr(0.0) == 1.0 &&
           runtime.getGlobal("second_exit").numberOr(0.0) == 1.0,
           "a deleted original target may still report its exit by name while it remains absent");
    expect(runtime.focusedEntity("player").empty(), "deleted target must no longer be focused");
    expect(!runtime.hasErrors(), "deleted-target exit should not produce diagnostics");
    std::filesystem::remove_all(root);
}

void testEnterEligibilityMutationInvalidatesLaterHandlers() {
    const auto root = std::filesystem::temp_directory_path() / "cx-interaction-enter-eligibility-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player" with texture {$player}
create entity "chest" with texture {$chest}
set entity "player" interaction range to 100
set entity "chest" interactable to true
on entity "player" focus enter entity "chest":
    add 1 to {first_enter}
    set entity "chest" interactable to false
on entity "player" focus enter entity "chest":
    add 1 to {stale_enter}
on entity "player" focus exit entity "chest":
    add 1 to {exit_count}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "enter eligibility fixture should load");
    runtime.tick();
    expect(runtime.getGlobal("first_enter").numberOr(0.0) == 1.0,
           "the first focus-enter handler should run");
    expect(runtime.getGlobal("stale_enter").numberOr(0.0) == 0.0,
           "later focus-enter handlers must be suppressed when an earlier handler invalidates eligibility");
    expect(runtime.focusedEntity("player").empty(),
           "the live focus query must reject a target disabled by its own enter callback");

    runtime.tick();
    expect(runtime.getGlobal("exit_count").numberOr(0.0) == 1.0,
           "the invalidated focus should emit exactly one exit on the next reconciliation pass");
    expect(!runtime.hasErrors(), "changing target eligibility from a focus callback should not diagnose");
    std::filesystem::remove_all(root);
}

void testActorReplacementDuringFocusCallbackInvalidatesLaterHandlers() {
    const auto root = std::filesystem::temp_directory_path() / "cx-interaction-actor-replacement-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
set {actor_replaced_once} to false
create entity "player" with texture {$player}
create entity "altar" with texture {$altar}
set entity "player" interaction range to 100
set entity "altar" interactable to true
on entity "player" focus enter entity "altar":
    add 1 to {first_handler_count}
    if {actor_replaced_once} is false:
        set {actor_replaced_once} to true
        delete entity "player"
        create entity "player" with texture {$player}
        set entity "player" interaction range to 100
on entity "player" focus enter entity "altar":
    add 1 to {second_handler_count}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "focus actor replacement fixture should load");
    const auto originalActorId = runtime.sprites().at("player").runtimeInstanceId;
    runtime.tick();
    expect(runtime.sprites().at("player").runtimeInstanceId != originalActorId,
           "first focus callback should replace the actor instance");
    expect(runtime.getGlobal("second_handler_count").numberOr(0.0) == 0.0,
           "later focus handlers must not run against a replacement actor");
    expect(runtime.focusedEntity("player").empty(),
           "replacement actor must not inherit the deleted actor's previous focus record");
    runtime.tick();
    expect(runtime.focusedEntity("player") == "altar",
           "replacement actor should independently acquire focus on a later tick");
    expect(runtime.getGlobal("second_handler_count").numberOr(0.0) == 1.0,
           "later handlers should run for the replacement actor's new focus event");
    expect(!runtime.hasErrors(), "same-name actor replacement should not produce diagnostics");
    std::filesystem::remove_all(root);
}

void testFocusQueryRevalidatesEligibilityBeforeNextTick() {
    const auto root = std::filesystem::temp_directory_path() / "cx-interaction-live-query-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player" with texture {$player}
create entity "chest" with texture {$chest}
set entity "player" interaction range to 100
set entity "chest" position to (40, 0)
set entity "chest" interactable to true
on entity "player" focus exit entity "chest":
    add 1 to {exit_count}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "live focus-query fixture should load");
    runtime.tick();
    expect(runtime.focusedEntity("player") == "chest", "eligible target should initially be focused");

    runtime.executeInline("set entity \"player\" interaction range to 0");
    expect(runtime.focusedEntity("player").empty(),
           "focus query must immediately stop exposing a target after the actor's range is disabled");
    runtime.tick();
    expect(runtime.getGlobal("exit_count").numberOr(0.0) == 1.0,
           "the next focus reconciliation should dispatch the corresponding exit exactly once");

    runtime.executeInline("set entity \"player\" interaction range to 100");
    runtime.tick();
    expect(runtime.focusedEntity("player") == "chest",
           "restoring a valid range should allow the actor to acquire focus again");
    expect(!runtime.hasErrors(), "live focus-query revalidation should not produce diagnostics");
    std::filesystem::remove_all(root);
}

void testRoomContextClearsOldFocusWithoutCrossRoomExit() {
    const auto root = std::filesystem::temp_directory_path() / "cx-interaction-room-context-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    const auto writeRoom = [&](const std::string& name) {
        write(root / "rooms" / (name + ".room.json"),
            "{\n  \"schema\": \"cx-room\", \"version\": 1, \"name\": \"" + name +
            "\", \"width\": 2, \"height\": 2, \"tileSize\": 16,\n"
            "  \"tiles\": [[\"floor\",\"floor\"],[\"floor\",\"floor\"]],\n"
            "  \"entrances\": { \"spawn\": { \"x\": 0, \"y\": 0 } }\n}\n");
    };
    writeRoom("hall");
    writeRoom("cave");
    write(root / "assets" / "textures" / "floor.png", "placeholder texture data");
    write(root / "scripts" / "main.cx", R"CX(
load room "hall"
load room "cave"
create entity "player" with texture {$player}
create entity "chest" with texture {$chest}
set entity "player" interaction range to 100
set entity "chest" interactable to true
on load:
    enter room "hall" at {entrance::hall::spawn}
on entity "player" focus enter entity "chest":
    add 1 to {enter_count}
on entity "player" focus exit entity "chest":
    add 1 to {exit_count}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "room-context focus fixture should load");
    runtime.tick();
    expect(runtime.rooms().activeRoomName() == "hall", "fixture should enter the first room");
    expect(runtime.focusedEntity("player") == "chest", "actor should acquire focus in the first room");
    expect(runtime.getGlobal("enter_count").numberOr(0.0) == 1.0, "first room focus should emit one enter");

    runtime.executeInline("enter room \"cave\" at {entrance::cave::spawn}");
    expect(runtime.focusedEntity("player").empty(),
           "the live focus query must reject old-room focus as soon as the room revision changes");
    runtime.tick();
    expect(runtime.getGlobal("exit_count").numberOr(0.0) == 0.0,
           "room teardown must not dispatch an exit callback across room contexts");
    expect(runtime.getGlobal("enter_count").numberOr(0.0) == 2.0,
           "the same still-eligible pair should be newly sampled in the destination room");
    expect(runtime.focusedEntity("player") == "chest", "actor should reacquire focus in the new room context");
    expect(!runtime.hasErrors(), "room-context focus teardown should not produce diagnostics");
    std::filesystem::remove_all(root);
}

void testInvalidRangeIsRejected() {
    const auto root = std::filesystem::temp_directory_path() / "cx-interaction-invalid-range-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player" with texture {$player}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "invalid-range fixture should load before the rejected command");
    runtime.executeInline("set entity \"player\" interaction range to -1");
    expect(!runtime.diagnostics().empty(), "negative range should be reported");
    expect(runtime.sprites().at("player").interactionRangePixels == 0.0F,
           "rejected range assignment must leave focus disabled");
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        testNearestInclusiveAndDeterministicTieSelection();
        testMutualChannelsHitboxCentersAndCollisionIndependence();
        testFocusIsIndependentPerActor();
        testTargetReplacementDuringFocusCallbackInvalidatesLaterHandlers();
        testExitCallbackTargetReplacementInvalidatesLaterHandlers();
        testDeletedTargetCanStillReportExitByName();
        testEnterEligibilityMutationInvalidatesLaterHandlers();
        testActorReplacementDuringFocusCallbackInvalidatesLaterHandlers();
        testFocusQueryRevalidatesEligibilityBeforeNextTick();
        testRoomContextClearsOldFocusWithoutCrossRoomExit();
        testInvalidRangeIsRejected();
        std::cout << "CX_INTERACTION_FOCUS_TESTS_OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CX_INTERACTION_FOCUS_TESTS_FAILED: " << error.what() << '\n';
        return 1;
    }
}
