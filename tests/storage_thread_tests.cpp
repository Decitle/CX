#include "cx/value.hpp"
#include "cx/variable_storage.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void verifyFinalSave(const std::filesystem::path& root, const std::string& phase) {
    cx::VariableStorage::VariableMap loaded;
    std::string warning;
    expect(cx::VariableStorage::load(root, loaded, warning),
           phase + ": the final committed save should load: " + warning);
    expect(loaded.contains("writer") && loaded.contains("iteration") && loaded.contains("payload"),
           phase + ": the save should contain every field from one complete writer map");

    const int writer = static_cast<int>(loaded.at("writer").numberOr(-1.0));
    const int iteration = static_cast<int>(loaded.at("iteration").numberOr(-1.0));
    expect(writer >= 0 && writer < 4, phase + ": writer ID should belong to a known worker");
    expect(iteration >= 0 && iteration < 12, phase + ": iteration should belong to a known save");

    const std::string payload = loaded.at("payload").toString();
    const char expectedByte = static_cast<char>('A' + writer);
    expect(payload.size() == 32U * 1024U,
           phase + ": payload length should not be truncated or combined with another writer");
    expect(std::all_of(payload.begin(), payload.end(), [expectedByte](char value) {
        return value == expectedByte;
    }), phase + ": payload should be one intact writer's data, not interleaved bytes");
}

void testConcurrentWritersLeaveOneCompleteRecoverableGeneration() {
    // A unique folder lets this test run repeatedly or alongside another test process.
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("cx-engine-storage-threads-" + std::to_string(stamp));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    constexpr int writerCount = 4;
    constexpr int saveCount = 12;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::vector<std::string> errors(static_cast<std::size_t>(writerCount));
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(writerCount));

    for (int writer = 0; writer < writerCount; ++writer) {
        workers.emplace_back([&, writer] {
            ready.fetch_add(1, std::memory_order_release);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();

            for (int iteration = 0; iteration < saveCount; ++iteration) {
                cx::VariableStorage::VariableMap values;
                values.emplace("writer", cx::Value(writer));
                values.emplace("iteration", cx::Value(iteration));
                values.emplace("payload", cx::Value(std::string(32U * 1024U,
                                      static_cast<char>('A' + writer))));

                std::string error;
                if (!cx::VariableStorage::save(root, values, error)) {
                    errors[static_cast<std::size_t>(writer)] =
                        "writer " + std::to_string(writer) + " failed at save " +
                        std::to_string(iteration) + ": " + error;
                    return;
                }
            }
        });
    }

    while (ready.load(std::memory_order_acquire) != writerCount) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (auto& worker : workers) worker.join();

    for (const auto& error : errors) expect(error.empty(), error);
    expect(std::filesystem::exists(root / ".variables.lock"),
           "the project-local coordination lock should remain present after all writers exit");
    expect(std::filesystem::exists(root / "variables.csv") &&
           std::filesystem::exists(root / "variables.cxv") &&
           std::filesystem::exists(root / "variables.meta"),
           "all three files in the latest committed save set should exist");

    verifyFinalSave(root, "CSV-backed load after concurrent writers");

    // Removing the editable CSV forces the exact cache-recovery path used after a
    // missing/corrupt CSV. The generation manifest must match the same last writer.
    std::filesystem::remove(root / "variables.csv");
    verifyFinalSave(root, "manifest-verified cache recovery after concurrent writers");

    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        testConcurrentWritersLeaveOneCompleteRecoverableGeneration();
    } catch (const std::exception& exception) {
        std::cerr << "TEST FAILED: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All CX Engine concurrent storage thread tests passed.\n";
    return 0;
}
