#include "cx/collision.hpp"
#include "cx/runtime.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << text;
    if (!output) throw std::runtime_error("could not write test fixture: " + path.string());
}

void createTextures(const std::filesystem::path& root) {
    for (const auto& name : {"boss.png", "player.png", "orb.png", "crate.png"}) {
        write(root / "assets" / "textures" / name, "placeholder");
    }
}

bool globalIsTrue(const cx::Runtime& runtime, const std::string& name) {
    return runtime.getGlobal(name).truthy();
}

void testClassificationAndTagQueries() {
    const auto root = std::filesystem::temp_directory_path() / "cx-collision-classification-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "warden" with texture {$boss}
set entity "warden" kind to "boss"
set entity "warden" team to "hostile"
set entity "warden" tags to "armored, flying, armored, phase_two"
on load:
    if entity "warden" kind is "boss" and entity "warden" team is "hostile":
        set {kind_and_team_match} to true
    if entity "warden" has tag "armored" and entity "warden" has tag "flying":
        set {tags_match} to true
    if entity "warden" has tag "friendly":
        set {missing_tag_match} to true
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "entity classifications should parse and initialize");
    expect(runtime.sprites().at("warden").kindId != 0U, "kind metadata should be interned on the entity");
    expect(runtime.sprites().at("warden").teamId != 0U, "team metadata should be interned on the entity");
    expect(runtime.sprites().at("warden").tagIds.size() == 3, "comma-separated tags should be normalized and de-duplicated");
    expect(globalIsTrue(runtime, "kind_and_team_match"), "kind/team conditions should match assigned metadata");
    expect(globalIsTrue(runtime, "tags_match"), "tag conditions should match assigned tags");
    expect(!globalIsTrue(runtime, "missing_tag_match"), "missing tag conditions should evaluate false");
    std::filesystem::remove_all(root);
}

void testTagMutationCommands() {
    const auto root = std::filesystem::temp_directory_path() / "cx-tag-mutation-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "warden" with texture {$boss}
create entity "cleanme" with texture {$boss}
set entity "warden" tags to "armored, flying"
set entity "cleanme" tags to "temporary, marked"
on load:
    add tag "elite" to entity "warden"
    add the tag "elite" to entity "warden"
    remove tag "flying" from entity "warden"
    remove tag "not_present" from entity "warden"
    if entity "warden" has tag "elite" and entity "warden" has tag "armored":
        set {tag_mutations_match} to true
    if entity "warden" has tag "flying":
        set {removed_tag_still_there} to true
    clear the tags of entity "cleanme"
    if entity "cleanme" has tag "temporary":
        set {tag_clear_failed} to true
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "tag add/remove/clear commands should parse and execute");
    expect(globalIsTrue(runtime, "tag_mutations_match"), "tag additions and removals should update tag queries");
    expect(runtime.sprites().at("warden").tagIds.size() == 2,
           "adding the same tag twice should be idempotent and removing a tag should remove only that tag");
    expect(!globalIsTrue(runtime, "removed_tag_still_there"), "removed tags should no longer match tag queries");
    expect(runtime.sprites().at("cleanme").tagIds.empty(), "clear tags should remove the complete tag set");
    expect(!globalIsTrue(runtime, "tag_clear_failed"), "cleared tags should no longer match tag queries");
    std::filesystem::remove_all(root);
}
void testCollisionLayerAndMaskFilteringForOverlapQueries() {
    const auto root = std::filesystem::temp_directory_path() / "cx-collision-overlap-filter-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player" with texture {$player}
create entity "crate" with texture {$crate}
set entity "crate" collision layer to "props"
set entity "player" position to (32, 32)
set entity "crate" position to (32, 32)
on load:
    if entity "player" overlaps entity "crate":
        set {default_overlap} to true
    set entity "player" collision mask to "actors"
    if entity "player" overlaps entity "crate":
        set {filtered_overlap} to true
    set entity "player" collision layer to "actors,pickups"
    set entity "player" collision mask to "props"
    if entity "player" overlaps entity "crate":
        set {restored_overlap} to true
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "collision filter fixture should load");
    expect(globalIsTrue(runtime, "default_overlap"), "default all-channel masks should preserve legacy overlap behavior");
    expect(!globalIsTrue(runtime, "filtered_overlap"), "a mask that excludes the other layer should block overlap queries");
    expect(globalIsTrue(runtime, "restored_overlap"), "multi-layer assignments should permit a matching channel pair");
    expect(runtime.sprites().at("player").collisionLayer == (cx::CollisionActors | cx::CollisionPickups),
           "multi-layer assignment should store the combined bitmask");
    std::filesystem::remove_all(root);
}

void testOutgoingProjectileMaskFiltersHits() {
    const auto root = std::filesystem::temp_directory_path() / "cx-collision-projectile-filter-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "warden" with texture {$boss}
set entity "warden" position to (32, 32)
set entity "warden" team to "hostile"
set entity "warden" collision projectile mask to "props"
create entity "player" with texture {$player}
set entity "player" position to (106, 32)
set entity "player" health to 2
on load:
    fire projectile from "warden" toward "player" with texture {$orb} speed 100 damage 1
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "filtered-projectile fixture should load");
    expect(runtime.sprites().at("warden").projectileCollisionMask == cx::CollisionProps,
           "the owner's projectile mask should be retained as weapon policy");
    const auto projectile = std::find_if(runtime.sprites().begin(), runtime.sprites().end(),
        [](const auto& item) { return item.second.projectile; });
    expect(projectile != runtime.sprites().end(), "the projectile should be spawned");
    expect(projectile->second.teamId == runtime.sprites().at("warden").teamId,
           "projectiles should inherit their owner's interned team ID");
    expect(projectile->second.kindId != 0U && projectile->second.collisionLayer == cx::CollisionProjectiles,
           "projectiles should have their own kind and collision layer");

    runtime.tick();
    runtime.tick();
    expect(runtime.sprites().at("player").health == 2,
           "a projectile mask that excludes the target layer must not apply damage");
    expect(std::any_of(runtime.sprites().begin(), runtime.sprites().end(),
        [](const auto& item) { return item.second.projectile; }),
        "a filtered projectile should continue travelling rather than being consumed by a rejected target");
    std::filesystem::remove_all(root);
}

void testTargetMaskFiltersProjectileHits() {
    const auto root = std::filesystem::temp_directory_path() / "cx-target-mask-projectile-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "warden" with texture {$boss}
set entity "warden" position to (32, 32)
create entity "player" with texture {$player}
set entity "player" position to (106, 32)
set entity "player" health to 2
set entity "player" collision mask to "props"
on load:
    fire projectile from "warden" toward "player" with texture {$orb} speed 100 damage 1
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "target-mask fixture should load");
    runtime.tick();
    runtime.tick();
    expect(runtime.sprites().at("player").health == 2,
           "a target mask that excludes the projectile layer must block incoming hits");
    expect(!runtime.hasErrors(), "valid collision filtering should not emit runtime diagnostics");
    std::filesystem::remove_all(root);
}

void testTriggerEnterExitLifecycleAndInstanceIdentity() {
    const auto root = std::filesystem::temp_directory_path() / "cx-trigger-event-lifecycle-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player" with texture {$player}
create entity "gate" with texture {$crate}
set entity "player" position to (32, 32)
set entity "gate" position to (32, 32)
set entity "player" collision mask to "triggers"
set entity "gate" collision layer to "triggers"
set entity "gate" collision mask to "actors"
on entity "player" enter trigger "gate":
    add 1 to {gate_enters}
    set {last_actor} to {_trigger_actor}
    set {last_trigger} to {_trigger_entity}
    if {replacement_check}:
        if {replacement_exit_seen} is false:
            set {replacement_order_wrong} to true
on entity "player" exit trigger "gate":
    add 1 to {gate_exits}
    set {last_exit_actor} to {_trigger_actor}
    set {last_exit_trigger} to {_trigger_entity}
    if {replacement_check}:
        set {replacement_exit_seen} to true
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "named trigger handlers should parse and load");
    runtime.tick();
    expect(runtime.getGlobal("gate_enters").numberOr(0.0) == 1.0,
           "first overlap should fire exactly one trigger-enter event");
    expect(runtime.getGlobal("last_actor").toString() == "player" &&
           runtime.getGlobal("last_trigger").toString() == "gate",
           "trigger handlers should receive their actor and trigger names in local event variables");

    runtime.tick();
    expect(runtime.getGlobal("gate_enters").numberOr(0.0) == 1.0,
           "continuous overlap must not repeat the enter event every tick");
    expect(runtime.getGlobal("gate_exits").numberOr(0.0) == 0.0,
           "continuous overlap must not fire an exit event");

    runtime.executeInline("set entity \"player\" position to (320, 32)");
    runtime.tick();
    expect(runtime.getGlobal("gate_exits").numberOr(0.0) == 1.0,
           "leaving the trigger should fire exactly one exit event");
    expect(runtime.getGlobal("last_exit_actor").toString() == "player" &&
           runtime.getGlobal("last_exit_trigger").toString() == "gate",
           "trigger exit handlers should receive the same named contact data");

    runtime.executeInline("set entity \"player\" position to (32, 32)");
    runtime.tick();
    expect(runtime.getGlobal("gate_enters").numberOr(0.0) == 2.0,
           "re-entering after a real separation should fire a second enter event");

    // Replacing a named entity creates a new runtime identity. The old contact
    // exits before the replacement's new contact enters.
    runtime.executeInline("set {replacement_check} to true\nset {replacement_exit_seen} to false\ndelete sprite \"player\"\ncreate entity \"player\" with texture {$player}\nset entity \"player\" position to (32, 32)\nset entity \"player\" collision mask to \"triggers\"");
    runtime.tick();
    expect(runtime.getGlobal("gate_exits").numberOr(0.0) == 2.0,
           "deleting an overlapping actor should close its old contact");
    expect(runtime.getGlobal("gate_enters").numberOr(0.0) == 3.0,
           "a replacement actor with the same script name should enter as a distinct instance");
    expect(globalIsTrue(runtime, "replacement_exit_seen") && !globalIsTrue(runtime, "replacement_order_wrong"),
           "the old instance's exit callback must run before the replacement instance's enter callback");

    runtime.executeInline("set entity \"player\" collision mask to \"actors\"");
    runtime.tick();
    expect(runtime.getGlobal("gate_exits").numberOr(0.0) == 3.0,
           "changing a mutual collision mask should end an active trigger contact");
    expect(!runtime.hasErrors(), "valid trigger lifecycle operations should not emit diagnostics");
    std::filesystem::remove_all(root);
}

void testTriggerCallbacksMayDeleteContactEntitiesSafely() {
    const auto root = std::filesystem::temp_directory_path() / "cx-trigger-event-mutation-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player" with texture {$player}
create entity "gate" with texture {$crate}
set entity "player" position to (32, 32)
set entity "gate" position to (32, 32)
set entity "player" collision mask to "triggers"
set entity "gate" collision layer to "triggers"
set entity "gate" collision mask to "actors"
on entity "player" enter trigger "gate":
    add 1 to {gate_enters}
    delete sprite "player"
on entity "player" exit trigger "gate":
    add 1 to {gate_exits}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "mutation-safe trigger fixture should load");
    runtime.tick();
    expect(runtime.getGlobal("gate_enters").numberOr(0.0) == 1.0,
           "the initial overlap should dispatch before the handler deletes its actor");
    expect(!runtime.sprites().contains("player"), "the enter callback should be allowed to delete its actor");

    runtime.tick();
    expect(runtime.getGlobal("gate_exits").numberOr(0.0) == 1.0,
           "the next update should dispatch exactly one exit for an actor deleted by its enter callback");
    runtime.tick();
    expect(runtime.getGlobal("gate_exits").numberOr(0.0) == 1.0,
           "a deleted contact should not dispatch duplicate exit events on later ticks");
    expect(!runtime.hasErrors(), "deleting a contact entity from its own enter handler should be safe");
    std::filesystem::remove_all(root);
}

void testTriggerContactsResetAcrossRoomContextChanges() {
    const auto root = std::filesystem::temp_directory_path() / "cx-trigger-event-room-context-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "assets" / "textures" / "floor.png", "placeholder");
    write(root / "assets" / "textures" / "wall_top.png", "placeholder");
    const auto writeRoom = [&](const std::string& name) {
        write(root / "rooms" / (name + ".room.json"),
            "{\n  \"schema\": \"cx-room\", \"version\": 1, \"name\": \"" + name +
            "\", \"width\": 3, \"height\": 3, \"tileSize\": 16,\n"
            "  \"tiles\": [[\"wall_top\",\"wall_top\",\"wall_top\"],"
            "[\"wall_top\",\"floor\",\"wall_top\"],[\"wall_top\",\"wall_top\",\"wall_top\"]],\n"
            "  \"entrances\": { \"spawn\": { \"x\": 1, \"y\": 1 } }\n}\n");
    };
    writeRoom("hall");
    writeRoom("cave");
    write(root / "scripts" / "main.cx", R"CX(
load room "hall"
load room "cave"
create entity "player" with texture {$player}
create entity "gate" with texture {$crate}
set entity "player" position to (32, 32)
set entity "gate" position to (32, 32)
set entity "player" collision mask to "triggers"
set entity "gate" collision layer to "triggers"
set entity "gate" collision mask to "actors"
on load:
    enter room "hall" at {entrance::hall::spawn}
on entity "player" enter trigger "gate":
    add 1 to {gate_enters}
on entity "player" exit trigger "gate":
    add 1 to {gate_exits}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "room-context trigger fixture should load");
    expect(runtime.rooms().activeRoomName() == "hall", "the initial room should be active before the first contact sample");
    runtime.tick();
    expect(runtime.getGlobal("gate_enters").numberOr(0.0) == 1.0,
           "the first room's overlap should dispatch an enter event");

    runtime.executeInline("enter room \"cave\" at {entrance::cave::spawn}");
    expect(runtime.rooms().activeRoomName() == "cave", "the fixture should switch to the second room");
    runtime.tick();
    expect(runtime.getGlobal("gate_exits").numberOr(0.0) == 0.0,
           "switching room context must clear old contacts without dispatching exits across rooms");
    expect(runtime.getGlobal("gate_enters").numberOr(0.0) == 2.0,
           "a still-overlapping pair should be sampled as a fresh contact in the new room context");
    expect(!runtime.hasErrors(), "room-context trigger cleanup should not produce diagnostics");
    std::filesystem::remove_all(root);
}

void testEntityProximityQueries() {
    const auto root = std::filesystem::temp_directory_path() / "cx-entity-proximity-query-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player" with texture {$player}
create entity "altar" with texture {$crate}
create entity "far_altar" with texture {$crate}
create entity "offset_player" with texture {$player}
create entity "hitbox_altar" with texture {$crate}
set entity "player" size to (20, 20)
set entity "altar" size to (20, 20)
set entity "far_altar" size to (20, 20)
set entity "offset_player" size to (20, 20)
set entity "hitbox_altar" size to (20, 20)
set entity "offset_player" position to (0, 0)
set entity "offset_player" hitbox to (2, 2) offset (18, 18)
set entity "hitbox_altar" position to (39, 19)
set entity "player" position to (100, 100)
set entity "altar" position to (145, 100)
set entity "far_altar" position to (146, 100)
set {interaction_range} to 45
set entity "player" collision mask to "none"
set entity "altar" collision mask to "none"
set entity "offset_player" collision mask to "none"
set entity "hitbox_altar" collision mask to "none"
on load:
    if entity "player" is within {interaction_range} + 0 pixels of entity "altar":
        set {exact_range_matches} to true
    if entity "player" is within 44 pixels of entity "altar":
        set {inside_short_range} to true
    if not entity "player" is within 45 pixels of entity "far_altar":
        set {outside_range_rejected} to true
    if entity "player" is within 0 pixels of entity "player":
        set {same_entity_is_zero_distance} to true
    if entity "offset_player" is within 32 pixels of entity "hitbox_altar":
        set {hitbox_center_used} to true
    if entity "offset_player" is within 31 pixels of entity "hitbox_altar":
        set {hitbox_short_range_matches} to true
    if entity "player" is within 50 pixels of entity "missing_entity":
        set {missing_entity_matches} to true
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "proximity-query fixture should load");
    expect(globalIsTrue(runtime, "exact_range_matches"),
           "distance checks should include the exact radius boundary and evaluate variable ranges");
    expect(!globalIsTrue(runtime, "inside_short_range"),
           "distance checks should reject entities just beyond the requested radius");
    expect(globalIsTrue(runtime, "outside_range_rejected"),
           "the negated proximity condition should evaluate true outside the radius");
    expect(globalIsTrue(runtime, "same_entity_is_zero_distance"),
           "an entity's center should be zero distance from itself");
    expect(globalIsTrue(runtime, "hitbox_center_used") && !globalIsTrue(runtime, "hitbox_short_range_matches"),
           "proximity checks should use the configured hitbox center and offset rather than the sprite center");
    expect(!globalIsTrue(runtime, "missing_entity_matches"),
           "a missing entity should not satisfy a proximity query");
    expect(!runtime.hasErrors(), "valid proximity checks must not emit diagnostics");
    std::filesystem::remove_all(root);
}

void testEntityProximityRejectsInvalidRadius() {
    const auto root = std::filesystem::temp_directory_path() / "cx-entity-proximity-invalid-range-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player" with texture {$player}
create entity "altar" with texture {$crate}
on load:
    if entity "player" is within -1 pixels of entity "altar":
        set {invalid_range_matched} to true
)CX");

    cx::Runtime runtime(root);
    expect(!runtime.loadScripts(), "a negative proximity radius should fail startup validation");
    expect(runtime.hasErrors(), "an invalid proximity radius should provide a diagnostic");
    expect(std::any_of(runtime.diagnostics().begin(), runtime.diagnostics().end(), [](const auto& diagnostic) {
        return diagnostic.message.find("finite, non-negative") != std::string::npos;
    }), "the proximity diagnostic should explain the accepted radius range");
    std::filesystem::remove_all(root);
}

void testEntityProximityRejectsNonNumericRadius() {
    const auto root = std::filesystem::temp_directory_path() / "cx-entity-proximity-nonnumeric-range-tests";
    std::filesystem::remove_all(root);
    createTextures(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "player" with texture {$player}
create entity "altar" with texture {$crate}
set {interaction_range} to "near"
on load:
    if entity "player" is within {interaction_range} pixels of entity "altar":
        set {invalid_range_matched} to true
)CX");

    cx::Runtime runtime(root);
    expect(!runtime.loadScripts(), "a non-numeric proximity radius should fail startup validation");
    expect(std::any_of(runtime.diagnostics().begin(), runtime.diagnostics().end(), [](const auto& diagnostic) {
        return diagnostic.message.find("finite, non-negative") != std::string::npos;
    }), "a non-numeric proximity radius should produce a clear range diagnostic");
    std::filesystem::remove_all(root);
}

void testCollisionChannelParserRejectsUnknownNames() {
    std::string error;
    const auto valid = cx::parseCollisionChannels("actors, projectiles, pickups", error);
    expect(valid.has_value() && *valid == (cx::CollisionActors | cx::CollisionProjectiles | cx::CollisionPickups),
           "known channel names should produce a deterministic bitmask");
    const auto all = cx::parseCollisionChannels("all", error);
    expect(all.has_value() && *all == cx::AllCollisionChannels, "all should resolve to the supported channel mask");
    const auto none = cx::parseCollisionChannels("none", error);
    expect(none.has_value() && *none == 0U, "none should resolve to an empty mask");
    const auto invalid = cx::parseCollisionChannels("actors, imaginary", error);
    expect(!invalid.has_value() && error.find("imaginary") != std::string::npos,
           "unknown channel names should produce a precise diagnostic");
    expect(cx::collisionFiltersAllow(cx::CollisionProjectiles, cx::AllCollisionChannels,
                                     cx::CollisionActors, cx::AllCollisionChannels),
           "default masks should be mutually compatible");
    expect(!cx::collisionFiltersAllow(cx::CollisionProjectiles, cx::CollisionProps,
                                      cx::CollisionActors, cx::AllCollisionChannels),
           "both sides must opt into the other's channel");
}

void testSharedSweptAabbGeometry() {
    const cx::CollisionAabb start{0.0F, 0.0F, 10.0F, 10.0F};
    const cx::CollisionAabb flushWall{10.0F, 0.0F, 20.0F, 10.0F};
    expect(!cx::collisionAabbOverlaps(start, flushWall),
           "touching AABB edges should not count as physical penetration");
    expect(cx::sweptCollisionAabbOverlaps(start, 30.0F, 0.0F,
                                         {20.0F, 0.0F, 30.0F, 10.0F}),
           "swept AABB should detect a thin obstacle crossed between endpoints");
    expect(!cx::sweptCollisionAabbOverlaps({-10.0F, 0.0F, 0.0F, 10.0F}, 10.0F, 0.0F, flushWall),
           "a movement that ends exactly flush with a wall should remain clear");
    expect(!cx::sweptCollisionAabbOverlaps(start, -20.0F, 0.0F, flushWall),
           "movement away from a touching wall should remain clear");
    expect(!cx::sweptCollisionAabbOverlaps(start, 30.0F, 30.0F,
                                          {0.0F, 20.0F, 10.0F, 30.0F}),
           "diagonal movement should not report an obstacle that is only inside the broad-phase bounding box");
    expect(!cx::sweptCollisionAabbOverlaps({5.0F, 5.0F, 15.0F, 15.0F}, -1.0F, 0.0F,
                                          {10.0F, 10.0F, 20.0F, 20.0F}),
           "an initially overlapping object must be able to make incremental progress along its shortest separation direction");
    expect(!cx::sweptCollisionAabbOverlaps({4.0F, 5.0F, 14.0F, 15.0F}, -1.0F, 0.0F,
                                          {10.0F, 10.0F, 20.0F, 20.0F}),
           "an initially overlapping object must be able to continue escaping over multiple ticks");
    expect(!cx::sweptCollisionAabbOverlaps({5.0F, 5.0F, 15.0F, 15.0F}, -10.0F, 0.0F,
                                          {10.0F, 10.0F, 20.0F, 20.0F}),
           "an initially overlapping object must be allowed to finish clearing the blocker");
    expect(cx::sweptCollisionAabbOverlaps({5.0F, 5.0F, 15.0F, 15.0F}, 20.0F, 0.0F,
                                         {10.0F, 10.0F, 20.0F, 20.0F}),
           "an initially overlapping object must not cross the entire blocker to escape on the far side");
    expect(cx::sweptCollisionAabbOverlaps({5.0F, 5.0F, 15.0F, 15.0F}, 2.0F, 0.0F,
                                         {10.0F, 10.0F, 20.0F, 20.0F}),
           "an initially overlapping object must not move deeper into the blocker");
    expect(cx::parseTweenCollisionResponse(" STOP ") == cx::TweenCollisionResponse::Stop &&
           cx::parseTweenCollisionResponse("slide") == cx::TweenCollisionResponse::Slide &&
           cx::parseTweenCollisionResponse("ignore") == cx::TweenCollisionResponse::Ignore &&
           !cx::parseTweenCollisionResponse("bounce"),
           "tween collision response parsing should normalize supported values and reject unknown modes");
}

} // namespace

int main() {
    try {
        testClassificationAndTagQueries();
        testTagMutationCommands();
        testCollisionLayerAndMaskFilteringForOverlapQueries();
        testOutgoingProjectileMaskFiltersHits();
        testTargetMaskFiltersProjectileHits();
        testTriggerEnterExitLifecycleAndInstanceIdentity();
        testTriggerCallbacksMayDeleteContactEntitiesSafely();
        testTriggerContactsResetAcrossRoomContextChanges();
        testEntityProximityQueries();
        testEntityProximityRejectsInvalidRadius();
        testEntityProximityRejectsNonNumericRadius();
        testCollisionChannelParserRejectsUnknownNames();
        testSharedSweptAabbGeometry();
        std::cout << "All collision classification/filter tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Collision test failure: " << error.what() << '\n';
        return 1;
    }
}
