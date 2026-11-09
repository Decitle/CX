#include "cx/rooms.hpp"
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

class TemporaryProject {
public:
    explicit TemporaryProject(std::string name)
        : root_(std::filesystem::temp_directory_path() / ("cx-room-collision-" + std::move(name))) {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
        std::filesystem::create_directories(root_ / "assets" / "textures");
        std::filesystem::create_directories(root_ / "rooms");
        std::filesystem::create_directories(root_ / "scripts");
    }
    ~TemporaryProject() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    const std::filesystem::path& root() const { return root_; }
    void texture(const std::string& id) const {
        std::ofstream(root_ / "assets" / "textures" / (id + ".png"), std::ios::binary) << "placeholder";
    }
    void write(const std::filesystem::path& relative, const std::string& contents) const {
        std::ofstream file(root_ / relative, std::ios::binary);
        if (!file) throw std::runtime_error("Could not create test fixture " + relative.string());
        file << contents;
    }
private:
    std::filesystem::path root_;
};

void testAssetDefaultsAndPerCellOverrides() {
    TemporaryProject project("metadata");
    project.texture("stone_floor");
    project.texture("grass");
    project.texture("rock_fringe");
    project.write("assets/collision.json", R"JSON({
  "schema": "cx-collision",
  "version": 1,
  "default": "none",
  "textures": {
    "stone_floor": "solid",
    "grass": "none"
  }
})JSON");
    project.write("rooms/arena.room.json", R"JSON({
  "schema": "cx-room", "version": 1, "name": "arena", "width": 3, "height": 2, "tileSize": 16,
  "tiles": [["stone_floor", "grass", null], ["stone_floor", "grass", "stone_floor"]],
  "tileCollision": [["inherit", "solid", "solid"], ["none", "inherit", "inherit"]],
  "overlays": [{"x": 0, "y": 0, "texture": "rock_fringe", "layer": "foreground"}]
})JSON");

    cx::RoomManager manager(project.root());
    std::string error;
    expect(manager.loadRoom("arena", error), "room with collision metadata should load: " + error);
    const auto* room = manager.loadedRoom("arena");
    expect(room != nullptr, "loaded room should be available");
    expect(room->tileCollisionAt(0, 0) == cx::TileCollisionMode::Inherit,
           "explicit inherit should be retained");
    expect(room->isSolidTile(0, 0, manager.textures()),
           "stone should inherit its explicit asset-level solid default");
    expect(room->isSolidTile(1, 0, manager.textures()),
           "a grass placement should be explicitly solid when the room overrides it");
    expect(room->isSolidTile(2, 0, manager.textures()),
           "an empty cell may be explicitly solid for invisible collision boundaries");
    expect(!room->isSolidTile(0, 1, manager.textures()),
           "room-level none should override a solid asset default");
    expect(!room->isSolidTile(1, 1, manager.textures()),
           "grass should inherit the asset-level non-solid default");
    expect(room->isSolidTile(2, 1, manager.textures()),
           "unoverridden stone should remain solid");
    expect(!room->isSolidTile(-1, 0, manager.textures()),
           "the room query should safely return false outside the grid");
    expect(room->overlays.size() == 1 && room->isSolidTile(0, 0, manager.textures()),
           "decorative overlays must not replace or change base-tile collision");
    expect(manager.textures().warnings().empty(), "valid asset collision metadata should not warn");
}

void testLegacyDefaultsRemainWhenManifestIsAbsent() {
    TemporaryProject project("legacy");
    project.texture("old_stone_wall");
    project.texture("carpet");
    cx::TextureRegistry textures(project.root());
    const auto* wall = textures.find("old_stone_wall");
    const auto* carpet = textures.find("carpet");
    expect(wall && wall->solid, "projects without collision.json must retain legacy name-based solidity");
    expect(carpet && !carpet->solid, "ordinary legacy floor textures should remain non-solid");
}

void testInvalidManifestIsRejectedAsAWhole() {
    TemporaryProject project("bad-manifest");
    project.texture("stone_wall");
    project.texture("grass");
    project.write("assets/collision.json", R"JSON({
  "schema": "cx-collision", "version": 1, "default": "none",
  "textures": {"stone_wall": "none", "missing_asset": "solid"}
})JSON");
    cx::TextureRegistry textures(project.root());
    const auto* wall = textures.find("stone_wall");
    expect(wall && wall->solid,
           "an invalid manifest must not partially apply a default that conflicts with legacy inference");
    expect(!textures.warnings().empty() && textures.warnings().front().find("missing texture") != std::string::npos,
           "invalid metadata should produce an actionable warning");
}

void testDuplicateNormalizedTextureIdsRejectWholeManifest() {
    TemporaryProject project("duplicate-normalized-id");
    project.texture("stone_wall");
    project.texture("grass");
    project.write("assets/collision.json", R"JSON({
  "schema": "cx-collision", "version": 1, "default": "none",
  "textures": {"stone_wall": "none", " Stone_Wall ": "solid"}
})JSON");
    cx::TextureRegistry textures(project.root());
    const auto* wall = textures.find("stone_wall");
    expect(wall && wall->solid,
           "case/whitespace duplicate IDs must reject the whole manifest rather than silently choose one mode");
    expect(!textures.warnings().empty() &&
           textures.warnings().front().find("duplicate texture IDs") != std::string::npos,
           "duplicate normalized IDs should report a clear warning");
}

void testRuntimePathfindingUsesRoomCollisionOverrides() {
    TemporaryProject project("runtime-path");
    project.texture("actor");
    project.texture("stone_wall");
    project.write("assets/collision.json", R"JSON({
  "schema": "cx-collision", "version": 1, "default": "none",
  "textures": {"stone_wall": "solid"}
})JSON");
    const std::string roomPrefix = R"JSON({
  "schema": "cx-room", "version": 1, "name": "corridor", "width": 3, "height": 1, "tileSize": 16,
  "tiles": [[null, "stone_wall", null]],
  "entrances": {"spawn": {"x": 0, "y": 0}},
  "tileCollision": [["inherit", ")JSON";
    const std::string roomSuffix = R"JSON(", "inherit"]]
})JSON";
    const std::string script = R"CX(
load room "corridor"
on load:
    enter room "corridor" at {entrance::corridor::spawn}
    create entity "npc" with texture {$actor}
    set entity "npc" size to (8, 8)
    set entity "npc" position to (0, 0)
    set entity "npc" avoids solid tiles to true
    create path "route" through tiles (2, 0)
    set path of "npc" to path "route" with speed "1"
)CX";
    project.write("rooms/corridor.room.json", roomPrefix + "none" + roomSuffix);
    project.write("scripts/main.cx", script);

    cx::Runtime passable(project.root());
    if (!passable.loadScripts()) {
        std::string details;
        for (const auto& diagnostic : passable.diagnostics()) details += diagnostic.message + " | ";
        throw std::runtime_error("runtime should load a corridor with explicit non-solid tile override: " + details);
    }
    passable.tick();
    const auto passableIt = passable.sprites().find("npc");
    expect(passableIt != passable.sprites().end(), "NPC should be created by its load event");
    expect(passableIt->second.navigationTiles.size() == 3,
           "BFS pathfinding should route through a texture that the room explicitly marks non-solid");

    project.write("rooms/corridor.room.json", roomPrefix + "solid" + roomSuffix);
    cx::Runtime blocked(project.root());
    expect(blocked.loadScripts(), "runtime should load the same corridor with a solid override");
    blocked.tick();
    const auto blockedIt = blocked.sprites().find("npc");
    expect(blockedIt != blocked.sprites().end(), "NPC should still exist when the corridor is blocked");
    expect(blockedIt->second.navigationTiles.empty(),
           "BFS should refuse a one-cell corridor when its only connecting tile is explicitly solid");
}

void testMalformedRoomCollisionGridDoesNotLoad() {
    TemporaryProject project("bad-room");
    project.texture("stone");
    project.write("rooms/broken.room.json", R"JSON({
  "schema": "cx-room", "version": 1, "name": "broken", "width": 2, "height": 1,
  "tiles": [["stone", "stone"]],
  "tileCollision": [["solid"]]
})JSON");
    cx::RoomManager manager(project.root());
    std::string error;
    expect(!manager.loadRoom("broken", error), "wrong-sized collision rows must be rejected");
    expect(error.find("tileCollision") != std::string::npos,
           "collision grid diagnostics should identify the malformed field");
    expect(manager.loadedRoom("broken") == nullptr,
           "a malformed room must not be partially inserted into loaded room state");
}

} // namespace

int main() {
    try {
        testAssetDefaultsAndPerCellOverrides();
        testLegacyDefaultsRemainWhenManifestIsAbsent();
        testInvalidManifestIsRejectedAsAWhole();
    testDuplicateNormalizedTextureIdsRejectWholeManifest();
        testRuntimePathfindingUsesRoomCollisionOverrides();
        testMalformedRoomCollisionGridDoesNotLoad();
        std::cout << "CX_ROOM_COLLISION_TESTS_OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CX_ROOM_COLLISION_TESTS_FAILED: " << error.what() << '\n';
        return 1;
    }
}
