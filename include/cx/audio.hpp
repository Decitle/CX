#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace cx {

enum class WaveformKind { Sine, Square, Triangle, Saw, Noise };

struct WaveLayer {
    WaveformKind kind{WaveformKind::Sine};
    double frequency{440.0};
    double amplitude{0.5};
    double phaseRadians{0.0};
    std::uint32_t seed{1};
};

struct WaveEnvelope {
    double attackSeconds{0.005};
    double decaySeconds{0.04};
    double sustainLevel{0.65};
    double releaseSeconds{0.08};
};

// WaveDefinition keeps the recipe (layers/envelope) alongside its generated samples.
// Rebuilding the recipe regenerates samples; saving/playback reads those samples.
struct WaveDefinition {
    std::string name;
    unsigned int sampleRate{44100};
    double durationSeconds{0.25};
    WaveEnvelope envelope;
    std::vector<WaveLayer> layers;
    std::vector<float> samples; // Mono floating point audio, clamped to [-1, 1].
    std::uint64_t revision{0};
};

class AudioSystem {
public:
    void reset();
    // Discover supported audio files under assets/audio/. A filename becomes {@name}.
    [[nodiscard]] bool discoverAssets(const std::filesystem::path& projectRoot, std::string& warning);
    [[nodiscard]] const std::filesystem::path* findAssetPath(std::string_view name) const noexcept;
    [[nodiscard]] const std::map<std::string, std::filesystem::path>& assets() const noexcept { return assets_; }
    [[nodiscard]] bool createWave(std::string_view name, double durationSeconds,
                                  unsigned int sampleRate, std::string& error);
    [[nodiscard]] bool addLayer(std::string_view name, WaveLayer layer, std::string& error);
    [[nodiscard]] bool setEnvelope(std::string_view name, WaveEnvelope envelope, std::string& error);
    [[nodiscard]] bool normalize(std::string_view name, std::string& error);
    [[nodiscard]] bool saveWav(std::string_view name, const std::filesystem::path& output,
                               std::string& error) const;
    [[nodiscard]] bool saveSvg(std::string_view name, const std::filesystem::path& output,
                               std::string& error) const;
    [[nodiscard]] bool requestPlay(std::string_view name, std::string& error);
    [[nodiscard]] const WaveDefinition* find(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<std::string> takePlayRequests();
    [[nodiscard]] const std::map<std::string, WaveDefinition>& waves() const noexcept { return waves_; }

    [[nodiscard]] static double noise2d(double x, double y, std::uint32_t seed = 0) noexcept;
    [[nodiscard]] static double fbm2d(double x, double y, std::uint32_t seed = 0,
                                      unsigned int octaves = 4) noexcept;

private:
    std::map<std::string, WaveDefinition> waves_;
    std::map<std::string, std::filesystem::path> assets_;
    std::vector<std::string> playRequests_;
    std::uint64_t revisionCounter_{0};

    [[nodiscard]] bool rebuild(WaveDefinition& wave, std::string& error);
};

[[nodiscard]] std::string normalizeAudioName(std::string_view raw);

} // namespace cx
