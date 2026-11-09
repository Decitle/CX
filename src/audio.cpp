#include "cx/audio.hpp"

#include "cx/value.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace cx {
namespace {

// Audio generation is independent from playback: this module builds mono float samples and
// exports files. The graphical frontend may consume play requests, but the CLI can still test
// wave recipes and file output on a machine without an audio device.
constexpr double Pi = 3.1415926535897932384626433832795;

std::uint32_t mixSeed(std::uint32_t x) noexcept {
    x += 0x9e3779b9U;
    x = (x ^ (x >> 16U)) * 0x85ebca6bU;
    x = (x ^ (x >> 13U)) * 0xc2b2ae35U;
    return x ^ (x >> 16U);
}

double fade(double t) noexcept { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
double lerp(double a, double b, double t) noexcept { return a + (b - a) * t; }

void putU16(std::ostream& out, std::uint16_t value) {
    out.put(static_cast<char>(value & 0xffU));
    out.put(static_cast<char>((value >> 8U) & 0xffU));
}
void putU32(std::ostream& out, std::uint32_t value) {
    for (unsigned int i = 0; i < 4; ++i) out.put(static_cast<char>((value >> (i * 8U)) & 0xffU));
}

std::string xmlEscape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

bool openOutput(const std::filesystem::path& path, std::ofstream& file, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = "Could not create output directory: " + ec.message(); return false; }
    file.open(path, std::ios::binary | std::ios::trunc);
    if (!file) { error = "Could not write output file `" + path.string() + "`."; return false; }
    return true;
}

} // namespace

std::string normalizeAudioName(std::string_view raw) {
    return lower(trim(raw));
}

void AudioSystem::reset() { waves_.clear(); assets_.clear(); playRequests_.clear(); revisionCounter_ = 0; }

bool AudioSystem::discoverAssets(const std::filesystem::path& projectRoot, std::string& warning) {
    assets_.clear();
    const auto directory = projectRoot / "assets" / "audio";
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec)) return true; // Audio assets are optional.
    if (ec || !std::filesystem::is_directory(directory, ec)) {
        warning = "assets/audio exists but could not be scanned.";
        return false;
    }
    std::vector<std::filesystem::path> paths;
    for (std::filesystem::recursive_directory_iterator it(directory, ec), end; it != end && !ec; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const auto extension = lower(it->path().extension().string());
        if (extension == ".wav" || extension == ".ogg" || extension == ".flac" || extension == ".aiff" || extension == ".aif")
            paths.push_back(it->path());
    }
    if (ec) {
        warning = "Some audio assets could not be scanned: " + ec.message();
        return false;
    }
    std::sort(paths.begin(), paths.end());
    for (const auto& path : paths) {
        const std::string baseId = normalizeAudioName(path.stem().string());
        if (baseId.empty()) continue;
        std::string id = baseId;
        if (assets_.contains(id)) {
            unsigned int suffix = 2;
            do { id = baseId + std::to_string(suffix++); } while (assets_.contains(id));
            warning = "Audio filename `" + baseId + "` occurs more than once; this file was assigned virtual ID `@" + id + "`. Rename the duplicate file to choose a clearer ID.";
        }
        std::error_code relativeError;
        auto relative = std::filesystem::relative(path, projectRoot, relativeError);
        assets_.emplace(id, relativeError ? path : relative);
    }
    return true;
}

const std::filesystem::path* AudioSystem::findAssetPath(std::string_view rawName) const noexcept {
    const auto found = assets_.find(normalizeAudioName(rawName));
    return found == assets_.end() ? nullptr : &found->second;
}

bool AudioSystem::createWave(std::string_view rawName, double durationSeconds,
                             unsigned int sampleRate, std::string& error) {
    const auto name = normalizeAudioName(rawName);
    if (name.empty()) { error = "A wave needs a name."; return false; }
    if (!std::isfinite(durationSeconds) || durationSeconds <= 0.0 || durationSeconds > 60.0) {
        error = "Wave duration must be greater than 0 and at most 60 seconds."; return false;
    }
    if (sampleRate < 8000U || sampleRate > 192000U) {
        error = "Sample rate must be between 8000 and 192000 Hz."; return false;
    }
    const auto sampleCount = static_cast<std::uint64_t>(std::ceil(durationSeconds * sampleRate));
    if (sampleCount == 0 || sampleCount > 5'760'000U) {
        error = "That wave would be too large. Reduce its duration or sample rate."; return false;
    }
    WaveDefinition wave;
    wave.name = name;
    wave.durationSeconds = durationSeconds;
    wave.sampleRate = sampleRate;
    wave.samples.assign(static_cast<std::size_t>(sampleCount), 0.0F);
    wave.revision = ++revisionCounter_;
    waves_[name] = std::move(wave);
    return true;
}

bool AudioSystem::addLayer(std::string_view rawName, WaveLayer layer, std::string& error) {
    auto found = waves_.find(normalizeAudioName(rawName));
    if (found == waves_.end()) { error = "There is no generated wave named `" + normalizeAudioName(rawName) + "`. Create it first."; return false; }
    if (layer.kind != WaveformKind::Noise && (!std::isfinite(layer.frequency) || layer.frequency <= 0.0 || layer.frequency > 96000.0)) {
        error = "Wave frequency must be greater than 0 and at most 96000 Hz."; return false;
    }
    if (!std::isfinite(layer.amplitude) || std::abs(layer.amplitude) > 4.0) {
        error = "Wave amplitude must be between -4 and 4; use normalize wave if stacked layers clip."; return false;
    }
    if (!std::isfinite(layer.phaseRadians)) { error = "Wave phase must be a finite number of radians."; return false; }
    found->second.layers.push_back(layer);
    if (!rebuild(found->second, error)) { found->second.layers.pop_back(); return false; }
    return true;
}

bool AudioSystem::setEnvelope(std::string_view rawName, WaveEnvelope envelope, std::string& error) {
    auto found = waves_.find(normalizeAudioName(rawName));
    if (found == waves_.end()) { error = "There is no generated wave named `" + normalizeAudioName(rawName) + "`."; return false; }
    if (!std::isfinite(envelope.attackSeconds) || !std::isfinite(envelope.decaySeconds) ||
        !std::isfinite(envelope.releaseSeconds) || !std::isfinite(envelope.sustainLevel) ||
        envelope.attackSeconds < 0.0 || envelope.decaySeconds < 0.0 || envelope.releaseSeconds < 0.0 ||
        envelope.attackSeconds > 10.0 || envelope.decaySeconds > 10.0 || envelope.releaseSeconds > 10.0 ||
        envelope.sustainLevel < 0.0 || envelope.sustainLevel > 1.0) {
        error = "Envelope times must be 0–10 seconds and sustain must be 0–1."; return false;
    }
    found->second.envelope = envelope;
    return rebuild(found->second, error);
}

// Re-render the wave recipe from its layers. For each sample position, the layers are mixed,
// an amplitude envelope is applied, and the result is clipped to the range [-1, 1] so later
// conversion to a WAV integer sample cannot overflow the intended audio range.
bool AudioSystem::rebuild(WaveDefinition& wave, std::string& error) {
    const double rawCount = std::ceil(wave.durationSeconds * static_cast<double>(wave.sampleRate));
    if (!std::isfinite(rawCount) || rawCount <= 0.0 || rawCount > 5'760'000.0) {
        error = "The wave sample count is outside the supported range."; return false;
    }
    const auto count = static_cast<std::size_t>(rawCount);
    wave.samples.assign(count, 0.0F);
    for (const WaveLayer& layer : wave.layers) {
        std::uint32_t rng = mixSeed(layer.seed);
        for (std::size_t i = 0; i < count; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(wave.sampleRate);
            const double phase = 2.0 * Pi * layer.frequency * t + layer.phaseRadians;
            const double sine = std::sin(phase);
            double sample = 0.0;
            switch (layer.kind) {
                case WaveformKind::Sine: sample = sine; break;
                case WaveformKind::Square: sample = sine >= 0.0 ? 1.0 : -1.0; break;
                case WaveformKind::Triangle: sample = (2.0 / Pi) * std::asin(sine); break;
                case WaveformKind::Saw: {
                    const double cycles = phase / (2.0 * Pi);
                    sample = 2.0 * (cycles - std::floor(cycles + 0.5));
                    break;
                }
                case WaveformKind::Noise: {
                    rng ^= rng << 13U; rng ^= rng >> 17U; rng ^= rng << 5U;
                    sample = (static_cast<double>(rng) / static_cast<double>(std::numeric_limits<std::uint32_t>::max())) * 2.0 - 1.0;
                    break;
                }
            }
            wave.samples[i] += static_cast<float>(sample * layer.amplitude);
        }
    }

    const auto env = wave.envelope;
    const double releaseStart = std::max(env.attackSeconds + env.decaySeconds, wave.durationSeconds - env.releaseSeconds);
    for (std::size_t i = 0; i < wave.samples.size(); ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(wave.sampleRate);
        double gain = 1.0;
        if (env.attackSeconds > 0.0 && t < env.attackSeconds) gain = t / env.attackSeconds;
        else if (env.decaySeconds > 0.0 && t < env.attackSeconds + env.decaySeconds) {
            const double progress = (t - env.attackSeconds) / env.decaySeconds;
            gain = lerp(1.0, env.sustainLevel, progress);
        } else gain = env.sustainLevel;
        if (env.releaseSeconds > 0.0 && t >= releaseStart) {
            const double progress = std::clamp((t - releaseStart) / std::max(0.000001, wave.durationSeconds - releaseStart), 0.0, 1.0);
            gain *= 1.0 - progress;
        }
        wave.samples[i] = std::clamp(wave.samples[i] * static_cast<float>(gain), -1.0F, 1.0F);
    }
    wave.revision = ++revisionCounter_;
    return true;
}

bool AudioSystem::normalize(std::string_view rawName, std::string& error) {
    auto found = waves_.find(normalizeAudioName(rawName));
    if (found == waves_.end()) { error = "There is no generated wave named `" + normalizeAudioName(rawName) + "`."; return false; }
    float peak = 0.0F;
    for (const float sample : found->second.samples) peak = std::max(peak, std::abs(sample));
    if (peak > 0.000001F) for (float& sample : found->second.samples) sample = std::clamp(sample * (0.98F / peak), -1.0F, 1.0F);
    found->second.revision = ++revisionCounter_;
    return true;
}

// WAV export is a conversion boundary: internal samples are normalized floats, while the
// PCM data written here is 16-bit integer audio. The header sizes must match the exact sample
// count so standard media players can read the file without engine-specific knowledge.
bool AudioSystem::saveWav(std::string_view rawName, const std::filesystem::path& path, std::string& error) const {
    const auto* wave = find(rawName);
    if (!wave) { error = "There is no generated wave named `" + normalizeAudioName(rawName) + "`."; return false; }
    if (wave->samples.size() > (std::numeric_limits<std::uint32_t>::max() - 36U) / 2U) { error = "Wave is too long for a standard WAV file."; return false; }
    std::ofstream output;
    if (!openOutput(path, output, error)) return false;
    const auto dataBytes = static_cast<std::uint32_t>(wave->samples.size() * 2U);
    output.write("RIFF", 4); putU32(output, 36U + dataBytes); output.write("WAVE", 4);
    output.write("fmt ", 4); putU32(output, 16U); putU16(output, 1U); putU16(output, 1U);
    putU32(output, wave->sampleRate); putU32(output, wave->sampleRate * 2U); putU16(output, 2U); putU16(output, 16U);
    output.write("data", 4); putU32(output, dataBytes);
    for (const float sample : wave->samples) {
        const auto clipped = std::clamp(sample, -1.0F, 1.0F);
        const auto pcm = static_cast<std::int16_t>(std::lround(clipped * (clipped < 0.0F ? 32768.0F : 32767.0F)));
        putU16(output, static_cast<std::uint16_t>(pcm));
    }
    if (!output) { error = "Writing the WAV file failed."; return false; }
    return true;
}

bool AudioSystem::saveSvg(std::string_view rawName, const std::filesystem::path& path, std::string& error) const {
    const auto* wave = find(rawName);
    if (!wave) { error = "There is no generated wave named `" + normalizeAudioName(rawName) + "`."; return false; }
    std::ofstream output;
    if (!openOutput(path, output, error)) return false;
    constexpr int width = 1200, height = 360, margin = 24;
    const std::size_t points = std::min<std::size_t>(wave->samples.size(), 2400);
    output << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width << "\" height=\"" << height
           << "\" viewBox=\"0 0 " << width << ' ' << height << "\">\n"
           << "<rect width=\"100%\" height=\"100%\" fill=\"#111827\"/>\n"
           << "<text x=\"24\" y=\"20\" font-family=\"monospace\" font-size=\"14\" fill=\"#cbd5e1\">"
           << xmlEscape(wave->name) << " · " << wave->sampleRate << " Hz · " << std::fixed << std::setprecision(3) << wave->durationSeconds << " s</text>\n"
           << "<line x1=\"" << margin << "\" x2=\"" << width-margin << "\" y1=\"" << height/2 << "\" y2=\"" << height/2 << "\" stroke=\"#475569\"/>\n"
           << "<polyline fill=\"none\" stroke=\"#38bdf8\" stroke-width=\"1.2\" points=\"";
    if (points > 0) for (std::size_t i = 0; i < points; ++i) {
        const auto index = points == 1 ? 0 : (i * (wave->samples.size() - 1)) / (points - 1);
        const double x = margin + static_cast<double>(i) * (width - 2 * margin) / static_cast<double>(std::max<std::size_t>(1, points - 1));
        const double y = height / 2.0 - static_cast<double>(wave->samples[index]) * (height / 2.0 - margin - 6);
        output << x << ',' << y << ' ';
    }
    output << "/>\n</svg>\n";
    if (!output) { error = "Writing the SVG waveform preview failed."; return false; }
    return true;
}

bool AudioSystem::requestPlay(std::string_view rawName, std::string& error) {
    const std::string raw = trim(rawName);
    if (raw.empty()) { error = "play sound needs a name, generated wave ID, or path to an audio file."; return false; }
    const bool looksLikePath = raw.find('/') != std::string::npos || raw.find('\\') != std::string::npos ||
        lower(std::filesystem::path(raw).extension().string()) == ".wav" || lower(std::filesystem::path(raw).extension().string()) == ".ogg";
    playRequests_.push_back(looksLikePath ? raw : normalizeAudioName(raw));
    return true;
}

const WaveDefinition* AudioSystem::find(std::string_view rawName) const noexcept {
    const auto found = waves_.find(normalizeAudioName(rawName));
    return found == waves_.end() ? nullptr : &found->second;
}

std::vector<std::string> AudioSystem::takePlayRequests() {
    std::vector<std::string> out;
    out.swap(playRequests_);
    return out;
}

// Seeded 2D noise should return the same result for the same inputs. Determinism matters for
// reproducible procedural effects and scripts: opening the same project should not change a
// generated value simply because an unrelated frame happened first.
double AudioSystem::noise2d(double x, double y, std::uint32_t seed) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y)) return 0.0;
    const auto ix = static_cast<std::int64_t>(std::floor(x));
    const auto iy = static_cast<std::int64_t>(std::floor(y));
    const double fx = fade(x - static_cast<double>(ix));
    const double fy = fade(y - static_cast<double>(iy));
    const auto lattice = [seed](std::int64_t a, std::int64_t b) {
        const std::uint64_t ua = static_cast<std::uint64_t>(a);
        const std::uint64_t ub = static_cast<std::uint64_t>(b);
        const std::uint32_t h = mixSeed(seed ^ static_cast<std::uint32_t>(ua) ^ (static_cast<std::uint32_t>(ub) * 0x9e3779b9U));
        return static_cast<double>(h) / static_cast<double>(std::numeric_limits<std::uint32_t>::max()) * 2.0 - 1.0;
    };
    const double a = lerp(lattice(ix, iy), lattice(ix + 1, iy), fx);
    const double b = lerp(lattice(ix, iy + 1), lattice(ix + 1, iy + 1), fx);
    return lerp(a, b, fy);
}

// Fractal Brownian motion (fBm) stacks several noise scales. Each octave samples at a finer
// frequency and contributes less amplitude, giving a richer pattern than a single noise layer.
// This helper is deterministic for the same coordinates, seed, and octave count.
double AudioSystem::fbm2d(double x, double y, std::uint32_t seed, unsigned int octaves) noexcept {
    octaves = std::clamp(octaves, 1U, 12U);
    double total = 0.0, amplitude = 1.0, amplitudeTotal = 0.0, frequency = 1.0;
    for (unsigned int i = 0; i < octaves; ++i) {
        total += noise2d(x * frequency, y * frequency, seed + i * 1013U) * amplitude;
        amplitudeTotal += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }
    return amplitudeTotal > 0.0 ? total / amplitudeTotal : 0.0;
}

} // namespace cx
