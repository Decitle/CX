#include "cx/diagnostics.hpp"
#include "cx/script.hpp"
#include "cx/runtime.hpp"
#include "cx/text_format.hpp"

#include <algorithm>
#include <filesystem>
#include <vector>
#include <chrono>
#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif
#include <iostream>
#include <string>
#include <string_view>

namespace {

void printUsage() {
    std::cout << "CX Engine v0.10.14 — Skript-style 2D game runtime\n\n"
              << "Usage:\n"
              << "  cx-cli [project-folder]       Load a project and open the terminal console\n"
              << "  cx-cli --check [project-folder]  Check existing scripts without modifying project files\n"
              << "  cx-cli --help                  Show this help\n\n"
              << "Runtime console: /help, /reload, /vars, /settings, /sprites, /paths, /lights,\n"
              << "  /rooms, /textures, /status, /clear, /tick, /press KEY, /click BUTTON_ID,\n"
              << "  /attach \"/path/to/file\" X Y\n"
              << "Type a CX statement directly, for example: broadcast \"Hello!\"\n"
              << "CLI exit commands: /quit, quit, or exit. Use /help for command descriptions.\n";
}

void printNewLogs(const cx::Runtime& runtime, std::size_t& shown) {
    const auto& logs = runtime.logs();
    while (shown < logs.size()) {
        std::cout << "[GAME] " << cx::toAnsi(logs[shown++]) << '\n';
    }
    if (shown > logs.size()) shown = logs.size();
}

void printNewDiagnostics(const cx::Runtime& runtime, std::size_t& shown,
                          const std::filesystem::path& root) {
    const auto& diagnostics = runtime.diagnostics();
    if (shown > diagnostics.size()) shown = 0; // /reload or a new console command may clear them.
    if (shown < diagnostics.size()) {
        const std::vector<cx::Diagnostic> fresh(diagnostics.begin() + static_cast<std::ptrdiff_t>(shown), diagnostics.end());
        cx::printDiagnostics(fresh, root);
        shown = diagnostics.size();
    }
}

int checkProject(const std::filesystem::path& root) {
    cx::ScriptParser parser;
    const auto program = parser.parseDirectory(root / "scripts");
    cx::printDiagnostics(program.diagnostics, root);
    if (cx::hasErrors(program.diagnostics)) {
        std::cerr << "Script check failed. Fix the errors above and run `cx-cli --check` again.\n";
        return 1;
    }
    std::cout << "Script check passed: " << program.filesLoaded << " .cx file(s), "
              << program.handlers.size() << " event handler(s), " << program.canvases.size() << " canvas(es).\n";
    return 0;
}

} // namespace

// The CLI is a thin host for the same Runtime used by the GUI. It is most useful for checking
// scripts and exercising logic/tests; it deliberately does not attempt to draw the game world.
int main(int argc, char** argv) {
    bool checkOnly = false;
    std::filesystem::path root = std::filesystem::current_path();
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--help" || arg == "-h") { printUsage(); return 0; }
        if (arg == "--check") { checkOnly = true; continue; }
        root = std::filesystem::path(argv[i]);
    }
    std::error_code ec;
    root = std::filesystem::absolute(root, ec);
    if (ec) root = std::filesystem::current_path();
    if (checkOnly) return checkProject(root);

    std::cout << "CX Engine v0.10.14 — .cx runtime\n"
              << "Project: " << root.string() << "\n"
              << "Loading scripts...\n";
    cx::Runtime runtime(root);
    const bool loaded = runtime.loadScripts(true);
    cx::printDiagnostics(runtime.diagnostics(), root);
    if (!loaded) {
        std::cerr << "Startup stopped because scripts contain errors. Correct them, then launch again.\n"
                  << "Tip: `cx-cli --check \"" << root.string() << "\"` checks syntax without starting the runtime.\n";
        return 1;
    }
    std::cout << "Loaded " << runtime.program().filesLoaded << " script file(s), "
              << runtime.program().handlers.size() << " event handler(s), and "
              << runtime.program().canvases.size() << " canvas(es).\n"
              << "Type /help for commands. Type /quit to exit.\n\n";

    std::size_t shownLogs = 0;
    std::size_t shownDiagnostics = runtime.diagnostics().size();
    printNewLogs(runtime, shownLogs);
    std::string command;
    auto processCommand = [&](const std::string& raw) -> bool {
        const auto cleaned = cx::trim(raw);
        if (cx::lower(cleaned) == "/quit" || cx::lower(cleaned) == "quit" || cx::lower(cleaned) == "exit") return false;
        if (cleaned.empty()) return true;
        runtime.executeConsole(cleaned);
        cx::printDiagnostics(runtime.diagnostics(), root);
        shownDiagnostics = runtime.diagnostics().size();
        printNewLogs(runtime, shownLogs);
        return true;
    };
#if defined(__unix__) || defined(__APPLE__)
    // Keep timers and `on tick:` alive while the CLI waits for input, without a worker
    // thread mutating the runtime concurrently. This also makes `wait 1 second` useful
    // during interactive script testing.
    auto nextTick = std::chrono::steady_clock::now() + std::chrono::milliseconds(16);
    bool showPrompt = true;
    bool keepRunning = true;
    while (keepRunning) {
        if (showPrompt) {
            std::cout << "cx> " << std::flush;
            showPrompt = false;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto rate = std::max(1U, runtime.settings().tickRate);
        const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / static_cast<double>(rate)));
        if (now >= nextTick) {
            runtime.tick();
            nextTick = now + interval; // Avoid a catch-up spiral after a slow handler.
            printNewDiagnostics(runtime, shownDiagnostics, root);
            printNewLogs(runtime, shownLogs);
            continue;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(nextTick - now).count();
        const int timeoutMs = static_cast<int>(std::clamp<long long>(remaining, 0, 100));
        pollfd descriptor{};
        descriptor.fd = STDIN_FILENO;
        descriptor.events = POLLIN;
        const int result = ::poll(&descriptor, 1, timeoutMs);
        if (result < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[ERROR] Terminal input polling failed.\n";
            break;
        }
        if (result > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
            if (!std::getline(std::cin, command)) break;
            keepRunning = processCommand(command);
            showPrompt = keepRunning;
        }
    }
#else
    // Portable fallback for platforms without poll(): this loop does not advance the
    // runtime automatically. Use /tick to advance on-tick and scheduled wait handlers.
    while (std::cout << "cx> " && std::getline(std::cin, command)) {
        if (!processCommand(command)) break;
    }
#endif
    std::cout << "CX Engine closed.\n";
    return 0;
}
