#include "cx/variable_storage.hpp"

#include <chrono>
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using Clock = std::chrono::steady_clock;

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string readBytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not read " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

cx::VariableStorage::VariableMap scoreMap(double score) {
    cx::VariableStorage::VariableMap values;
    values.emplace("score", cx::Value(score));
    return values;
}

int childSave(const std::filesystem::path& root) {
    std::string error;
    if (!cx::VariableStorage::save(root, scoreMap(2.0), error)) {
        std::cerr << "child save unexpectedly returned before interruption: " << error << '\n';
        return 20;
    }
    return 0;
}

int childConcurrentSave(const std::filesystem::path& root, int writer, int iterations) {
    const auto gate = root / "writers.go";
    const auto gateDeadline = Clock::now() + std::chrono::seconds(8);
    while (Clock::now() < gateDeadline && !std::filesystem::exists(gate)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!std::filesystem::exists(gate)) {
        std::cerr << "concurrent writer start gate timed out\n";
        return 25;
    }
    for (int iteration = 0; iteration < iterations; ++iteration) {
        cx::VariableStorage::VariableMap values;
        values.emplace("score", cx::Value(static_cast<double>(writer * 1000 + iteration)));
        values.emplace("writer", cx::Value(std::string("writer-") + std::to_string(writer)));
        values.emplace("payload", cx::Value(std::string(32768, writer == 1 ? 'A' : 'B')));
        std::string error;
        if (!cx::VariableStorage::save(root, values, error)) {
            std::cerr << "concurrent writer " << writer << " failed at iteration " << iteration
                      << ": " << error << '\n';
            return 24;
        }
    }
    return 0;
}

void expectEquivalent(const cx::VariableStorage::VariableMap& left,
                      const cx::VariableStorage::VariableMap& right) {
    expect(left.size() == right.size(), "CSV and cache contain different variable counts after concurrent saves");
    for (const auto& [name, value] : left) {
        const auto found = right.find(name);
        expect(found != right.end() && found->second.toString() == value.toString(),
               "CSV and cache disagree for variable '" + name + "' after concurrent saves");
    }
}

void waitForSuccessWithTimeout(pid_t pid, std::chrono::seconds timeout) {
    const auto deadline = Clock::now() + timeout;
    int status = 0;
    while (Clock::now() < deadline) {
        const pid_t result = ::waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            expect(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                   "concurrent writer process failed (status=" + std::to_string(status) + ")");
            return;
        }
        if (result < 0 && errno != EINTR) throw std::runtime_error("waitpid for concurrent writer failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("concurrent writer exceeded its timeout; caller will terminate and reap it");
}

int childCheck(const std::filesystem::path& root, double expectedScore) {
    cx::VariableStorage::VariableMap values;
    std::string warning;
    if (!cx::VariableStorage::load(root, values, warning)) {
        std::cerr << "child load failed: " << warning << '\n';
        return 21;
    }
    const auto found = values.find("score");
    if (found == values.end() || found->second.numberOr(-999.0) != expectedScore) {
        std::cerr << "unexpected score after process restart; expected " << expectedScore << '\n';
        return 22;
    }
    if (!warning.empty()) {
        std::cerr << "unexpected load warning: " << warning << '\n';
        return 23;
    }
    return 0;
}

int childCheckAfterReady(const std::filesystem::path& root, double expectedScore) {
    std::ofstream ready(root / "recovery-reader-ready", std::ios::binary | std::ios::trunc);
    if (!ready) return 26;
    ready << "ready\n";
    ready.close();
    return childCheck(root, expectedScore);
}

pid_t spawn(const std::filesystem::path& executable, const std::vector<std::string>& args) {
    const pid_t pid = ::fork();
    if (pid < 0) throw std::runtime_error("fork failed");
    if (pid == 0) {
        std::vector<char*> raw;
        raw.reserve(args.size() + 2);
        raw.push_back(const_cast<char*>(executable.c_str()));
        for (const auto& arg : args) raw.push_back(const_cast<char*>(arg.c_str()));
        raw.push_back(nullptr);
        ::execv(executable.c_str(), raw.data());
        _exit(127);
    }
    return pid;
}

int waitForSuccess(pid_t pid) {
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;
        throw std::runtime_error("waitpid failed");
    }
    expect(WIFEXITED(status) && WEXITSTATUS(status) == 0,
           "child process did not exit successfully (status=" + std::to_string(status) + ")");
    return status;
}

void testKilledWriterLeavesReadableAuthoritativeCsvAndManualRecoveryCopies(
    const std::filesystem::path& executable) {
    const auto root = std::filesystem::temp_directory_path() /
        ("cx-engine-killed-save-" + std::to_string(static_cast<long long>(::getpid())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    pid_t writer = -1;
    bool writerReaped = false;

    try {
        // Create a known complete generation before interrupting the next save.
        std::string error;
        expect(cx::VariableStorage::save(root, scoreMap(1.0), error),
               "initial save should establish a complete generation: " + error);
        const auto csvV1 = readBytes(root / "variables.csv");
        const auto cacheV1 = readBytes(root / "variables.cxv");
        expect(!csvV1.empty() && !cacheV1.empty(), "initial generation should have CSV and cache files");

        // This FIFO is a deterministic barrier. The next save commits variables.csv first,
        // then blocks opening variables.cxv.tmp for writing because no reader has the FIFO open.
        // The parent can kill the child at the real file-commit boundary without a production
        // failpoint or test-only branch in VariableStorage.
        const auto cacheTemp = root / "variables.cxv.tmp";
        expect(::mkfifo(cacheTemp.c_str(), 0600) == 0, "could not create cache-temp FIFO barrier");

        writer = spawn(executable, {"--child-save", root.string()});
        const std::string csvV2 = "# CX Engine persistent variables. Edit or delete rows while the engine is closed.\n"
                                  "# name,type,value\nscore,number,2\n";
        bool sawCommittedCsv = false;
        int status = 0;
        const auto deadline = Clock::now() + std::chrono::seconds(8);
        while (Clock::now() < deadline) {
            const auto done = ::waitpid(writer, &status, WNOHANG);
            if (done == writer) {
                writerReaped = true;
                throw std::runtime_error("writer exited before the test observed the CSV commit (status=" +
                                         std::to_string(status) + ")");
            }
            if (done < 0 && errno != EINTR) throw std::runtime_error("waitpid(WNOHANG) failed");
            std::error_code ec;
            if (std::filesystem::is_regular_file(root / "variables.csv", ec) && !ec) {
                if (readBytes(root / "variables.csv") == csvV2) {
                    sawCommittedCsv = true;
                    break;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        expect(sawCommittedCsv, "timed out waiting for the second generation's authoritative CSV commit");

        // Allow the child to reach the FIFO open. It must still be alive because no process
        // can complete the cache-temp open until a reader opens the FIFO.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        expect(::waitpid(writer, &status, WNOHANG) == 0,
               "writer unexpectedly completed instead of blocking at the cache replacement stage");
        expect(::kill(writer, SIGKILL) == 0, "could not terminate writer at the intended interruption point");
        while (::waitpid(writer, &status, 0) < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("waitpid after writer kill failed");
        }
        writerReaped = true;
        expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
               "writer was not terminated by SIGKILL as intended");

        // At this boundary, CSV contains generation 2; cache and .bak files retain generation 1.
        expect(readBytes(root / "variables.csv") == csvV2,
               "the authoritative CSV should be the new, complete generation after interruption");
        expect(readBytes(root / "variables.cxv") == cacheV1,
               "the cache should still be the prior complete generation after interruption");
        expect(readBytes(root / "variables.csv.bak") == csvV1,
               "the CSV recovery copy should retain the prior complete generation");
        expect(readBytes(root / "variables.cxv.bak") == cacheV1,
               "the cache recovery copy should retain the prior complete generation");
        expect(std::filesystem::is_fifo(cacheTemp), "the blocked cache temp should remain a FIFO test barrier");

        // A real new process confirms that the engine honors the committed CSV, not stale cache.
        waitForSuccess(spawn(executable, {"--child-check", root.string(), "2"}));

        // Model the documented, deliberate manual recovery while the game is closed: preserve
        // a copy of the newer CSV, then restore the paired previous-generation backups.
        std::filesystem::copy_file(root / "variables.csv", root / "variables.csv.after-interruption",
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(root / "variables.csv.bak", root / "variables.csv",
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(root / "variables.cxv.bak", root / "variables.cxv",
                                   std::filesystem::copy_options::overwrite_existing);
        waitForSuccess(spawn(executable, {"--child-check", root.string(), "1"}));

        std::cout << "PASS: SIGKILL after CSV commit; fresh-process load and manual .bak recovery both verified.\n";
    } catch (...) {
        // Do not leak a blocked child if an assertion fails before the intended SIGKILL.
        if (writer > 0 && !writerReaped) {
            // ESRCH means the child already exited; otherwise issue the kill and still wait
            // below so the parent never leaves an unreaped child behind.
            if (::kill(writer, SIGKILL) < 0 && errno != ESRCH) {
                std::cerr << "warning: could not kill blocked storage-test child: " << errno << '\n';
            }
            int ignoredStatus = 0;
            while (::waitpid(writer, &ignoredStatus, 0) < 0 && errno == EINTR) {}
        }
        std::filesystem::remove_all(root);
        throw;
    }
    std::filesystem::remove_all(root);
}

void testCacheRecoveryDoesNotOverwriteCsvCommittedWhileWaitingForLock(
    const std::filesystem::path& executable) {
    const auto root = std::filesystem::temp_directory_path() /
        ("cx-engine-cache-recovery-lock-" + std::to_string(static_cast<long long>(::getpid())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    pid_t reader = -1;
    bool readerReaped = false;
    int lockFd = -1;
    bool lockHeld = false;
    try {
        std::string error;
        expect(cx::VariableStorage::save(root, scoreMap(1.0), error),
               "initial save for cache-recovery lock test failed: " + error);
        std::filesystem::remove(root / "variables.csv");
        lockFd = ::open((root / ".variables.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        expect(lockFd >= 0, "could not open project lock for cache-recovery race test");
        expect(::flock(lockFd, LOCK_EX) == 0, "could not hold project lock for cache-recovery race test");
        lockHeld = true;

        reader = spawn(executable, {"--child-check-ready", root.string(), "2"});
        const auto readyDeadline = Clock::now() + std::chrono::seconds(5);
        while (Clock::now() < readyDeadline && !std::filesystem::exists(root / "recovery-reader-ready")) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        expect(std::filesystem::exists(root / "recovery-reader-ready"),
               "cache recovery child did not reach its ready marker");
        // The child has announced immediately before load(). Give it time to decode the
        // old cache and reach the lock acquisition while the authoritative CSV is absent.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        int status = 0;
        expect(::waitpid(reader, &status, WNOHANG) == 0,
               "cache recovery did not wait for the held project lock");
        expect(!std::filesystem::exists(root / "variables.csv"),
               "cache recovery wrote a CSV before acquiring the held project lock");

        const std::string newerCsv = "# CX Engine persistent variables. Edit or delete rows while the engine is closed.\n"
                                     "# name,type,value\nscore,number,2\n";
        {
            std::ofstream output(root / "variables.csv", std::ios::binary | std::ios::trunc);
            expect(static_cast<bool>(output), "could not write newer authoritative CSV in lock-race test");
            output.write(newerCsv.data(), static_cast<std::streamsize>(newerCsv.size()));
            output.flush();
            expect(static_cast<bool>(output), "could not flush newer authoritative CSV in lock-race test");
        }
        expect(::flock(lockFd, LOCK_UN) == 0, "could not release lock in cache-recovery race test");
        lockHeld = false;
        (void)::close(lockFd);
        lockFd = -1;
        waitForSuccessWithTimeout(reader, std::chrono::seconds(12));
        readerReaped = true;
        expect(readBytes(root / "variables.csv") == newerCsv,
               "cache recovery overwrote the newer CSV instead of rechecking authority after locking");
        std::cout << "PASS: cache recovery rechecks a newer authoritative CSV after waiting for the project lock.\n";
    } catch (...) {
        if (lockHeld && lockFd >= 0) (void)::flock(lockFd, LOCK_UN);
        if (lockFd >= 0) (void)::close(lockFd);
        if (reader > 0 && !readerReaped) {
            (void)::kill(reader, SIGKILL);
            int ignored = 0;
            while (::waitpid(reader, &ignored, 0) < 0 && errno == EINTR) {}
        }
        std::filesystem::remove_all(root);
        throw;
    }
    std::filesystem::remove_all(root);
}

void testProjectLockSerializesConcurrentWritersAndKeepsCsvCachePaired(
    const std::filesystem::path& executable) {
    const auto root = std::filesystem::temp_directory_path() /
        ("cx-engine-concurrent-save-" + std::to_string(static_cast<long long>(::getpid())));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    std::vector<pid_t> writers;
    try {
        // Establish a save root and the persistent lock inode before taking the lock
        // externally. The child must wait rather than rotate backups or touch shared .tmp files.
        std::string error;
        expect(cx::VariableStorage::save(root, scoreMap(0.0), error),
               "initial save before lock test failed: " + error);
        const auto initialCsv = readBytes(root / "variables.csv");
        const int lockFd = ::open((root / ".variables.lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        expect(lockFd >= 0, "could not open project lock file for lock-serialization test");
        expect(::flock(lockFd, LOCK_EX) == 0, "could not acquire external project lock");
        const pid_t blockedWriter = spawn(executable, {"--child-save", root.string()});
        writers.push_back(blockedWriter);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        int status = 0;
        expect(::waitpid(blockedWriter, &status, WNOHANG) == 0,
               "save process did not wait for the held project lock");
        expect(readBytes(root / "variables.csv") == initialCsv,
               "blocked writer changed the authoritative CSV before acquiring the lock");
        expect(::flock(lockFd, LOCK_UN) == 0, "could not release external project lock");
        (void)::close(lockFd);
        waitForSuccessWithTimeout(blockedWriter, std::chrono::seconds(12));
        writers.clear();
        waitForSuccess(spawn(executable, {"--child-check", root.string(), "2"}));

        // Now race two independent processes through repeated saves. The lock must
        // serialize backup rotation and the shared .tmp names as one per-project operation.
        const pid_t first = spawn(executable, {"--child-concurrent-save", root.string(), "1", "8"});
        writers.push_back(first);
        const pid_t second = spawn(executable, {"--child-concurrent-save", root.string(), "2", "8"});
        writers.push_back(second);
        {
            std::ofstream gate(root / "writers.go", std::ios::binary | std::ios::trunc);
            expect(static_cast<bool>(gate), "could not create concurrent-writer start gate");
            gate << "start\n";
        }
        waitForSuccessWithTimeout(first, std::chrono::seconds(25));
        writers.erase(writers.begin());
        waitForSuccessWithTimeout(second, std::chrono::seconds(25));
        writers.clear();

        cx::VariableStorage::VariableMap csvValues;
        std::string warning;
        expect(cx::VariableStorage::load(root, csvValues, warning), "could not load CSV after concurrent saves: " + warning);
        expect(warning.empty(), "CSV produced a warning after concurrent saves: " + warning);

        // Hide the authoritative CSV briefly to force the ordinary cache-recovery path.
        // Its decoded values must describe exactly the same completed writer generation.
        const auto csvPath = root / "variables.csv";
        const auto hiddenCsvPath = root / "variables.csv.concurrent-test-hidden";
        std::filesystem::rename(csvPath, hiddenCsvPath);
        cx::VariableStorage::VariableMap cacheValues;
        warning.clear();
        const bool cacheLoaded = cx::VariableStorage::load(root, cacheValues, warning);
        std::error_code restoreError;
        if (std::filesystem::exists(csvPath, restoreError)) std::filesystem::remove(csvPath, restoreError);
        std::filesystem::rename(hiddenCsvPath, csvPath, restoreError);
        expect(!restoreError, "could not restore authoritative CSV after cache consistency check: " + restoreError.message());
        expect(cacheLoaded, "cache recovery failed after concurrent saves: " + warning);
        expect(warning.empty(), "cache recovery warned after concurrent saves: " + warning);
        expectEquivalent(csvValues, cacheValues);
        std::cout << "PASS: project lock serializes competing writers and preserves paired CSV/cache generations.\n";
    } catch (...) {
        for (const pid_t writer : writers) {
            (void)::kill(writer, SIGKILL);
            int ignored = 0;
            while (::waitpid(writer, &ignored, 0) < 0 && errno == EINTR) {}
        }
        std::filesystem::remove_all(root);
        throw;
    }
    std::filesystem::remove_all(root);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--child-save") return childSave(argv[2]);
        if (argc == 4 && std::string(argv[1]) == "--child-check") {
            return childCheck(argv[2], std::stod(argv[3]));
        }
        if (argc == 4 && std::string(argv[1]) == "--child-check-ready") {
            return childCheckAfterReady(argv[2], std::stod(argv[3]));
        }
        if (argc == 5 && std::string(argv[1]) == "--child-concurrent-save") {
            return childConcurrentSave(argv[2], std::stoi(argv[3]), std::stoi(argv[4]));
        }
        const auto executable = std::filesystem::absolute(argv[0]);
        testKilledWriterLeavesReadableAuthoritativeCsvAndManualRecoveryCopies(executable);
        testCacheRecoveryDoesNotOverwriteCsvCommittedWhileWaitingForLock(executable);
        testProjectLockSerializesConcurrentWritersAndKeepsCsvCachePaired(executable);
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "TEST FAILED: " << exception.what() << '\n';
        return 1;
    }
}
