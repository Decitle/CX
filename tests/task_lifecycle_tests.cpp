#include "cx/runtime.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void testWaitContinuationIsCancelledAfterRoomContextChanges() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-wait-room-context-cancel";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "rooms");
    const auto writeRoom = [&](const std::string& name) {
        std::ofstream room(root / "rooms" / (name + ".room.json"));
        room << "{\"schema\":\"cx-room\",\"version\":1,\"name\":\"" << name
             << "\",\"width\":2,\"height\":2,\"tileSize\":16,"
                "\"tiles\":[[null,null],[null,null]],\"entrances\":{\"spawn\":{\"x\":0,\"y\":0}}}";
    };
    writeRoom("hall");
    writeRoom("cave");
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
load room "hall"
load room "cave"
on load:
    enter room "hall" at {entrance::hall::spawn}
on press key "A":
    set {wait_started} to true
    wait 500 milliseconds
    set {stale_wait_continued} to true
on press key "B":
    enter room "cave" at {entrance::cave::spawn}
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "room-context wait fixture should load");
    runtime.pressKey("A");
    expect(runtime.getGlobal("wait_started").truthy(), "the wait handler should execute before suspending");
    runtime.pressKey("B");
    expect(runtime.rooms().activeRoomName() == "cave", "the second handler should transition to the cave");
    runtime.tick(); // Remove the stale context even though its deadline has not elapsed.
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    runtime.tick();
    expect(runtime.getGlobal("stale_wait_continued").isNull(),
           "a suspended continuation from the old room must not resume after a room transition");
    expect(!runtime.hasErrors(), "cancelling a stale wait should not generate a runtime error");
    std::filesystem::remove_all(root);
}

void testWaitsDueTogetherRespectRoomContextAndCanRewaitAfterTransition() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-wait-room-context-due";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    std::filesystem::create_directories(root / "rooms");
    for (const auto& name : {std::string("hall"), std::string("cave")}) {
        std::ofstream room(root / "rooms" / (name + ".room.json"));
        room << "{\"schema\":\"cx-room\",\"version\":1,\"name\":\"" << name
             << "\",\"width\":1,\"height\":1,\"tileSize\":16,\"tiles\":[[null]],"
                "\"entrances\":{\"spawn\":{\"x\":0,\"y\":0}}}";
    }
    {
        std::ofstream script(root / "scripts" / "main.cx");
        script << R"CX(
load room "hall"
load room "cave"
on load:
    enter room "hall" at {entrance::hall::spawn}
on press key "A":
    wait 1 milliseconds
    enter room "cave" at {entrance::cave::spawn}
    set {first_continued} to true
    wait 1 milliseconds
    set {new_context_wait_survived} to true
on press key "A":
    wait 1 milliseconds
    set {stale_ready_context_ran} to true
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "same-tick wait fixture should load");
    runtime.pressKey("A");
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    runtime.tick();
    expect(runtime.rooms().activeRoomName() == "cave", "the first ready continuation should change the room");
    expect(runtime.getGlobal("first_continued").truthy(), "the transition-causing continuation should finish its current run");
    expect(runtime.getGlobal("stale_ready_context_ran").isNull(),
           "a second continuation already ready on the same tick must be cancelled after the first changes rooms");
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    runtime.tick();
    expect(runtime.getGlobal("new_context_wait_survived").truthy(),
           "a continuation that waits again after entering the new room should belong to that new context");
    expect(!runtime.hasErrors(), "room-context-aware waits should not create runtime errors");
    std::filesystem::remove_all(root);
}

void testFailedReloadPreservesExistingWaitContinuation() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-failed-reload-keeps-wait";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    const auto scriptPath = root / "scripts" / "main.cx";
    {
        std::ofstream script(scriptPath);
        script << R"CX(
on press key "A":
    wait 100 milliseconds
    set {old_wait_continued} to true
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "the last-good runtime should load before testing failed reload");
    runtime.pressKey("A");
    {
        std::ofstream script(scriptPath, std::ios::trunc);
        script << "on press key \"A\"\n    set {broken} to true\n";
    }
    expect(!runtime.loadScripts(), "a malformed candidate should fail to reload");
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    runtime.tick();
    expect(runtime.getGlobal("old_wait_continued").truthy(),
           "a failed transactional reload must leave the previous runtime's pending wait intact");
    std::filesystem::remove_all(root);
}

void testSuccessfulReloadReplacesOldWaitAndKeepsOnLoadWait() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-successful-reload-wait";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    const auto scriptPath = root / "scripts" / "main.cx";
    {
        std::ofstream script(scriptPath);
        script << R"CX(
on press key "A":
    wait 100 milliseconds
    set {old_wait_continued} to true
)CX";
    }
    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "the initial runtime should load before scheduling an old wait");
    runtime.pressKey("A");
    {
        std::ofstream script(scriptPath, std::ios::trunc);
        script << R"CX(
on load:
    wait 1 milliseconds
    set {on_load_continued} to true
)CX";
    }
    expect(runtime.loadScripts(), "a valid candidate with a startup wait should commit");
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    runtime.tick();
    expect(runtime.getGlobal("on_load_continued").truthy(),
           "the successful staged runtime must rebase and keep its own on-load wait after commit");
    expect(runtime.getGlobal("old_wait_continued").isNull(),
           "a successful reload must replace waits that belong to the previous program tree");
    expect(!runtime.hasErrors(), "a committed on-load wait should not be incorrectly treated as stale");
    std::filesystem::remove_all(root);
}

void testSuccessfulReloadRestartsPeriodicDeadlines() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-reload-periodic-deadlines";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    const auto scriptPath = root / "scripts" / "main.cx";
    {
        std::ofstream script(scriptPath);
        script << R"CX(
every 40 milliseconds:
    add 1 to {old_periodic_runs}
)CX";
    }

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(), "the initial short-period program should load");
    std::this_thread::sleep_for(std::chrono::milliseconds(55)); // The old deadline is now overdue.
    {
        std::ofstream script(scriptPath, std::ios::trunc);
        script << R"CX(
every 200 milliseconds:
    add 1 to {new_periodic_runs}
)CX";
    }
    expect(runtime.loadScripts(false), "the replacement program should load with a new periodic definition");
    runtime.tick();
    expect(runtime.getGlobal("old_periodic_runs").isNull(),
           "the previous program's periodic callback must not survive successful reload");
    expect(runtime.getGlobal("new_periodic_runs").isNull(),
           "the candidate's periodic deadline must begin after reload instead of inheriting an overdue deadline");

    std::this_thread::sleep_for(std::chrono::milliseconds(220));
    runtime.tick();
    expect(runtime.getGlobal("new_periodic_runs").numberOr() == 1.0,
           "the replacement periodic handler should run after its own full interval elapses");
    expect(!runtime.hasErrors(), "restarting periodic deadlines should not create runtime errors");
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        testWaitContinuationIsCancelledAfterRoomContextChanges();
        testWaitsDueTogetherRespectRoomContextAndCanRewaitAfterTransition();
        testFailedReloadPreservesExistingWaitContinuation();
        testSuccessfulReloadReplacesOldWaitAndKeepsOnLoadWait();
        testSuccessfulReloadRestartsPeriodicDeadlines();
    } catch (const std::exception& exception) {
        std::cerr << "TEST FAILED: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All CX Engine task-lifecycle tests passed.\n";
    return 0;
}
