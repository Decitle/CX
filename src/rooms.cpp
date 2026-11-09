#include "cx/rooms.hpp"

#include "cx/value.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <variant>
#include <unordered_set>

namespace cx {
namespace {

// Room files are external project data. Parsing/validation happens before the manager swaps
// a candidate definition into live state, so a malformed JSON edit should not half-load a room.

struct Json {
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value{nullptr};

    [[nodiscard]] const Json* get(std::string_view key) const {
        const auto* object = std::get_if<Object>(&value);
        if (!object) return nullptr;
        const auto it = object->find(std::string(key));
        return it == object->end() ? nullptr : &it->second;
    }
    [[nodiscard]] const Array* array() const { return std::get_if<Array>(&value); }
    [[nodiscard]] const std::string* string() const { return std::get_if<std::string>(&value); }
    [[nodiscard]] const double* number() const { return std::get_if<double>(&value); }
    [[nodiscard]] bool isNull() const { return std::holds_alternative<std::nullptr_t>(value); }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view source) : source_(source) {}
    bool parse(Json& result, std::string& error) {
        skipSpace();
        if (!parseValue(result, error)) return false;
        skipSpace();
        if (position_ != source_.size()) { error = "Unexpected text after the end of the room data."; return false; }
        return true;
    }
private:
    std::string_view source_;
    std::size_t position_{0};

    void skipSpace() {
        while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_]))) ++position_;
    }
    bool consume(char expected) {
        skipSpace();
        if (position_ >= source_.size() || source_[position_] != expected) return false;
        ++position_;
        return true;
    }
    bool parseString(std::string& result, std::string& error) {
        if (!consume('"')) { error = "Expected a quoted text value."; return false; }
        result.clear();
        while (position_ < source_.size()) {
            const char c = source_[position_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) { error = "A text value contains an unescaped control character."; return false; }
            if (c != '\\') { result.push_back(c); continue; }
            if (position_ >= source_.size()) { error = "A text value ends after a backslash."; return false; }
            const char escaped = source_[position_++];
            switch (escaped) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    if (position_ + 4 > source_.size()) { error = "An escaped Unicode character is incomplete."; return false; }
                    // IDs/names are normally ASCII; preserve unsupported escapes as a clear parse error rather than corrupting text.
                    unsigned int code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = source_[position_++];
                        code <<= 4U;
                        if (h >= '0' && h <= '9') code += static_cast<unsigned int>(h - '0');
                        else if (h >= 'a' && h <= 'f') code += static_cast<unsigned int>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code += static_cast<unsigned int>(h - 'A' + 10);
                        else { error = "An escaped Unicode character contains an invalid hex digit."; return false; }
                    }
                    if (code <= 0x7fU) result.push_back(static_cast<char>(code));
                    else if (code <= 0x7ffU) {
                        result.push_back(static_cast<char>(0xc0U | (code >> 6U)));
                        result.push_back(static_cast<char>(0x80U | (code & 0x3fU)));
                    } else {
                        result.push_back(static_cast<char>(0xe0U | (code >> 12U)));
                        result.push_back(static_cast<char>(0x80U | ((code >> 6U) & 0x3fU)));
                        result.push_back(static_cast<char>(0x80U | (code & 0x3fU)));
                    }
                    break;
                }
                default: error = "A text value uses an unknown escape sequence."; return false;
            }
        }
        error = "A quoted text value was never closed.";
        return false;
    }
    bool parseValue(Json& out, std::string& error) {
        skipSpace();
        if (position_ >= source_.size()) { error = "The room file ends before a value is complete."; return false; }
        const char c = source_[position_];
        if (c == '"') { std::string s; if (!parseString(s, error)) return false; out.value = std::move(s); return true; }
        if (c == '{') {
            ++position_;
            Json::Object object;
            skipSpace();
            if (consume('}')) { out.value = std::move(object); return true; }
            while (true) {
                std::string key;
                if (!parseString(key, error)) return false;
                if (!consume(':')) { error = "Expected `:` after a room-data property name."; return false; }
                Json child;
                if (!parseValue(child, error)) return false;
                object[std::move(key)] = std::move(child);
                if (consume('}')) break;
                if (!consume(',')) { error = "Expected `,` or `}` inside a room-data object."; return false; }
            }
            out.value = std::move(object);
            return true;
        }
        if (c == '[') {
            ++position_;
            Json::Array array;
            skipSpace();
            if (consume(']')) { out.value = std::move(array); return true; }
            while (true) {
                Json child;
                if (!parseValue(child, error)) return false;
                array.push_back(std::move(child));
                if (consume(']')) break;
                if (!consume(',')) { error = "Expected `,` or `]` inside a room-data list."; return false; }
            }
            out.value = std::move(array);
            return true;
        }
        if (source_.substr(position_, 4) == "null") { position_ += 4; out.value = nullptr; return true; }
        if (source_.substr(position_, 4) == "true") { position_ += 4; out.value = true; return true; }
        if (source_.substr(position_, 5) == "false") { position_ += 5; out.value = false; return true; }
        const std::size_t start = position_;
        if (source_[position_] == '-') ++position_;
        while (position_ < source_.size() && (std::isdigit(static_cast<unsigned char>(source_[position_])) || source_[position_] == '.')) ++position_;
        if (start == position_) { error = std::string("Unexpected character in room data: `") + source_[position_] + "`."; return false; }
        try {
            std::size_t consumed = 0;
            const double number = std::stod(std::string(source_.substr(start, position_ - start)), &consumed);
            if (consumed != position_ - start || !std::isfinite(number)) { error = "A room coordinate or size is not a valid finite number."; return false; }
            out.value = number;
            return true;
        } catch (...) { error = "A room coordinate or size is not a valid number."; return false; }
    }
};

bool readInteger(const Json* json, int& output) {
    if (!json || !json->number()) return false;
    const double value = *json->number();
    if (!std::isfinite(value) || std::floor(value) != value || value < 0 || value > 1000000) return false;
    output = static_cast<int>(value);
    return true;
}

std::string textureIdFromPath(const std::filesystem::path& path) {
    std::string id = lower(path.stem().string());
    std::string out;
    out.reserve(id.size());
    for (const unsigned char c : id) {
        if (std::isalnum(c) || c == '_' || c == '-') out.push_back(static_cast<char>(c));
        else if (std::isspace(c)) out.push_back('_');
    }
    return out;
}

bool isImageExtension(std::string extension) {
    extension = lower(extension);
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga" || extension == ".gif";
}

} // namespace

TextureRegistry::TextureRegistry(std::filesystem::path projectRoot) : projectRoot_(std::move(projectRoot)) { scan(); }

// Build the lookup table from files inside this game project's assets/textures folder.
// The lowercase filename stem becomes the script-facing texture ID, so `Stone_Up.png`
// is referred to as `{$stone_up}`. Solidity comes from the registry's classification rule,
// not from sampling the visible color of a pixel every time movement is checked.
void TextureRegistry::scan() {
    textures_.clear();
    byId_.clear();
    warnings_.clear();
    autotileRules_.clear();
    const auto folder = projectRoot_ / "assets" / "textures";
    std::error_code ec;
    if (std::filesystem::exists(folder, ec) && std::filesystem::is_directory(folder, ec)) {
        std::vector<std::filesystem::path> files;
        for (std::filesystem::directory_iterator it(folder, ec), end; it != end && !ec; it.increment(ec)) {
            if (it->is_regular_file(ec) && isImageExtension(it->path().extension().string())) files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        for (const auto& file : files) {
            const std::string id = textureIdFromPath(file);
            if (id.empty()) continue;
            if (byId_.contains(id)) {
                warnings_.push_back("Two texture files resolve to the same ID `" + id + "`; using " + file.filename().string() + ".");
                const auto index = byId_[id];
                textures_[index].path = file;
            } else {
                byId_[id] = textures_.size();
                // A small conventional default: solid-looking material names begin as solid.
                const bool solid = id.find("stone") != std::string::npos || id.find("wall") != std::string::npos ||
                                   id.find("trunk") != std::string::npos || id.find("crate") != std::string::npos;
                textures_.push_back({id, file, solid});
            }
        }
    }
    std::sort(textures_.begin(), textures_.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    byId_.clear();
    for (std::size_t i = 0; i < textures_.size(); ++i) byId_[textures_[i].id] = i;
    if (ec) warnings_.push_back("Could not scan assets/textures: " + ec.message());

    // Optional asset collision metadata replaces filename guesses for this project.
    // Parse into a temporary map first: one bad entry must not partially change defaults.
    const auto collisionPath = projectRoot_ / "assets" / "collision.json";
    std::ifstream collisionInput(collisionPath, std::ios::binary);
    if (collisionInput) {
        const std::string source((std::istreambuf_iterator<char>(collisionInput)), std::istreambuf_iterator<char>());
        Json root;
        std::string parseError;
        JsonParser parser(source);
        bool valid = parser.parse(root, parseError);
        const Json* schema = valid ? root.get("schema") : nullptr;
        const Json* versionJson = valid ? root.get("version") : nullptr;
        const Json* defaultJson = valid ? root.get("default") : nullptr;
        const Json* texturesJson = valid ? root.get("textures") : nullptr;
        int version = 0;
        const auto* entries = texturesJson ? std::get_if<Json::Object>(&texturesJson->value) : nullptr;
        bool defaultSolid = false;
        if (!valid) {
            // parseError already carries the useful location-specific diagnostic.
        } else if (!schema || !schema->string() || *schema->string() != "cx-collision" ||
                   !readInteger(versionJson, version) || version != 1 || !defaultJson || !defaultJson->string() ||
                   !entries || (*defaultJson->string() != "solid" && *defaultJson->string() != "none")) {
            valid = false;
            parseError = "Expected schema `cx-collision`, version 1, default `solid` or `none`, and a `textures` object.";
        } else {
            defaultSolid = *defaultJson->string() == "solid";
        }

        std::unordered_map<std::string, bool> overrides;
        if (valid) {
            for (const auto& [rawId, value] : *entries) {
                const std::string id = lower(trim(rawId));
                const auto* mode = value.string();
                if (id.empty() || !mode || (*mode != "solid" && *mode != "none")) {
                    valid = false;
                    parseError = "Every collision texture entry needs a valid texture ID and mode `solid` or `none`.";
                    break;
                }
                if (!byId_.contains(id)) {
                    valid = false;
                    parseError = "Collision metadata refers to missing texture `" + id + "`.";
                    break;
                }
                if (!overrides.emplace(id, *mode == "solid").second) {
                    valid = false;
                    parseError = "Collision metadata contains duplicate texture IDs after normalization: `" + id + "`.";
                    break;
                }
            }
        }
        if (valid) {
            for (auto& texture : textures_) {
                const auto it = overrides.find(texture.id);
                texture.solid = it == overrides.end() ? defaultSolid : it->second;
            }
        } else {
            warnings_.push_back("assets/collision.json was ignored; legacy texture-name defaults remain active. " + parseError);
        }
    }

    // Optional visual seam rules live separately from the room grid. Missing
    // manifests or missing overlay images do not prevent a room from loading.
    const auto rulesPath = projectRoot_ / "assets" / "autotiles.json";
    std::ifstream rulesInput(rulesPath, std::ios::binary);
    if (rulesInput) {
        const std::string source((std::istreambuf_iterator<char>(rulesInput)), std::istreambuf_iterator<char>());
        Json root;
        std::string parseError;
        JsonParser parser(source);
        if (!parser.parse(root, parseError)) {
            warnings_.push_back("assets/autotiles.json could not be parsed: " + parseError);
        } else {
            const Json* schema = root.get("schema");
            const Json* terrainsJson = root.get("terrains");
            const auto* terrains = terrainsJson ? terrainsJson->array() : nullptr;
            if (!schema || !schema->string() || *schema->string() != "cx-autotiles" || !terrains) {
                warnings_.push_back("assets/autotiles.json must use schema `cx-autotiles` and contain a `terrains` array.");
            } else {
                const auto readStringMap = [&](const Json* json, std::map<std::string, std::string>& destination,
                                               const std::string& terrainName, std::string_view section) {
                    if (!json) return;
                    const auto* object = std::get_if<Json::Object>(&json->value);
                    if (!object) {
                        warnings_.push_back("Terrain `" + terrainName + "` has a non-object `" + std::string(section) + "` section.");
                        return;
                    }
                    for (const auto& [keyRaw, value] : *object) {
                        const auto* texture = value.string();
                        const std::string key = lower(trim(keyRaw));
                        if (!texture || key.empty()) {
                            warnings_.push_back("Terrain `" + terrainName + "` has an invalid texture mapping in `" + std::string(section) + "`.");
                            continue;
                        }
                        const std::string textureId = lower(trim(*texture));
                        if (!contains(textureId)) {
                            warnings_.push_back("Autotile `" + terrainName + "` refers to missing overlay texture `" + textureId + "`.");
                            continue;
                        }
                        destination[key] = textureId;
                    }
                };
                std::unordered_set<std::string> seenTerrainNames;
                for (const Json& terrainJson : *terrains) {
                    const Json* nameJson = terrainJson.get("name");
                    const Json* membersJson = terrainJson.get("members");
                    const auto* members = membersJson ? membersJson->array() : nullptr;
                    if (!nameJson || !nameJson->string() || trim(*nameJson->string()).empty() || !members || members->empty()) {
                        warnings_.push_back("Each autotile terrain needs a non-empty `name` and `members` array.");
                        continue;
                    }
                    TerrainAutotileRule rule;
                    rule.name = lower(trim(*nameJson->string()));
                    if (!seenTerrainNames.insert(rule.name).second) {
                        warnings_.push_back("Duplicate autotile terrain name `" + rule.name + "`; later definition ignored.");
                        continue;
                    }
                    for (const auto& memberJson : *members) {
                        const auto* member = memberJson.string();
                        if (!member) {
                            warnings_.push_back("Terrain `" + rule.name + "` contains a non-string member.");
                            continue;
                        }
                        const std::string id = lower(trim(*member));
                        if (!contains(id)) {
                            warnings_.push_back("Terrain `" + rule.name + "` includes missing base texture `" + id + "`.");
                            continue;
                        }
                        rule.members.push_back(id);
                    }
                    readStringMap(terrainJson.get("edges"), rule.edges, rule.name, "edges");
                    readStringMap(terrainJson.get("corners"), rule.corners, rule.name, "corners");
                    readStringMap(terrainJson.get("inner_corners"), rule.innerCorners, rule.name, "inner_corners");
                    if (!rule.members.empty() && (!rule.edges.empty() || !rule.corners.empty() || !rule.innerCorners.empty()))
                        autotileRules_.push_back(std::move(rule));
                    else if (rule.members.empty())
                        warnings_.push_back("Autotile terrain `" + rule.name + "` has no available member textures and was ignored.");
                }
            }
        }
    }
}

const TextureDefinition* TextureRegistry::find(std::string_view id) const noexcept {
    const auto it = byId_.find(lower(trim(id)));
    return it == byId_.end() ? nullptr : &textures_[it->second];
}
bool TextureRegistry::contains(std::string_view id) const noexcept { return find(id) != nullptr; }

const std::string& RoomDefinition::tileAt(int x, int y) const {
    static const std::string empty;
    if (x < 0 || y < 0 || x >= width || y >= height) return empty;
    return tiles[static_cast<std::size_t>(y * width + x)];
}

float RoomDefinition::tileScaleAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return 1.0F;
    const auto index = static_cast<std::size_t>(y * width + x);
    return index < tileScales.size() ? tileScales[index] : 1.0F;
}

TileCollisionMode RoomDefinition::tileCollisionAt(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= width || y >= height) return TileCollisionMode::Inherit;
    const auto index = static_cast<std::size_t>(y * width + x);
    return index < tileCollision.size() ? tileCollision[index] : TileCollisionMode::Inherit;
}

bool RoomDefinition::isSolidTile(int x, int y, const TextureRegistry& textures) const noexcept {
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    switch (tileCollisionAt(x, y)) {
        case TileCollisionMode::Solid: return true;
        case TileCollisionMode::None: return false;
        case TileCollisionMode::Inherit: break;
    }
    const auto* definition = textures.find(tileAt(x, y));
    return definition && definition->solid;
}

RoomManager::RoomManager(std::filesystem::path projectRoot)
    : projectRoot_(std::move(projectRoot)), textures_(projectRoot_) {}

void RoomManager::reset() {
    loaded_.clear();
    activeRoomName_.clear();
    activeEntrance_ = {};
    textures_.scan();
    ++revision_;
}
void RoomManager::scanTextures() { textures_.scan(); }

std::string RoomManager::normalizeId(std::string_view raw) {
    std::string id = lower(trim(raw));
    if (id.ends_with(".room.json")) id.resize(id.size() - std::string(".room.json").size());
    std::string normalized;
    normalized.reserve(id.size());
    for (const unsigned char c : id) {
        if (std::isalnum(c) || c == '_' || c == '-') normalized.push_back(static_cast<char>(c));
    }
    return normalized;
}
std::filesystem::path RoomManager::roomPath(std::string_view rawName) const {
    return projectRoot_ / "rooms" / (normalizeId(rawName) + ".room.json");
}

// Load into the caller-provided candidate room. The live active room is not changed here;
// callers such as reloadRoom can validate every field before committing the replacement.
bool RoomManager::readRoomFile(std::string_view rawName, RoomDefinition& room, std::string& error) const {
    const auto path = roomPath(rawName);
    if (normalizeId(rawName).empty()) { error = "The room name is empty or contains no valid characters."; return false; }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Couldn't find room `" + std::string(rawName) + "` at " + path.lexically_relative(projectRoot_).string() + ".";
        return false;
    }
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    Json root;
    JsonParser parser(contents);
    std::string parseError;
    if (!parser.parse(root, parseError)) {
        error = "Room file `" + path.filename().string() + "` is not valid JSON: " + parseError;
        return false;
    }
    const Json* schema = root.get("schema");
    if (!schema || !schema->string() || *schema->string() != "cx-room") {
        error = "`" + path.filename().string() + "` doesn't declare the CX room format.";
        return false;
    }
    const Json* nameJson = root.get("name");
    const Json* widthJson = root.get("width");
    const Json* heightJson = root.get("height");
    const Json* tileSizeJson = root.get("tileSize");
    const Json* tilesJson = root.get("tiles");
    if (!nameJson || !nameJson->string() || !widthJson || !heightJson || !tilesJson || !tilesJson->array()) {
        error = "Room data needs `name`, `width`, `height`, and a two-dimensional `tiles` array.";
        return false;
    }
    int width = 0, height = 0, tileSize = 16;
    if (!readInteger(widthJson, width) || !readInteger(heightJson, height) || width < 1 || height < 1 || width > 512 || height > 512) {
        error = "Room width and height must be whole numbers from 1 to 512.";
        return false;
    }
    if (tileSizeJson && (!readInteger(tileSizeJson, tileSize) || tileSize < 1 || tileSize > 256)) {
        error = "Room `tileSize` must be a whole number from 1 to 256.";
        return false;
    }
    const auto* rows = tilesJson->array();
    if (static_cast<int>(rows->size()) != height) {
        error = "The tile grid has " + std::to_string(rows->size()) + " rows, but room height is " + std::to_string(height) + ". Resize the grid in the room editor.";
        return false;
    }

    RoomDefinition candidate;
    candidate.name = *nameJson->string();
    candidate.file = path;
    candidate.width = width;
    candidate.height = height;
    candidate.tileSize = tileSize;
    if (const Json* projectionJson = root.get("projection")) {
        if (!projectionJson->string()) {
            error = "Room `projection` must be `orthogonal` or `oblique`.";
            return false;
        }
        candidate.projection = lower(trim(*projectionJson->string()));
        if (candidate.projection == "isometric") candidate.projection = "oblique";
        if (candidate.projection != "orthogonal" && candidate.projection != "oblique") {
            error = "Unknown room projection `" + candidate.projection + "`. Use `orthogonal` or `oblique`.";
            return false;
        }
    }
    candidate.tiles.assign(static_cast<std::size_t>(width * height), {});
    candidate.tileScales.assign(static_cast<std::size_t>(width * height), 1.0F);
    candidate.tileCollision.assign(static_cast<std::size_t>(width * height), TileCollisionMode::Inherit);
    for (int y = 0; y < height; ++y) {
        const Json& rowJson = (*rows)[static_cast<std::size_t>(y)];
        const auto* row = rowJson.array();
        if (!row || static_cast<int>(row->size()) != width) {
            error = "Row " + std::to_string(y) + " has the wrong number of tiles. Each row needs exactly " + std::to_string(width) + " cells.";
            return false;
        }
        for (int x = 0; x < width; ++x) {
            const Json& cell = (*row)[static_cast<std::size_t>(x)];
            if (cell.isNull()) continue;
            const auto* id = cell.string();
            if (!id) { error = "Every tile must be a texture ID or `null` for an empty cell."; return false; }
            if (id->empty()) continue;
            if (!textures_.contains(*id)) {
                error = "Room `" + candidate.name + "` uses unknown texture ID `" + *id + "` at tile (" + std::to_string(x) + ", " + std::to_string(y) + ").";
                error += " Add a matching image such as `assets/textures/" + *id + ".png`, then reload the room.";
                return false;
            }
            candidate.tiles[static_cast<std::size_t>(y * width + x)] = lower(*id);
        }
    }

    if (const Json* tileCollisionJson = root.get("tileCollision")) {
        const auto* collisionRows = tileCollisionJson->array();
        if (!collisionRows || static_cast<int>(collisionRows->size()) != height) {
            error = "`tileCollision` must have one row per tile row, matching the room dimensions.";
            return false;
        }
        for (int y = 0; y < height; ++y) {
            const auto* collisionRow = (*collisionRows)[static_cast<std::size_t>(y)].array();
            if (!collisionRow || static_cast<int>(collisionRow->size()) != width) {
                error = "A `tileCollision` row has the wrong number of cells. Every row must match room width.";
                return false;
            }
            for (int x = 0; x < width; ++x) {
                const Json& cell = (*collisionRow)[static_cast<std::size_t>(x)];
                if (cell.isNull()) continue;
                const auto* mode = cell.string();
                if (!mode) {
                    error = "Every `tileCollision` cell must be `inherit`, `solid`, `none`, or null.";
                    return false;
                }
                const std::string normalizedMode = lower(trim(*mode));
                TileCollisionMode parsedMode = TileCollisionMode::Inherit;
                if (normalizedMode == "solid") parsedMode = TileCollisionMode::Solid;
                else if (normalizedMode == "none") parsedMode = TileCollisionMode::None;
                else if (normalizedMode != "inherit") {
                    error = "Unknown tile collision mode `" + normalizedMode + "` at (" + std::to_string(x) + ", " + std::to_string(y) + "). Use `inherit`, `solid`, or `none`.";
                    return false;
                }
                candidate.tileCollision[static_cast<std::size_t>(y * width + x)] = parsedMode;
            }
        }
    }

    if (const Json* tileScalesJson = root.get("tileScales")) {
        const auto* scaleRows = tileScalesJson->array();
        if (!scaleRows || static_cast<int>(scaleRows->size()) != height) {
            error = "`tileScales` must have one row per tile row. Re-save the room from Room Studio, or remove `tileScales` to use the default scale of 1.0.";
            return false;
        }
        for (int y = 0; y < height; ++y) {
            const auto* scaleRow = (*scaleRows)[static_cast<std::size_t>(y)].array();
            if (!scaleRow || static_cast<int>(scaleRow->size()) != width) {
                error = "A `tileScales` row has the wrong number of cells. Every scale row must match room width. Re-save the room from Room Studio.";
                return false;
            }
            for (int x = 0; x < width; ++x) {
                const Json& scaleCell = (*scaleRow)[static_cast<std::size_t>(x)];
                const double* scale = scaleCell.number();
                if (!scale || !std::isfinite(*scale) || *scale < 0.25 || *scale > 4.0) {
                    error = "Texture scale at tile (" + std::to_string(x) + ", " + std::to_string(y) + ") must be from 0.25 to 4.0. Use 1.0 for normal scale, values below 1 to repeat more tightly, or values above 1 to zoom in.";
                    return false;
                }
                candidate.tileScales[static_cast<std::size_t>(y * width + x)] = static_cast<float>(*scale);
            }
        }
    }

    if (const Json* overlaysJson = root.get("overlays")) {
        const auto* overlays = overlaysJson->array();
        if (!overlays) { error = "Room `overlays` must be an array of overlay tile objects."; return false; }
        std::set<std::pair<int, std::string>> overlayCellLayers;
        for (const Json& overlayJson : *overlays) {
            int x = -1, y = -1;
            const Json* textureJson = overlayJson.get("texture");
            const Json* layerJson = overlayJson.get("layer");
            if (!readInteger(overlayJson.get("x"), x) || !readInteger(overlayJson.get("y"), y) ||
                x >= width || y >= height || !textureJson || !textureJson->string()) {
                error = "Every room overlay needs integer `x` and `y` inside the room plus a string `texture`.";
                return false;
            }
            const std::string textureId = lower(trim(*textureJson->string()));
            const std::string layer = layerJson && layerJson->string() ? lower(trim(*layerJson->string())) : "foreground";
            if (layer != "background" && layer != "foreground" && layer != "above_entities") {
                error = "Overlay layer must be `background`, `foreground`, or `above_entities`.";
                return false;
            }
            if (!textures_.contains(textureId)) {
                error = "Overlay at (" + std::to_string(x) + ", " + std::to_string(y) + ") uses unknown texture `" + textureId + "`.";
                return false;
            }
            const auto key = std::make_pair(y * width + x, layer + ":" + textureId);
            if (!overlayCellLayers.insert(key).second) {
                error = "The same overlay texture is listed more than once for this cell and layer. Remove the duplicate entry; different transparent overlays may share a cell.";
                return false;
            }
            candidate.overlays.push_back({x, y, textureId, layer});
        }
    }

    if (const Json* entrancesJson = root.get("entrances")) {
        const auto* entrances = std::get_if<Json::Object>(&entrancesJson->value);
        if (!entrances) { error = "`entrances` must be an object whose values have x and y coordinates."; return false; }
        for (const auto& [entranceName, entry] : *entrances) {
            int x = 0, y = 0;
            if (!readInteger(entry.get("x"), x) || !readInteger(entry.get("y"), y) || x >= width || y >= height) {
                error = "Entrance `" + entranceName + "` must point to a tile inside the room (x: 0–" + std::to_string(width - 1) + ", y: 0–" + std::to_string(height - 1) + ").";
                return false;
            }
            candidate.entrances[lower(entranceName)] = {x, y};
        }
    }
    room = std::move(candidate);
    return true;
}

bool RoomManager::loadRoom(std::string_view name, std::string& error) {
    const auto id = normalizeId(name);
    if (id.empty()) { error = "Give the room a name, for example: load room \"forest\"."; return false; }
    if (loaded_.contains(id)) return true;
    RoomDefinition room;
    if (!readRoomFile(id, room, error)) return false;
    loaded_.emplace(id, std::move(room));
    ++revision_;
    return true;
}

bool RoomManager::unloadRoom(std::string_view name, std::string& error) {
    const auto id = normalizeId(name);
    const auto found = loaded_.find(id);
    if (found == loaded_.end()) { error = "Room `" + std::string(name) + "` is not currently loaded."; return false; }
    if (activeRoomName_ == id) { activeRoomName_.clear(); activeEntrance_ = {}; }
    loaded_.erase(found);
    ++revision_;
    return true;
}

// Transactional room reload: read and validate a separate RoomDefinition first. If parsing
// fails, keep the previously loaded room intact so the author can repair the JSON and retry.
bool RoomManager::reloadRoom(std::string_view name, std::string& error) {
    const auto id = normalizeId(name);
    if (id.empty()) {
        error = "Give the room a name, for example: reload room \"forest\".";
        return false;
    }

    // Read and validate into a candidate before touching the live room. If the
    // editor is halfway through a save, or the JSON is malformed, the currently
    // loaded/active room remains usable and the user can fix the file and retry.
    RoomDefinition candidate;
    if (!readRoomFile(id, candidate, error)) return false;

    const bool wasActive = activeRoomName_ == id;
    const TilePoint oldEntrance = activeEntrance_;
    loaded_.insert_or_assign(id, std::move(candidate));
    if (wasActive) {
        const auto* active = loadedRoom(id);
        activeRoomName_ = id;
        if (!active || oldEntrance.x < 0 || oldEntrance.y < 0 ||
            oldEntrance.x >= active->width || oldEntrance.y >= active->height) {
            activeEntrance_ = {};
        } else {
            activeEntrance_ = oldEntrance;
        }
    }
    ++revision_;
    return true;
}

// A room transition changes which already-loaded room is active and records the target
// entrance. Keeping room-grid coordinates here means orthogonal and oblique rooms share the
// same gameplay coordinates; only the renderer changes their projected screen positions.
bool RoomManager::enterRoom(std::string_view name, TilePoint entrance, std::string& error) {
    const auto id = normalizeId(name);
    if (!loadRoom(id, error)) return false;
    const auto it = loaded_.find(id);
    if (it == loaded_.end()) { error = "The room loaded but could not be found in memory."; return false; }
    if (entrance.x < 0 || entrance.y < 0 || entrance.x >= it->second.width || entrance.y >= it->second.height) {
        error = "Entrance point (" + std::to_string(entrance.x) + ", " + std::to_string(entrance.y) + ") is outside room `" + it->second.name + "`.";
        error += " Room tile coordinates start at (0, 0); this room is " + std::to_string(it->second.width) + " by " + std::to_string(it->second.height) + " tiles.";
        return false;
    }
    const std::string oldName = activeRoomName_;
    activeRoomName_ = id;
    activeEntrance_ = entrance;
    // Keep only the current room by default. Explicitly preloaded rooms are cached until a transition.
    if (!oldName.empty() && oldName != id) loaded_.erase(oldName);
    ++revision_;
    return true;
}

bool RoomManager::setActiveEntrance(TilePoint entrance, std::string& error) {
    const auto* room = activeRoom();
    if (!room) { error = "There is no active room. Enter a room before moving the active entrance."; return false; }
    if (entrance.x < 0 || entrance.y < 0 || entrance.x >= room->width || entrance.y >= room->height) {
        error = "That entrance point is outside the active room.";
        return false;
    }
    activeEntrance_ = entrance;
    ++revision_;
    return true;
}

bool RoomManager::setActiveTile(int x, int y, std::string textureId, std::string& error) {
    auto it=loaded_.find(activeRoomName_);
    if(it==loaded_.end()) { error="There is no active room to edit."; return false; }
    auto& room=it->second;
    if(x<0||y<0||x>=room.width||y>=room.height) { error="Tile coordinates are outside the active room."; return false; }
    if(!textures_.contains(textureId)) { error="No texture named `"+textureId+"` exists."; return false; }
    const auto index = static_cast<std::size_t>(y * room.width + x);
    room.tiles[index] = lower(trim(textureId));
    if (index < room.tileCollision.size()) room.tileCollision[index] = TileCollisionMode::Inherit;
    ++revision_;
    return true;
}

bool RoomManager::clearActiveTile(int x, int y, std::string& error) {
    auto it=loaded_.find(activeRoomName_);
    if(it==loaded_.end()) { error="There is no active room to edit."; return false; }
    auto& room=it->second;
    if(x<0||y<0||x>=room.width||y>=room.height) { error="Tile coordinates are outside the active room."; return false; }
    const auto index = static_cast<std::size_t>(y * room.width + x);
    room.tiles[index].clear();
    if (index < room.tileCollision.size()) room.tileCollision[index] = TileCollisionMode::Inherit;
    ++revision_;
    return true;
}

// Store decorative art separately from the base tile array. That separation is important:
// transparent pixels can reveal the floor, and drawing an overlay should not silently replace
// the underlying floor tile used for collision/pathing.
bool RoomManager::setActiveOverlayTile(int x, int y, std::string textureId, std::string layer, std::string& error) {
    auto it = loaded_.find(activeRoomName_);
    if (it == loaded_.end()) { error = "There is no active room to edit."; return false; }
    auto& room = it->second;
    if (x < 0 || y < 0 || x >= room.width || y >= room.height) { error = "Overlay coordinates are outside the active room."; return false; }
    textureId = lower(trim(textureId));
    layer = lower(trim(layer));
    if (!textures_.contains(textureId)) { error = "No texture named `" + textureId + "` exists."; return false; }
    if (layer != "background" && layer != "foreground" && layer != "above_entities") { error = "Overlay layer must be background, foreground, or above_entities."; return false; }
    // Overlay layers can stack multiple transparent textures on one cell (for
    // example north-edge fringe plus a rounded north-west corner). Replacing
    // the exact same texture at that location remains idempotent.
    std::erase_if(room.overlays, [&](const OverlayTile& tile) {
        return tile.x == x && tile.y == y && tile.layer == layer && tile.textureId == textureId;
    });
    room.overlays.push_back({x, y, textureId, layer});
    ++revision_;
    return true;
}

bool RoomManager::clearActiveOverlayTile(int x, int y, std::string layer, std::string& error) {
    auto it = loaded_.find(activeRoomName_);
    if (it == loaded_.end()) { error = "There is no active room to edit."; return false; }
    auto& room = it->second;
    if (x < 0 || y < 0 || x >= room.width || y >= room.height) { error = "Overlay coordinates are outside the active room."; return false; }
    layer = lower(trim(layer));
    const auto before = room.overlays.size();
    std::erase_if(room.overlays, [&](const OverlayTile& tile) { return tile.x == x && tile.y == y && (layer.empty() || tile.layer == layer); });
    if (before == room.overlays.size()) { error = "No overlay tile exists at that position and layer."; return false; }
    ++revision_;
    return true;
}

const RoomDefinition* RoomManager::activeRoom() const noexcept { return loadedRoom(activeRoomName_); }
const RoomDefinition* RoomManager::loadedRoom(std::string_view name) const noexcept {
    const auto it = loaded_.find(normalizeId(name));
    return it == loaded_.end() ? nullptr : &it->second;
}
std::vector<std::string> RoomManager::loadedRoomNames() const {
    std::vector<std::string> result;
    result.reserve(loaded_.size());
    for (const auto& [name, room] : loaded_) { (void)room; result.push_back(name); }
    std::sort(result.begin(), result.end());
    return result;
}
bool RoomManager::parseEntrancePoint(std::string_view raw, TilePoint& point) {
    static const std::regex pattern(R"(^\s*\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)\s*$)");
    std::smatch match;
    const std::string text(raw);
    if (!std::regex_match(text, match, pattern)) return false;
    try { point.x = std::stoi(match[1].str()); point.y = std::stoi(match[2].str()); return true; }
    catch (...) { return false; }
}
std::string RoomManager::formatEntrancePoint(TilePoint point) { return "(" + std::to_string(point.x) + ", " + std::to_string(point.y) + ")"; }

} // namespace cx
