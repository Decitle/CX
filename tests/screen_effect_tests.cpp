#include "cx/runtime.hpp"
#include "cx/script.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void write(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << contents;
    if (!output) throw std::runtime_error("could not write screen effect fixture");
}
}

int main() {
    const std::string script = R"CX(
on load:
    set screen opacity to 0.25
    fade screen to black over 1 second
    shake screen for 3.5 seconds with amplitude 0.35
)CX";
    const auto parsed = cx::ScriptParser{}.parseText(script);
    if (!parsed.diagnostics.empty()) {
        for (const auto& d : parsed.diagnostics) std::cerr << d.message << " " << d.suggestion << '\n';
        return 1;
    }
    const auto root = std::filesystem::temp_directory_path() / "cx-screen-effect-tests";
    std::filesystem::remove_all(root);
    write(root / "scripts" / "main.cx", script);
    cx::Runtime runtime(root);
    if (!runtime.loadScripts() || runtime.hasErrors()) {
        for (const auto& d : runtime.diagnostics()) std::cerr << d.message << '\n';
        std::filesystem::remove_all(root);
        return 1;
    }
    const auto beforeTick = runtime.screenEffects();
    if (std::abs(beforeTick.fadeOpacity - 0.25F) > 0.0001F ||
        std::abs(beforeTick.shakeAmplitudeTiles - 0.35F) > 0.0001F ||
        std::abs(beforeTick.shakeDurationSeconds - 3.5F) > 0.0001F ||
        std::abs(beforeTick.shakeRemainingSeconds - 3.5F) > 0.0001F) {
        std::cerr << "Screen fade/shake state did not preserve the requested starting values.\n";
        return 1;
    }
    runtime.tick();
    const auto afterTick = runtime.screenEffects();
    if (!(afterTick.fadeOpacity > 0.25F && afterTick.fadeOpacity < 1.0F) ||
        !(afterTick.shakeElapsedSeconds > 0.0F && afterTick.shakeRemainingSeconds < 3.5F)) {
        std::cerr << "Screen effects should advance by one fixed logic tick.\n";
        return 1;
    }
    runtime.executeInline("fade screen from black over 500 milliseconds");
    if (std::abs(runtime.screenEffects().fadeOpacity - 1.0F) > 0.0001F) {
        std::cerr << "Fading in should begin at fully black.\n";
        return 1;
    }
    runtime.executeInline("set screen opacity to 0");
    if (runtime.screenEffects().fadeOpacity != 0.0F) {
        std::cerr << "Setting screen opacity should update immediately.\n";
        return 1;
    }
    runtime.executeInline("fade screen to opacity 0.5 over 250 milliseconds");
    runtime.tick();
    if (!(runtime.screenEffects().fadeOpacity > 0.0F && runtime.screenEffects().fadeOpacity < 0.5F)) {
        std::cerr << "Opacity fades should interpolate toward their requested target.\n";
        return 1;
    }
    const auto effects = runtime.screenEffects();
    for (int i = 0; i < 16; ++i) runtime.tick();
    if (runtime.screenEffects().shakeAmplitudeTiles != 0.0F) {
        // The 3.5 second test effect will not expire yet; ensure it remains finite.
        if (!std::isfinite(runtime.screenEffects().shakeRemainingSeconds)) {
            std::cerr << "Screen shake timer should remain finite.\n";
            return 1;
        }
    }
    (void)effects;
    std::filesystem::remove_all(root);
    std::cout << "CX_SCREEN_EFFECT_TESTS_OK\n";
    return 0;
}
