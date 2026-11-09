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

void createTexture(const std::filesystem::path& root) {
    write(root / "assets" / "textures" / "hero.png", "placeholder image data");
}

void testOneShotClipAndCompletionEvent() {
    const auto root = std::filesystem::temp_directory_path() / "cx-animation-one-shot-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "actor" with texture {$hero}
set entity "actor" animation sheet to (16, 16) columns 4 rows 8
define animation "wake" for entity "actor" frames 1 to 3 row 5 at 30 fps once
on load:
    play animation "wake" for entity "actor"
on animation complete "wake" for entity "actor":
    add 1 to {wake_count}
    set {finished_frame} to {_animation_frame}
    set {finished_row} to {_animation_row}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "named one-shot clip should parse and initialize");
    expect(runtime.sprites().at("actor").currentAnimation == "wake", "on-load should select the named clip");
    expect(runtime.sprites().at("actor").animationFrame == 1, "clip playback should begin at its first frame");
    expect(runtime.sprites().at("actor").animationRow == 5, "fixed clip row should be selected");

    runtime.tick();
    expect(runtime.sprites().at("actor").animationFrame == 1, "30 fps animation should hold its first frame for two 60 Hz ticks");
    runtime.tick();
    expect(runtime.sprites().at("actor").animationFrame == 2, "clip should advance on its second simulation tick");
    runtime.tick();
    expect(runtime.sprites().at("actor").animationFrame == 2, "middle frame should also respect its duration");
    runtime.tick();
    expect(runtime.sprites().at("actor").animationFrame == 3, "clip should reach its inclusive final frame");
    runtime.tick();
    expect(runtime.sprites().at("actor").animationPlaying, "final frame must remain visible for a complete frame interval");
    runtime.tick();

    const auto& finished = runtime.sprites().at("actor");
    expect(!finished.animationPlaying, "one-shot clip should stop after its final frame interval");
    expect(finished.animationFrame == 3, "completed one-shot should hold the final frame");
    expect(runtime.getGlobal("wake_count").numberOr(0.0) == 1.0, "completion event should fire exactly once");
    expect(runtime.getGlobal("finished_frame").numberOr(-1.0) == 3.0, "completion event should expose the final frame");
    expect(runtime.getGlobal("finished_row").numberOr(-1.0) == 5.0, "completion event should expose the resolved row");
    for (int i = 0; i < 30; ++i) runtime.tick();
    expect(runtime.getGlobal("wake_count").numberOr(0.0) == 1.0, "completed clip must not emit repeatedly");
    expect(!runtime.hasErrors(), "valid one-shot clip should not produce runtime errors");
    std::filesystem::remove_all(root);
}

void testLoopingClipWrapsWithoutCompletion() {
    const auto root = std::filesystem::temp_directory_path() / "cx-animation-loop-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "actor" with texture {$hero}
set entity "actor" animation sheet to (16, 16) columns 4 rows 8
define animation "walk" for entity "actor" frames 1 to 2 row facing at 60 fps looping
on load:
    play animation "walk" for entity "actor"
on animation complete "walk" for entity "actor":
    add 1 to {unexpected_completion}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "named looping clip should parse and initialize");
    runtime.tick();
    expect(runtime.sprites().at("actor").animationFrame == 2, "60 fps clip should advance every simulation tick at 60 Hz");
    runtime.tick();
    expect(runtime.sprites().at("actor").animationFrame == 1, "looping clip should wrap to its first frame");
    runtime.tick();
    expect(runtime.sprites().at("actor").animationFrame == 2, "looping clip should continue after wrapping");
    expect(runtime.sprites().at("actor").animationRow == 0, "facing row should resolve to the entity's current direction");
    expect(runtime.sprites().at("actor").animationPlaying, "looping clip should remain active");
    expect(!runtime.getGlobal("unexpected_completion").truthy(), "looping clip must not emit one-shot completion");
    expect(!runtime.hasErrors(), "valid looping clip should not produce runtime errors");
    std::filesystem::remove_all(root);
}

void testReplacingEntityInvalidatesLaterCompletionHandlers() {
    const auto root = std::filesystem::temp_directory_path() / "cx-animation-identity-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "actor" with texture {$hero}
set entity "actor" animation sheet to (16, 16) columns 4 rows 8
define animation "wake" for entity "actor" frames 0 to 1 row 0 at 60 fps once
on load:
    play animation "wake" for entity "actor"
on animation complete "wake" for entity "actor":
    delete sprite "actor"
    create entity "actor" with texture {$hero}
on animation complete "wake" for entity "actor":
    add 1 to {stale_event_count}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "entity-replacement scenario should initialize");
    const auto originalId = runtime.sprites().at("actor").runtimeInstanceId;
    runtime.tick();
    runtime.tick();
    expect(runtime.sprites().contains("actor"), "first completion handler should recreate the entity");
    expect(runtime.sprites().at("actor").runtimeInstanceId != originalId, "recreated entity must have a new runtime identity");
    expect(!runtime.getGlobal("stale_event_count").truthy(), "later completion handlers must not target a replacement entity");
    expect(!runtime.hasErrors(), "safe entity replacement from a completion callback should not error");
    std::filesystem::remove_all(root);
}

void testRestartingPlaybackInvalidatesLaterCompletionHandlers() {
    const auto root = std::filesystem::temp_directory_path() / "cx-animation-generation-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "actor" with texture {$hero}
set entity "actor" animation sheet to (16, 16) columns 4 rows 8
define animation "wake" for entity "actor" frames 0 to 1 row 0 at 60 fps once
define animation "walk" for entity "actor" frames 2 to 3 row facing at 60 fps looping
on load:
    play animation "wake" for entity "actor"
on animation complete "wake" for entity "actor":
    add 1 to {first_completion_handler}
    play animation "walk" for entity "actor"
on animation complete "wake" for entity "actor":
    add 1 to {stale_completion_handler}
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "playback-generation scenario should initialize");
    const auto initialGeneration = runtime.sprites().at("actor").animationPlaybackGeneration;
    runtime.tick();
    runtime.tick();
    const auto& actor = runtime.sprites().at("actor");
    expect(actor.currentAnimation == "walk", "first completion handler should start the next clip");
    expect(actor.animationPlaying, "the replacement looping clip should be active");
    expect(actor.animationPlaybackGeneration > initialGeneration,
           "starting the next clip should advance the playback generation");
    expect(runtime.getGlobal("first_completion_handler").numberOr(0.0) == 1.0,
           "the first matching completion handler should run exactly once");
    expect(!runtime.getGlobal("stale_completion_handler").truthy(),
           "restarting playback must invalidate remaining callbacks for the old completion");
    expect(!runtime.hasErrors(), "restarting playback from a completion handler should not error");
    std::filesystem::remove_all(root);
}

void testInvalidClipDoesNotInstallPartialDefinition() {
    const auto root = std::filesystem::temp_directory_path() / "cx-animation-invalid-clip-tests";
    std::filesystem::remove_all(root);
    createTexture(root);
    write(root / "scripts" / "main.cx", R"CX(
create entity "actor" with texture {$hero}
set entity "actor" animation sheet to (16, 16) columns 4 rows 8
)CX");

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "invalid-clip test fixture should initialize");
    runtime.executeInline("define animation \"broken\" for entity \"actor\" frames 2 to 4 row 0 at 8 fps once");
    expect(runtime.hasErrors(), "a clip that exceeds the sheet columns should produce a diagnostic");
    expect(!runtime.sprites().at("actor").animationClips.contains("broken"), "invalid clip definition must not partially modify the entity");
    cx::Runtime missingClipRuntime(root);
    expect(missingClipRuntime.loadScripts(), "missing-clip test fixture should initialize");
    missingClipRuntime.executeInline("play animation \"missing\" for entity \"actor\"");
    expect(missingClipRuntime.hasErrors(), "playing an undefined clip should report a useful runtime error");
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        testOneShotClipAndCompletionEvent();
        testLoopingClipWrapsWithoutCompletion();
        testReplacingEntityInvalidatesLaterCompletionHandlers();
        testRestartingPlaybackInvalidatesLaterCompletionHandlers();
        testInvalidClipDoesNotInstallPartialDefinition();
        std::cout << "CX_ANIMATION_TESTS_OK checks=passed failures=0\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Animation test failure: " << error.what() << '\n';
        return 1;
    }
}
