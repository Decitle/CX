#include "cx/runtime.hpp"

#include "cx/diagnostics.hpp"
#include "cx/value.hpp"
#include "cx/collision.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace cx {
namespace {

// These small parsing helpers are local to the statement executor. Keeping them
// here avoids coupling execution behavior to the expression evaluator's internals.
std::vector<std::string> splitTopLevelComma(std::string_view text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    int braces = 0, parentheses = 0;
    bool quoted = false, escaped = false;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i < text.size()) {
            const char c = text[i];
            if (escaped) { escaped = false; continue; }
            if (c == '\\' && quoted) { escaped = true; continue; }
            if (c == '"') { quoted = !quoted; continue; }
            if (!quoted) {
                if (c == '{') ++braces; else if (c == '}' && braces > 0) --braces;
                else if (c == '(') ++parentheses; else if (c == ')' && parentheses > 0) --parentheses;
                if (c != ',' || braces != 0 || parentheses != 0) continue;
            } else continue;
        }
        parts.push_back(trim(text.substr(start, i - start)));
        start = i + 1;
    }
    return parts;
}
bool isListWildcard(std::string_view name) {
    return name.ends_with("::*");
}
bool sameValue(const Value& a, const Value& b) {
    if (a.isObjectReference() || b.isObjectReference()) {
        const auto* left = a.objectReference();
        const auto* right = b.objectReference();
        return left && right && *left == *right;
    }
    if (a.isNull() || b.isNull()) return a.isNull() && b.isNull();
    if (a.isNumber() || b.isNumber()) {
        const double av = a.numberOr(std::numeric_limits<double>::quiet_NaN());
        const double bv = b.numberOr(std::numeric_limits<double>::quiet_NaN());
        if (std::isfinite(av) && std::isfinite(bv)) return approximatelyEqual(av, bv);
    }
    return lower(a.toString()) == lower(b.toString());
}

} // namespace

void Runtime::runExecution(ExecutionContext& context) {
    context.suspended = false;
    std::size_t instructionCount = 0;
    while (!context.frames.empty() && !context.stopped) {
        if (++instructionCount > maximumInstructionsPerResume_) {
            const Statement* origin = nullptr;
            const auto& frame = context.frames.back();
            if (frame.statements && frame.nextIndex > 0 && frame.nextIndex <= frame.statements->size())
                origin = &(*frame.statements)[frame.nextIndex - 1];
            if (origin) runtimeError(*origin, "This script exceeded the instruction safety limit in one uninterrupted run.",
                "Add `wait 1 tick` inside long-running loops, reduce the iteration count, or split the work across `on tick:` events.");
            else appendDiagnostic(diagnostics_, {false, "<runtime>", 0, "This script exceeded the instruction safety limit in one uninterrupted run.",
                "Add a wait inside long-running loops or split the work across ticks.", 1, 1});
            context.stopped = true;
            break;
        }
        auto& activeFrame = context.frames.back();
        if (!activeFrame.statements) {
            context.stopped = true;
            break;
        }
        if (activeFrame.nextIndex >= activeFrame.statements->size()) {
            if (activeFrame.loopFrame && activeFrame.owner) {
                bool repeat = false;
                if (activeFrame.whileLoop) {
                    std::string conditionError;
                    bool conditionResult = false;
                    if (!evaluateCondition(activeFrame.owner->expression, context, conditionResult, conditionError)) {
                        runtimeError(*activeFrame.owner, conditionError,
                            "Use a condition like `while {_count} > 0:` and make sure the variables used by it are set.");
                        context.stopped = true;
                        break;
                    }
                    repeat = conditionResult;
                    if (repeat && activeFrame.iteration >= maximumCountedLoopIterations_) {
                        runtimeError(*activeFrame.owner, "This while loop reached the safety limit of 1,000,000 iterations.",
                            "Make sure its condition can become false, or add a wait and a clear exit condition.");
                        context.stopped = true;
                        break;
                    }
                } else {
                    repeat = activeFrame.iteration < activeFrame.repetitions;
                }
                if (repeat) {
                    ++activeFrame.iteration;
                    activeFrame.nextIndex = 0;
                    if (!context.loopNumbers.empty()) context.loopNumbers.back() = activeFrame.iteration;
                    if (!context.loopValues.empty()) {
                        if (activeFrame.listLoop && activeFrame.iteration <= activeFrame.listEntries.size()) {
                            context.loopValues.back() = activeFrame.listEntries[activeFrame.iteration - 1].value;
                            if (!context.loopIndexes.empty()) context.loopIndexes.back() = activeFrame.listEntries[activeFrame.iteration - 1].index;
                        } else {
                            context.loopValues.back() = Value(static_cast<double>(activeFrame.iteration));
                            if (!context.loopIndexes.empty()) context.loopIndexes.back() = std::to_string(activeFrame.iteration);
                        }
                    }
                    continue;
                }
                context.frames.pop_back();
                if (!context.loopNumbers.empty()) context.loopNumbers.pop_back();
                if (!context.loopValues.empty()) context.loopValues.pop_back();
                if (!context.loopIndexes.empty()) context.loopIndexes.pop_back();
                continue;
            }
            context.frames.pop_back();
            continue;
        }
        const Statement& statement = (*activeFrame.statements)[activeFrame.nextIndex++];
        std::string error;
        switch (statement.type) {
            case StatementType::PlaceOverlayTile: {
                const auto separator = statement.expression.find('|');
                if (separator == std::string::npos) {
                    runtimeError(statement, "Overlay tile coordinates are incomplete.",
                                 "Use: place overlay tile {$cover_stone} at (12, 9) layer \"foreground\"");
                    break;
                }
                std::string texture = lower(trim(statement.target));
                if (texture.size() >= 3 && texture.starts_with("{$") && texture.back() == '}') texture = texture.substr(2, texture.size() - 3);
                int x = -1, y = -1;
                try { x = std::stoi(statement.expression.substr(0, separator)); y = std::stoi(statement.expression.substr(separator + 1)); } catch (...) { x = y = -1; }
                std::string tileError;
                if (!rooms_.setActiveOverlayTile(x, y, texture, statement.property, tileError))
                    runtimeError(statement, tileError, "Load a room first, then place transparent overlays at valid tile coordinates.");
                break;
            }
            case StatementType::ClearOverlayTile: {
                const auto separator = statement.expression.find('|');
                if (separator == std::string::npos) { runtimeError(statement, "Overlay tile coordinates are incomplete.", "Use: clear overlay tile at (12, 9) layer \"foreground\""); break; }
                int x = -1, y = -1;
                try { x = std::stoi(statement.expression.substr(0, separator)); y = std::stoi(statement.expression.substr(separator + 1)); } catch (...) { x = y = -1; }
                std::string tileError;
                if (!rooms_.clearActiveOverlayTile(x, y, statement.property, tileError))
                    runtimeError(statement, tileError, "Check that an overlay exists at those coordinates and layer.");
                break;
            }
            case StatementType::PlaceTile: {
                const auto separator=statement.expression.find('|');
                if(separator==std::string::npos) { runtimeError(statement,"Tile placement coordinates are incomplete.","Use: place tile {$cover_stone} at (12, 9)"); break; }
                std::string texture=lower(trim(statement.target));
                if(texture.size()>=3 && texture.starts_with("{$") && texture.back()=='}') texture=texture.substr(2,texture.size()-3);
                int x=-1,y=-1; try {x=std::stoi(statement.expression.substr(0,separator));y=std::stoi(statement.expression.substr(separator+1));} catch(...) {x=y=-1;}
                std::string tileError;
                if(!rooms_.setActiveTile(x,y,texture,tileError)) runtimeError(statement,tileError,"Load a room first, then use tile coordinates inside its width and height.");
                break;
            }
            case StatementType::ClearTile: {
                const auto sep=statement.expression.find('|'); if(sep==std::string::npos) {runtimeError(statement,"Tile coordinates are incomplete.","Use: clear tile at (12, 9)");break;}
                int x=-1,y=-1;try{x=std::stoi(statement.expression.substr(0,sep));y=std::stoi(statement.expression.substr(sep+1));}catch(...){x=y=-1;}
                std::string tileError; if(!rooms_.clearActiveTile(x,y,tileError)) runtimeError(statement,tileError,"Check that a room is active and these coordinates are inside it.");
                break;
            }
            case StatementType::SetEntrance: {
                TilePoint point;
                if (!RoomManager::parseEntrancePoint(statement.expression, point)) {
                    runtimeError(statement, "That entrance point could not be read.",
                                 "Use whole tile coordinates, for example: set {entrance1::room1} to an entrance point at (0, 0)");
                    break;
                }
                setVariable(statement.target, Value(RoomManager::formatEntrancePoint(point)), context, &statement);
                break;
            }
            case StatementType::LoadRoom: {
                std::string roomError;
                if (!rooms_.loadRoom(statement.target, roomError)) {
                    runtimeError(statement, roomError, "Create rooms/" + RoomManager::normalizeId(statement.target) + ".room.json in the project folder, or check the room name.");
                    break;
                }
                if (const auto* room = rooms_.loadedRoom(statement.target)) {
                    const std::string roomId = RoomManager::normalizeId(statement.target);
                    for (const auto& [entranceName, point] : room->entrances) {
                        globals_["entrance::" + roomId + "::" + entranceName] = Value(RoomManager::formatEntrancePoint(point));
                    }
                    log("Loaded room `" + room->name + "` (" + std::to_string(room->width) + " x " + std::to_string(room->height) + " tiles).");
                }
                break;
            }
            case StatementType::UnloadRoom: {
                const std::string id = RoomManager::normalizeId(statement.target);
                const bool unloadsActiveRoom = rooms_.activeRoomName() == id;
                std::string roomError;
                if (!rooms_.unloadRoom(id, roomError)) {
                    runtimeError(statement, roomError, R"HINT(Load the room first, or use `enter room "name" at {entrance}` to switch rooms.)HINT");
                    break;
                }
                if (unloadsActiveRoom) ++roomContextRevision_;
                const std::string prefix = "entrance::" + id + "::";
                for (auto it = globals_.begin(); it != globals_.end();) {
                    if (it->first.starts_with(prefix)) it = globals_.erase(it); else ++it;
                }
                globals_["room::current"] = rooms_.activeRoomName();
                if (rooms_.activeRoomName().empty()) { globals_["room::spawn_x"] = 0; globals_["room::spawn_y"] = 0; }
                log("Unloaded room `" + id + "`.");
                break;
            }
            case StatementType::ReloadRoom: {
                const std::string id = RoomManager::normalizeId(statement.target);
                const bool reloadsActiveRoom = rooms_.activeRoomName() == id;
                std::string roomError;
                if (!rooms_.reloadRoom(id, roomError)) {
                    runtimeError(statement, roomError, "Check the room JSON in rooms/ and ensure its tile IDs exist in assets/textures/.");
                    break;
                }
                if (const auto* room = rooms_.loadedRoom(id)) {
                    // These variables mirror the current room file. Remove entrances
                    // that were deleted/renamed there, but only after reload succeeded.
                    const std::string entrancePrefix = "entrance::" + id + "::";
                    for (auto it = globals_.begin(); it != globals_.end();) {
                        if (it->first.starts_with(entrancePrefix)) it = globals_.erase(it);
                        else ++it;
                    }
                    for (const auto& [entranceName, point] : room->entrances) {
                        globals_["entrance::" + id + "::" + entranceName] = Value(RoomManager::formatEntrancePoint(point));
                    }
                    if (rooms_.activeRoomName() == id) {
                        globals_["room::current"] = id;
                        globals_["room::spawn_x"] = rooms_.activeEntrance().x;
                        globals_["room::spawn_y"] = rooms_.activeEntrance().y;
                    }
                    log("Reloaded room `" + room->name + "` from disk.");
                }
                if (reloadsActiveRoom) ++roomContextRevision_;
                break;
            }
            case StatementType::EnterRoom: {
                std::string valueError;
                const Value value = evaluateValue(statement.expression, context, valueError);
                if (!valueError.empty()) {
                    runtimeError(statement, valueError, R"HINT(Use a point variable, for example: enter room "cave" at {entrance::cave::spawn})HINT");
                    break;
                }
                TilePoint point;
                if (!RoomManager::parseEntrancePoint(value.toString(), point)) {
                    runtimeError(statement, "Variable " + statement.expression + " is not an entrance point.",
                                 "Set it using `set {entrance1::room1} to an entrance point at (0, 0)` or use a named entrance generated from the room file.");
                    break;
                }
                const std::string previous = rooms_.activeRoomName();
                std::string roomError;
                if (!rooms_.enterRoom(statement.target, point, roomError)) {
                    runtimeError(statement, roomError, "Room coordinates are tile coordinates starting at (0, 0). Use the room editor to check the dimensions and entrance positions.");
                    break;
                }
                // Entering even the same room at another entrance changes the live
                // scene context and invalidates hits queued before that transition.
                ++roomContextRevision_;
                const std::string activeId = RoomManager::normalizeId(statement.target);
                if (!previous.empty() && previous != activeId) {
                    const std::string oldPrefix = "entrance::" + RoomManager::normalizeId(previous) + "::";
                    for (auto it = globals_.begin(); it != globals_.end();) {
                        if (it->first.starts_with(oldPrefix)) it = globals_.erase(it); else ++it;
                    }
                }
                globals_["room::current"] = activeId;
                globals_["room::previous"] = previous;
                globals_["room::spawn_x"] = point.x;
                globals_["room::spawn_y"] = point.y;
                if (const auto* room = rooms_.activeRoom()) {
                    for (const auto& [entranceName, entryPoint] : room->entrances) {
                        globals_["entrance::" + activeId + "::" + entranceName] = Value(RoomManager::formatEntrancePoint(entryPoint));
                    }
                    log("Entered room `" + room->name + "` at tile (" + std::to_string(point.x) + ", " + std::to_string(point.y) + ").");
                }
                break;
            }
            case StatementType::CreateLight: {
                static const std::regex pointPattern(R"POINT(^\s*\(\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*\)\s*$)POINT");
                std::smatch match;
                if (!std::regex_match(statement.expression, match, pointPattern)) {
                    runtimeError(statement, "The light position is not a valid coordinate pair.",
                                 "Use tile coordinates like: create light \"torch\" at (8, 5)");
                    break;
                }
                LightSource light;
                light.name = lower(trim(statement.target));
                light.roomName = rooms_.activeRoomName();
                try {
                    light.xTiles = std::stof(match[1].str());
                    light.yTiles = std::stof(match[2].str());
                } catch (...) {
                    runtimeError(statement, "The light position must contain finite numbers.",
                                 "Example: create light \"torch\" at (8, 5)");
                    break;
                }
                lights_[light.name] = light;
                ++lightsRevision_;
                log("Created light `" + light.name + "` at tile (" + match[1].str() + ", " + match[2].str() + ").");
                break;
            }
            case StatementType::RemoveLight: {
                const std::string name = lower(trim(statement.target));
                if (lights_.erase(name) == 0) {
                    runtimeError(statement, "There is no light named `" + name + "`.",
                                 "Create it first with: create light \"" + name + "\" at (8, 5)");
                } else {
                    ++lightsRevision_;
                    log("Removed light `" + name + "`.");
                }
                break;
            }
            case StatementType::SetLight: {
                const std::string name = lower(trim(statement.target));
                auto found = lights_.find(name);
                if (found == lights_.end()) {
                    runtimeError(statement, "There is no light named `" + name + "`.",
                                 "Create the light first with: create light \"" + name + "\" at (8, 5)");
                    break;
                }
                LightSource& light = found->second;
                if (statement.property == "color") {
                    static const std::regex colorPattern(R"COLOR(^\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)\s*$)COLOR");
                    std::smatch match;
                    if (!std::regex_match(statement.expression, match, colorPattern)) {
                        runtimeError(statement, "A light color needs three whole-number RGB values.",
                                     "Use: set light \"" + name + "\" color to (255, 190, 120)");
                        break;
                    }
                    try {
                        const int red = std::stoi(match[1].str()), green = std::stoi(match[2].str()), blue = std::stoi(match[3].str());
                        if (red > 255 || green > 255 || blue > 255) throw std::out_of_range("RGB component");
                        light.red = static_cast<std::uint8_t>(red);
                        light.green = static_cast<std::uint8_t>(green);
                        light.blue = static_cast<std::uint8_t>(blue);
                        ++lightsRevision_;
                    } catch (...) {
                        runtimeError(statement, "Each RGB color component must be from 0 to 255.",
                                     "Example: set light \"" + name + "\" color to (255, 190, 120)");
                    }
                    break;
                }
                Value value = evaluateValue(statement.expression, context, error);
                if (!error.empty()) {
                    runtimeError(statement, error, "Use a numeric value such as 180 or 0.8 for this light setting.");
                    break;
                }
                const double number = value.numberOr(std::numeric_limits<double>::quiet_NaN());
                if (!std::isfinite(number)) {
                    runtimeError(statement, "Light property `" + statement.property + "` needs a number.",
                                 "Use a number, for example: set light \"" + name + "\" radius to 180");
                    break;
                }
                if (statement.property == "radius") {
                    if (number < 1.0 || number > 4096.0) {
                        runtimeError(statement, "Light radius must be between 1 and 4096 pixels.", "Example: set light \"" + name + "\" radius to 180");
                        break;
                    }
                    light.radius = static_cast<float>(number);
                } else if (statement.property == "intensity") {
                    if (number < 0.0 || number > 1.0) {
                        runtimeError(statement, "Light intensity must be between 0 and 1.", "Example: set light \"" + name + "\" intensity to 0.85");
                        break;
                    }
                    light.intensity = static_cast<float>(number);
                } else if (statement.property == "direction") {
                    if (number < -360.0 || number > 360.0) {
                        runtimeError(statement, "Light direction must be between -360 and 360 degrees.", "Example: set light \"" + name + "\" direction to 35");
                        break;
                    }
                    light.directionDegrees = static_cast<float>(number);
                } else if (statement.property == "beam") {
                    if (number < 1.0 || number > 360.0) {
                        runtimeError(statement, "Beam angle must be between 1 and 360 degrees.", "Use 45 for a narrow beam or 360 for an all-around light.");
                        break;
                    }
                    light.beamAngleDegrees = static_cast<float>(number);
                }
                ++lightsRevision_;
                break;
            }
            case StatementType::CreateBeam: {
                const auto parsePoint = [](const std::string& raw, EffectPoint& point) {
                    static const std::regex pattern(R"POINT(^\s*\(\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*\)\s*$)POINT");
                    std::smatch match;
                    if (!std::regex_match(raw, match, pattern)) return false;
                    try { point.x = std::stof(match[1].str()); point.y = std::stof(match[2].str()); }
                    catch (...) { return false; }
                    return std::isfinite(point.x) && std::isfinite(point.y);
                };
                BeamEffect beam;
                beam.name = lower(trim(statement.target));
                beam.roomName = rooms_.activeRoomName();
                if (!parsePoint(statement.expression, beam.start) || !parsePoint(statement.secondaryExpression, beam.end)) {
                    runtimeError(statement, "The beam endpoints must be finite world-space coordinate pairs.",
                                 "Example: create beam \"sun shaft\" from (320, -80) to (320, 240) using {$beam_texture}");
                    break;
                }
                std::string textureId = trim(statement.property);
                if (textureId.size() < 4 || !textureId.starts_with("{$") || textureId.back() != '}') {
                    runtimeError(statement, "A beam needs a valid texture asset reference.",
                                 "Place a texture in assets/textures/ and use its ID, e.g. using {$beam_texture}.");
                    break;
                }
                textureId = lower(trim(std::string_view(textureId).substr(2, textureId.size() - 3)));
                if (!rooms_.textures().find(textureId)) {
                    runtimeError(statement, "The beam texture `" + textureId + "` was not found.",
                                 "Add its image to assets/textures/ and use the filename (without the extension) as the texture ID.");
                    break;
                }
                beam.textureId = std::move(textureId);
                BeamQuad geometry;
                if (!buildBeamQuad(beam, geometry)) {
                    runtimeError(statement, "The beam endpoints must not be identical and its dimensions must be valid.",
                                 "Move the end point away from the start point. Beam coordinates are world-space pixels.");
                    break;
                }
                beams_[beam.name] = std::move(beam);
                ++beamsRevision_;
                log("Created beam `" + lower(trim(statement.target)) + "`.");
                break;
            }
            case StatementType::RemoveBeam: {
                const std::string name = lower(trim(statement.target));
                if (beams_.erase(name) == 0) {
                    runtimeError(statement, "There is no beam named `" + name + "`.",
                                 "Create it first with: create beam \"sun shaft\" from (320, -80) to (320, 240) using {$beam_texture}");
                } else { ++beamsRevision_; log("Removed beam `" + name + "`."); }
                break;
            }
            case StatementType::SetBeam: {
                const std::string name = lower(trim(statement.target));
                auto found = beams_.find(name);
                if (found == beams_.end()) {
                    runtimeError(statement, "There is no beam named `" + name + "`.",
                                 "Create the beam first with: create beam \"sun shaft\" from (320, -80) to (320, 240) using {$beam_texture}");
                    break;
                }
                BeamEffect candidate = found->second;
                const auto parsePoint = [](const std::string& raw, EffectPoint& point) {
                    static const std::regex pattern(R"POINT(^\s*\(\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*\)\s*$)POINT");
                    std::smatch match;
                    if (!std::regex_match(raw, match, pattern)) return false;
                    try { point.x = std::stof(match[1].str()); point.y = std::stof(match[2].str()); }
                    catch (...) { return false; }
                    return std::isfinite(point.x) && std::isfinite(point.y);
                };
                const auto parseColor = [](const std::string& raw, std::uint8_t& red,
                                           std::uint8_t& green, std::uint8_t& blue) {
                    static const std::regex pattern(R"COLOR(^\s*\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)\s*$)COLOR");
                    std::smatch match;
                    if (!std::regex_match(raw, match, pattern)) return false;
                    try {
                        const int r = std::stoi(match[1].str()), g = std::stoi(match[2].str()), b = std::stoi(match[3].str());
                        if (r > 255 || g > 255 || b > 255) return false;
                        red = static_cast<std::uint8_t>(r); green = static_cast<std::uint8_t>(g); blue = static_cast<std::uint8_t>(b);
                    } catch (...) { return false; }
                    return true;
                };
                if (statement.property == "start" || statement.property == "end") {
                    EffectPoint& point = statement.property == "start" ? candidate.start : candidate.end;
                    if (!parsePoint(statement.expression, point)) {
                        runtimeError(statement, "A beam endpoint needs a finite coordinate pair.",
                                     "Example: set beam \"sun shaft\" end to (320, 240)");
                        break;
                    }
                } else if (statement.property == "tint" || statement.property == "endpoint_light_color") {
                    auto& r = statement.property == "tint" ? candidate.red : candidate.endpointLightRed;
                    auto& g = statement.property == "tint" ? candidate.green : candidate.endpointLightGreen;
                    auto& b = statement.property == "tint" ? candidate.blue : candidate.endpointLightBlue;
                    if (!parseColor(statement.expression, r, g, b)) {
                        runtimeError(statement, "A beam color needs three integer RGB values from 0 to 255.",
                                     "Example: set beam \"sun shaft\" tint to (255, 230, 190)");
                        break;
                    }
                } else if (statement.property == "layer") {
                    std::string layer = trim(statement.expression);
                    if (layer.size() < 2 || layer.front() != '"' || layer.back() != '"') {
                        runtimeError(statement, "A beam draw layer must be quoted.", "Use \"background\", \"world\", or \"foreground\".");
                        break;
                    }
                    layer = lower(trim(std::string_view(layer).substr(1, layer.size() - 2)));
                    if (layer == "background") candidate.layer = BeamDrawLayer::Background;
                    else if (layer == "world") candidate.layer = BeamDrawLayer::World;
                    else if (layer == "foreground") candidate.layer = BeamDrawLayer::Foreground;
                    else { runtimeError(statement, "Unknown beam draw layer `" + layer + "`.", "Use \"background\", \"world\", or \"foreground\"."); break; }
                } else {
                    Value value = evaluateValue(statement.expression, context, error);
                    if (!error.empty()) { runtimeError(statement, error, "Use a number or true/false for the beam property."); break; }
                    if (statement.property == "endpoint_light") {
                        candidate.endpointLightEnabled = value.truthy();
                    } else {
                        const double number = value.numberOr(std::numeric_limits<double>::quiet_NaN());
                        if (!std::isfinite(number)) { runtimeError(statement, "Beam property `" + statement.property + "` needs a finite number.", "Use a finite numeric value."); break; }
                        if (statement.property == "width") {
                            if (number < 1 || number > 2048) { runtimeError(statement, "Beam width must be from 1 to 2048 pixels.", "Example: set beam \"sun shaft\" width to 48"); break; }
                            candidate.width = static_cast<float>(number);
                        } else if (statement.property == "opacity") {
                            if (number < 0 || number > 1) { runtimeError(statement, "Beam opacity must be from 0 to 1.", "Example: set beam \"sun shaft\" opacity to 0.7"); break; }
                            candidate.opacity = static_cast<float>(number);
                        } else if (statement.property == "lifetime") {
                            if (number < 0 || number > 3600) { runtimeError(statement, "Beam lifetime must be from 0 to 3600 seconds; zero means persistent.", "Example: set beam \"sun shaft\" lifetime to 2.5"); break; }
                            candidate.lifetimeSeconds = static_cast<float>(number); candidate.ageSeconds = 0.0F;
                        } else if (statement.property == "texture_repeat") {
                            if (number < 1 || number > 4096) { runtimeError(statement, "Beam texture repeat length must be from 1 to 4096 pixels.", "Example: set beam \"sun shaft\" texture_repeat to 64"); break; }
                            candidate.textureRepeatLength = static_cast<float>(number);
                        } else if (statement.property == "endpoint_light_radius") {
                            if (number < 1 || number > 4096) { runtimeError(statement, "Endpoint light radius must be from 1 to 4096 pixels.", "Example: set beam \"sun shaft\" endpoint_light_radius to 160"); break; }
                            candidate.endpointLightRadius = static_cast<float>(number);
                        } else if (statement.property == "endpoint_light_intensity") {
                            if (number < 0 || number > 1) { runtimeError(statement, "Endpoint light intensity must be from 0 to 1.", "Example: set beam \"sun shaft\" endpoint_light_intensity to 0.7"); break; }
                            candidate.endpointLightIntensity = static_cast<float>(number);
                        }
                    }
                }
                BeamQuad geometry;
                if (!buildBeamQuad(candidate, geometry)) {
                    runtimeError(statement, "The updated beam geometry is degenerate or outside the supported numeric range.",
                                 "Keep the endpoints distinct and use finite coordinates with positive width and texture repeat length.");
                    break;
                }
                found->second = std::move(candidate);
                ++beamsRevision_;
                break;
            }
            case StatementType::CreatePath: {
                EntityPath path;
                path.name = lower(trim(statement.target));
                static const std::regex pointPattern(R"POINT(\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\))POINT");
                const std::string pointsText = statement.expression;
                for (std::sregex_iterator it(pointsText.begin(), pointsText.end(), pointPattern), end; it != end; ++it) {
                    try {
                        const int x = std::stoi((*it)[1].str());
                        const int y = std::stoi((*it)[2].str());
                        const auto* room = rooms_.activeRoom();
                        if (x < 0 || y < 0 || (room && (x >= room->width || y >= room->height))) {
                            runtimeError(statement, "Path waypoint (" + std::to_string(x) + ", " + std::to_string(y) + ") is outside the active room.",
                                "Use tile coordinates inside the current room, e.g. create path \"patrol\" through tiles (5, 5), (10, 5).");
                            path.waypoints.clear();
                            break;
                        }
                        path.waypoints.push_back({x, y});
                    } catch (...) {
                        runtimeError(statement, "A path waypoint is not a valid whole tile coordinate.",
                            "Use comma-separated pairs such as (8, 4), (12, 4), (12, 9).");
                        path.waypoints.clear();
                        break;
                    }
                }
                if (!path.waypoints.empty()) {
                    paths_[path.name] = std::move(path);
                    log("Created tile path `" + lower(trim(statement.target)) + "`.");
                } else if (!hasErrors()) {
                    runtimeError(statement, "No valid waypoints were found in this path.", "Example: create path \"patrol\" through tiles (5, 5), (10, 5), (10, 9)");
                }
                break;
            }
            case StatementType::SetEntityPath: {
                const std::string id = lower(trim(statement.target));
                auto entity = sprites_.find(id);
                if (entity == sprites_.end() || !entity->second.entity) {
                    runtimeError(statement, "There is no entity named `" + id + "`.", "Create it first with create entity \"" + id + "\" with texture {$hero}.");
                    break;
                }
                const std::string pathName = lower(trim(statement.expression));
                const auto path = paths_.find(pathName);
                if (path == paths_.end() || path->second.waypoints.empty()) {
                    runtimeError(statement, "There is no path named `" + pathName + "`.", "Define it first: create path \"" + pathName + "\" through tiles (4, 4), (8, 4)");
                    break;
                }
                float speed = 0.0F;
                try { speed = std::stof(statement.property); } catch (...) { speed = 0.0F; }
                if (!std::isfinite(speed) || speed <= 0.0F || speed > 100.0F) {
                    runtimeError(statement, "Path speed must be between 0 and 100 tiles per second.", "Use: set path of \"npc\" to path \"patrol\" with speed \"1.25\"");
                    break;
                }
                entityTweens_.erase(id);
                entity->second.pathName = pathName;
                entity->second.pathWaypoint = 0;
                entity->second.pathSpeedTilesPerSecond = speed;
                entity->second.navigationTiles.clear();
                entity->second.navigationWaypoint = 0;
                entity->second.navigationGoal = {-1, -1};
                break;
            }
            case StatementType::TweenEntity: {
                const std::string id = lower(trim(statement.target));
                auto entity = sprites_.find(id);
                if (entity == sprites_.end() || !entity->second.entity || entity->second.projectile) {
                    runtimeError(statement, "There is no ordinary entity named `" + id + "`.",
                        "Create it first with `create entity \"" + id + "\" with texture {$hero}`; projectiles cannot be tweened directly.");
                    break;
                }

                std::string pairText = trim(statement.expression);
                if (pairText.size() >= 2 && pairText.front() == '(' && pairText.back() == ')')
                    pairText = trim(std::string_view(pairText).substr(1, pairText.size() - 2));
                const auto parts = splitTopLevelComma(pairText);
                if (parts.size() != 2) {
                    runtimeError(statement, "A tween destination needs exactly two coordinates.",
                        "Use: tween entity \"boulder\" to (640, 240) over 0.8 seconds easing ease_in_out");
                    break;
                }
                Value xValue = evaluateValue(parts[0], context, error);
                if (!error.empty()) { runtimeError(statement, error, "Tween destination coordinates must be numeric expressions."); break; }
                Value yValue = evaluateValue(parts[1], context, error);
                if (!error.empty()) { runtimeError(statement, error, "Tween destination coordinates must be numeric expressions."); break; }
                Value durationValue = evaluateValue(statement.secondaryExpression, context, error);
                if (!error.empty()) { runtimeError(statement, error, "Tween duration must be a numeric expression in seconds."); break; }
                const double x = xValue.numberOr(std::numeric_limits<double>::quiet_NaN());
                const double y = yValue.numberOr(std::numeric_limits<double>::quiet_NaN());
                const double seconds = durationValue.numberOr(std::numeric_limits<double>::quiet_NaN());
                double arcHeight = 0.0;
                if (!statement.detailExpression.empty()) {
                    Value arcValue = evaluateValue(statement.detailExpression, context, error);
                    if (!error.empty()) { runtimeError(statement, error, "Arc height must be a numeric expression in pixels."); break; }
                    arcHeight = arcValue.numberOr(std::numeric_limits<double>::quiet_NaN());
                }
                if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 10000000.0 || std::abs(y) > 10000000.0) {
                    runtimeError(statement, "Tween destination coordinates must be finite and within 10,000,000 pixels.",
                        "Use coordinates such as `(640, 240)`.");
                    break;
                }
                if (!std::isfinite(seconds) || seconds <= 0.0 || seconds > 3600.0) {
                    runtimeError(statement, "Tween duration must be greater than 0 and no more than 3600 seconds.",
                        "Use a duration such as `0.8 seconds`; the simulation converts it to a fixed number of ticks.");
                    break;
                }
                if (!std::isfinite(arcHeight) || arcHeight < 0.0 || arcHeight > 100000.0) {
                    runtimeError(statement, "Tween arc height must be between 0 and 100,000 pixels.",
                        "Use a non-negative value such as `96`, or omit `with arc height` for a straight tween.");
                    break;
                }
                const double tickCount = std::ceil(seconds * static_cast<double>(std::max(1U, settings_.tickRate)));
                if (!std::isfinite(tickCount) || tickCount < 1.0 || tickCount > 3600000.0) {
                    runtimeError(statement, "The requested tween duration exceeds the supported tick count.",
                        "Use a shorter duration or a lower engine tick rate.");
                    break;
                }

                EntityTween tween;
                tween.entityInstanceId = entity->second.runtimeInstanceId;
                tween.startX = entity->second.x;
                tween.startY = entity->second.y;
                tween.targetX = static_cast<float>(x);
                tween.targetY = static_cast<float>(y);
                tween.arcHeightPixels = static_cast<float>(arcHeight);
                tween.durationTicks = static_cast<std::uint64_t>(tickCount);
                tween.elapsedTicks = 0;
                tween.roomContextRevision = roomContextRevision_;
                tween.easing = statement.property;
                entity->second.pathName.clear();
                entity->second.navigationTiles.clear();
                entity->second.navigationWaypoint = 0;
                entity->second.navigationGoal = {-1, -1};
                entityTweens_[id] = std::move(tween); // Replacing a tween does not fire its completion event.
                break;
            }
            case StatementType::CancelEntityTween: {
                const std::string id = lower(trim(statement.target));
                const auto entity = sprites_.find(id);
                if (entity == sprites_.end() || !entity->second.entity || entity->second.projectile) {
                    runtimeError(statement, "There is no ordinary entity named `" + id + "`.",
                        "Check the entity name before cancelling its tween.");
                    break;
                }
                entityTweens_.erase(id); // Cancellation is silent and never fires completion.
                break;
            }
            case StatementType::DefineAnimation: {
                const std::string id = lower(trim(statement.target));
                const std::string clipName = lower(trim(statement.property));
                auto found = sprites_.find(id);
                if (found == sprites_.end() || !found->second.entity || found->second.projectile) {
                    runtimeError(statement, "There is no ordinary entity named `" + id + "`.",
                        "Create the entity and configure its animation sheet before defining clips.");
                    break;
                }
                SpriteObject& entity = found->second;
                if (entity.frameWidth <= 0 || entity.frameHeight <= 0 ||
                    entity.frameColumns <= 1 || entity.frameRows <= 1) {
                    runtimeError(statement, "The entity has no usable animation sheet.",
                        "First use: set entity \"" + id + "\" animation sheet to (32, 32) columns 4 rows 8");
                    break;
                }
                const auto rangeSeparator = statement.expression.find('|');
                const auto detailSeparator = statement.detailExpression.find('|');
                int firstFrame = -1;
                int lastFrame = -1;
                double framesPerSecond = 0.0;
                try {
                    if (rangeSeparator == std::string::npos || detailSeparator == std::string::npos)
                        throw std::runtime_error("incomplete animation definition");
                    firstFrame = std::stoi(statement.expression.substr(0, rangeSeparator));
                    lastFrame = std::stoi(statement.expression.substr(rangeSeparator + 1));
                    framesPerSecond = std::stod(statement.detailExpression.substr(0, detailSeparator));
                } catch (...) {
                    runtimeError(statement, "The animation definition contains an invalid number.",
                        "Use integer frame indices and a positive frame rate, for example `frames 0 to 3 row facing at 8 fps once`.");
                    break;
                }
                const std::string rowText = lower(trim(statement.secondaryExpression));
                const std::string playbackMode = lower(trim(statement.detailExpression.substr(detailSeparator + 1)));
                const bool followsFacing = rowText == "facing";
                int row = -1;
                if (!followsFacing) {
                    try { row = std::stoi(rowText); }
                    catch (...) { row = -1; }
                }
                const double maximumFps = static_cast<double>(std::max(1U, settings_.tickRate));
                if (firstFrame < 0 || lastFrame <= firstFrame || lastFrame >= entity.frameColumns ||
                    (followsFacing ? entity.frameRows < 8 : (row < 0 || row >= entity.frameRows)) ||
                    !std::isfinite(framesPerSecond) || framesPerSecond < 0.1 ||
                    framesPerSecond > maximumFps ||
                    (playbackMode != "once" && playbackMode != "looping")) {
                    runtimeError(statement, "The animation clip is outside the configured sheet or timing limits.",
                        "Frames are inclusive and zero-based; rows are zero-based (or `facing` on an eight-row sheet). Use 0.1 fps up to the engine tick rate, and choose `once` or `looping`.");
                    break;
                }
                AnimationClip clip;
                clip.firstFrame = firstFrame;
                clip.lastFrame = lastFrame;
                clip.row = followsFacing ? -1 : row;
                clip.framesPerSecond = framesPerSecond;
                clip.followsFacing = followsFacing;
                clip.looping = playbackMode == "looping";
                entity.animationClips[clipName] = clip;
                // Redefining the currently selected clip invalidates its old playback
                // and any completion event that might still be queued for it.
                if (entity.currentAnimation == clipName) {
                    entity.currentAnimation.clear();
                    entity.animationPlaying = false;
                    entity.animationElapsedTicks = 0.0;
                    ++entity.animationPlaybackGeneration;
                }
                break;
            }
            case StatementType::PlayAnimation: {
                const std::string id = lower(trim(statement.target));
                const std::string clipName = lower(trim(statement.property));
                auto found = sprites_.find(id);
                if (found == sprites_.end() || !found->second.entity || found->second.projectile) {
                    runtimeError(statement, "There is no ordinary entity named `" + id + "`.",
                        "Create the entity before playing one of its named animation clips.");
                    break;
                }
                SpriteObject& entity = found->second;
                const auto clip = entity.animationClips.find(clipName);
                if (clip == entity.animationClips.end()) {
                    runtimeError(statement, "Entity `" + id + "` has no animation clip named `" + clipName + "`.",
                        "Define the clip first with `define animation \"" + clipName + "\" for entity \"" + id + "\" ...`.");
                    break;
                }
                if (entity.frameWidth <= 0 || entity.frameHeight <= 0 ||
                    clip->second.lastFrame >= entity.frameColumns ||
                    (clip->second.followsFacing ? entity.frameRows < 8 : clip->second.row >= entity.frameRows)) {
                    runtimeError(statement, "The animation clip no longer matches the entity's current sprite sheet.",
                        "Configure the animation sheet first, then define its clips again.");
                    break;
                }
                entity.currentAnimation = clipName;
                entity.animationFrame = clip->second.firstFrame;
                entity.animationRow = clip->second.followsFacing
                    ? std::clamp(entity.facingRow, 0, entity.frameRows - 1) : clip->second.row;
                entity.animationElapsedTicks = 0.0;
                entity.animationPlaying = true;
                ++entity.animationPlaybackGeneration;
                if (entity.animationPlaybackGeneration == 0U) ++entity.animationPlaybackGeneration;
                break;
            }
            case StatementType::StopAnimation: {
                const std::string id = lower(trim(statement.target));
                auto found = sprites_.find(id);
                if (found == sprites_.end() || !found->second.entity || found->second.projectile) {
                    runtimeError(statement, "There is no ordinary entity named `" + id + "`.",
                        "Check the entity name before stopping its animation.");
                    break;
                }
                SpriteObject& entity = found->second;
                entity.currentAnimation.clear();
                entity.animationPlaying = false;
                entity.animationElapsedTicks = 0.0;
                entity.animationFrame = 0;
                entity.animationTicks = 0;
                ++entity.animationPlaybackGeneration;
                break;
            }
            case StatementType::SetEntityProperty: {
                const std::string id = lower(trim(statement.target));
                auto found = sprites_.find(id);
                if (found == sprites_.end() || !found->second.entity) {
                    runtimeError(statement, "There is no entity named `" + id + "`.", "Create it first with create entity \"" + id + "\" with texture {$hero}.");
                    break;
                }
                SpriteObject& entity = found->second;
                const auto property = statement.property;
                if (property == "collision" || property == "avoid_solid_tiles" ||
                    property == "interactable" || property == "blocks_movement") {
                    const bool enabled = lower(trim(statement.expression)) == "true";
                    if (property == "collision") entity.collisionEnabled = enabled;
                    else if (property == "interactable") entity.interactable = enabled;
                    else if (property == "blocks_movement") entity.blocksMovement = enabled;
                    else { entity.avoidSolidTiles = enabled; entity.navigationTiles.clear(); }
                } else if (property == "tween_collision_response") {
                    const auto response = parseTweenCollisionResponse(statement.expression);
                    if (!response) {
                        runtimeError(statement, "Tween collision response must be `ignore`, `stop`, or `slide`.",
                            "Use: set entity \"boulder\" tween collision response to \"stop\".");
                        break;
                    }
                    entity.tweenCollisionResponse = *response;
                } else if (property == "interaction_range") {
                    Value rangeValue = evaluateValue(statement.expression, context, error);
                    const double range = rangeValue.numberOr(std::numeric_limits<double>::quiet_NaN());
                    if (!error.empty() || !std::isfinite(range) || range < 0.0 || range > 1000000.0) {
                        runtimeError(statement, "Interaction range must be a finite number from 0 to 1,000,000 pixels.",
                            "Use a non-negative value such as `set entity \"player\" interaction range to 96`. A range of 0 disables focus selection.");
                        break;
                    }
                    entity.interactionRangePixels = static_cast<float>(range);
                } else if (property == "kind" || property == "team") {
                    const std::string value = lower(trim(statement.expression));
                    if (!validEntityClassification(value)) {
                        runtimeError(statement, "Entity kind/team identifiers must contain 1–40 letters, numbers, underscores or hyphens.",
                            "Examples: set entity \"warden\" kind to \"boss\" and set entity \"warden\" team to \"hostile\".");
                        break;
                    }
                    const std::uint32_t labelId = internEntityLabel(value);
                    if (labelId == 0U) {
                        runtimeError(statement, "The runtime could not allocate an entity classification ID.",
                            "Use a non-empty kind or team identifier and keep the number of unique identifiers reasonable.");
                        break;
                    }
                    if (property == "kind") entity.kindId = labelId;
                    else entity.teamId = labelId;
                } else if (property == "tags") {
                    std::vector<std::uint32_t> tags;
                    if (!trim(statement.expression).empty()) {
                        const auto values = splitTopLevelComma(statement.expression);
                        if (values.size() > 32) {
                            runtimeError(statement, "An entity can have at most 32 tags.",
                                "Use a short comma-separated list such as \"boss, flying, armored\".");
                            break;
                        }
                        bool valid = true;
                        for (const auto& item : values) {
                            const std::string tag = lower(trim(item));
                            if (!validEntityClassification(tag)) { valid = false; break; }
                            const std::uint32_t tagId = internEntityLabel(tag);
                            if (tagId == 0U) { valid = false; break; }
                            if (std::find(tags.begin(), tags.end(), tagId) == tags.end()) tags.push_back(tagId);
                        }
                        if (!valid) {
                            runtimeError(statement, "Entity tags must contain 1–40 letters, numbers, underscores or hyphens.",
                                "Use: set entity \"warden\" tags to \"boss, flying, armored\". Use an empty string to clear all tags.");
                            break;
                        }
                    }
                    entity.tagIds = std::move(tags);
                } else if (property == "collision_layer" || property == "collision_mask" || property == "projectile_collision_mask") {
                    std::string channelError;
                    const auto channels = parseCollisionChannels(statement.expression, channelError);
                    if (!channels) {
                        runtimeError(statement, channelError,
                            "Supported channels: actors, projectiles, props, triggers, effects, pickups, hazards, sensors. Use \"all\" or \"none\" where appropriate.");
                        break;
                    }
                    if (property == "collision_layer" && *channels == 0U) {
                        runtimeError(statement, "An entity collision layer cannot be `none`.",
                            "Choose at least one layer such as \"actors\" or disable collision with `set entity \"id\" collision to false`.");
                        break;
                    }
                    if (property == "collision_layer") entity.collisionLayer = *channels;
                    else if (property == "collision_mask") entity.collisionMask = *channels;
                    else entity.projectileCollisionMask = *channels;
                } else if (property == "hitbox") {
                    const auto split = statement.expression.find('|');
                    if (split == std::string::npos) { runtimeError(statement, "The hitbox syntax is incomplete.", "Use: set entity \"player\" hitbox to (14, 12) offset (9, 18)"); break; }
                    auto parsePair = [&](std::string pair, float& a, float& b) -> bool {
                        pair = trim(pair);
                        if (pair.size() >= 2 && pair.front() == '(' && pair.back() == ')') pair = pair.substr(1, pair.size()-2);
                        const auto parts = splitTopLevelComma(pair);
                        if (parts.size() != 2) return false;
                        try { a = std::stof(parts[0]); b = std::stof(parts[1]); } catch (...) { return false; }
                        return std::isfinite(a) && std::isfinite(b);
                    };
                    float w=0, h=0, ox=0, oy=0;
                    if (!parsePair(statement.expression.substr(0,split),w,h) || !parsePair(statement.expression.substr(split+1),ox,oy) || w <= 0 || h <= 0 || w > 8192 || h > 8192) {
                        runtimeError(statement, "Hitbox dimensions must be positive, finite numbers.", "Use: set entity \"player\" hitbox to (14, 12) offset (9, 18)"); break;
                    }
                    entity.hitboxWidth=w; entity.hitboxHeight=h; entity.hitboxOffsetX=ox; entity.hitboxOffsetY=oy;
                } else if (property == "health" || property == "max_health") {
                    int value=0; try { value=std::stoi(statement.expression); } catch (...) { value=0; }
                    if (value < 0 || value > 1000000) { runtimeError(statement, "Entity health must be between 0 and 1,000,000.", "Set entity \"boss\" health to 12"); break; }
                    if (property == "max_health") { entity.maxHealth=value; entity.health=std::min(entity.health,value); }
                    else { entity.health=value; entity.maxHealth=std::max(entity.maxHealth,value); }
                    globals_["entity::" + id + "::health"] = Value(static_cast<double>(entity.health));
                    globals_["entity::" + id + "::max_health"] = Value(static_cast<double>(entity.maxHealth));
                } else if (property == "animation_sheet") {
                    const auto first=statement.expression.find('|'); const auto second=first==std::string::npos?std::string::npos:statement.expression.find('|',first+1);
                    if (first==std::string::npos || second==std::string::npos) { runtimeError(statement,"Animation-sheet settings are incomplete.","Use: set entity \"player\" animation sheet to (32, 32) columns 4 rows 8"); break; }
                    float fw=0,fh=0; int cols=0,rows=0;
                    std::string pair=statement.expression.substr(0,first); if(pair.size()>1) pair=pair.substr(1,pair.size()-2);
                    const auto xy=splitTopLevelComma(pair);
                    try { if(xy.size()!=2) throw std::runtime_error("pair"); fw=std::stof(xy[0]); fh=std::stof(xy[1]); cols=std::stoi(statement.expression.substr(first+1,second-first-1)); rows=std::stoi(statement.expression.substr(second+1)); } catch (...) { fw=fh=0; }
                    if (fw<1 || fh<1 || cols<1 || rows<1 || cols>64 || rows>64) { runtimeError(statement,"Animation frame dimensions and grid counts must be positive.","Use: set entity \"player\" animation sheet to (32, 32) columns 4 rows 8"); break; }
                    entity.frameWidth=static_cast<int>(fw); entity.frameHeight=static_cast<int>(fh); entity.frameColumns=cols; entity.frameRows=rows; entity.animationFrame=0; entity.animationTicks=0;
                    entity.animationClips.clear();
                    entity.currentAnimation.clear();
                    entity.animationPlaying=false;
                    entity.animationElapsedTicks=0.0;
                    ++entity.animationPlaybackGeneration;
                }
                break;
            }
            case StatementType::SpawnProjectile:
            case StatementType::SpawnProjectilePattern:
                executeProjectileStatement(statement);
                break;
            case StatementType::CreateSprite: {
                const std::string id = lower(trim(statement.target));
                Value textureValue = evaluateValue(statement.expression, context, error);
                if (!error.empty()) {
                    runtimeError(statement, error, "Use an automatically discovered texture such as `create sprite \"player\" with texture {$crate}`.");
                    break;
                }
                const std::string textureId = lower(trim(textureValue.toString()));
                if (!rooms_.textures().find(textureId)) {
                    runtimeError(statement, "No texture named `" + textureId + "` exists.",
                        "Put an image in assets/textures/ and use its filename as a texture variable, for example `{$player}` for player.png.");
                    break;
                }
                SpriteObject sprite;
                sprite.name = id;
                sprite.runtimeInstanceId = ++spriteInstanceCounter_;
                sprite.textureId = textureId;
                sprite.entity = lower(trim(statement.property)) == "entity";
                sprite.kindId = internEntityLabel(sprite.entity ? "entity" : "sprite");
                sprite.teamId = internEntityLabel("neutral");
                sprite.collisionLayer = sprite.entity ? CollisionActors : CollisionProps;
                if (const auto existing = sprites_.find(id); existing != sprites_.end() && existing->second.projectile)
                    --activeProjectileCount_;
                entityTweens_.erase(id);
                sprites_[id] = sprite;
                if (sprite.entity) { globals_["entity::" + id + "::health"] = Value(1); globals_["entity::" + id + "::max_health"] = Value(1); }
                log("Created sprite `" + id + "` using texture `" + textureId + "`.");
                break;
            }
            case StatementType::SetSprite: {
                std::string id;
                std::uint64_t expectedInstanceId = 0;
                const bool usesObjectExpression = statement.target.starts_with("sprite ") ||
                    statement.target.starts_with("entity ") || statement.target.starts_with("object ");
                if (usesObjectExpression) {
                    const Value targetValue = evaluateValue(statement.target, context, error);
                    if (!error.empty()) { runtimeError(statement, error, "Use an object reference such as `sprite \"player\"` or a variable that stores one."); break; }
                    const auto* reference = targetValue.objectReference();
                    if (!reference) { runtimeError(statement, "That value is not a sprite or entity reference.", "Set a local variable first, for example: set {_object} to sprite \"player\"."); break; }
                    id = reference->name;
                    expectedInstanceId = reference->instanceId;
                } else {
                    id = lower(trim(statement.target)); // Preserve the legacy named setter.
                }
                auto found = sprites_.find(id);
                if (found == sprites_.end() || (expectedInstanceId != 0 && found->second.runtimeInstanceId != expectedInstanceId)) {
                    runtimeError(statement, "There is no current sprite/entity object matching `" + id + "`.",
                        "Create the object first, or refresh a local reference after replacing it.");
                    break;
                }
                SpriteObject& sprite = found->second;
                if (statement.property == "x-coordinate" || statement.property == "y-coordinate" || statement.property == "rotation") {
                    Value coordinateValue = evaluateValue(statement.expression, context, error);
                    const double number = coordinateValue.numberOr(std::numeric_limits<double>::quiet_NaN());
                    if (!error.empty() || !coordinateValue.isNumber() || !std::isfinite(number) || std::abs(number) > 10000000.0) {
                        runtimeError(statement, "This sprite property needs a finite number within 10,000,000.",
                            "Use a value such as `15`, `-20`, or `90`.");
                        break;
                    }
                    if (statement.property == "x-coordinate") { entityTweens_.erase(id); sprite.x = static_cast<float>(number); }
                    else if (statement.property == "y-coordinate") { entityTweens_.erase(id); sprite.y = static_cast<float>(number); }
                    else sprite.rotationDegrees = static_cast<float>(number);
                    break;
                }
                if (statement.property == "position" || statement.property == "size") {
                    std::string pairText = trim(statement.expression);
                    if (pairText.size() >= 2 && pairText.front() == '(' && pairText.back() == ')')
                        pairText = trim(std::string_view(pairText).substr(1, pairText.size() - 2));
                    const auto parts = splitTopLevelComma(pairText);
                    if (parts.size() != 2) {
                        runtimeError(statement, "Sprite coordinates must contain exactly two values.",
                            "Use a pair such as `(640, 360)` for position or `(48, 48)` for size.");
                        break;
                    }
                    Value first = evaluateValue(parts[0], context, error);
                    if (!error.empty()) { runtimeError(statement, error, "Both coordinate values must be numeric expressions."); break; }
                    Value second = evaluateValue(parts[1], context, error);
                    if (!error.empty()) { runtimeError(statement, error, "Both coordinate values must be numeric expressions."); break; }
                    const double a = first.numberOr(std::numeric_limits<double>::quiet_NaN());
                    const double b = second.numberOr(std::numeric_limits<double>::quiet_NaN());
                    if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a) > 10000000.0 || std::abs(b) > 10000000.0) {
                        runtimeError(statement, "Sprite position/size values must be finite numbers within 10,000,000 pixels.",
                            "Use values such as `(640, 360)` or `(48, 48)`. Sizes must also be positive.");
                        break;
                    }
                    if (statement.property == "size") {
                        if (a < 1.0 || b < 1.0 || a > 8192.0 || b > 8192.0) {
                            runtimeError(statement, "Sprite size must be between 1 and 8192 pixels on each axis.",
                                "Use a size such as `(32, 32)` or `(48, 48)`.");
                            break;
                        }
                        sprite.width = static_cast<float>(a);
                        sprite.height = static_cast<float>(b);
                    } else {
                        entityTweens_.erase(id);
                        sprite.x = static_cast<float>(a);
                        sprite.y = static_cast<float>(b);
                    }
                } else if (statement.property == "speed") {
                    Value speedValue = evaluateValue(statement.expression, context, error);
                    const double speed = speedValue.numberOr(std::numeric_limits<double>::quiet_NaN());
                    if (!error.empty() || !std::isfinite(speed) || speed < 0.0 || speed > 100000.0) runtimeError(statement, "Entity speed must be a non-negative finite value.", "Use: set entity \"npc\" speed to 150");
                    else sprite.moveSpeedPixelsPerSecond = static_cast<float>(speed);
                } else if (statement.property == "texture") {
                    Value textureValue = evaluateValue(statement.expression, context, error);
                    if (!error.empty()) { runtimeError(statement, error, "Use a texture variable like `{$crate}`."); break; }
                    const std::string textureId = lower(trim(textureValue.toString()));
                    if (!rooms_.textures().find(textureId)) {
                        runtimeError(statement, "No texture named `" + textureId + "` exists.",
                            "Place the image in assets/textures/ or check its filename.");
                        break;
                    }
                    sprite.textureId = textureId;
                } else if (statement.property == "visible") {
                    const Value visibleValue = evaluateValue(statement.expression, context, error);
                    if (!error.empty() || !visibleValue.isBool()) {
                        runtimeError(statement, "Sprite visibility must evaluate to true or false.",
                            "Use: set visibility of sprite \"player\" to false.");
                    } else sprite.visible = visibleValue.truthy();
                }
                break;
            }
            case StatementType::MoveEntityInput: {
                const std::string id=lower(trim(statement.target));
                auto found=sprites_.find(id);
                if(found==sprites_.end() || !found->second.entity) { runtimeError(statement,"There is no entity named `"+id+"`.","Create it first using create entity \""+id+"\" with texture {$hero}."); break; }
                float speed=0.0F; try { speed=std::stof(statement.expression); } catch (...) { speed=0.0F; }
                if(!std::isfinite(speed)||speed<0.0F||speed>100000.0F) { runtimeError(statement,"Input movement speed must be between 0 and 100,000 pixels per second.","Use: move entity \"player\" using WASD at speed 170"); break; }
                float dx=0.0F,dy=0.0F;
                if(statement.property=="wasd") {
                    if (keyDown("A")) dx -= 1.0F;
                    if (keyDown("D")) dx += 1.0F;
                    if (keyDown("W")) dy -= 1.0F;
                    if (keyDown("S")) dy += 1.0F;
                } else {
                    if (keyDown("LEFT")) dx -= 1.0F;
                    if (keyDown("RIGHT")) dx += 1.0F;
                    if (keyDown("UP")) dy -= 1.0F;
                    if (keyDown("DOWN")) dy += 1.0F;
                }
                const float len=std::sqrt(dx*dx+dy*dy);
                if(len>0.001F) {
                    entityTweens_.erase(id);
                    dx=dx/len*speed/static_cast<float>(std::max(1U,settings_.tickRate));
                    dy=dy/len*speed/static_cast<float>(std::max(1U,settings_.tickRate));
                    auto& entity=found->second;
                    entity.moving=true;
                    if(std::abs(dx)>0.001F&&std::abs(dy)>0.001F) entity.facingRow=(dx<0?(dy<0?3:1):(dy<0?5:7));
                    else if(std::abs(dx)>0.001F) entity.facingRow=dx<0?2:6;
                    else entity.facingRow=dy<0?4:0;
                    if(entity.avoidSolidTiles) {
                        if(!positionHitsSolidTile(entity,entity.x+dx,entity.y)) entity.x+=dx;
                        if(!positionHitsSolidTile(entity,entity.x,entity.y+dy)) entity.y+=dy;
                    } else { entity.x+=dx; entity.y+=dy; }
                }
                break;
            }
            case StatementType::MoveSprite: {
                const std::string id = lower(trim(statement.target));
                auto found = sprites_.find(id);
                if (found == sprites_.end()) {
                    runtimeError(statement, "There is no sprite named `" + id + "`.",
                        "Create it first with `create sprite \"" + id + "\" with texture {$crate}`.");
                    break;
                }
                std::string pairText = trim(statement.expression);
                if (pairText.size() >= 2 && pairText.front() == '(' && pairText.back() == ')')
                    pairText = trim(std::string_view(pairText).substr(1, pairText.size() - 2));
                const auto parts = splitTopLevelComma(pairText);
                if (parts.size() != 2) {
                    runtimeError(statement, "Sprite movement must contain two values.", "Use: move sprite \"player\" by (0, -3)");
                    break;
                }
                Value dxValue = evaluateValue(parts[0], context, error);
                if (!error.empty()) { runtimeError(statement, error, "Movement values must be numeric expressions."); break; }
                Value dyValue = evaluateValue(parts[1], context, error);
                if (!error.empty()) { runtimeError(statement, error, "Movement values must be numeric expressions."); break; }
                const double dx = dxValue.numberOr(std::numeric_limits<double>::quiet_NaN());
                const double dy = dyValue.numberOr(std::numeric_limits<double>::quiet_NaN());
                if (!std::isfinite(dx) || !std::isfinite(dy) || std::abs(dx) > 100000.0 || std::abs(dy) > 100000.0) {
                    runtimeError(statement, "Sprite movement requires finite deltas within 100,000 pixels per command.",
                        "Use small values in `on tick:`, e.g. `(0, -3)` for smooth movement at 60 ticks per second.");
                    break;
                }
                SpriteObject& moved = found->second;
                const float fx=static_cast<float>(dx), fy=static_cast<float>(dy);
                if (std::abs(fx)>0.001F || std::abs(fy)>0.001F) {
                    entityTweens_.erase(id);
                    moved.moving=true;
                    if (std::abs(fx)>0.001F && std::abs(fy)>0.001F) moved.facingRow = (fx<0 ? (fy<0?3:1) : (fy<0?5:7));
                    else if (std::abs(fx)>0.001F) moved.facingRow = fx<0?2:6;
                    else moved.facingRow = fy<0?4:0;
                }
                if (moved.entity && moved.avoidSolidTiles) {
                    if (!positionHitsSolidTile(moved, moved.x+fx, moved.y)) moved.x+=fx;
                    if (!positionHitsSolidTile(moved, moved.x, moved.y+fy)) moved.y+=fy;
                } else { moved.x += fx; moved.y += fy; }
                break;
            }
            case StatementType::DeleteSprite: {
                const std::string id = lower(trim(statement.target));
                const auto existing = sprites_.find(id);
                if (existing == sprites_.end()) {
                    runtimeError(statement, "There is no sprite named `" + id + "`.",
                        "Check the name or create the sprite before deleting it.");
                } else {
                    entityTweens_.erase(id);
                    if (existing->second.projectile) --activeProjectileCount_;
                    sprites_.erase(existing);
                    log("Deleted sprite `" + id + "`.");
                }
                break;
            }
            case StatementType::SeedRandomStream: {
                const Value streamValue = evaluateValue(statement.target, context, error);
                if (!error.empty()) {
                    runtimeError(statement, error, "Use a quoted stream name, for example: seed random stream \"loot\" with 12345.");
                    break;
                }
                if (!streamValue.isString()) {
                    runtimeError(statement, "A random stream name must be text.",
                                 "Use a stable purpose name such as \"encounters\" or \"loot\".");
                    break;
                }
                const std::string streamName = lower(trim(streamValue.toString()));
                if (streamName.empty() || streamName.size() > 64) {
                    runtimeError(statement, "A random stream name must contain 1 to 64 characters.",
                                 "Use a short stable name, for example: seed random stream \"arena_loot\" with 4815.");
                    break;
                }
                const Value seedValue = evaluateValue(statement.expression, context, error);
                if (!error.empty()) {
                    runtimeError(statement, error, "The seed must evaluate to a whole number in the safe-integer range.");
                    break;
                }
                constexpr double maximumSafeInteger = 9007199254740991.0;
                const double seedNumber = seedValue.numberOr(std::numeric_limits<double>::quiet_NaN());
                if (!seedValue.isNumber() || !std::isfinite(seedNumber) ||
                    std::trunc(seedNumber) != seedNumber || std::abs(seedNumber) > maximumSafeInteger) {
                    runtimeError(statement, "A random stream seed must be a whole number from -9007199254740991 to 9007199254740991.",
                                 "Example: seed random stream \"loot\" with 48151623.");
                    break;
                }
                // Signed values are converted modulo 2^64, giving negative seeds a
                // deterministic and well-defined bit pattern as well.
                const auto seed = static_cast<std::uint64_t>(static_cast<std::int64_t>(seedNumber));
                randomStreams_[streamName] = RandomStreamState{seed};
                break;
            }
            case StatementType::Set: {
                const std::string target = resolveVariableName(statement.target, context);
                if (isListWildcard(target)) {
                    const auto sourceName = resolveVariableName(statement.expression, context);
                    std::vector<ListEntry> sourceEntries;
                    const bool copyingList = isListWildcard(sourceName);
                    if (copyingList) sourceEntries = listEntries(sourceName, context); // snapshot before clearing, including self-copy
                    clearList(target, context, &statement);
                    if (copyingList) {
                        for (const auto& entry : sourceEntries) setVariable(target.substr(0, target.size() - 1) + entry.index, entry.value, context, &statement);
                    } else if (lower(trim(statement.expression)) != "none" && lower(trim(statement.expression)) != "null") {
                        for (const auto& part : splitTopLevelComma(statement.expression)) {
                            if (part.empty()) { runtimeError(statement, "A list literal contains an empty item.", "Use `set {items::*} to \"first\", \"second\"` with no trailing comma."); break; }
                            Value value = evaluateValue(part, context, error);
                            if (!error.empty()) { runtimeError(statement, error, "List items use the same value syntax as ordinary variables."); break; }
                            appendListValue(target, std::move(value), context, &statement);
                        }
                    }
                } else {
                    Value value = evaluateValue(statement.expression, context, error);
                    if (!error.empty()) runtimeError(statement, error, "Use a literal such as 10, true, false, \"text\", a list item, or a variable such as {score}.");
                    else setVariable(statement.target, std::move(value), context, &statement);
                }
                break;
            }
            case StatementType::DeleteVariable: {
                const std::string target = resolveVariableName(statement.target, context);
                if (isListWildcard(target)) { clearList(target, context, &statement); break; }
                if (target.starts_with("$" ) || target.starts_with("@")) {
                    runtimeError(statement, "Automatic texture/audio variables cannot be deleted from scripts.", "Remove or rename the corresponding asset file instead.");
                    break;
                }
                if (target.starts_with("_")) context.locals.erase(target);
                else if (target.starts_with("^")) transient_.erase(target.substr(1));
                else { globals_.erase(target); variableStorageDirty_ = true; }
                break;
            }
            case StatementType::Add:
            case StatementType::Subtract: {
                const std::string target = resolveVariableName(statement.target, context);
                Value amount = evaluateValue(statement.expression, context, error);
                if (!error.empty()) { runtimeError(statement, error, "Use a number or a value, for example: add 1 to {score} or add \"potion\" to {inventory::*}"); break; }
                if (isListWildcard(target)) {
                    if (statement.type == StatementType::Add) appendListValue(target, std::move(amount), context, &statement);
                    else {
                        // remove X from a list removes the first matching item, matching
                        // Skript's default. "remove all X from a list" explicitly removes
                        // every equal typed value from the snapshot.
                        const auto entries = listEntries(target, context);
                        bool removedOne = false;
                        for (const auto& entry : entries) {
                            if (!sameValue(entry.value, amount)) continue;
                            const std::string itemName = target.substr(0, target.size() - 1) + entry.index;
                            if (itemName.starts_with("_")) context.locals.erase(itemName);
                            else if (itemName.starts_with("^")) transient_.erase(itemName.substr(1));
                            else { globals_.erase(itemName); variableStorageDirty_ = true; }
                            if (statement.property != "all") { removedOne = true; break; }
                        }
                        (void)removedOne;
                    }
                    break;
                }
                Value current = getVariable(statement.target, context);
                double currentNumber = 0.0;
                if (!current.isNull()) {
                    currentNumber = current.numberOr(std::numeric_limits<double>::quiet_NaN());
                    if (!std::isfinite(currentNumber)) {
                        runtimeError(statement, "Variable {" + statement.target + "} does not contain a number.",
                                     "Set it to a number first, for example: set {" + statement.target + "} to 0");
                        break;
                    }
                }
                const double amountNumber = amount.numberOr(std::numeric_limits<double>::quiet_NaN());
                if (!std::isfinite(amountNumber)) {
                    runtimeError(statement, "The amount must be a number, but got `" + amount.toString() + "`.",
                                 "Example: add 1 to {score} or add {bonus} to {score}");
                    break;
                }
                const double next = statement.type == StatementType::Add ? currentNumber + amountNumber : currentNumber - amountNumber;
                setVariable(statement.target, Value(next), context, &statement);
                break;
            }
            case StatementType::Toggle: {
                const Value current = getVariable(statement.target, context);
                setVariable(statement.target, Value(!current.truthy()), context, &statement);
                break;
            }
            case StatementType::CreateWave: {
                double duration = 0.0;
                unsigned int sampleRate = 0;
                try {
                    duration = std::stod(statement.expression);
                    const auto parsed = std::stoul(statement.property);
                    if (parsed > 192000UL) throw std::out_of_range("sample rate");
                    sampleRate = static_cast<unsigned int>(parsed);
                } catch (...) {
                    runtimeError(statement, "Wave duration or sample rate is invalid.",
                                 "Example: create wave \"chime\" duration 0.25 sample_rate 44100");
                    break;
                }
                if (!audio_.createWave(statement.target, duration, sampleRate, error)) runtimeError(statement, error,
                    "Duration is 0–60 seconds and sample_rate is 8000–192000. Small sound effects usually use 22050 or 44100 Hz.");
                else log("Created wave `" + statement.target + "` (" + statement.expression + " s, " + statement.property + " Hz).");
                break;
            }
            case StatementType::AddWaveLayer: {
                static const std::regex layerArgs(R"ARGS(^(?:frequency\s+([-+]?(?:\d+(?:\.\d*)?|\.\d+))\s+)?amplitude\s+([-+]?(?:\d+(?:\.\d*)?|\.\d+))(?:\s+phase\s+([-+]?(?:\d+(?:\.\d*)?|\.\d+)))?(?:\s+seed\s+(\d+))?\s*$)ARGS", std::regex::icase);
                std::smatch match;
                if (!std::regex_match(statement.expression, match, layerArgs)) {
                    runtimeError(statement, "Wave layer parameters could not be read.",
                        "Use `add sine wave to \"chime\" frequency 440 amplitude 0.5 phase 0` or `add noise wave to \"wind\" amplitude 0.15 seed 42`.");
                    break;
                }
                WaveLayer layer;
                const auto kind = lower(statement.property);
                if (kind == "sine") layer.kind = WaveformKind::Sine;
                else if (kind == "square") layer.kind = WaveformKind::Square;
                else if (kind == "triangle") layer.kind = WaveformKind::Triangle;
                else if (kind == "saw") layer.kind = WaveformKind::Saw;
                else layer.kind = WaveformKind::Noise;
                try {
                    if (match[1].matched) layer.frequency = std::stod(match[1].str());
                    else if (layer.kind != WaveformKind::Noise) throw std::invalid_argument("frequency");
                    layer.amplitude = std::stod(match[2].str());
                    if (match[3].matched) layer.phaseRadians = std::stod(match[3].str());
                    if (match[4].matched) layer.seed = static_cast<std::uint32_t>(std::stoul(match[4].str()));
                } catch (...) {
                    runtimeError(statement, "Wave layer is missing a required number.",
                        "Sine, square, triangle, and saw require frequency and amplitude. Noise requires amplitude and can accept seed.");
                    break;
                }
                if (!audio_.addLayer(statement.target, layer, error)) runtimeError(statement, error,
                    "Make the wave first. Example: add sine wave to \"chime\" frequency 440 amplitude 0.5");
                else log("Added " + kind + " layer to wave `" + statement.target + "`.");
                break;
            }
            case StatementType::SetWaveEnvelope: {
                std::array<double, 4> values{};
                std::stringstream input(statement.expression);
                std::string part;
                bool valid = true;
                for (std::size_t i = 0; i < values.size(); ++i) {
                    if (!std::getline(input, part, ',')) { valid = false; break; }
                    try { values[i] = std::stod(trim(part)); } catch (...) { valid = false; break; }
                }
                if (!valid) { runtimeError(statement, "Wave envelope parameters are invalid.", "Use: set wave \"chime\" envelope attack 0.01 decay 0.08 sustain 0.4 release 0.12"); break; }
                WaveEnvelope envelope{values[0], values[1], values[2], values[3]};
                if (!audio_.setEnvelope(statement.target, envelope, error)) runtimeError(statement, error,
                    "Envelope attack/decay/release are seconds from 0–10; sustain is 0–1.");
                else log("Updated envelope for wave `" + statement.target + "`.");
                break;
            }
            case StatementType::NormalizeWave: {
                if (!audio_.normalize(statement.target, error)) runtimeError(statement, error, "Create and add layers to the wave first.");
                else log("Normalized wave `" + statement.target + "`.");
                break;
            }
            case StatementType::SaveWave: {
                std::filesystem::path outputPath(statement.expression);
                if (outputPath.is_relative()) outputPath = projectRoot_ / outputPath;
                std::filesystem::path writePath = outputPath;
                if (stagingLoad_ && !makeStagedOutputPath(outputPath, writePath, error)) {
                    runtimeError(statement, error, "The previous runtime has been kept. Check that the system temporary directory is writable.");
                    break;
                }
                if (!audio_.saveWav(statement.target, writePath, error)) {
                    if (stagingLoad_) { std::error_code ec; std::filesystem::remove(writePath, ec); }
                    runtimeError(statement, error,
                    "Use `save wave \"chime\" to \"assets/audio/chime.wav\"` after creating the wave.");
                } else if (stagingLoad_) {
                    stagedOutputFiles_.push_back({writePath, outputPath});
                    log("Prepared WAV audio for `" + outputPath.string() + "`.");
                } else log("Saved WAV audio to `" + outputPath.string() + "`.");
                break;
            }
            case StatementType::VisualizeWave: {
                std::filesystem::path outputPath(statement.expression);
                if (outputPath.is_relative()) outputPath = projectRoot_ / outputPath;
                std::filesystem::path writePath = outputPath;
                if (stagingLoad_ && !makeStagedOutputPath(outputPath, writePath, error)) {
                    runtimeError(statement, error, "The previous runtime has been kept. Check that the system temporary directory is writable.");
                    break;
                }
                if (!audio_.saveSvg(statement.target, writePath, error)) {
                    if (stagingLoad_) { std::error_code ec; std::filesystem::remove(writePath, ec); }
                    runtimeError(statement, error,
                    "Use `visualize wave \"chime\" to \"assets/audio/chime.svg\"` to generate an offline waveform graph.");
                } else if (stagingLoad_) {
                    stagedOutputFiles_.push_back({writePath, outputPath});
                    log("Prepared waveform preview for `" + outputPath.string() + "`.");
                } else log("Saved waveform preview to `" + outputPath.string() + "`.");
                break;
            }
            case StatementType::PlaySound: {
                const Value soundValue = evaluateValue(statement.expression, context, error);
                if (!error.empty()) { runtimeError(statement, error, "Use `play sound \"click\"`, `play sound {sound_id}`, or a path to a .wav/.ogg file."); break; }
                const std::string soundName = soundValue.toString();
                if (!audio_.find(soundName)) {
                    std::filesystem::path audioPath(soundName);
                    if (audioPath.is_relative()) audioPath = projectRoot_ / audioPath;
                    const auto extension = lower(audioPath.extension().string());
                    std::error_code ec;
                    if (!std::filesystem::is_regular_file(audioPath, ec) || ec ||
                        (extension != ".wav" && extension != ".ogg" && extension != ".flac" && extension != ".aiff" && extension != ".aif")) {
                        runtimeError(statement, "No generated wave or supported audio file named `" + soundName + "` was found.",
                            "Create the wave first, or import a supported .wav/.ogg/.flac/.aiff file. Runtime audio playback requires the graphical audio backend.");
                        break;
                    }
                    if (!audio_.requestPlay(audioPath.string(), error)) runtimeError(statement, error);
                    else log("Queued audio file `" + audioPath.filename().string() + "`.");
                } else {
                    if (!audio_.requestPlay(soundName, error)) runtimeError(statement, error);
                    else log("Played generated sound `" + soundName + "` (when the graphical audio backend is available).");
                }
                break;
            }
            case StatementType::Broadcast: {
                Value value = evaluateValue(statement.expression, context, error);
                if (!error.empty()) runtimeError(statement, error, "Example: broadcast \"Hello!\"");
                else log(value.toString());
                break;
            }
            case StatementType::If: {
                bool result = false;
                if (!evaluateCondition(statement.expression, context, result, error)) {
                    runtimeError(statement, error, "Use conditions like `if {score} > 10:` or `if {paused} is true:`.");
                    break;
                }
                const auto& branch = result ? statement.body : statement.elseBody;
                if (!branch.empty()) context.frames.push_back({&branch, 0, 1, 1, false, false, nullptr, false, {}});
                break;
            }
            case StatementType::LoopTimes: {
                const Value countValue = evaluateValue(statement.expression, context, error);
                if (!error.empty()) {
                    runtimeError(statement, error, "Use `loop 5 times:` or `loop {_count} times:` with a whole-number count.");
                    break;
                }
                const double count = countValue.numberOr(std::numeric_limits<double>::quiet_NaN());
                if (!std::isfinite(count) || count < 0.0 || std::floor(count) != count ||
                    count > static_cast<double>(maximumCountedLoopIterations_)) {
                    runtimeError(statement, "The loop count must be a whole number from 0 to 1,000,000.",
                        "Example: loop 5 times:. A count of 0 skips the body.");
                    break;
                }
                const auto repetitions = static_cast<std::size_t>(count);
                if (repetitions > 0 && !statement.body.empty()) {
                    context.loopNumbers.push_back(1);
                    context.loopValues.emplace_back(1.0);
                    context.loopIndexes.push_back("1");
                    context.frames.push_back({&statement.body, 0, repetitions, 1, true, false, &statement, false, {}});
                }
                break;
            }
            case StatementType::LoopList: {
                const std::string wildcard = resolveVariableName(statement.expression, context);
                if (!isListWildcard(wildcard)) {
                    runtimeError(statement, "A list loop needs a variable ending in `::*`.", "Use `loop {inventory::*}:`.");
                    break;
                }
                auto entries = listEntries(wildcard, context);
                if (!entries.empty() && !statement.body.empty()) {
                    context.loopNumbers.push_back(1);
                    context.loopValues.push_back(entries.front().value);
                    context.loopIndexes.push_back(entries.front().index);
                    ExecutionFrame frame{&statement.body, 0, entries.size(), 1, true, false, &statement, true, entries};
                    frame.listLoop = true;
                    frame.listEntries = std::move(entries);
                    context.frames.push_back(std::move(frame));
                }
                break;
            }
            case StatementType::While: {
                bool conditionResult = false;
                if (!evaluateCondition(statement.expression, context, conditionResult, error)) {
                    runtimeError(statement, error, "Use a valid condition such as `while {_count} > 0:`.");
                    break;
                }
                if (conditionResult && !statement.body.empty()) {
                    context.loopNumbers.push_back(1);
                    context.loopValues.emplace_back(1.0);
                    context.loopIndexes.push_back("1");
                    context.frames.push_back({&statement.body, 0, maximumCountedLoopIterations_, 1, true, true, &statement, false, {}});
                }
                break;
            }
            case StatementType::Wait: {
                if (!context.allowSuspend) {
                    runtimeError(statement, "A wait cannot suspend a one-line terminal command.",
                        "Put `wait 1 second` inside an event or a script block so the runtime can resume it safely.");
                    break;
                }
                const Value durationValue = evaluateValue(statement.expression, context, error);
                if (!error.empty()) {
                    runtimeError(statement, error, "Use `wait 1 second`, `wait 250 milliseconds`, or `wait 1 tick`.");
                    break;
                }
                const double amount = durationValue.numberOr(std::numeric_limits<double>::quiet_NaN());
                if (!std::isfinite(amount) || amount < 0.0 || amount > 604800.0) {
                    runtimeError(statement, "Wait duration must be a finite non-negative number no larger than 7 days.",
                        "Examples: `wait 0.5 seconds`, `wait 250 milliseconds`, `wait 1 tick`.");
                    break;
                }
                const std::string unit = lower(statement.property);
                double seconds = amount;
                if (unit == "ms" || unit.starts_with("millisecond") || unit.starts_with("msec")) seconds *= 0.001;
                else if (unit == "m" || unit.starts_with("min")) seconds *= 60.0;
                else if (unit == "h" || unit.starts_with("hour") || unit.starts_with("hr")) seconds *= 3600.0;
                else if (unit == "t" || unit.starts_with("tick")) seconds *= 1.0 / static_cast<double>(std::max(1U, settings_.tickRate));
                context.wakeAt = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
                // Bind this continuation to the scene at the point where the wait
                // suspends. If this handler entered/reloaded a room earlier in the
                // same run, it naturally records the new context here.
                context.roomContextRevision = roomContextRevision_;
                context.suspended = true;
                break;
            }
            case StatementType::ExitLoop: {
                std::size_t loopFrameIndex = context.frames.size();
                for (std::size_t i = context.frames.size(); i > 0; --i) {
                    if (context.frames[i - 1].loopFrame) { loopFrameIndex = i - 1; break; }
                }
                if (loopFrameIndex == context.frames.size()) {
                    runtimeError(statement, "`exit loop` was used outside a loop.",
                        "Place it inside `loop N times:` or `while condition:`.");
                    break;
                }
                while (context.frames.size() > loopFrameIndex) {
                    if (context.frames.back().loopFrame) {
                        if (!context.loopNumbers.empty()) context.loopNumbers.pop_back();
                        if (!context.loopValues.empty()) context.loopValues.pop_back();
                        if (!context.loopIndexes.empty()) context.loopIndexes.pop_back();
                    }
                    context.frames.pop_back();
                }
                break;
            }
            case StatementType::ContinueLoop: {
                std::size_t loopFrameIndex = context.frames.size();
                for (std::size_t i = context.frames.size(); i > 0; --i) {
                    if (context.frames[i - 1].loopFrame) { loopFrameIndex = i - 1; break; }
                }
                if (loopFrameIndex == context.frames.size()) {
                    runtimeError(statement, "`continue loop` was used outside a loop.",
                        "Place it inside `loop N times:` or `while condition:`.");
                    break;
                }
                while (context.frames.size() > loopFrameIndex + 1) context.frames.pop_back();
                auto& loopFrame = context.frames[loopFrameIndex];
                if (loopFrame.statements) loopFrame.nextIndex = loopFrame.statements->size();
                break;
            }
            case StatementType::OpenCanvas: {
                const auto* canvas = findCanvas(statement.target);
                if (!canvas) runtimeError(statement, "There is no canvas named `" + statement.target + "`.",
                                          "Add `canvas \"" + statement.target + "\":` to a .cx file, or check the spelling.");
                else openCanvases_.insert(lower(canvas->name));
                break;
            }
            case StatementType::CloseCanvas: {
                openCanvases_.erase(lower(statement.target));
                break;
            }
            case StatementType::Return: {
                if (functionCallDepth_ == 0) {
                    runtimeError(statement, "`return` can only be used inside a function.", "Define a function with `function name(parameter):` before using return.");
                    break;
                }
                context.returnValue = evaluateValue(statement.expression, context, error);
                if (!error.empty()) { runtimeError(statement, error, "Use `return value` or `return none`."); break; }
                context.returned = true;
                context.stopped = true;
                break;
            }
            case StatementType::Stop:
                context.stopped = true;
                break;
        }
        if (context.suspended) break; // Yield until a later tick; do not sleep/block the render thread.
        if (hasErrors()) {
            // Stop this event after a runtime failure so one bad value does not cause a cascade.
            context.stopped = true;
        }
    }
    if (context.stopped) {
        context.frames.clear();
        context.loopNumbers.clear();
        context.loopValues.clear();
        context.loopIndexes.clear();
    }
}

// Convert a script expression such as `{score} * 2` into a typed Value. This evaluator
// intentionally works on source strings instead of a compiled machine-code form, which
// keeps the language implementation small and allows runtime-aware values/functions.

} // namespace cx
