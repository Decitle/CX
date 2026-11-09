#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cx {

// TilePoint is an integer cell coordinate, not a pixel position. Projection converts
// between tile coordinates and screen pixels; collision and room data stay in tiles.
struct TilePoint {
    int x{0};
    int y{0};
};

enum class TileCollisionMode : std::uint8_t { Inherit, Solid, None };

struct TextureDefinition {
    std::string id;
    std::filesystem::path path;
    bool solid{false};
};

// A terrain rule lets a room keep a simple logical tile ID while the renderer
// draws optional transparent edge/corner overlays where terrain meets a different
// group. Overlay art is usually authored on the tile canvas, but the loader does not
// enforce image dimensions; the renderer maps each image to the tile footprint.
struct TerrainAutotileRule {
    std::string name;
    std::vector<std::string> members;
    std::map<std::string, std::string> edges;          // north/east/south/west
    std::map<std::string, std::string> corners;        // northwest/northeast/southwest/southeast
    std::map<std::string, std::string> innerCorners;   // same keys; concave notches
};

class TextureRegistry {
public:
    explicit TextureRegistry(std::filesystem::path projectRoot = ".");
    void scan();
    [[nodiscard]] const TextureDefinition* find(std::string_view id) const noexcept;
    [[nodiscard]] const std::vector<TextureDefinition>& all() const noexcept { return textures_; }
    [[nodiscard]] const std::vector<std::string>& warnings() const noexcept { return warnings_; }
    [[nodiscard]] const std::vector<TerrainAutotileRule>& autotileRules() const noexcept { return autotileRules_; }
    [[nodiscard]] bool contains(std::string_view id) const noexcept;
private:
    std::filesystem::path projectRoot_;
    std::vector<TextureDefinition> textures_;
    std::unordered_map<std::string, std::size_t> byId_;
    std::vector<std::string> warnings_;
    std::vector<TerrainAutotileRule> autotileRules_;
};

// Overlay art is drawn in addition to the base tile. Alpha-transparent pixels reveal
// whatever is underneath, so decorative rocks/edges should usually be overlays rather
// than replacements for the grass/floor tile beneath them.
struct OverlayTile {
    int x{0};
    int y{0};
    std::string textureId;
    std::string layer{"foreground"};
};

struct RoomDefinition {
    std::string name;
    std::filesystem::path file;
    int width{0};
    int height{0};
    int tileSize{16};
    // orthogonal = square grid; oblique/isometric = 2:1 diamond tiles.
    // Entity/collision/path coordinates remain ordinary tile-grid coordinates.
    std::string projection{"orthogonal"};
    // Empty string means an empty tile. Other cells contain a texture ID.
    std::vector<std::string> tiles;
    // Per-cell texture zoom: 1.0 is the native tile mapping; lower values repeat; higher values crop/zoom.
    std::vector<float> tileScales;
    // Per-cell collision overrides are independent from visual overlays. `Inherit`
    // uses the texture default in assets/collision.json (or the legacy default).
    std::vector<TileCollisionMode> tileCollision;
    std::vector<OverlayTile> overlays;
    std::map<std::string, TilePoint> entrances;

    [[nodiscard]] const std::string& tileAt(int x, int y) const;
    [[nodiscard]] float tileScaleAt(int x, int y) const;
    [[nodiscard]] TileCollisionMode tileCollisionAt(int x, int y) const noexcept;
    [[nodiscard]] bool isSolidTile(int x, int y, const TextureRegistry& textures) const noexcept;
};

class RoomManager {
public:
    explicit RoomManager(std::filesystem::path projectRoot = ".");
    void reset();
    void scanTextures();

    [[nodiscard]] bool loadRoom(std::string_view name, std::string& error);
    [[nodiscard]] bool unloadRoom(std::string_view name, std::string& error);
    [[nodiscard]] bool reloadRoom(std::string_view name, std::string& error);
    [[nodiscard]] bool enterRoom(std::string_view name, TilePoint entrance, std::string& error);
    [[nodiscard]] bool setActiveEntrance(TilePoint entrance, std::string& error);
    [[nodiscard]] bool setActiveTile(int x, int y, std::string textureId, std::string& error);
    [[nodiscard]] bool clearActiveTile(int x, int y, std::string& error);
    [[nodiscard]] bool setActiveOverlayTile(int x, int y, std::string textureId, std::string layer, std::string& error);
    [[nodiscard]] bool clearActiveOverlayTile(int x, int y, std::string layer, std::string& error);

    [[nodiscard]] const RoomDefinition* activeRoom() const noexcept;
    [[nodiscard]] const RoomDefinition* loadedRoom(std::string_view name) const noexcept;
    [[nodiscard]] const std::string& activeRoomName() const noexcept { return activeRoomName_; }
    [[nodiscard]] TilePoint activeEntrance() const noexcept { return activeEntrance_; }
    [[nodiscard]] std::vector<std::string> loadedRoomNames() const;
    [[nodiscard]] const TextureRegistry& textures() const noexcept { return textures_; }
    [[nodiscard]] TextureRegistry& textures() noexcept { return textures_; }
    [[nodiscard]] const std::filesystem::path& projectRoot() const noexcept { return projectRoot_; }
    [[nodiscard]] std::size_t revision() const noexcept { return revision_; }

    [[nodiscard]] static std::string normalizeId(std::string_view raw);
    [[nodiscard]] static bool parseEntrancePoint(std::string_view raw, TilePoint& point);
    [[nodiscard]] static std::string formatEntrancePoint(TilePoint point);
private:
    std::filesystem::path projectRoot_;
    TextureRegistry textures_;
    std::unordered_map<std::string, RoomDefinition> loaded_;
    std::string activeRoomName_;
    TilePoint activeEntrance_{};
    std::size_t revision_{0};

    [[nodiscard]] bool readRoomFile(std::string_view name, RoomDefinition& room, std::string& error) const;
    [[nodiscard]] std::filesystem::path roomPath(std::string_view name) const;
};

} // namespace cx
