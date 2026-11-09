#include "cx/runtime.hpp"
#include "cx/diagnostics.hpp"
#include "cx/script.hpp"
#include "cx/value.hpp"
#include "cx/audio.hpp"
#include "cx/text_format.hpp"
#include "cx/variable_storage.hpp"

#include <filesystem>
#include <array>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <thread>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void testValueConversions() {
    expect(cx::Value::fromString("true").truthy(), "true should parse as bool");
    expect(!cx::Value::fromString("false").truthy(), "false should parse as bool");
    expect(cx::Value::fromString("42").numberOr() == 42.0, "42 should parse as number");
    expect(cx::Value(5).toString() == "5", "integer values should not print a decimal");
}

void testParserAndLocalVariables() {
    const std::string script = R"CX(
set {score} to 0
on press key "SPACE":
    add 1 to {score}
    set {_temp} to {score}
    broadcast "local={_temp}"
    if {score} >= 2:
        broadcast "two or more"
)CX";
    cx::ScriptParser parser;
    const auto program = parser.parseText(script, "<memory>");
    expect(!cx::hasErrors(program.diagnostics), "valid test script should parse without errors");
    expect(program.handlers.size() == 1, "one event handler should parse");

    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-project";
    std::filesystem::create_directories(root / "scripts");
    {
        std::ofstream file(root / "scripts" / "main.cx");
        file << script;
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "test script should load");
    runtime.pressKey("SPACE");
    expect(runtime.getGlobal("score").numberOr() == 1.0, "global score should increment on the first keypress");
    expect(runtime.getGlobal("_temp").isNull(), "event-local variable must not leak into globals");
    runtime.pressKey("SPACE");
    expect(runtime.getGlobal("score").numberOr() == 2.0, "global score should persist between event calls");
    expect(runtime.getGlobal("_temp").isNull(), "event-local variable must remain private after multiple event calls");
    std::filesystem::remove_all(root);
}

void testSyntaxSuggestions() {
    cx::ScriptParser parser;
    const auto program = parser.parseText("on pres key \"SPACE\":\n    broadcast \"hello\"\n", "<memory>");
    expect(cx::hasErrors(program.diagnostics), "misspelled event keyword should be a syntax error");
    expect(!program.diagnostics.front().suggestion.empty(), "syntax error should include a correction hint");

    const auto malformed = parser.parseText("set {score} 10\n", "<memory>");
    expect(cx::hasErrors(malformed.diagnostics), "missing `to` should be a syntax error");
    expect(malformed.diagnostics.front().suggestion.find("set {score} to 10") != std::string::npos,
           "set syntax error should show the correct command shape");

    const auto badComparison = parser.parseText("on load:\n    if {score} greater than 10:\n        broadcast \"oops\"\n", "<memory>");
    expect(cx::hasErrors(badComparison.diagnostics), "a comparison missing `is` should be rejected");
    expect(badComparison.diagnostics.front().column == 12, "comparison caret should point at `greater`, not the start of the condition");

    const auto leakedLocal = parser.parseText("set {score} to {_temporary}\n", "<memory>");
    expect(cx::hasErrors(leakedLocal.diagnostics), "an event-local read at top level should be rejected");
    expect(leakedLocal.diagnostics.front().message.find("only available while an event is running") != std::string::npos,
           "local-scope errors should explain the variable lifetime rule");
}

void testFriendlyTerminalDiagnostics() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-diagnostics";
    std::filesystem::create_directories(root / "scripts");
    const auto scriptPath = root / "scripts" / "main.cx";
    {
        std::ofstream file(scriptPath);
        file << "on press key \"SPACE\":\n    set {score} 10\n";
    }
    cx::ScriptParser parser;
    const auto program = parser.parseFile(scriptPath);
    expect(cx::hasErrors(program.diagnostics), "malformed nested command should create an error");

    std::ostringstream captured;
    auto* originalBuffer = std::cerr.rdbuf(captured.rdbuf());
    cx::printDiagnostics(program.diagnostics, root);
    std::cerr.rdbuf(originalBuffer);
    const auto output = captured.str();
    expect(output.find("scripts/main.cx:2") != std::string::npos, "diagnostic should show a relative path and line number");
    expect(output.find("set {score} 10") != std::string::npos, "diagnostic should show the offending source line");
    expect(output.find("HOW TO FIX:") != std::string::npos, "diagnostic should include a correction hint");
    expect(output.find("    |     ^") != std::string::npos, "caret should align past indentation inside an event block");
    std::filesystem::remove_all(root);
}

void testConfigAndScriptOverride() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-config";
    std::filesystem::create_directories(root / "scripts");
    {
        std::ofstream file(root / "config.json");
        file << "{\n  \"target_fps\": 30,\n  \"vsync\": false,\n  \"window_width\": 1024,\n  \"texture_filter\": \"nearest\",\n  \"ambient_light\": 0.55,\n  \"lighting_quality\": 3\n}\n";
    }
    {
        std::ofstream file(root / "scripts" / "main.cx");
        file << "set engine::target_fps to 90\non load:\n    broadcast \"Config applied\"\n";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "valid config and script should load");
    expect(runtime.settings().targetFps == 90U, "script settings should override config.json");
    expect(!runtime.settings().vsync, "config.json boolean should be applied");
    expect(runtime.settings().windowWidth == 1024U, "config.json window width should be applied");
    expect(runtime.settings().textureFilter == "nearest", "nearest texture filtering should be loaded from config");
    expect(std::abs(runtime.settings().ambientLight - 0.55F) < 0.001F, "ambient lighting should be loaded from config");
    expect(runtime.settings().lightingQuality == 3U, "lighting quality should be loaded from config");
    std::filesystem::remove_all(root);
}

void testRoomsTexturesAndEntrances() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-rooms";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "textures");
    std::filesystem::create_directories(root / "rooms");
    // The registry intentionally discovers IDs from file names without needing a graphics library.
    std::ofstream(root / "assets" / "textures" / "floor.png").put('x');
    std::ofstream(root / "assets" / "textures" / "wall_top.png").put('x');
    const auto writeRoom = [&](const std::string& name, int width, int height, const std::string& entranceName) {
        std::ofstream file(root / "rooms" / (name + ".room.json"));
        file << "{\n  \"schema\": \"cx-room\", \"version\": 1, \"name\": \"" << name
             << "\", \"width\": " << width << ", \"height\": " << height << ", \"tileSize\": 16,\n  \"tiles\": [\n";
        for (int y = 0; y < height; ++y) {
            file << "    [";
            for (int x = 0; x < width; ++x) {
                if (x) file << ", ";
                file << (x == 0 || y == 0 ? "\"wall_top\"" : "\"floor\"");
            }
            file << "]" << (y + 1 == height ? "\n" : ",\n");
        }
        file << "  ],\n  \"entrances\": { \"" << entranceName << "\": { \"x\": 1, \"y\": 1 } }\n}\n";
    };
    writeRoom("hall", 4, 3, "spawn");
    writeRoom("cave", 5, 4, "return");
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
load room "hall"
load room "cave"
set {entrance1::hall} to an entrance point at (1, 1)
set {entrance1::cave} to an entrance point at (2, 2)
on load:
    enter room "hall" at {entrance1::hall}
on press key "C":
    enter room "cave" at {entrance1::cave}
on press key "H":
    enter room "hall" at {entrance1::hall}
on press key "T":
    broadcast "texture={$wall_top}"
)CX";
    }
    cx::ScriptParser parser;
    const auto parsed = parser.parseDirectory(root / "scripts");
    expect(!cx::hasErrors(parsed.diagnostics), "room command script should parse with no errors");
    expect(parsed.handlers.size() == 4, "expected load and three key handlers");
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "room sample project should load successfully");
    expect(runtime.rooms().activeRoomName() == "hall", "on load should enter the first room");
    expect(runtime.rooms().activeRoom() && runtime.rooms().activeRoom()->width == 4, "active room should expose its tile dimensions");
    expect(runtime.getGlobal("entrance::hall::spawn").toString() == "(1, 1)", "room-file entrances should become global point variables");
    expect(runtime.getGlobal("entrance1::hall").toString() == "(1, 1)", "manual entrance-point variables should persist as readable coordinate values");
    expect(runtime.getGlobal("$wall_top").isNull(), "texture variables must be read-only virtual variables, not copied into globals");
    runtime.pressKey("T");
    bool sawTextureId = false;
    for (const auto& line : runtime.logs()) if (line == "texture=wall_top") sawTextureId = true;
    expect(sawTextureId, "{$texture_id} should resolve from the texture file name");
    runtime.pressKey("C");
    expect(runtime.rooms().activeRoomName() == "cave", "enter room should change the active room");
    const auto loadedAfterSwitch = runtime.rooms().loadedRoomNames();
    expect(loadedAfterSwitch.size() == 1 && loadedAfterSwitch.front() == "cave", "entering a new room should unload the previous active room");
    runtime.pressKey("H");
    expect(runtime.rooms().activeRoomName() == "hall", "an unloaded room should be loadable again from disk");
    expect(runtime.rooms().loadedRoomNames().size() == 1, "room transition should leave only the destination room loaded");
    std::filesystem::remove_all(root);
}

void testRoomFileErrors() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-bad-room";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "rooms");
    std::filesystem::create_directories(root / "assets" / "textures");
    {
        std::ofstream file(root / "rooms" / "broken.room.json");
        file << "{ this isn't valid JSON }\n";
    }
    {
        std::ofstream file(root / "scripts" / "main.cx");
        file << "load room \"broken\"\n";
    }
    cx::Runtime runtime(root);
    expect(!runtime.loadScripts(), "a bad room loaded from bootstrap should stop startup");
    expect(!runtime.diagnostics().empty(), "bad room should create a diagnostic");
    expect(runtime.diagnostics().front().message.find("not valid JSON") != std::string::npos,
           "bad room errors should explain that room JSON is invalid");
    std::filesystem::remove_all(root);
}

void testFailedRoomReloadPreservesLiveRoom() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-safe-room-reload";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "assets" / "textures");
    std::filesystem::create_directories(root / "rooms");
    std::ofstream(root / "assets" / "textures" / "grass.png").put('x');
    const auto roomPath = root / "rooms" / "preserve.room.json";
    const auto writeValidRoom = [&](const std::string& tiles) {
        std::ofstream room(roomPath);
        room << R"JSON({
  "schema":"cx-room", "version":1, "name":"preserve", "width":2, "height":1, "tileSize":16,
  "projection":"orthogonal", "tiles":)JSON" << tiles << R"JSON(,
  "entrances":{"spawn":{"x":1,"y":0}}
})JSON";
    };
    writeValidRoom(R"JSON([["grass","grass"]])JSON");

    cx::RoomManager manager(root);
    std::string error;
    expect(manager.enterRoom("preserve", {1, 0}, error), "valid room should load and become active before reload: " + error);
    const auto revisionBeforeFailure = manager.revision();

    {
        std::ofstream broken(roomPath);
        broken << R"JSON({"schema":"cx-room", "name":"preserve", "width":2, "height":1, "tiles":[["grass"]])JSON";
    }
    expect(!manager.reloadRoom("preserve", error), "invalid room JSON should make reload fail");
    expect(manager.revision() == revisionBeforeFailure,
           "a failed room reload should not advance the loaded-room revision");
    expect(manager.activeRoomName() == "preserve",
           "a failed room reload should keep the active room selected");
    const auto* activeAfterFailure = manager.activeRoom();
    expect(activeAfterFailure != nullptr,
           "a failed room reload should preserve the active room object");
    expect(activeAfterFailure && activeAfterFailure->tileAt(1, 0) == "grass",
           "a failed room reload should preserve the previous tile data");
    expect(manager.activeEntrance().x == 1 && manager.activeEntrance().y == 0,
           "a failed room reload should preserve the active entrance");

    writeValidRoom(R"JSON([["grass",null]])JSON");
    expect(manager.reloadRoom("preserve", error), "a corrected room should reload successfully: " + error);
    expect(manager.activeRoomName() == "preserve", "successful reload should keep the room active");
    expect(manager.activeRoom() && manager.activeRoom()->tileAt(1, 0).empty(),
           "successful reload should replace the live room with the corrected disk contents");
    expect(manager.activeEntrance().x == 1 && manager.activeEntrance().y == 0,
           "successful reload should retain an entrance that remains in bounds");
    std::filesystem::remove_all(root);
}

void testRoomReloadRefreshesNamedEntranceVariables() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-refresh-entrances";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "textures");
    std::filesystem::create_directories(root / "rooms");
    std::ofstream(root / "assets" / "textures" / "grass.png").put('x');
    const auto roomPath = root / "rooms" / "hall.room.json";
    const auto writeRoom = [&](const std::string& entrances) {
        std::ofstream room(roomPath);
        room << R"JSON({
  "schema":"cx-room", "version":1, "name":"hall", "width":2, "height":1, "tileSize":16,
  "tiles":[["grass","grass"]], "entrances":)JSON" << entrances << "\n}\n";
    };
    writeRoom(R"JSON({"old_entry":{"x":0,"y":0},"spawn":{"x":1,"y":0}})JSON");
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
load room "hall"
set {test_spawn} to an entrance point at (1, 0)
enter room "hall" at {test_spawn}
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "room project should load before testing entrance refresh");
    expect(runtime.rooms().activeRoomName() == "hall", "hall should be the active room");
    expect(runtime.getGlobal("entrance::hall::old_entry").toString() == "(0, 0)",
           "the original named entrance should initially be available as a global");

    writeRoom(R"JSON({"spawn":{"x":1,"y":0}})JSON");
    runtime.executeConsole("reload room \"hall\"");
    expect(!runtime.hasErrors(), "a valid room reload should not create a diagnostic");
    expect(runtime.getGlobal("entrance::hall::old_entry").isNull(),
           "a successful reload should remove globals for entrances deleted from the room file");
    expect(runtime.getGlobal("entrance::hall::spawn").toString() == "(1, 0)",
           "a successful reload should repopulate named entrance globals from disk");
    expect(runtime.rooms().activeRoomName() == "hall", "reloading a room should keep it active");
    std::filesystem::remove_all(root);
}

void testLightSyntaxAndSettings() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-lights";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    {
        std::ofstream file(root / "scripts" / "main.cx");
        file << R"CX(
set engine::texture_filter to "nearest"
set engine::pixel_perfect to true
set engine::lighting_enabled to true
set engine::ambient_light to 0.55
set engine::lighting_quality to 3
on load:
    create light "torch" at (2, 1)
    set light "torch" radius to 220
    set light "torch" intensity to 0.7
    set light "torch" color to (255, 150, 90)
    set light "torch" direction to 40
    set light "torch" beam to 45
on press key "L":
    remove light "torch"
)CX";
    }
    cx::ScriptParser parser;
    const auto parsed = parser.parseDirectory(root / "scripts");
    expect(!cx::hasErrors(parsed.diagnostics), "light syntax should parse cleanly");
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "light test script should load and execute");
    expect(runtime.settings().textureFilter == "nearest" && runtime.settings().pixelPerfect,
           "script graphical options should update runtime settings");
    expect(runtime.settings().lightingEnabled && runtime.settings().lightingQuality == 3U,
           "script lighting settings should update runtime settings");
    expect(std::abs(runtime.settings().ambientLight - 0.55F) < 0.001F,
           "script should configure ambient light level");
    expect(runtime.lights().contains("torch"), "create light should register a light by name");
    const auto& light = runtime.lights().at("torch");
    expect(light.xTiles == 2.0F && light.yTiles == 1.0F, "light position should use tile coordinates");
    expect(std::abs(light.radius - 220.0F) < 0.01F && std::abs(light.intensity - 0.7F) < 0.01F,
           "light radius and intensity should apply");
    expect(light.red == 255 && light.green == 150 && light.blue == 90,
           "light RGB color should apply");
    expect(std::abs(light.directionDegrees - 40.0F) < 0.01F && std::abs(light.beamAngleDegrees - 45.0F) < 0.01F,
           "light direction and beam angle should apply");
    runtime.pressKey("L");
    expect(!runtime.lights().contains("torch"), "remove light should unregister the source");
    std::filesystem::remove_all(root);
}

void testRoomTextureScale() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-scale";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "textures");
    std::filesystem::create_directories(root / "rooms");
    std::ofstream(root / "assets" / "textures" / "floor.png").put('x');
    {
        std::ofstream file(root / "rooms" / "scale_test.room.json");
        file << R"JSON({
  "schema":"cx-room", "version":1, "name":"scale_test", "width":2, "height":2, "tileSize":16,
  "tiles":[["floor","floor"],["floor",null]],
  "tileScales":[[2.0,0.5],[1.25,1.0]],
  "entrances":{"spawn":{"x":0,"y":0}}
})JSON";
    }
    cx::RoomManager manager(root);
    std::string error;
    expect(manager.loadRoom("scale_test", error), "room with per-tile scale should load: " + error);
    const auto* room = manager.loadedRoom("scale_test");
    expect(room != nullptr, "loaded scaled room should be in memory");
    expect(std::abs(room->tileScaleAt(0, 0) - 2.0F) < 0.001F, "scale > 1 should be preserved");
    expect(std::abs(room->tileScaleAt(1, 0) - 0.5F) < 0.001F, "scale < 1 should be preserved");
    expect(std::abs(room->tileScaleAt(0, 1) - 1.25F) < 0.001F, "fractional scale should be preserved");
    expect(std::abs(room->tileScaleAt(1, 1) - 1.0F) < 0.001F, "empty tiles should have normal default scale");
    std::filesystem::remove_all(root);
}

void testConditionAndInterpolation() {
    const std::string script = R"CX(
set {score} to 1
on load:
    if {score} > 0:
        broadcast "score={score}"
    else:
        broadcast "score was zero"
)CX";
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-condition";
    std::filesystem::create_directories(root / "scripts");
    {
        std::ofstream file(root / "scripts" / "main.cx");
        file << script;
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "condition test script should load");
    bool found = false;
    for (const auto& log : runtime.logs()) if (log == "score=1") found = true;
    expect(found, "condition and variable interpolation should work");
    std::filesystem::remove_all(root);
}

void testRichTextColorsAndGradients() {
    const std::string source = "<#0099ff>Blue</#9900ff> <gradient:#ff0000:#0000ff>RGB</gradient> &lBold&r plain &#00ff00Green";
    const auto spans = cx::parseRichText(source);
    const auto stripped = cx::stripRichText(source);
    expect(stripped.find("Blue") != std::string::npos, "paired hex color tags should preserve visible text");
    expect(stripped.find("RGB") != std::string::npos, "gradient tags should preserve visible text");
    expect(stripped.find("<gradient:") == std::string::npos, "gradient tag syntax should not leak into visible text");
    expect(stripped.find("Bold") != std::string::npos && stripped.find("Green") != std::string::npos,
           "Minecraft legacy and TAB-style hex formatting should preserve visible text");
    bool foundBlueStart = false, foundBlueEnd = false, foundGradientStart = false, foundGradientEnd = false;
    bool foundBold = false, foundGreen = false;
    const auto rgbStart = stripped.find("RGB");
    std::size_t visibleOffset = 0;
    for (const auto& span : spans) {
        if (visibleOffset <= 0 && !span.text.empty() && span.text.front() == 'B')
            foundBlueStart = span.style.red == 0 && span.style.green == 153 && span.style.blue == 255;
        if (visibleOffset <= 3 && visibleOffset + span.text.size() > 3 && span.text.size() == 1 && span.text == "e")
            foundBlueEnd = span.style.red == 153 && span.style.green == 0 && span.style.blue == 255;
        if (rgbStart != std::string::npos && visibleOffset <= rgbStart && visibleOffset + span.text.size() > rgbStart)
            foundGradientStart = span.style.red == 255 && span.style.green == 0 && span.style.blue == 0;
        if (rgbStart != std::string::npos && visibleOffset <= rgbStart + 2 && visibleOffset + span.text.size() > rgbStart + 2)
            foundGradientEnd = span.style.red == 0 && span.style.green == 0 && span.style.blue == 255;
        if (span.text.find("Bold") != std::string::npos) foundBold = span.style.bold;
        if (span.text.find("Green") != std::string::npos) foundGreen = span.style.green == 255 && span.style.red == 0;
        visibleOffset += span.text.size();
    }
    expect(foundBlueStart && foundBlueEnd, "paired hex tags should interpolate colors from their start color to their end color");
    expect(foundGradientStart && foundGradientEnd, "MiniMessage gradient syntax should generate a red-to-blue gradient");
    expect(foundBold, "&l should set bold styling");
    expect(foundGreen, "&#RRGGBB should be parsed as a hex color");
    expect(cx::toAnsi("<#0099ff>blue</#9900ff>").find("38;2;0;153;255") != std::string::npos,
           "terminal output should include ANSI truecolor escapes");
}

std::string makePortableFallbackCache(std::string_view csv) {
    std::string cache("CXVB", 4);
    const auto appendU32ForTest = [&](std::uint32_t value) {
        for (unsigned int shift = 0; shift < 32; shift += 8) {
            cache.push_back(static_cast<char>((value >> shift) & 0xffU));
        }
    };
    appendU32ForTest(0U); // Codec 0 = portable built-in PackBits-style encoding.
    appendU32ForTest(static_cast<std::uint32_t>(csv.size()));

    // Literal packets are a valid subset of PackBits. Keep each packet at or below
    // 127 bytes so the high bit remains clear and all bytes are copied verbatim.
    for (std::size_t offset = 0; offset < csv.size();) {
        const auto count = std::min<std::size_t>(127, csv.size() - offset);
        cache.push_back(static_cast<char>(count));
        cache.append(csv.substr(offset, count));
        offset += count;
    }
    return cache;
}

void testVariableStorageCsvRoundTripsEscapesAndClearsStatus() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-csv-escaping";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const std::string punctuation = R"CX(comma, quote " and path C:\games\save.cx)CX";
    const std::string literalEscapes = R"CX(these are literal backslash-n: \n and backslash-r: \r)CX";
    const std::string actualNewlines = "first line\nsecond line\r\nlast line\r";
    cx::VariableStorage::VariableMap source;
    source.emplace("punctuation", cx::Value(punctuation));
    source.emplace("literal_escapes", cx::Value(literalEscapes));
    source.emplace("actual_newlines", cx::Value(actualNewlines));
    source.emplace("empty_text", cx::Value(std::string{}));
    source.emplace("mixed_case\\name", cx::Value("name contains a backslash too"));

    // Public storage calls should never leave a stale error/warning from an earlier call.
    std::string error = "stale save error";
    expect(cx::VariableStorage::save(root, source, error), "a valid variable map should save successfully");
    expect(error.empty(), "a successful save should clear an old error message");

    cx::VariableStorage::VariableMap restored;
    std::string warning = "stale load warning";
    expect(cx::VariableStorage::load(root, restored, warning), "a freshly written CSV should reload");
    expect(warning.empty(), "a clean load should clear an old warning message");
    for (const auto& [name, value] : source) {
        const auto found = restored.find(cx::lower(name));
        expect(found != restored.end(), "CSV round-trip should preserve variable name: " + name);
        expect(found->second.toString() == value.toString(), "CSV round-trip should preserve exact string content for: " + name);
    }
    std::filesystem::remove_all(root);
}

void testVariableStorageMalformedCsvKeepsValidRowsAndReportsEachProblem() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-malformed-csv";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    // Create a stale cache first. When CSV exists it remains authoritative, so the cache-only
    // variable must not reappear even though some CSV rows below are malformed.
    cx::VariableStorage::VariableMap oldValues;
    oldValues.emplace("cache_only", cx::Value("must not come back"));
    std::string error;
    expect(cx::VariableStorage::save(root, oldValues, error), "seed data should save before corrupting the editable CSV");
    {
        std::ofstream csv(root / "variables.csv", std::ios::binary | std::ios::trunc);
        csv << "# CX Engine persistent variables. Edit or delete rows while the engine is closed.\n"
               "# name,type,value\n"
               "kept_text,string,still here\n"
               "bad_number,number,not-a-number\n"
               "bad_quote,string,\"unfinished\n"
               "bad_type,not-a-type,some value\n"
               "kept_bool,bool,true\n";
    }

    cx::VariableStorage::VariableMap loaded;
    std::string warning = "old warning";
    expect(cx::VariableStorage::load(root, loaded, warning), "an editable CSV with a few malformed rows should retain recoverable valid rows");
    expect(loaded.contains("kept_text") && loaded.at("kept_text").toString() == "still here",
           "valid rows before damaged data should survive");
    expect(loaded.contains("kept_bool") && loaded.at("kept_bool").truthy(),
           "valid rows after damaged data should survive");
    expect(!loaded.contains("bad_number") && !loaded.contains("bad_quote") && !loaded.contains("bad_type"),
           "malformed rows must be skipped");
    expect(!loaded.contains("cache_only"), "CSV authority must prevent stale cache-only variables from being resurrected");
    expect(warning.find("line 4") != std::string::npos && warning.find("line 5") != std::string::npos &&
           warning.find("line 6") != std::string::npos,
           "the warning should identify each malformed row instead of reporting only the last one");
    std::filesystem::remove_all(root);
}

void testVariableStorageDoesNotReconstructCsvFromMalformedCacheTable() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-malformed-cache-table";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string damagedCsv =
        "# CX Engine persistent variables. Edit or delete rows while the engine is closed.\n"
        "# name,type,value\n"
        "would_be_partial,string,not safe to commit\n"
        "broken_number,number,not-a-number\n";
    const auto cache = makePortableFallbackCache(damagedCsv);
    {
        std::ofstream file(root / "variables.cxv", std::ios::binary);
        file.write(cache.data(), static_cast<std::streamsize>(cache.size()));
    }

    cx::VariableStorage::VariableMap loaded;
    std::string warning;
    expect(!cx::VariableStorage::load(root, loaded, warning),
           "cache recovery must fail if the decoded variable table contains malformed records");
    expect(loaded.empty(), "failed cache-table recovery must not expose a partial variable map");
    expect(warning.find("table is damaged") != std::string::npos && warning.find("line 4") != std::string::npos,
           "cache-table failure should explain that recovered text was damaged and identify the row");
    expect(!std::filesystem::exists(root / "variables.csv"),
           "a malformed decoded cache must not be written back as a newly reconstructed CSV");
    std::filesystem::remove_all(root);
}

void testVariableStorageMaintainsPreviousSaveBackups() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-save-backups";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto readBytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        std::ostringstream contents;
        contents << input.rdbuf();
        return contents.str();
    };

    cx::VariableStorage::VariableMap first;
    first.emplace("score", cx::Value(1));
    std::string error;
    expect(cx::VariableStorage::save(root, first, error), "the first save should succeed");
    expect(!std::filesystem::exists(root / "variables.csv.bak"),
           "the first save should not invent a backup when no previous CSV exists");
    const auto oldCsv = readBytes(root / "variables.csv");
    const auto oldCache = readBytes(root / "variables.cxv");

    cx::VariableStorage::VariableMap second;
    second.emplace("score", cx::Value(2));
    expect(cx::VariableStorage::save(root, second, error), "the second save should succeed");
    expect(readBytes(root / "variables.csv.bak") == oldCsv,
           "variables.csv.bak should contain the previous authoritative save byte-for-byte");
    expect(readBytes(root / "variables.cxv.bak") == oldCache,
           "variables.cxv.bak should contain the previous cache byte-for-byte");

    cx::VariableStorage::VariableMap loaded;
    std::string warning;
    expect(cx::VariableStorage::load(root, loaded, warning) && loaded.at("score").numberOr() == 2.0,
           "the current CSV should remain authoritative after backup rotation");
    std::filesystem::remove_all(root);
}

void testRuntimeDoesNotImmediatelyOverwriteMalformedCsv() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-malformed-csv-runtime";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    {
        cx::VariableStorage::VariableMap oldValues;
        oldValues.emplace("cache_only", cx::Value("old snapshot"));
        std::string error;
        expect(cx::VariableStorage::save(root, oldValues, error), "seed save should succeed");
    }
    const std::string damagedCsv =
        "# CX Engine persistent variables. Edit or delete rows while the engine is closed.\n"
        "# name,type,value\n"
        "kept,string,still valid\n"
        "broken,number,not-a-number\n";
    {
        std::ofstream csv(root / "variables.csv", std::ios::binary | std::ios::trunc);
        csv << damagedCsv;
    }
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << "on load:\n    broadcast \\\"loaded\\\"\n";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "malformed-save warnings should not prevent an otherwise valid script from starting");
    expect(runtime.getGlobal("kept").toString() == "still valid", "valid CSV data should remain available to the runtime");
    std::ifstream csvAfter(root / "variables.csv", std::ios::binary);
    std::ostringstream after;
    after << csvAfter.rdbuf();
    expect(after.str() == damagedCsv,
           "a warning-only load must not immediately serialize partial data over the malformed authoritative CSV");
    expect(std::filesystem::exists(root / "variables.cxv"), "the previous cache should remain available until a deliberate save");
    std::filesystem::remove_all(root);
}

void testVariableStorageBackupFailuresDoNotCommitNewGeneration() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-backup-failure-injection";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto readBytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };

    // Start with a complete, known-good generation. The two file contents are our
    // evidence that a failure while preparing backups must happen before a commit.
    cx::VariableStorage::VariableMap first;
    first.emplace("score", cx::Value(1));
    std::string error;
    expect(cx::VariableStorage::save(root, first, error), "initial generation should save before fault injection");
    const std::string originalCsv = readBytes(root / "variables.csv");
    const std::string originalCache = readBytes(root / "variables.cxv");
    expect(!originalCsv.empty() && !originalCache.empty(), "the original CSV and cache should both exist");

    cx::VariableStorage::VariableMap next;
    next.emplace("score", cx::Value(2));

    // Inject failure at the first backup step. Making the .bak destination a directory
    // forces its atomic replacement to fail, without relying on permissions (which are
    // unreliable in privileged CI containers). The live CSV/cache must remain untouched.
    std::filesystem::create_directories(root / "variables.csv.bak");
    {
        std::ofstream sentinel(root / "variables.csv.bak" / "keep.txt");
        sentinel << "preserve csv backup destination";
    }
    error.clear();
    expect(!cx::VariableStorage::save(root, next, error),
           "save should abort when the CSV recovery backup cannot be installed");
    expect(error.find("recovery backup variables.csv.bak") != std::string::npos,
           "CSV backup failure should identify the recovery backup that blocked the save");
    expect(readBytes(root / "variables.csv") == originalCsv && readBytes(root / "variables.cxv") == originalCache,
           "failure while backing up CSV must leave both live representations byte-for-byte unchanged");
    expect(std::filesystem::is_directory(root / "variables.csv.bak") &&
           readBytes(root / "variables.csv.bak" / "keep.txt") == "preserve csv backup destination",
           "failed backup replacement must preserve the unexpected destination and its contents");
    expect(!std::filesystem::exists(root / "variables.csv.bak.tmp"),
           "failed CSV backup should clean up its temporary file");
    std::filesystem::remove_all(root / "variables.csv.bak");

    // Inject a second-stage backup failure. The CSV backup can be prepared, but the cache
    // backup destination is blocked. Even though the first backup has been refreshed, the
    // current CSV/cache generation must not be replaced before both backups are ready.
    std::filesystem::create_directories(root / "variables.cxv.bak");
    {
        std::ofstream sentinel(root / "variables.cxv.bak" / "keep.txt");
        sentinel << "preserve cache backup destination";
    }
    error.clear();
    expect(!cx::VariableStorage::save(root, next, error),
           "save should abort when the cache recovery backup cannot be installed");
    expect(error.find("recovery backup variables.cxv.bak") != std::string::npos,
           "cache backup failure should identify the blocked recovery backup");
    expect(readBytes(root / "variables.csv") == originalCsv && readBytes(root / "variables.cxv") == originalCache,
           "failure while backing up cache must leave the current CSV/cache generation unchanged");
    expect(readBytes(root / "variables.csv.bak") == originalCsv,
           "a successfully refreshed CSV backup should contain the still-current generation");
    expect(std::filesystem::is_directory(root / "variables.cxv.bak") &&
           readBytes(root / "variables.cxv.bak" / "keep.txt") == "preserve cache backup destination",
           "failed cache backup replacement must preserve the unexpected destination and its contents");
    expect(!std::filesystem::exists(root / "variables.cxv.bak.tmp"),
           "failed cache backup should clean up its temporary file");
    std::filesystem::remove_all(root);
}

void testVariableStorageCacheCommitFailureKeepsRecoveryGeneration() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-cache-commit-failure";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto readBytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };

    // Commit generation one normally and remember its exact bytes. The later injected
    // fault must demonstrate that a half-completed two-file save remains understandable.
    cx::VariableStorage::VariableMap first;
    first.emplace("score", cx::Value(1));
    std::string error;
    expect(cx::VariableStorage::save(root, first, error), "initial generation should save before cache fault injection");
    const std::string originalCsv = readBytes(root / "variables.csv");
    const std::string originalCache = readBytes(root / "variables.cxv");

    // Block the temporary cache filename, not the existing cache file. This lets the CSV
    // backup and cache backup both succeed, then makes the cache update fail exactly after
    // the authoritative CSV has been committed.
    std::filesystem::create_directories(root / "variables.cxv.tmp");
    {
        std::ofstream sentinel(root / "variables.cxv.tmp" / "keep.txt");
        sentinel << "preserve temp destination";
    }

    cx::VariableStorage::VariableMap second;
    second.emplace("score", cx::Value(2));
    error.clear();
    expect(!cx::VariableStorage::save(root, second, error),
           "save should report failure if the cache temporary file cannot be opened after CSV commit");
    expect(error.find("variables.csv was saved") != std::string::npos &&
           error.find("variables.cxv") != std::string::npos,
           "late cache failure must clearly report the partial commit");
    expect(readBytes(root / "variables.csv") != originalCsv,
           "the authoritative CSV should contain the newer generation after a late cache failure");
    expect(readBytes(root / "variables.cxv") == originalCache,
           "the old cache should remain intact when installing the new cache fails");
    expect(readBytes(root / "variables.csv.bak") == originalCsv &&
           readBytes(root / "variables.cxv.bak") == originalCache,
           "both recovery backups should preserve the previous complete generation");
    expect(std::filesystem::is_directory(root / "variables.cxv.tmp") &&
           readBytes(root / "variables.cxv.tmp" / "keep.txt") == "preserve temp destination",
           "the fault-injection fixture must remain intact after the failed cache write");

    // A new load call models the next process starting: CSV is authoritative, so the
    // next run should see score 2 while still retaining the old cache and backups for repair.
    cx::VariableStorage::VariableMap afterRestart;
    std::string warning;
    expect(cx::VariableStorage::load(root, afterRestart, warning),
           "the next-run load should recover from the committed authoritative CSV");
    expect(afterRestart.contains("score") && afterRestart.at("score").numberOr() == 2.0,
           "the next-run load should observe the new value from CSV, not stale cache data");
    expect(warning.empty(), "reading the committed CSV should not warn merely because its cache is stale");
    std::filesystem::remove_all(root);
}

void testVariableStoragePartialSaveReportsCsvWasCommitted() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-partial-save";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "variables.cxv");
    {
        std::ofstream sentinel(root / "variables.cxv" / "keep.txt");
        sentinel << "preserve cache destination";
    }

    cx::VariableStorage::VariableMap values;
    values.emplace("score", cx::Value(42));
    std::string error;
    expect(!cx::VariableStorage::save(root, values, error),
           "save should report failure when the authoritative CSV is written but the cache cannot be replaced");
    expect(error.find("variables.csv was saved") != std::string::npos && error.find("variables.cxv") != std::string::npos,
           "partial-save error must tell the caller that CSV was committed and cache update failed");
    expect(std::filesystem::is_directory(root / "variables.cxv") &&
           std::filesystem::exists(root / "variables.cxv" / "keep.txt"),
           "cache replacement failure must preserve the unexpected destination and its contents");

    cx::VariableStorage::VariableMap restored;
    std::string warning;
    expect(cx::VariableStorage::load(root, restored, warning), "the authoritative CSV remains usable after a cache update failure");
    expect(restored.contains("score") && restored.at("score").numberOr() == 42.0,
           "the committed CSV should contain the current value after a cache failure");
    std::filesystem::remove_all(root);
}

void testVariableStorageSavePreservesUnexpectedDestination() {
    // An empty directory is the regression trigger: the old delete-and-retry fallback
    // removed it, then incorrectly succeeded by installing a file at that path.
    const auto emptyRoot = std::filesystem::temp_directory_path() / "cx-engine-test-save-empty-destination";
    std::filesystem::remove_all(emptyRoot);
    std::filesystem::create_directories(emptyRoot / "variables.csv");
    std::string error;
    const bool savedEmpty = cx::VariableStorage::save(emptyRoot, cx::VariableStorage::VariableMap{}, error);
    expect(!savedEmpty, "saving must fail instead of replacing an empty directory at the variables.csv path");
    expect(std::filesystem::is_directory(emptyRoot / "variables.csv"),
           "failed replacement must not delete an unexpected empty destination directory");
    expect(!std::filesystem::exists(emptyRoot / "variables.csv.tmp"), "failed replacement should clean up its temporary file");
    expect(!error.empty(), "failed replacement should explain the storage error");
    std::filesystem::remove_all(emptyRoot);

    // A non-empty unexpected destination additionally checks that existing contents survive.
    const auto nonemptyRoot = std::filesystem::temp_directory_path() / "cx-engine-test-save-nonempty-destination";
    std::filesystem::remove_all(nonemptyRoot);
    std::filesystem::create_directories(nonemptyRoot / "variables.csv");
    {
        std::ofstream sentinel(nonemptyRoot / "variables.csv" / "keep.txt");
        sentinel << "preserve this directory";
    }
    error.clear();
    const bool savedNonempty = cx::VariableStorage::save(nonemptyRoot, cx::VariableStorage::VariableMap{}, error);
    expect(!savedNonempty, "saving must fail instead of replacing a non-empty directory at the variables.csv path");
    expect(std::filesystem::is_directory(nonemptyRoot / "variables.csv"),
           "a failed atomic replacement must not delete the existing destination directory");
    std::ifstream sentinel(nonemptyRoot / "variables.csv" / "keep.txt");
    std::string contents;
    std::getline(sentinel, contents);
    expect(contents == "preserve this directory", "the prior destination contents must remain intact after save failure");
    expect(!std::filesystem::exists(nonemptyRoot / "variables.csv.tmp"), "failed replacement should clean up its temporary file");
    expect(!error.empty(), "failed replacement should explain the storage error");
    std::filesystem::remove_all(nonemptyRoot);
}

void testPortableVariableCacheLoadsInEveryBuild() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-portable-cache";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    // This fixture deliberately uses codec 0 (the engine's built-in fallback),
    // regardless of whether the test binary itself was compiled with zlib. This
    // catches the old bug where the reader rejected the fallback marker before
    // it could reach the fallback decoder.
    const std::string csv =
        "# CX Engine persistent variables. Edit or delete rows while the engine is closed.\n"
        "# name,type,value\n"
        "hero_name,string,Emery\n"
        "story_step,number,3\n";
    const auto cache = makePortableFallbackCache(csv);
    {
        std::ofstream file(root / "variables.cxv", std::ios::binary);
        file.write(cache.data(), static_cast<std::streamsize>(cache.size()));
    }

    cx::VariableStorage::VariableMap loaded;
    std::string warning;
    expect(cx::VariableStorage::load(root, loaded, warning),
           "a portable fallback cache must load in both zlib and no-zlib builds");
    expect(loaded.contains("hero_name") && loaded.at("hero_name").toString() == "Emery",
           "the fallback cache should preserve string values");
    expect(loaded.contains("story_step") && loaded.at("story_step").numberOr() == 3.0,
           "the fallback cache should preserve numeric values");
    expect(std::filesystem::exists(root / "variables.csv"),
           "loading from a cache should rebuild the human-editable CSV copy");
    {
        std::ifstream manifest(root / "variables.meta", std::ios::binary);
        std::string header;
        std::getline(manifest, header);
        std::string generation;
        std::getline(manifest, generation);
        expect(header == "CXVM1" && generation == "generation=1",
               "legacy cache reconstruction should migrate the project to generation-1 metadata");
    }

    // Truncating the cache should produce an explicit failure, not pretend that an
    // empty save loaded successfully. The existing reconstructed CSV remains intact.
    std::filesystem::remove(root / "variables.csv");
    std::filesystem::remove(root / "variables.meta"); // Exercise the pre-manifest legacy decoder path.
    {
        std::ofstream file(root / "variables.cxv", std::ios::binary | std::ios::trunc);
        file.write(cache.data(), 15); // Header plus an incomplete literal packet.
    }
    loaded.clear();
    warning.clear();
    expect(!cx::VariableStorage::load(root, loaded, warning),
           "truncated fallback cache data must fail recovery");
    expect(warning.find("damaged") != std::string::npos || warning.find("unexpected size") != std::string::npos,
           "truncated fallback cache should explain that its data is damaged");
    expect(!std::filesystem::exists(root / "variables.csv"),
           "failed cache recovery must not write a fabricated CSV snapshot");
    std::filesystem::remove_all(root);
}

void testPersistentAndRamOnlyVariables() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-persistent-vars";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    {
        std::ofstream file(root / "scripts" / "main.cx");
        file << R"CX(
if {score} is not set:
    set {score} to 0
set {profile} to "ProfileA"
set {buttonsound1::%{profile}%} to "assets/audio/click.wav"
on load:
    add 1 to {score}
    if {score} is set:
        broadcast "persistent={score}"
on press key "S":
    add 1 to {^session_counter}
    set {session_result} to {^session_counter}
)CX";
    }
    {
        cx::Runtime first(root);
        expect(first.loadScripts(), "persistent-variable test script should parse and run");
        expect(first.getGlobal("score").numberOr() == 1.0, "is-not-set should initialize an absent variable");
        expect(first.getGlobal("buttonsound1::profilea").toString() == "assets/audio/click.wav",
               "dynamic nested variable segments should resolve from other variables");
        first.executeInline("set {^session_counter} to 40");
        first.pressKey("S");
        expect(first.getGlobal("session_result").numberOr() == 41.0, "RAM-only variables should be available between event calls");
        expect(std::filesystem::exists(root / "variables.csv"), "persistent values should be stored in editable CSV");
        expect(std::filesystem::exists(root / "variables.cxv"), "persistent values should also have a compressed binary cache");
        std::ifstream input(root / "variables.csv");
        std::stringstream contents; contents << input.rdbuf();
        expect(contents.str().find("session_counter") == std::string::npos && contents.str().find("^session_counter") == std::string::npos,
               "RAM-only variables must not be written to persistent files");
    }
    {
        cx::Runtime second(root);
        expect(second.loadScripts(), "a new runtime should reload its persistent variables");
        expect(second.getGlobal("score").numberOr() == 2.0, "a global variable should survive across engine processes");
        second.pressKey("S");
        expect(second.getGlobal("session_result").numberOr() == 1.0,
               "RAM-only variables must start empty in a new runtime process");
    }
    // Demonstrate that editing variables.csv is authoritative even when a compressed cache exists.
    {
        std::ifstream input(root / "variables.csv");
        std::stringstream contents; contents << input.rdbuf();
        std::string kept;
        std::istringstream lines(contents.str());
        std::string line;
        while (std::getline(lines, line)) {
            if (line.find("buttonsound1::profilea,") != std::string::npos) continue;
            kept += line + '\n';
        }
        std::ofstream output(root / "variables.csv", std::ios::trunc);
        output << kept;
    }
    cx::VariableStorage::VariableMap reloaded;
    std::string warning;
    expect(cx::VariableStorage::load(root, reloaded, warning), "edited variables.csv should reload");
    expect(!reloaded.contains("buttonsound1::profilea"), "deleting a CSV row should delete that variable even when a binary cache is present");
    std::filesystem::remove(root / "variables.csv");
    cx::VariableStorage::VariableMap recoveredFromCache;
    warning.clear();
    expect(cx::VariableStorage::load(root, recoveredFromCache, warning), "the compressed cache should provide recovery when the CSV is missing");
    expect(recoveredFromCache.contains("buttonsound1::profilea"), "the compressed cache should retain the last saved snapshot for recovery");
    std::filesystem::remove_all(root);
}

void testFileAttachEventsAndButtonHover() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-file-attach";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    const auto attached = root / "sample audio.wav";
    { std::ofstream file(attached, std::ios::binary); file << "fake file for event test"; }
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
on load:
    open canvas "ImportMenu"
on file attach:
    if mouse cursor is over button "sound_import_btn":
        set {last_imported_path} to {_attached_file}
        set {last_imported_x} to {_mouse_x}
        set {last_imported_y} to {_mouse_y}
canvas "ImportMenu":
    create button "sound_import_btn" at (10, 10) size (120, 40) label "Drop sound here"
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "file-attach script should parse and open its canvas");
    runtime.attachFile(attached, 30.0F, 20.0F);
    expect(runtime.getGlobal("last_imported_path").toString() == std::filesystem::absolute(attached).string(),
           "file attach should expose the absolute attached file path to its event-local variable");
    expect(runtime.getGlobal("last_imported_x").numberOr() == 30.0 && runtime.getGlobal("last_imported_y").numberOr() == 20.0,
           "file attach should expose the cursor position so button hit-testing can reuse existing canvas bounds");
    runtime.attachFile(attached, 500.0F, 500.0F);
    expect(runtime.getGlobal("last_imported_path").toString() == std::filesystem::absolute(attached).string(),
           "drops outside the button should not execute a guarded button-import branch");
    std::filesystem::remove_all(root);
}

void testAudioAssetVariables() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-audio-assets";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "audio" / "zduplicate");
    {
        std::ofstream file(root / "assets" / "audio" / "click.wav", std::ios::binary);
        file << "placeholder audio bytes for registry test";
    }
    {
        std::ofstream file(root / "assets" / "audio" / "zduplicate" / "click.wav", std::ios::binary);
        file << "second placeholder audio file with a duplicate base name";
    }
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
if {@click} is set:
    set {discovered_audio_path} to {@click}
on load:
    play sound {@click}
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "audio asset variable should parse at bootstrap and in event handlers");
    expect(runtime.getGlobal("discovered_audio_path").toString() == "assets/audio/click.wav",
           "{@click} should resolve to its relative asset path and can be assigned into a user variable");
    expect(runtime.audio().assets().size() == 2 && runtime.audio().findAssetPath("click") && runtime.audio().findAssetPath("click2"),
           "duplicate audio base names should remain addressable under stable, unique virtual IDs");
    const auto queued = runtime.audio().takePlayRequests();
    expect(queued.size() == 1 && std::filesystem::path(queued.front()).filename() == "click.wav",
           "play sound {@click} should queue the discovered audio file");
    runtime.executeInline("set {@click} to \"not allowed\"");
    expect(!runtime.diagnostics().empty() && runtime.diagnostics().back().message.find("read-only") != std::string::npos,
           "discovered audio identifiers should be read-only virtual variables");
    std::filesystem::remove_all(root);
}

void testWaveGenerationAndNoise() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-audio";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    {
        std::ofstream script(root / "scripts" / "audio.cx");
        script << R"CX(
on load:
    create wave "chime" duration 0.05 sample_rate 8000
    add sine wave to "chime" frequency 440 amplitude 0.5 phase 0
    add sine wave to "chime" frequency 880 amplitude 0.2 phase 0.4
    set wave "chime" envelope attack 0.002 decay 0.01 sustain 0.7 release 0.01
    normalize wave "chime"
    save wave "chime" to "assets/audio/chime.wav"
    visualize wave "chime" to "assets/audio/chime.svg"
    play sound "chime"
    set {sample_sine} to sine(440, 0.002)
    set {sample_noise} to noise2d(1.2, 7.8, 123)
    set {terrain_noise} to fbm2d(1.2, 7.8, 123, 5)
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "audio/noise script should parse and execute");
    const auto* wave = runtime.audio().find("chime");
    expect(wave != nullptr, "script should create a reusable named generated wave");
    expect(wave->samples.size() == 400U, "generated samples should match duration times sample rate");
    expect(std::any_of(wave->samples.begin(), wave->samples.end(), [](float v) { return std::abs(v) > 0.01F; }),
           "stacked sine layers should generate audible nonzero samples");
    expect(std::filesystem::exists(root / "assets/audio/chime.wav"), "script should export its sound to a standard WAV file");
    expect(std::filesystem::exists(root / "assets/audio/chime.svg"), "script should export a waveform visualization SVG");
    const auto queued = runtime.audio().takePlayRequests();
    expect(queued.size() == 1 && queued.front() == "chime", "play sound should queue the generated wave for the graphical audio backend");
    expect(runtime.getGlobal("sample_sine").isNumber(), "sine(hz, seconds[, phase]) should work as a numeric expression");
    const auto noise = runtime.getGlobal("sample_noise").numberOr(2.0);
    expect(noise >= -1.0 && noise <= 1.0, "noise2d should return normalized noise");
    expect(runtime.getGlobal("terrain_noise").numberOr(2.0) >= -1.0 && runtime.getGlobal("terrain_noise").numberOr(2.0) <= 1.0,
           "fbm2d should return normalized fractal noise");
    std::filesystem::remove_all(root);
}

void testLoopsWaitAndFlexibleIndentation() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-loops";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    const std::string script =
        "on press key \"SPACE\":\n"
        "  set {_num} to 5\n"
        "  loop 5 times:\n"
        "\t\tbroadcast \"%{_num}% (iteration %loop-number%)\"\n"
        "    add 1 to {_num}\n"
        "    wait 1 milliseconds\n"
        "  send \"Countdown over!\"\n"
        "on press key \"W\":\n"
        "  set {_n} to 0\n"
        "  while {_n} < 3:\n"
        "    send \"while=%{_n}%\"\n"
        "    add 1 to {_n}\n"
        "on press key \"E\":\n"
        "  loop 10 times:\n"
        "    if loop-number >= 3:\n"
        "      exit loop\n"
        "    send \"exit-test=%loop-number%\"\n"
        "on press key \"F\":\n"
        "  loop 5 times:\n"
        "    if loop-number == 3:\n"
        "      continue loop\n"
        "    send \"continue-test=%loop-number%\"\n";
    {
        std::ofstream file(root / "scripts" / "main.cx");
        file << script;
    }

    cx::ScriptParser parser;
    const auto parsed = parser.parseDirectory(root / "scripts");
    expect(!cx::hasErrors(parsed.diagnostics), "counted loops, while loops, waits, and mixed indentation should parse cleanly");
    expect(parsed.handlers.size() == 4, "expected four loop test handlers");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "loop test project should load");
    runtime.pressKey("SPACE");
    auto hasLog = [&](const std::string& expected) {
        return std::any_of(runtime.logs().begin(), runtime.logs().end(), [&](const std::string& line) { return line == expected; });
    };
    expect(hasLog("5 (iteration 1)"), "counted loop should start immediately and interpolate local variables/loop-number");
    for (int i = 0; i < 5; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
        runtime.tick();
    }
    for (int i = 5; i < 10; ++i) {
        expect(hasLog(std::to_string(i) + " (iteration " + std::to_string(i - 4) + ")"), "waited counted loop should resume each iteration in order");
    }
    expect(hasLog("Countdown over!"), "statements after a waited loop should run after the final iteration");

    runtime.clearLogs();
    runtime.pressKey("W");
    expect(hasLog("while=0") && hasLog("while=1") && hasLog("while=2"), "while should re-check its condition between iterations");

    runtime.clearLogs();
    runtime.pressKey("E");
    expect(hasLog("exit-test=1") && hasLog("exit-test=2") && !hasLog("exit-test=3"), "exit loop should leave the nearest active loop only");

    runtime.clearLogs();
    runtime.pressKey("F");
    expect(hasLog("continue-test=1") && hasLog("continue-test=2") && !hasLog("continue-test=3") &&
           hasLog("continue-test=4") && hasLog("continue-test=5"),
           "continue loop should skip to the next iteration without ending the loop");
    std::filesystem::remove_all(root);
}

void testPeriodicEvents() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-periodic";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
every 5 milliseconds:
    add 1 to {periodic_runs}
    send "periodic=%{periodic_runs}%"
on tick:
    add 1 to {tick_runs}
)CX";
    }
    cx::ScriptParser parser;
    const auto parsed = parser.parseDirectory(root / "scripts");
    expect(!cx::hasErrors(parsed.diagnostics), "periodic event syntax should parse cleanly");
    expect(parsed.handlers.size() == 2 && parsed.handlers.front().type == cx::EventType::Periodic,
           "every duration should register a periodic handler, separate from on tick");
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "periodic test project should load");
    expect(runtime.getGlobal("periodic_runs").numberOr() == 0.0, "periodic events should not execute before their interval elapses");
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    runtime.tick();
    expect(runtime.getGlobal("periodic_runs").numberOr() == 1.0, "periodic handler should execute after its deadline");
    expect(runtime.getGlobal("tick_runs").numberOr() == 1.0, "on tick should still execute on each engine tick");
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
    runtime.tick();
    expect(runtime.getGlobal("periodic_runs").numberOr() == 2.0, "periodic handlers should schedule the next interval without a tight loop");
    std::filesystem::remove_all(root);
}

void testListsFunctionsAndMathExpressions() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-lists-functions";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
function double(value):
    return {_value} * 2
function suffix(value):
    return {_value} + "!"
function clamp(value, maximum):
    if {_value} > {_maximum}:
        return {_maximum}
    return {_value}
on load:
    set {colors::*} to "red", "green", "blue"
    set {colors::featured} to "purple"
    loop {colors::*}:
        broadcast "%loop-index%=%loop-value%"
    set {doubled} to double(7)
    set {suffix_test} to suffix("hello")
    set {clamped} to clamp(15, 10)
    set {math_test} to (2 + 3) * 4 - 1
    add "yellow" to {colors::*}
    remove "green" from {colors::*}
    delete {colors::featured}
)CX";
    }
    cx::ScriptParser parser;
    const auto parsed = parser.parseDirectory(root / "scripts");
    expect(!cx::hasErrors(parsed.diagnostics), "list, function, and arithmetic syntax should parse");
    expect(parsed.functions.size() == 3, "custom functions should be collected across the script program");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "list and function project should load");
    expect(runtime.getGlobal("doubled").numberOr() == 14.0, "custom functions should return values through an expression");
    expect(runtime.getGlobal("suffix_test").toString() == "hello!", "return should support text concatenation expressions that contain multiple variable references");
    expect(runtime.getGlobal("clamped").numberOr() == 10.0, "return inside an if branch should exit a custom function");
    expect(runtime.getGlobal("math_test").numberOr() == 19.0, "arithmetic should respect parentheses and operator precedence");
    const auto hasLog = [&](const std::string& expected) {
        return std::any_of(runtime.logs().begin(), runtime.logs().end(), [&](const std::string& line) { return line == expected; });
    };
    expect(hasLog("1=red") && hasLog("2=green") && hasLog("3=blue") && hasLog("featured=purple"),
           "list loop should expose list values and their indices");
    expect(!hasLog("4=yellow"), "list iteration should enumerate the list's initial contents before later mutations");
    expect(runtime.getGlobal("colors::2").isNull(), "removing a value from a list should delete the matching entry, not leave a null placeholder");
    expect(runtime.getGlobal("colors::featured").isNull(), "deleting a named list entry should remove it");
    expect(runtime.getGlobal("colors::4").toString() == "yellow", "append should choose the next available integer index");

    runtime.executeInline("set {copy::*} to {colors::*}");
    expect(runtime.getGlobal("copy::1").toString() == "red" && runtime.getGlobal("copy::4").toString() == "yellow",
           "setting one list wildcard from another should copy all key/value entries");
    runtime.executeInline("clear {copy::*}");
    expect(runtime.getGlobal("copy::1").isNull() && runtime.getGlobal("copy::4").isNull(), "clear should remove every list entry");
    std::filesystem::remove_all(root);
}

void testResponsiveCanvasGridAndRelativeLayout() {
    const auto parsed = cx::ScriptParser{}.parseText(R"CX(
canvas "HUD":
    create button "map_btn" at grid (127, 127) anchor bottom_right size (180, 44) label "Map"
    create panel "map_backdrop" to the left of button "map_btn" gap 8 size (160, 44) color (40, 40, 40, 220)
    create button "hotbar" at grid (64, 127) anchor bottom_center size (500, 54) label "Hotbar"
)CX", "<memory>");
    expect(!cx::hasErrors(parsed.diagnostics), "grid anchors and relative canvas declarations should parse");
    expect(parsed.canvases.size() == 1 && parsed.canvases[0].elements.size() == 3, "expected three layout elements in HUD canvas");
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-layout";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
set engine::ui_scale to 1.0
canvas "HUD":
    create button "map_btn" at grid (127, 127) anchor bottom_right size (180, 44) label "Map"
    create panel "map_backdrop" to the left of button "map_btn" gap 8 size (160, 44) color (40, 40, 40, 220)
    create button "hotbar" at grid (64, 127) anchor bottom_center size (500, 54) label "Hotbar"
on load:
    open canvas "HUD"
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "responsive UI test project should load");
    const auto* canvas = runtime.findCanvas("HUD");
    expect(canvas != nullptr, "HUD canvas should be findable by name");
    auto elements = runtime.resolveCanvasElements(*canvas, 1920.0F, 1080.0F);
    expect(elements.size() == 3, "layout resolver should preserve all elements");
    const auto map = std::find_if(elements.begin(), elements.end(), [](const auto& e) { return e.id == "map_btn"; });
    const auto panel = std::find_if(elements.begin(), elements.end(), [](const auto& e) { return e.id == "map_backdrop"; });
    const auto hotbar = std::find_if(elements.begin(), elements.end(), [](const auto& e) { return e.id == "hotbar"; });
    expect(map != elements.end() && std::abs((map->x + map->width) - (127.0F / 128.0F * 1920.0F)) < 0.1F &&
           std::abs((map->y + map->height) - (127.0F / 128.0F * 1080.0F)) < 0.1F,
           "bottom-right anchor should remain pinned to the current viewport edges");
    expect(panel != elements.end() && map != elements.end() && std::abs((panel->x + panel->width + panel->gap) - map->x) < 0.1F,
           "relative panel placement should use the target's resolved position and scaled gap");
    expect(hotbar != elements.end() && std::abs((hotbar->x + hotbar->width * 0.5F) - 960.0F) < 0.1F &&
           std::abs((hotbar->y + hotbar->height) - (127.0F / 128.0F * 1080.0F)) < 0.1F,
           "bottom-center anchor should remain centered at the bottom edge");
    runtime.executeInline("set engine::ui_scale to 1.25");
    const auto scaled = runtime.resolveCanvasElements(*canvas, 1920.0F, 1080.0F);
    const auto scaledMap = std::find_if(scaled.begin(), scaled.end(), [](const auto& e) { return e.id == "map_btn"; });
    expect(scaledMap != scaled.end() && scaledMap->width > map->width,
           "universal UI scale should increase element dimensions without moving their anchor off-screen");
    std::filesystem::remove_all(root);
}

void testSpriteObjectsAndHeldKeyInput() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-sprites";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "textures");
    { std::ofstream texture(root / "assets" / "textures" / "player.png", std::ios::binary); texture << "test texture entry"; }
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
set engine::show_debug_overlay to false
set engine::tick_rate to 60
on load:
    create sprite "player" with texture {$player}
    set sprite "player" size to (32, 32)
    set sprite "player" position to (100, 200)
on tick:
    if key "D" is down:
        move sprite "player" by (3, 0)
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "sprite test project should load");
    expect(!runtime.settings().showDebugOverlay, "script should be able to hide the engine's debug overlay");
    expect(runtime.sprites().contains("player"), "on load should create the player sprite");
    const auto& created = runtime.sprites().at("player");
    expect(created.textureId == "player" && created.x == 100.0F && created.y == 200.0F,
           "sprite should use the discovered texture and configured pixel position");
    expect(created.width == 32.0F && created.height == 32.0F, "sprite size should be configured in pixels");
    runtime.setKeyDown("D", true);
    runtime.tick();
    expect(std::abs(runtime.sprites().at("player").x - 103.0F) < 0.001F,
           "held D should move the sprite once per simulation tick");
    // Simulate the host losing OS focus while multiple keys are held. The host
    // cannot rely on the window receiving one KeyReleased event per held key.
    runtime.setKeyDown("A", true);
    runtime.clearHeldKeys();
    expect(!runtime.keyDown("D") && !runtime.keyDown("A"),
           "clearing held input after focus loss should release every tracked key");
    runtime.tick();
    expect(std::abs(runtime.sprites().at("player").x - 103.0F) < 0.001F,
           "movement should stop after the host clears held keys");
    expect(!runtime.hasErrors(), "valid sprite and key-state actions should not emit runtime errors");
    std::filesystem::remove_all(root);
}

void testEntityPathsProjectilesAndTilePlacement() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-entities-world";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "textures");
    for (const auto& name : {"player.png", "boss.png", "orb.png", "stone.png"}) {
        std::ofstream texture(root / "assets" / "textures" / name, std::ios::binary);
        texture << "texture registry placeholder";
    }
    std::filesystem::create_directories(root / "rooms");
    {
        std::ofstream room(root / "rooms" / "arena.room.json");
        room << R"JSON({
  "schema":"cx-room","version":1,"name":"arena","width":8,"height":8,"tileSize":32,
  "tiles":[[null,null,null,null,null,null,null,null],[null,null,null,null,null,null,null,null],
           [null,null,null,null,null,null,null,null],[null,null,null,null,null,null,null,null],
           [null,null,null,null,null,null,null,null],[null,null,null,null,null,null,null,null],
           [null,null,null,null,null,null,null,null],[null,null,null,null,null,null,null,null]],
  "entrances":{"spawn":{"x":1,"y":1}}
})JSON";
    }
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
set engine::world_view_height_tiles to 18
set engine::camera_follow to "player"
set {^started} to false
load room "arena"
on load:
    enter room "arena" at {entrance::arena::spawn}
    create entity "player" with texture {$player}
    set entity "player" size to (16, 16)
    set entity "player" position to (32, 32)
    set entity "player" hitbox to (10, 10) offset (3, 3)
    set entity "player" avoids solid tiles to true
    create entity "boss" with texture {$boss}
    set entity "boss" size to (16, 16)
    set entity "boss" position to (64, 32)
    set entity "boss" max_health to 2
    set entity "boss" health to 2
    create path "patrol" through tiles (3, 3), (4, 3), (4, 4)
    set path of "boss" to path "patrol" with speed "0.5"
    place tile {$stone} at (0, 0)
    if {^started} is not true:
        set {^started} to true
        fire projectile from "player" toward "boss" with texture {$orb} speed 5 damage 1
on projectile hit entity "boss":
    set {hit_received} to true
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "entity/world fixture scripts should load without errors");
    expect(runtime.settings().worldViewHeightTiles == 18.0F && runtime.settings().cameraFollow == "player",
           "world view height and camera target should be configurable by script");
    expect(runtime.paths().contains("patrol"), "named tile routes should be registered");
    expect(runtime.sprites().contains("player") && runtime.sprites().contains("boss"),
           "entity creation should register both actors");
    expect(runtime.sprites().at("player").hitboxWidth == 10.0F && runtime.sprites().at("player").hitboxOffsetY == 3.0F,
           "entity collision boxes should preserve configured dimensions and offsets");
    expect(runtime.rooms().activeRoom() && runtime.rooms().activeRoom()->tileAt(0, 0) == "stone",
           "script-driven tile placement should update the active room");
    for (int i = 0; i < 40; ++i) runtime.tick();
    expect(runtime.getGlobal("hit_received").truthy(), "projectile collision should dispatch the matching hit event");
    expect(runtime.sprites().at("boss").health == 1, "projectiles should apply damage to the target entity");
    expect(!runtime.hasErrors(), "entity, path, tile, and projectile workflows should not emit runtime errors");
    std::filesystem::remove_all(root);
}

void testScriptDrivenWindowSettingsMenu() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-window-menu";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
set engine::console_enabled to false
set engine::show_debug_overlay to false
set engine::world_view_height_tiles to 18
set engine::camera_follow to "player"
canvas "Settings":
    create button "set_1600" at (0, 0) size (180, 44) label "1600 x 900"
on press key "F2":
    open canvas "Settings"
on click button "set_1600":
    set engine::resolution_mode to "fixed"
    set engine::window_width to 1600
    set engine::window_height to 900
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "script-controlled window settings should parse and load");
    expect(!runtime.settings().consoleEnabled, "a single startup setting should disable the in-game terminal");
    expect(!runtime.settings().showDebugOverlay, "debug overlay can be disabled from script");
    expect(runtime.settings().worldViewHeightTiles == 18.0F && runtime.settings().cameraFollow == "player",
           "tile-based world view and camera follow settings should accept script overrides");
    runtime.pressKey("F2");
    expect(runtime.openCanvases().contains("settings"), "F2 handler should open the settings canvas");
    runtime.clickButton("set_1600");
    expect(runtime.settings().resolutionMode == "fixed", "settings menu should be able to select fixed resolution mode");
    expect(runtime.settings().windowWidth == 1600 && runtime.settings().windowHeight == 900,
           "settings menu button should update the target window resolution");
    std::filesystem::remove_all(root);
}

void testFourSpaceAndTabIndentationCompatibility() {
    cx::ScriptParser parser;
    const auto fourSpaces = parser.parseText(R"CX(on load:
    set {value} to 1
    if {value} is 1:
        broadcast "four"
)CX", "<memory>");
    expect(!cx::hasErrors(fourSpaces.diagnostics), "classic four-space indentation should remain valid");
    expect(fourSpaces.handlers.size() == 1 && fourSpaces.handlers[0].body.size() == 2 &&
           fourSpaces.handlers[0].body[1].body.size() == 1,
           "four-space indentation should preserve nested if structure");
    const auto tabs = parser.parseText(R"CX(on load:
	set {value} to 1
	if {value} is 1:
		broadcast "tabs"
)CX", "<memory>");
    expect(!cx::hasErrors(tabs.diagnostics), "tab indentation should count as one logical indentation level");
    expect(tabs.handlers.size() == 1 && tabs.handlers[0].body.size() == 2 &&
           tabs.handlers[0].body[1].body.size() == 1,
           "tab indentation should preserve the same nested structure as spaces");
    const auto twoSpaces = parser.parseText(R"CX(on load:
  set {value} to 1
  if {value} is 1:
    broadcast "two spaces"
)CX", "<memory>");
    expect(!cx::hasErrors(twoSpaces.diagnostics), "two-space indentation should be auto-detected and normalized");
    expect(twoSpaces.handlers.size() == 1 && twoSpaces.handlers[0].body.size() == 2 &&
           twoSpaces.handlers[0].body[1].body.size() == 1,
           "two-space indentation should preserve the same nested structure as spaces and tabs");
}

void testFailedReloadPreservesRunningProgram() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-safe-reload";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "textures");
    std::ofstream(root / "assets" / "textures" / "player.png").put('x');
    const auto scriptPath = root / "scripts" / "main.cx";
    {
        std::ofstream script(scriptPath);
        script << R"CX(
set {baseline} to 7
on load:
    create sprite "survivor" with texture {$player}
    set sprite "survivor" position to (10, 20)
on tick:
    add 1 to {ticks}
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "the valid program should load before reload regression test");
    runtime.tick();
    expect(runtime.getGlobal("ticks").numberOr() == 1.0, "the original tick handler should run before reload");
    expect(runtime.sprites().contains("survivor"), "the original program should create its sprite");
    const auto handlersBefore = runtime.program().handlers.size();
    {
        std::ofstream script(scriptPath);
        script << "set {baseline} 99\n";
    }
    runtime.executeConsole("/reload");
    expect(runtime.hasErrors(), "an invalid reload should report a syntax error");
    expect(runtime.diagnostics().front().file.string().find("main.cx") != std::string::npos,
           "the failed reload diagnostic should identify the script file");
    expect(runtime.getGlobal("baseline").numberOr() == 7.0,
           "a failed reload must preserve the live global variables");
    expect(runtime.sprites().contains("survivor"), "a failed reload must preserve existing runtime sprites");
    expect(runtime.program().handlers.size() == handlersBefore,
           "a failed reload must preserve the previously active handler tree");
    runtime.tick();
    expect(runtime.getGlobal("ticks").numberOr() == 2.0,
           "the previously active tick handler must continue after a failed reload");
    bool sawReloadFailure = false;
    for (const auto& line : runtime.logs()) if (line.find("Reload failed") != std::string::npos) sawReloadFailure = true;
    expect(sawReloadFailure, "the command log should explain that reload failed");
    std::filesystem::remove_all(root);
}

void testFailedRuntimeReloadPreservesRuntimeAndFiles() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-safe-runtime-reload";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "textures");
    std::filesystem::create_directories(root / "assets" / "audio");
    std::ofstream(root / "assets" / "textures" / "player.png").put('x');
    const auto scriptPath = root / "scripts" / "main.cx";
    const auto variablesPath = root / "variables.csv";
    const auto generatedWavePath = root / "assets" / "audio" / "keep.wav";
    {
        std::ofstream script(scriptPath);
        script << R"CX(
set {baseline} to 7
on load:
    create sprite "survivor" with texture {$player}
    set sprite "survivor" position to (10, 20)
on tick:
    add 1 to {ticks}
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "the initial program should load before testing runtime-error rollback");
    runtime.tick();
    expect(runtime.getGlobal("baseline").numberOr() == 7.0, "the initial global should be established");
    expect(runtime.getGlobal("ticks").numberOr() == 1.0, "the initial tick handler should execute");
    expect(runtime.sprites().contains("survivor"), "the initial load handler should create its sprite");

    const auto readFile = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };
    const std::string savedVariablesBefore = readFile(variablesPath);
    expect(!savedVariablesBefore.empty(), "the initial persistent-variable save should exist");
    {
        std::ofstream existingWave(generatedWavePath, std::ios::binary);
        existingWave << "preserve-this-existing-file";
    }
    const std::string existingWaveBefore = readFile(generatedWavePath);

    // This program parses, changes a persistent variable, and generates a WAV;
    // only afterward does a setting type error make startup fail. Neither the
    // active runtime nor either on-disk file may be replaced by the candidate.
    {
        std::ofstream script(scriptPath);
        script << R"CX(
set {baseline} to 99
create wave "probe" duration 0.01 sample_rate 8000
add sine wave to "probe" frequency 440 amplitude 0.5 phase 0
save wave "probe" to "assets/audio/keep.wav"
set engine::window_width to "not-a-number"
on tick:
    add 1 to {new_ticks}
)CX";
    }
    runtime.executeConsole("/reload");
    expect(runtime.hasErrors(), "a bootstrap runtime error must be reported");
    expect(runtime.getGlobal("baseline").numberOr() == 7.0,
           "a runtime-error reload must preserve the live persistent/global value");
    expect(runtime.getGlobal("new_ticks").isNull(),
           "a runtime-error reload must not install the candidate's new handlers");
    expect(runtime.sprites().contains("survivor"),
           "a runtime-error reload must preserve the live sprite state");
    expect(runtime.settings().windowWidth == 1280,
           "a runtime-error reload must preserve current settings");
    expect(readFile(variablesPath) == savedVariablesBefore,
           "a failed candidate must not rewrite the persistent-variable files");
    expect(readFile(generatedWavePath) == existingWaveBefore,
           "a failed candidate must not replace a file generated during bootstrap");

    runtime.tick();
    expect(runtime.getGlobal("ticks").numberOr() == 2.0,
           "the old tick handler must remain operational after runtime-error reload failure");
    expect(runtime.getGlobal("new_ticks").isNull(),
           "the failed candidate's tick handler must remain inactive");
    bool sawReloadFailure = false;
    for (const auto& line : runtime.logs()) if (line.find("Reload failed") != std::string::npos) sawReloadFailure = true;
    expect(sawReloadFailure, "the reload command should explain that the runtime-error reload failed");
    std::filesystem::remove_all(root);
}

void testSuccessfulReloadWithoutGlobalResetKeepsPendingPersistentSave() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-reload-false-keeps-dirty-save";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    const auto scriptPath = root / "scripts" / "main.cx";
    {
        std::ofstream script(scriptPath);
        script << R"CX(
set {baseline} to 1
on tick:
    add 1 to {old_tick_runs}
)CX";
    }

    const auto readFile = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "the initial program should load before pending-save reload test");
    const auto variablesPath = root / "variables.csv";
    expect(std::filesystem::exists(variablesPath), "initial persistent values should create the CSV save");

    // Console edits are intentionally not flushed on every command. This simulates
    // a developer changing a value and immediately using the public loadScripts(false)
    // API before another event/tick has a chance to save it.
    runtime.executeInline("set {pending_console_edit} to 77");
    expect(runtime.getGlobal("pending_console_edit").numberOr() == 77.0,
           "the console edit should be present in the live global map");
    expect(readFile(variablesPath).find("pending_console_edit") == std::string::npos,
           "the test must begin with a genuinely dirty, not-yet-saved global edit");

    {
        std::ofstream script(scriptPath, std::ios::trunc);
        script << R"CX(
on tick:
    add 1 to {new_tick_runs}
)CX";
    }
    expect(runtime.loadScripts(false), "a valid candidate should reload while retaining live globals");
    expect(runtime.getGlobal("pending_console_edit").numberOr() == 77.0,
           "loadScripts(false) should keep the live persistent value in memory");
    expect(readFile(variablesPath).find("pending_console_edit,number,77") != std::string::npos,
           "loadScripts(false) must not lose the pending persistent-save dirty flag");

    runtime.tick();
    expect(runtime.getGlobal("new_tick_runs").numberOr() == 1.0,
           "the newly loaded program should own future ticks");
    expect(runtime.getGlobal("old_tick_runs").isNull(),
           "handlers from the old program must not survive a successful reload");
    expect(!runtime.hasErrors(), "the successful reload and follow-up tick should remain error-free");
    std::filesystem::remove_all(root);
}

void testSuccessfulScriptReloadRebuildsRuntimeOwnedState() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-reload-state-replacement";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "textures");
    std::filesystem::create_directories(root / "assets" / "audio");
    std::filesystem::create_directories(root / "rooms");
    std::ofstream(root / "assets" / "textures" / "hero.png").put('x');
    std::ofstream(root / "assets" / "audio" / "tone.wav", std::ios::binary).put('x');
    const auto writeRoom = [&](const std::string& name) {
        std::ofstream room(root / "rooms" / (name + ".room.json"));
        room << R"JSON({
  "schema":"cx-room","version":1,"name":")JSON" << name << R"JSON(","width":2,"height":2,"tileSize":16,
  "tiles":[[null,null],[null,null]],"entrances":{"spawn":{"x":0,"y":0}}
})JSON";
    };
    writeRoom("hall");
    writeRoom("arena");
    const auto scriptPath = root / "scripts" / "main.cx";
    {
        std::ofstream script(scriptPath);
        script << R"CX(
load room "hall"
canvas "OldMenu":
    create button "old_button" at (0, 0) size (80, 24) label "Old"
on load:
    enter room "hall" at {entrance::hall::spawn}
    create entity "old_actor" with texture {$hero}
    create light "old_light" at (1, 1)
    open canvas "OldMenu"
    play sound {@tone}
)CX";
    }

    cx::Runtime runtime(root);
    const bool initialLoaded = runtime.loadScripts();
    if (!initialLoaded) {
        for (const auto& diagnostic : runtime.diagnostics())
            std::cerr << "RELOAD STATE FIXTURE: " << diagnostic.message << "\n";
    }
    expect(initialLoaded, "the initial world should load before successful reload-state test");
    expect(runtime.rooms().activeRoomName() == "hall", "the first program should activate its starting room");
    expect(runtime.sprites().contains("old_actor") && runtime.lights().contains("old_light"),
           "the initial program should create its actor and light");
    expect(runtime.openCanvases().contains("oldmenu"), "the initial program should open its menu");
    expect(runtime.audio().takePlayRequests().size() == 1, "the first startup should queue its sound request");

    runtime.executeInline("set {persistent_marker} to 23");
    runtime.executeInline("set {^session_marker} to 42");
    runtime.executeInline("set engine::target_fps to 75");
    runtime.setKeyDown("W", true);
    runtime.executeInline("play sound {@tone}"); // Deliberately leave one old request queued.
    runtime.log("pre-reload log marker");
    runtime.executeInline("set engine::window_width to \"not-a-number\"");
    expect(runtime.hasErrors(), "the test should establish a diagnostic to be replaced by a successful reload");

    {
        std::ofstream script(scriptPath, std::ios::trunc);
        script << R"CX(
load room "arena"
canvas "NewMenu":
    create button "new_button" at (0, 0) size (80, 24) label "New"
on load:
    enter room "arena" at {entrance::arena::spawn}
    create entity "new_actor" with texture {$hero}
    create light "new_light" at (1, 1)
    open canvas "NewMenu"
    play sound {@tone}
on press key "A":
    if {^session_marker} is set:
        set {transient_survived} to true
    else:
        set {transient_survived} to false
)CX";
    }

    expect(runtime.loadScripts(false), "the candidate program should successfully replace the old runtime session");
    expect(runtime.getGlobal("persistent_marker").numberOr() == 23.0,
           "loadScripts(false) should carry global variables into the new session");
    expect(runtime.settings().targetFps == 75U,
           "loadScripts(false) should carry the current engine settings rather than re-read config defaults");
    expect(runtime.rooms().activeRoomName() == "arena", "the candidate startup should construct its selected room");
    expect(!runtime.sprites().contains("old_actor") && runtime.sprites().contains("new_actor"),
           "successful reload should replace old sprites with the candidate startup world");
    expect(!runtime.lights().contains("old_light") && runtime.lights().contains("new_light"),
           "successful reload should replace old lights with the candidate startup lights");
    expect(!runtime.openCanvases().contains("oldmenu") && runtime.openCanvases().contains("newmenu"),
           "successful reload should rebuild open canvas state from the candidate startup");
    expect(runtime.audio().assets().contains("tone"), "audio assets should be rediscovered for the new session");
    const auto newAudioRequests = runtime.audio().takePlayRequests();
    expect(newAudioRequests.size() == 1,
           "successful reload should discard an old queued audio request and keep only the candidate on-load request");
    expect(!runtime.keyDown("W"), "successful reload should clear the host's held-key snapshot");
    expect(!runtime.hasErrors() && runtime.diagnostics().empty(),
           "successful reload should replace the prior diagnostic list with the candidate's clean diagnostics");
    expect(std::any_of(runtime.logs().begin(), runtime.logs().end(), [](const std::string& line) {
        return line.find("pre-reload log marker") != std::string::npos;
    }), "the runtime log history should survive a successful script reload");

    runtime.pressKey("A");
    expect(runtime.getGlobal("transient_survived").truthy() == false &&
           runtime.getGlobal("transient_survived").isBool(),
           "process-only caret variables should reset even while ordinary globals survive");
    expect(runtime.getGlobal("session_marker").isNull(), "the old process-only variable must be absent after reload");
    std::filesystem::remove_all(root);
}

void testDefaultReloadReloadsPersistentValuesAndConfig() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-reload-default-global-reset";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    const auto configPath = root / "config.json";
    const auto scriptPath = root / "scripts" / "main.cx";
    {
        std::ofstream config(configPath);
        config << "{\n  \"target_fps\": 30\n}\n";
    }
    {
        std::ofstream script(scriptPath);
        script << "set {saved_score} to 5\n";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "the initial config/save baseline should load");
    expect(runtime.getGlobal("saved_score").numberOr() == 5.0, "the initial script should save its baseline score");
    expect(runtime.settings().targetFps == 30U, "the initial config should be applied");

    runtime.executeInline("set {saved_score} to 99"); // Dirty in-memory edit; deliberately not flushed.
    runtime.executeInline("set {^session_marker} to 42");
    runtime.executeInline("set engine::target_fps to 120");
    {
        std::ofstream config(configPath, std::ios::trunc);
        config << "{\n  \"target_fps\": 45\n}\n";
    }
    {
        std::ofstream script(scriptPath, std::ios::trunc);
        script << R"CX(
on tick:
    add 1 to {fresh_tick_runs}
)CX";
    }

    expect(runtime.loadScripts(), "the default reload should start a fresh state from disk and config");
    expect(runtime.getGlobal("saved_score").numberOr() == 5.0,
           "loadScripts() should reload the last saved persistent value instead of keeping an unsaved console edit");
    expect(runtime.settings().targetFps == 45U,
           "loadScripts() should re-read config.json rather than carrying the live setting override");
    expect(runtime.getGlobal("^session_marker").isNull(),
           "the default reload should also clear process-only caret variables");
    runtime.tick();
    expect(runtime.getGlobal("fresh_tick_runs").numberOr() == 1.0,
           "the newly loaded program should run after default reload");
    expect(!runtime.hasErrors(), "the default reload should finish with no diagnostics");
    std::filesystem::remove_all(root);
}

void testObliqueRoomOverlaysAndAutotileRules() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-oblique-overlays";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "textures");
    std::filesystem::create_directories(root / "rooms");
    for (const auto* id : {"grass", "path", "edge_north", "corner_northwest", "inner_northwest"})
        std::ofstream(root / "assets" / "textures" / (std::string(id) + ".png")).put('x');
    {
        std::ofstream manifest(root / "assets" / "autotiles.json");
        manifest << R"JSON({
  "schema": "cx-autotiles",
  "terrains": [{
    "name": "path",
    "members": ["path"],
    "edges": {"north": "edge_north"},
    "corners": {"northwest": "corner_northwest"},
    "inner_corners": {"northwest": "inner_northwest"}
  }]
})JSON";
    }
    {
        std::ofstream room(root / "rooms" / "iso.room.json");
        room << R"JSON({
  "schema":"cx-room", "version":1, "name":"iso", "width":2, "height":2, "tileSize":16,
  "projection":"oblique",
  "tiles":[["path","grass"],["grass","grass"]],
  "overlays":[
    {"x":0,"y":0,"texture":"edge_north","layer":"foreground"},
    {"x":0,"y":0,"texture":"corner_northwest","layer":"foreground"}
  ],
  "entrances": {}
})JSON";
    }
    cx::RoomManager manager(root);
    std::string error;
    expect(manager.loadRoom("iso", error), "an oblique room with transparent overlays should load: " + error);
    expect(manager.enterRoom("iso", {0, 0}, error), "the oblique room should be enterable: " + error);
    const auto* active = manager.activeRoom();
    expect(active && active->projection == "oblique", "room projection should be retained");
    expect(active && active->overlays.size() == 2, "multiple different overlays may share a tile and layer");
    expect(manager.textures().autotileRules().size() == 1, "the autotile manifest should be discovered and loaded");
    expect(manager.textures().warnings().empty(), "valid texture IDs in the autotile manifest should not warn");
    std::filesystem::remove_all(root);
}

void testConsoleUtilityReports() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-console-utility";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "assets" / "textures");
    std::ofstream(root / "assets" / "textures" / "hero.png").put('x');
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
create sprite "hero" with texture {$hero}
create path "patrol" through tiles (1, 2), (3, 4)
create light "lamp" at (1, 1)
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "utility reporting project should load");
    runtime.executeConsole("/settings");
    runtime.executeConsole("/sprites");
    runtime.executeConsole("/paths");
    runtime.executeConsole("/lights");
    const auto joined = [&]() { std::string out; for (const auto& line : runtime.logs()) out += line + "\n"; return out; }();
    expect(joined.find("window_width") != std::string::npos, "/settings should list engine settings");
    expect(joined.find("hero") != std::string::npos, "/sprites should list the current sprite");
    expect(joined.find("patrol") != std::string::npos, "/paths should list named paths");
    expect(joined.find("lamp") != std::string::npos, "/lights should list named lights");
    std::filesystem::remove_all(root);
}

void testConsoleHelpCoverage() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-console-help";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    cx::Runtime runtime(root);
    runtime.executeConsole("/help");
    const auto joinLogs = [&]() {
        std::string output;
        for (const auto& line : runtime.logs()) output += line + "\n";
        return output;
    };
    const std::string help = joinLogs();
    const std::array<std::string_view, 15> requiredCommands{{
        "/help", "/reload", "/vars", "/settings", "/sprites", "/paths", "/lights",
        "/rooms", "/textures", "/status", "/clear", "/tick", "/press KEY",
        "/click BUTTON_ID", "/attach"
    }};
    for (const auto command : requiredCommands) {
        expect(help.find(command) != std::string::npos,
               "runtime /help should list the implemented command " + std::string(command));
    }
    expect(help.find("Ctrl+Tab") != std::string::npos,
           "runtime /help should explain how to open the graphical terminal");
    expect(help.find("PageUp/PageDown") != std::string::npos,
           "runtime /help should explain terminal output scrolling");
    expect(help.find("CLI only") != std::string::npos,
           "runtime /help should distinguish commands owned by the CLI host");
    expect(help.find("set {score} to 10") != std::string::npos,
           "runtime /help should show an example CX statement");

    runtime.executeConsole("help");
    expect(runtime.logs().size() >= requiredCommands.size() * 2,
           "the documented `help` alias should show the same command catalogue");

    // Runtime log retention is a count of stored message records, not a disk log.
    // Repeated status queries must evict the oldest entries instead of growing forever.
    for (int i = 0; i < 550; ++i) runtime.executeConsole("/status");
    expect(runtime.logs().size() == 500,
           "the runtime log should retain no more than its 500-message limit");
    std::filesystem::remove_all(root);
}

void testDiagnosticHistoryIsBoundedForParserAndRuntime() {
    cx::ScriptParser parser;
    std::string malformedSource;
    for (int i = 0; i < 400; ++i) malformedSource += "not_a_supported_top_level_command\n";
    const auto parsed = parser.parseText(malformedSource, "<memory>");
    expect(parsed.diagnostics.size() == cx::maximumDiagnosticEntries,
           "parser diagnostics should stop at the shared diagnostic limit");
    expect(!parsed.diagnostics.back().warning &&
               parsed.diagnostics.back().message.find("limit of 256") != std::string::npos,
           "the parser should clearly report that additional diagnostics were suppressed");
    expect(cx::hasErrors(parsed.diagnostics),
           "a diagnostic overflow notice must remain an error so invalid scripts cannot load");

    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-runtime-diagnostic-limit";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << "on tick:\n"
                  "    set engine::definitely_not_a_setting to true\n";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "runtime diagnostic test script should load before its tick handler runs");
    for (int i = 0; i < 400; ++i) runtime.tick();
    expect(runtime.diagnostics().size() == cx::maximumDiagnosticEntries,
           "runtime diagnostics should remain bounded after repeated event failures");
    expect(!runtime.diagnostics().back().warning &&
               runtime.diagnostics().back().message.find("limit of 256") != std::string::npos,
           "runtime should report that further event errors were suppressed");
    expect(runtime.hasErrors(), "suppressed runtime errors must still block a successful status");
    std::filesystem::remove_all(root);
}

} // namespace

// Tests here focus on externally observable language/runtime contracts. When fixing a bug,
// prefer adding a small regression that would fail on the old behavior before changing code.
int main() {
    try {
        testValueConversions();
        testParserAndLocalVariables();
        testSyntaxSuggestions();
        testFriendlyTerminalDiagnostics();
        testConfigAndScriptOverride();
        testLightSyntaxAndSettings();
        testRoomTextureScale();
        testConditionAndInterpolation();
        testRichTextColorsAndGradients();
        testVariableStorageSavePreservesUnexpectedDestination();
        testPortableVariableCacheLoadsInEveryBuild();
        testVariableStorageCsvRoundTripsEscapesAndClearsStatus();
        testVariableStorageMalformedCsvKeepsValidRowsAndReportsEachProblem();
        testVariableStorageDoesNotReconstructCsvFromMalformedCacheTable();
        testVariableStorageMaintainsPreviousSaveBackups();
        testRuntimeDoesNotImmediatelyOverwriteMalformedCsv();
        testVariableStorageBackupFailuresDoNotCommitNewGeneration();
        testVariableStorageCacheCommitFailureKeepsRecoveryGeneration();
        testVariableStoragePartialSaveReportsCsvWasCommitted();
        testPersistentAndRamOnlyVariables();
        testFileAttachEventsAndButtonHover();
        testWaveGenerationAndNoise();
        testAudioAssetVariables();
        testRoomsTexturesAndEntrances();
        testRoomFileErrors();
        testFailedRoomReloadPreservesLiveRoom();
        testRoomReloadRefreshesNamedEntranceVariables();
        testLoopsWaitAndFlexibleIndentation();
        testPeriodicEvents();
        testListsFunctionsAndMathExpressions();
        testResponsiveCanvasGridAndRelativeLayout();
        testSpriteObjectsAndHeldKeyInput();
        testEntityPathsProjectilesAndTilePlacement();
        testScriptDrivenWindowSettingsMenu();
        testFourSpaceAndTabIndentationCompatibility();
        testFailedReloadPreservesRunningProgram();
        testFailedRuntimeReloadPreservesRuntimeAndFiles();
        testSuccessfulReloadWithoutGlobalResetKeepsPendingPersistentSave();
        testSuccessfulScriptReloadRebuildsRuntimeOwnedState();
        testDefaultReloadReloadsPersistentValuesAndConfig();
        testObliqueRoomOverlaysAndAutotileRules();
        testConsoleUtilityReports();
        testConsoleHelpCoverage();
        testDiagnosticHistoryIsBoundedForParserAndRuntime();
    } catch (const std::exception& exception) {
        std::cerr << "TEST FAILED: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All CX Engine core tests passed.\n";
    return 0;
}
