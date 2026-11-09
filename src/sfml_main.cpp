#include "cx/diagnostics.hpp"
#include "cx/runtime.hpp"
#include "cx/value.hpp"
#include "cx/text_format.hpp"
#ifdef CX_WITH_X11_DND
#include "cx/x11_file_drop.hpp"
#endif
#ifdef CX_WITH_SFML_AUDIO
#include <SFML/Audio.hpp>
#endif

#include <SFML/Graphics.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <limits>
#include <memory>
#include <map>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

std::optional<sf::Font> loadFont(const std::filesystem::path& root, const std::string& configuredPath) {
    std::vector<std::filesystem::path> candidates;
    if (!configuredPath.empty()) {
        std::filesystem::path configured(configuredPath);
        if (configured.is_relative()) configured = root / configured;
        candidates.push_back(std::move(configured));
    }
    // Prefer Nerd Font Propo builds because they are legible in compact, code-like UI.
    std::error_code ec;
    const auto fontDirectory = root / "assets" / "fonts";
    if (std::filesystem::is_directory(fontDirectory, ec)) {
        std::vector<std::filesystem::path> discovered;
        for (std::filesystem::directory_iterator it(fontDirectory, ec), end; it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const auto extension = cx::lower(it->path().extension().string());
            if (extension == ".ttf" || extension == ".otf" || extension == ".ttc") discovered.push_back(it->path());
        }
        std::sort(discovered.begin(), discovered.end(), [](const auto& a, const auto& b) {
            const auto an = cx::lower(a.filename().string());
            const auto bn = cx::lower(b.filename().string());
            const bool ap = an.find("propo") != std::string::npos;
            const bool bp = bn.find("propo") != std::string::npos;
            if (ap != bp) return ap > bp;
            const bool anerd = an.find("nerd") != std::string::npos;
            const bool bnerd = bn.find("nerd") != std::string::npos;
            if (anerd != bnerd) return anerd > bnerd;
            return an < bn;
        });
        candidates.insert(candidates.end(), discovered.begin(), discovered.end());
    }
    candidates.push_back(root / "assets" / "font.ttf");
    candidates.push_back(root / "assets" / "DejaVuSans.ttf");
#ifdef _WIN32
    candidates.emplace_back("C:/Windows/Fonts/segoeui.ttf");
    candidates.emplace_back("C:/Windows/Fonts/arial.ttf");
#elif defined(__APPLE__)
    candidates.emplace_back("/System/Library/Fonts/Supplemental/Arial.ttf");
    candidates.emplace_back("/System/Library/Fonts/SFNS.ttf");
#else
    candidates.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    candidates.emplace_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
    candidates.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");
#endif
    std::unordered_set<std::string> seen;
    for (const auto& path : candidates) {
        const auto key = path.lexically_normal().string();
        if (!seen.insert(key).second) continue;
        // SFML logs a load error for an expected-but-absent optional font. Check
        // existence first so a fresh project with no bundled font stays quiet.
        ec.clear();
        if (!std::filesystem::is_regular_file(path, ec) || ec) continue;
        sf::Font font;
        if (font.openFromFile(path)) return std::optional<sf::Font>(std::move(font));
    }
    return std::nullopt;
}

std::string keyName(sf::Keyboard::Key key) {
    using Key = sf::Keyboard::Key;
    if (key >= Key::A && key <= Key::Z) return std::string(1, static_cast<char>('A' + (static_cast<int>(key) - static_cast<int>(Key::A))));
    if (key >= Key::Num0 && key <= Key::Num9) return std::string(1, static_cast<char>('0' + (static_cast<int>(key) - static_cast<int>(Key::Num0))));
    switch (key) {
        case Key::Space: return "SPACE";
        case Key::Escape: return "ESCAPE";
        case Key::Enter: return "ENTER";
        case Key::Tab: return "TAB";
        case Key::Backspace: return "BACKSPACE";
        case Key::Left: return "LEFT";
        case Key::Right: return "RIGHT";
        case Key::Up: return "UP";
        case Key::Down: return "DOWN";
        case Key::Add: return "ADD";
        case Key::Subtract: return "SUBTRACT";
        case Key::F1: return "F1";
        case Key::F2: return "F2";
        case Key::F3: return "F3";
        case Key::F4: return "F4";
        case Key::F5: return "F5";
        case Key::F6: return "F6";
        case Key::F7: return "F7";
        case Key::F8: return "F8";
        case Key::F9: return "F9";
        case Key::F10: return "F10";
        case Key::F11: return "F11";
        case Key::F12: return "F12";
        case Key::LShift: case Key::RShift: return "SHIFT";
        case Key::LControl: case Key::RControl: return "CTRL";
        default: return "UNKNOWN";
    }
}

std::vector<std::string> diagnosticLinesForWindow(const std::vector<cx::Diagnostic>& diagnostics,
                                                   const std::filesystem::path& projectRoot);

std::vector<std::string> printAndClearDiagnostics(cx::Runtime& runtime) {
    const auto windowLines = diagnosticLinesForWindow(runtime.diagnostics(), runtime.projectRoot());
    if (!runtime.diagnostics().empty()) cx::printDiagnostics(runtime.diagnostics(), runtime.projectRoot());
    runtime.clearDiagnostics();
    return windowLines;
}

std::vector<std::string> diagnosticLinesForWindow(const std::vector<cx::Diagnostic>& diagnostics,
                                                   const std::filesystem::path& projectRoot) {
    std::vector<std::string> result;
    for (const auto& diagnostic : diagnostics) {
        std::string path;
        if (!diagnostic.file.empty()) {
            std::error_code ec;
            const auto relative = std::filesystem::relative(diagnostic.file, projectRoot, ec);
            path = (!ec && !relative.empty() && !relative.string().starts_with(".."))
                ? relative.generic_string() : diagnostic.file.generic_string();
        } else path = "CX Engine";
        if (diagnostic.line > 0) path += ":" + std::to_string(diagnostic.line);
        result.push_back(std::string(diagnostic.warning ? "[WARNING] " : "[ERROR] ") + path);
        result.push_back("  " + diagnostic.message);

        std::string source = diagnostic.sourceLineText;
        if (source.empty() && diagnostic.line > 0 && !diagnostic.file.empty() &&
            diagnostic.file != "<console>" && diagnostic.file != "<memory>") {
            std::ifstream in(diagnostic.file);
            std::string line;
            for (int n = 1; n <= diagnostic.line && std::getline(in, line); ++n)
                if (n == diagnostic.line) source = line;
        }
        if (!source.empty()) {
            result.push_back("  " + source);
            const auto column = static_cast<std::size_t>(std::clamp(diagnostic.column, 1, 180) - 1);
            result.push_back("  " + std::string(column, ' ') + "^" + std::string(static_cast<std::size_t>(std::clamp(diagnostic.length - 1, 0, 32)), '~'));
        }
        if (!diagnostic.suggestion.empty()) result.push_back("  FIX: " + diagnostic.suggestion);
    }
    return result;
}

void drawText(sf::RenderWindow& window, const std::optional<sf::Font>& font,
              const std::string& content, float x, float y, unsigned int size, sf::Color color) {
    if (!font) return;
    cx::TextStyle base;
    base.red = color.r; base.green = color.g; base.blue = color.b; base.alpha = color.a;
    for (const auto& span : cx::parseRichText(content, base)) {
        if (span.text.empty()) continue;
        sf::Text text(*font, span.text, size);
        std::uint32_t style = sf::Text::Regular;
        if (span.style.bold) style |= sf::Text::Bold;
        if (span.style.italic) style |= sf::Text::Italic;
        if (span.style.underline) style |= sf::Text::Underlined;
        if (span.style.strikethrough) style |= sf::Text::StrikeThrough;
        text.setStyle(style);
        text.setPosition({x, y});
        text.setFillColor(sf::Color(span.style.red, span.style.green, span.style.blue, span.style.alpha));
        window.draw(text);
        // SFML 3.1 exposes shaped glyph positions directly. The last glyph's
        // advance includes trailing spaces, so the next differently styled span
        // begins at the correct horizontal position without the deprecated caret API.
        float spanAdvance = 0.0F;
        for (const auto& shaped : text.getShapedGlyphs()) {
            spanAdvance = std::max(spanAdvance, shaped.position.x + shaped.glyph.advance);
        }
        x += std::max(0.0F, spanAdvance);
    }
}

std::vector<sf::Vertex> makeBackground(unsigned int width, unsigned int height) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float tile = 64.0F;
    const sf::Color dark(17, 24, 39);
    const sf::Color light(19, 28, 44);
    std::vector<sf::Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>((width / 64 + 2) * (height / 64 + 2) * 6));
    for (float y = 0.0F; y < h; y += tile) {
        for (float x = 0.0F; x < w; x += tile) {
            const auto color = (static_cast<int>(x / tile + y / tile) % 2 == 0) ? dark : light;
            const sf::Vector2f a{x, y};
            const sf::Vector2f b{std::min(x + tile, w), y};
            const sf::Vector2f c{std::min(x + tile, w), std::min(y + tile, h)};
            const sf::Vector2f d{x, std::min(y + tile, h)};
            vertices.emplace_back(a, color); vertices.emplace_back(b, color); vertices.emplace_back(c, color);
            vertices.emplace_back(a, color); vertices.emplace_back(c, color); vertices.emplace_back(d, color);
        }
    }
    return vertices;
}

bool pointIn(float x, float y, const cx::CanvasElement& element) {
    return x >= element.x && y >= element.y && x <= element.x + element.width && y <= element.y + element.height;
}

struct RoomTextureBatch {
    std::string textureId;
    std::vector<sf::Vertex> vertices;
};

bool isObliqueRoom(const cx::RoomDefinition& room) {
    return room.projection == "oblique" || room.projection == "isometric";
}

sf::Vector2f tileOrigin(const cx::RoomDefinition& room, int x, int y) {
    const float tile = static_cast<float>(std::max(1, room.tileSize));
    if (!isObliqueRoom(room)) return {static_cast<float>(x) * tile, static_cast<float>(y) * tile};
    // A logical grid cell projects to a 2:1 diamond canvas. The room-height
    // offset keeps the north-west corner inside positive render coordinates.
    return {(static_cast<float>(x - y) * tile) + static_cast<float>(room.height) * tile - tile,
            (static_cast<float>(x + y) * tile) * 0.5F};
}

sf::Vector2f projectWorldPoint(const cx::RoomDefinition* room, cx::EffectPoint point) {
    if (!room || !isObliqueRoom(*room)) return {point.x, point.y};
    const float tile = static_cast<float>(std::max(1, room->tileSize));
    return {point.x - point.y + static_cast<float>(room->height) * tile,
            (point.x + point.y) * 0.5F};
}

void drawTexturedBeam(sf::RenderTarget& target, const sf::Texture& texture,
                      const cx::BeamEffect& beam, const cx::RoomDefinition* room,
                      bool pixelPerfect) {
    const sf::Vector2f start = projectWorldPoint(room, beam.start);
    const sf::Vector2f end = projectWorldPoint(room, beam.end);
    cx::BeamEffect projected = beam;
    projected.start = {start.x, start.y};
    projected.end = {end.x, end.y};
    cx::BeamQuad quad;
    if (!cx::buildBeamQuad(projected, quad)) return;
    const auto textureSize = texture.getSize();
    if (textureSize.x == 0 || textureSize.y == 0) return;
    const auto alpha = static_cast<std::uint8_t>(std::clamp(std::lround(beam.opacity * 255.0F), 0L, 255L));
    const sf::Color tint(beam.red, beam.green, beam.blue, alpha);
    sf::Vertex vertices[6];
    for (std::size_t i = 0; i < quad.vertices.size(); ++i) {
        const auto& source = quad.vertices[i];
        float x = source.position.x, y = source.position.y;
        if (pixelPerfect) { x = std::round(x); y = std::round(y); }
        vertices[i] = sf::Vertex({x, y}, tint,
            {source.u * static_cast<float>(textureSize.x), source.v * static_cast<float>(textureSize.y)});
    }
    sf::RenderStates states;
    states.texture = &texture;
    target.draw(vertices, 6, sf::PrimitiveType::Triangles, states);
}

sf::Vector2f projectWorldFoot(const cx::RoomDefinition& room, float x, float y,
                              float width, float height) {
    if (!isObliqueRoom(room)) return {x + width * 0.5F, y + height * 0.5F};
    const float tile = static_cast<float>(std::max(1, room.tileSize));
    const float gridX = (x + width * 0.5F) / tile;
    const float gridY = (y + height) / tile;
    return {(gridX - gridY) * tile + static_cast<float>(room.height) * tile,
            (gridX + gridY) * tile * 0.5F};
}

sf::Vector2f roomProjectedExtent(const cx::RoomDefinition& room) {
    const float tile = static_cast<float>(std::max(1, room.tileSize));
    if (!isObliqueRoom(room)) return {static_cast<float>(room.width) * tile, static_cast<float>(room.height) * tile};
    return {static_cast<float>(room.width + room.height) * tile,
            static_cast<float>(room.width + room.height) * tile * 0.5F + tile};
}

void drawRoomTextureTile(sf::RenderTarget& target, const sf::Texture& texture,
                         const cx::RoomDefinition& room, int x, int y) {
    const float tile = static_cast<float>(std::max(1, room.tileSize));
    const auto size = texture.getSize();
    if (size.x == 0 || size.y == 0) return;
    if (!isObliqueRoom(room)) {
        sf::Sprite sprite(texture);
        sprite.setPosition({static_cast<float>(x) * tile, static_cast<float>(y) * tile});
        sprite.setScale({tile / static_cast<float>(size.x), tile / static_cast<float>(size.y)});
        target.draw(sprite);
        return;
    }
    // In oblique rooms each logical cell is projected into a 2:1 diamond.
    // UVs span the whole texture; source alpha therefore remains intact for
    // diamond-shaped PNGs and for transparent border/corner overlays.
    const sf::Vector2f origin = tileOrigin(room, x, y);
    const sf::Vector2f top{origin.x + tile, origin.y};
    const sf::Vector2f right{origin.x + tile * 2.0F, origin.y + tile * 0.5F};
    const sf::Vector2f bottom{origin.x + tile, origin.y + tile};
    const sf::Vector2f left{origin.x, origin.y + tile * 0.5F};
    const sf::Vector2f uvTop{static_cast<float>(size.x) * 0.5F, 0.0F};
    const sf::Vector2f uvRight{static_cast<float>(size.x), static_cast<float>(size.y) * 0.5F};
    const sf::Vector2f uvBottom{static_cast<float>(size.x) * 0.5F, static_cast<float>(size.y)};
    const sf::Vector2f uvLeft{0.0F, static_cast<float>(size.y) * 0.5F};
    const sf::Vertex vertices[] = {
        sf::Vertex(top, sf::Color::White, uvTop), sf::Vertex(right, sf::Color::White, uvRight), sf::Vertex(bottom, sf::Color::White, uvBottom),
        sf::Vertex(top, sf::Color::White, uvTop), sf::Vertex(bottom, sf::Color::White, uvBottom), sf::Vertex(left, sf::Color::White, uvLeft)
    };
    sf::RenderStates states;
    states.texture = &texture;
    target.draw(vertices, 6, sf::PrimitiveType::Triangles, states);
}

void appendLightBeam(std::vector<sf::Vertex>& vertices, const cx::LightSource& light,
                     float centerX, float centerY, unsigned int quality) {
    const float radius = std::max(1.0F, light.radius);
    const float beam = std::clamp(light.beamAngleDegrees, 1.0F, 360.0F);
    const int rings = static_cast<int>(4U + quality * 3U);
    const int slices = beam >= 359.9F
        ? static_cast<int>(24U + quality * 16U)
        : std::max(12, static_cast<int>(std::ceil(beam * (0.32F + 0.12F * static_cast<float>(quality)))));
    const float start = beam >= 359.9F ? 0.0F : light.directionDegrees - beam * 0.5F;
    const float end = beam >= 359.9F ? 360.0F : light.directionDegrees + beam * 0.5F;
    const float alphaBase = std::clamp(light.intensity, 0.0F, 1.0F) * 168.0F;
    if (alphaBase <= 0.0F) return;

    const auto makeVertex = [&](float radiusAt, float angleDegrees, float alpha) {
        const float radians = angleDegrees * 0.01745329251994329577F;
        const auto alphaByte = static_cast<std::uint8_t>(std::clamp(alpha, 0.0F, 255.0F));
        return sf::Vertex({centerX + std::cos(radians) * radiusAt,
                           centerY + std::sin(radians) * radiusAt},
                          sf::Color(light.red, light.green, light.blue, alphaByte));
    };
    vertices.reserve(vertices.size() + static_cast<std::size_t>(rings * slices * 6));
    for (int ring = 0; ring < rings; ++ring) {
        const float r0 = radius * static_cast<float>(ring) / static_cast<float>(rings);
        const float r1 = radius * static_cast<float>(ring + 1) / static_cast<float>(rings);
        const float a0 = alphaBase * std::pow(1.0F - r0 / radius, 1.25F);
        const float a1 = alphaBase * std::pow(1.0F - r1 / radius, 1.25F);
        for (int slice = 0; slice < slices; ++slice) {
            const float angle0 = start + (end - start) * static_cast<float>(slice) / static_cast<float>(slices);
            const float angle1 = start + (end - start) * static_cast<float>(slice + 1) / static_cast<float>(slices);
            const sf::Vertex inner0 = makeVertex(r0, angle0, a0);
            const sf::Vertex inner1 = makeVertex(r0, angle1, a0);
            const sf::Vertex outer1 = makeVertex(r1, angle1, a1);
            const sf::Vertex outer0 = makeVertex(r1, angle0, a1);
            vertices.push_back(inner0); vertices.push_back(inner1); vertices.push_back(outer1);
            vertices.push_back(inner0); vertices.push_back(outer1); vertices.push_back(outer0);
        }
    }
}

#if defined(__linux__)
bool hasLinuxGraphicalDisplay() {
    const char* display = std::getenv("DISPLAY");
    if (display != nullptr && display[0] != '\0') return true;

    const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
    if (waylandDisplay != nullptr && waylandDisplay[0] != '\0') {
        std::cerr << "[ERROR] CX currently uses SFML 3.1's X11 window backend. "
                     "This Wayland session does not expose an XWayland DISPLAY. "
                     "Enable/install XWayland for this session, then launch CX again.\n";
    } else {
        std::cerr << "[ERROR] No graphical X11 display is available. CX needs an X11 display, "
                     "or a Wayland session with XWayland enabled.\n";
    }
    return false;
}
#endif

} // namespace

// The graphical host owns the OS window and input events. Runtime owns game rules and state;
// this loop passes input into Runtime, advances fixed logic steps, and then draws the latest state.
int main(int argc, char** argv) {
    std::filesystem::path root = std::filesystem::current_path();
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--help" || arg == "-h") {
            std::cout << "CX Engine graphical runtime\nUsage: cx-engine [project-folder]\n";
            return 0;
        }
        root = arg;
    }
#if defined(__linux__)
    // Check before Runtime::loadScripts(): startup can scaffold an empty project,
    // and a missing display should not create project files as a side effect.
    if (!hasLinuxGraphicalDisplay()) return 2;
#endif
    std::error_code ec;
    root = std::filesystem::absolute(root, ec);
    cx::Runtime runtime(root);
    const bool loaded = runtime.loadScripts(true);
    printAndClearDiagnostics(runtime);
    if (!loaded) {
        std::cerr << "Startup stopped. Fix the reported errors and run the engine again.\n";
        return 1;
    }

    const auto settings = runtime.settings();
    sf::RenderWindow window(sf::VideoMode({settings.windowWidth, settings.windowHeight}), settings.windowTitle);
    window.setVerticalSyncEnabled(settings.vsync);
    window.setFramerateLimit(settings.targetFps);
    window.setKeyRepeatEnabled(false);
    window.setView(sf::View(sf::FloatRect({0.0F, 0.0F}, {static_cast<float>(settings.windowWidth), static_cast<float>(settings.windowHeight)})));
    runtime.setViewportSize(static_cast<float>(settings.windowWidth), static_cast<float>(settings.windowHeight));
#ifdef CX_WITH_X11_DND
    cx::X11FileDropBridge fileDropBridge(static_cast<std::uintptr_t>(window.getNativeHandle()));
    if (!fileDropBridge.available()) std::cerr << "[INFO] Native drag-and-drop is unavailable on this display; use /attach in the console.\n";
#endif
#ifdef CX_WITH_SFML_AUDIO
    struct ActiveSound { std::unique_ptr<sf::SoundBuffer> buffer; std::unique_ptr<sf::Sound> sound; };
    std::vector<ActiveSound> activeSounds;
    auto dispatchAudioRequests = [&]() {
        for (const auto& request : runtime.audio().takePlayRequests()) {
            auto buffer = std::make_unique<sf::SoundBuffer>();
            if (const auto* wave = runtime.audio().find(request)) {
                std::vector<std::int16_t> pcm;
                pcm.reserve(wave->samples.size());
                for (float sample : wave->samples) {
                    const float clipped = std::clamp(sample, -1.0F, 1.0F);
                    const auto integer = static_cast<std::int16_t>(std::lround(clipped * (clipped < 0.0F ? 32768.0F : 32767.0F)));
                    pcm.push_back(integer);
                }
                if (!buffer->loadFromSamples(pcm.data(), static_cast<std::uint64_t>(pcm.size()), 1U,
                                             wave->sampleRate, {sf::SoundChannel::Mono})) {
                    std::cerr << "[AUDIO] Could not create playback buffer for generated wave `" << request << "`.\n";
                    continue;
                }
            } else {
                std::filesystem::path path(request);
                if (path.is_relative()) path = root / path;
                if (!buffer->loadFromFile(path)) {
                    std::cerr << "[AUDIO] Could not load audio file `" << path.string() << "`. Check that the format is supported.\n";
                    continue;
                }
            }
            auto sound = std::make_unique<sf::Sound>(*buffer);
            sound->play();
            activeSounds.push_back({std::move(buffer), std::move(sound)});
        }
        std::erase_if(activeSounds, [](const ActiveSound& sound) {
            return sound.sound->getStatus() == sf::SoundSource::Status::Stopped;
        });
    };
#else
    auto dispatchAudioRequests = [&]() {
        const auto requests = runtime.audio().takePlayRequests();
        if (!requests.empty()) {
            static bool warned = false;
            if (!warned) {
                std::cerr << "[AUDIO] Playback is disabled because this build has no SFML Audio module. Wave generation/export still works.\n";
                warned = true;
            }
        }
    };
#endif
    dispatchAudioRequests();
    const auto font = loadFont(root, runtime.settings().fontPath);
    if (!font) std::cerr << "[INFO] No font was found. Add a .ttf/.otf font to assets/fonts/ (NerdFontPropo preferred) or set engine::font_path.\n";

    unsigned int lastWidth = settings.windowWidth;
    unsigned int lastHeight = settings.windowHeight;
    bool lastVsync = settings.vsync;
    unsigned int lastTargetFps = settings.targetFps;
    std::string lastTitle = settings.windowTitle;
    std::string lastTextureFilter = settings.textureFilter;
    std::string lastResolutionMode = settings.resolutionMode;
    auto background = makeBackground(settings.uiReferenceWidth, settings.uiReferenceHeight);
    unsigned int lastReferenceWidth = settings.uiReferenceWidth;
    unsigned int lastReferenceHeight = settings.uiReferenceHeight;
    // Textures are loaded once and retained; room geometry is rebuilt only when the room manager's revision changes.
    std::unordered_map<std::string, std::unique_ptr<sf::Texture>> textureCache;
    std::unordered_set<std::string> failedTextures;
    std::vector<RoomTextureBatch> roomBatches;
    struct ForegroundTile { std::string textureId; int x{0}; int y{0}; std::string layer{"foreground"}; };
    std::vector<ForegroundTile> backgroundOverlays;
    std::vector<ForegroundTile> foregroundTiles;
    std::vector<ForegroundTile> aboveEntityOverlays;
    std::size_t roomRevision = std::numeric_limits<std::size_t>::max();
    // Light vertices are static unless a light/settings/room changes, so cache their expensive trigonometry.
    std::vector<sf::Vertex> cachedLightVertices;
    std::size_t cachedLightRevision = std::numeric_limits<std::size_t>::max();
    std::size_t cachedBeamRevision = std::numeric_limits<std::size_t>::max();
    std::size_t cachedLightRoomRevision = std::numeric_limits<std::size_t>::max();
    unsigned int cachedLightQuality = 0;
    bool cachedPixelPerfect = false;
    std::string cachedLightRoomName;
    int cachedTileSize = 0;
    auto ensureTexture = [&](const std::string& textureId) -> const sf::Texture* {
        if (const auto found = textureCache.find(textureId); found != textureCache.end()) return found->second.get();
        if (failedTextures.contains(textureId)) return nullptr;
        const auto* definition = runtime.rooms().textures().find(textureId);
        if (!definition) { failedTextures.insert(textureId); return nullptr; }
        auto texture = std::make_unique<sf::Texture>();
        if (!texture->loadFromFile(definition->path)) {
            std::cerr << "[WARN] Couldn't load texture `" << textureId << "` from " << definition->path << ".\n";
            failedTextures.insert(textureId);
            return nullptr;
        }
        texture->setSmooth(runtime.settings().textureFilter == "linear");
        texture->setRepeated(true); // Needed for sub-1× tile zoom, where UVs intentionally repeat.
        const auto* result = texture.get();
        textureCache.emplace(textureId, std::move(texture));
        return result;
    };
    auto drawBeamsForLayer = [&](cx::BeamDrawLayer layer, const cx::RoomDefinition* room) {
        const std::string activeRoom = runtime.rooms().activeRoomName();
        for (const auto& [name, beam] : runtime.beams()) {
            (void)name;
            if (beam.layer != layer) continue;
            if (!beam.roomName.empty() && beam.roomName != activeRoom) continue;
            const auto* texture = ensureTexture(beam.textureId);
            if (texture) drawTexturedBeam(window, *texture, beam, room, runtime.settings().pixelPerfect);
        }
    };
    auto rebuildRoomBatches = [&]() {
        roomBatches.clear();
        backgroundOverlays.clear();
        foregroundTiles.clear();
        aboveEntityOverlays.clear();
        const auto* room = runtime.rooms().activeRoom();
        if (!room) { roomRevision = runtime.rooms().revision(); return; }
        const float tile = static_cast<float>(room->tileSize);
        const bool oblique = isObliqueRoom(*room);
        const auto addOverlay = [&](const std::string& textureId, int x, int y, const std::string& layer) {
            ForegroundTile entry{textureId, x, y, layer};
            if (layer == "above_entities") aboveEntityOverlays.push_back(std::move(entry));
            else if (layer == "foreground") foregroundTiles.push_back(std::move(entry));
            else backgroundOverlays.push_back(std::move(entry));
        };

        std::unordered_map<std::string, std::size_t> batchIndex;
        for (int y = 0; y < room->height; ++y) {
            for (int x = 0; x < room->width; ++x) {
                const std::string& id = room->tileAt(x, y);
                if (id.empty()) continue;
                const sf::Texture* texture = ensureTexture(id);
                if (!texture) continue;
                auto [batchIt, inserted] = batchIndex.emplace(id, roomBatches.size());
                if (inserted) roomBatches.push_back({id, {}});
                auto& vertices = roomBatches[batchIt->second].vertices;
                const float left = static_cast<float>(x) * tile;
                const float top = static_cast<float>(y) * tile;
                const float scale = std::clamp(room->tileScaleAt(x, y), 0.25F, 4.0F);
                const float textureWidth = static_cast<float>(texture->getSize().x);
                const float textureHeight = static_cast<float>(texture->getSize().y);
                const float uvWidth = textureWidth / scale;
                const float uvHeight = textureHeight / scale;
                const float u0 = scale > 1.0F ? (textureWidth - uvWidth) * 0.5F : 0.0F;
                const float v0 = scale > 1.0F ? (textureHeight - uvHeight) * 0.5F : 0.0F;
                sf::Vector2f p0{left, top}, p1{left + tile, top}, p2{left + tile, top + tile}, p3{left, top + tile};
                sf::Vector2f uv0{u0, v0}, uv1{u0 + uvWidth, v0}, uv2{u0 + uvWidth, v0 + uvHeight}, uv3{u0, v0 + uvHeight};
                if (oblique) {
                    const auto origin = tileOrigin(*room, x, y);
                    p0 = {origin.x + tile, origin.y};
                    p1 = {origin.x + tile * 2.0F, origin.y + tile * 0.5F};
                    p2 = {origin.x + tile, origin.y + tile};
                    p3 = {origin.x, origin.y + tile * 0.5F};
                    uv0 = {u0 + uvWidth * 0.5F, v0};
                    uv1 = {u0 + uvWidth, v0 + uvHeight * 0.5F};
                    uv2 = {u0 + uvWidth * 0.5F, v0 + uvHeight};
                    uv3 = {u0, v0 + uvHeight * 0.5F};
                }
                vertices.emplace_back(p0, sf::Color::White, uv0);
                vertices.emplace_back(p1, sf::Color::White, uv1);
                vertices.emplace_back(p2, sf::Color::White, uv2);
                vertices.emplace_back(p0, sf::Color::White, uv0);
                vertices.emplace_back(p2, sf::Color::White, uv2);
                vertices.emplace_back(p3, sf::Color::White, uv3);
            }
        }

        // Explicit transparent overlays from the room file. Several textures
        // can stack on one cell/layer, e.g. an edge plus a rounded corner.
        for (const auto& overlay : room->overlays)
            addOverlay(overlay.textureId, overlay.x, overlay.y, overlay.layer);

        // Generate terrain seam/corner overlays from assets/autotiles.json.
        // These are cached with the room revision, never evaluated every frame.
        for (const auto& rule : runtime.rooms().textures().autotileRules()) {
            std::unordered_set<std::string> members(rule.members.begin(), rule.members.end());
            const auto inTerrain = [&](int tx, int ty) {
                if (tx < 0 || ty < 0 || tx >= room->width || ty >= room->height) return false;
                return members.contains(room->tileAt(tx, ty));
            };
            for (int y = 0; y < room->height; ++y) for (int x = 0; x < room->width; ++x) {
                if (!inTerrain(x, y)) continue;
                const bool n = inTerrain(x, y - 1), e = inTerrain(x + 1, y);
                const bool s = inTerrain(x, y + 1), w = inTerrain(x - 1, y);
                const bool exposedN = !n, exposedE = !e, exposedS = !s, exposedW = !w;
                if (exposedN && rule.edges.contains("north")) addOverlay(rule.edges.at("north"), x, y, "background");
                if (exposedE && rule.edges.contains("east")) addOverlay(rule.edges.at("east"), x, y, "background");
                if (exposedS && rule.edges.contains("south")) addOverlay(rule.edges.at("south"), x, y, "background");
                if (exposedW && rule.edges.contains("west")) addOverlay(rule.edges.at("west"), x, y, "background");
                const auto addCorner = [&](std::string_view key, bool active, const std::map<std::string, std::string>& map) {
                    const auto it = map.find(std::string(key));
                    if (active && it != map.end()) addOverlay(it->second, x, y, "background");
                };
                addCorner("northwest", exposedN && exposedW, rule.corners);
                addCorner("northeast", exposedN && exposedE, rule.corners);
                addCorner("southwest", exposedS && exposedW, rule.corners);
                addCorner("southeast", exposedS && exposedE, rule.corners);
                addCorner("northwest", n && w && !inTerrain(x - 1, y - 1), rule.innerCorners);
                addCorner("northeast", n && e && !inTerrain(x + 1, y - 1), rule.innerCorners);
                addCorner("southwest", s && w && !inTerrain(x - 1, y + 1), rule.innerCorners);
                addCorner("southeast", s && e && !inTerrain(x + 1, y + 1), rule.innerCorners);
            }
        }
        roomRevision = runtime.rooms().revision();
    };
    std::string consoleInput;
    std::size_t consoleCursor = 0;
    std::vector<std::string> commandHistory;
    std::size_t historyIndex = 0;
    bool consoleFocused = false;
    bool consoleDrawerOpen = false;
    float consoleDrawerProgress = 0.0F;
    bool running = true;
    float currentMouseX = 0.0F;
    float currentMouseY = 0.0F;
    std::vector<std::string> displayLogs = runtime.logs();
    std::vector<std::string> diagnosticHistory;
    std::vector<std::string> lastRuntimeLogSnapshot = runtime.logs();
    std::size_t logScrollOffset = 0; // Number of newer lines intentionally kept below the visible slice.
    auto refreshConsoleDisplay = [&](const std::vector<std::string>& diagnosticLines) {
        bool isError = false;
        if (!diagnosticLines.empty()) {
            const bool duplicateTail = diagnosticHistory.size() >= diagnosticLines.size() &&
                std::equal(diagnosticLines.rbegin(), diagnosticLines.rend(), diagnosticHistory.rbegin());
            if (!duplicateTail) {
                diagnosticHistory.insert(diagnosticHistory.end(), diagnosticLines.begin(), diagnosticLines.end());
                if (diagnosticHistory.size() > 300)
                    diagnosticHistory.erase(diagnosticHistory.begin(), diagnosticHistory.begin() + static_cast<std::ptrdiff_t>(diagnosticHistory.size() - 300));
            }
            isError = std::any_of(diagnosticLines.begin(), diagnosticLines.end(), [](const std::string& line) {
                return line.starts_with("[ERROR]");
            });
        }
        displayLogs = runtime.logs();
        displayLogs.insert(displayLogs.end(), diagnosticHistory.begin(), diagnosticHistory.end());
        if (displayLogs.size() > 500)
            displayLogs.erase(displayLogs.begin(), displayLogs.begin() + static_cast<std::ptrdiff_t>(displayLogs.size() - 500));
        lastRuntimeLogSnapshot = runtime.logs();
        if (isError) {
            // Never leave reload or runtime errors hidden in the parent terminal only.
            consoleDrawerOpen = true;
            consoleFocused = true;
            consoleCursor = consoleInput.size();
            logScrollOffset = 0;
        } else if (logScrollOffset == 0) {
            // Follow the newest output unless the developer has scrolled back.
        }
    };
    using Clock = std::chrono::steady_clock;
    auto previousTime = Clock::now();
    double accumulator = 0.0;

    // Main host loop: events first, then fixed logic updates, then rendering. Runtime state is
    // updated by tick(); drawing below reads that state. Keep expensive file work out of this
    // loop where possible so a single script or asset operation cannot stall user input.
    while (window.isOpen() && running) {
        runtime.setViewportSize(static_cast<float>(window.getSize().x), static_cast<float>(window.getSize().y));
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::FocusLost>()) {
                // A key may still be marked down when Alt-Tab removes focus. Do not
                // wait for KeyReleased: the OS may not send that event to this window.
                runtime.clearHeldKeys();
            } else if (event->is<sf::Event::Closed>()) {
                window.close();
            } else if (const auto* resized = event->getIf<sf::Event::Resized>()) {
                if (runtime.settings().resolutionMode == "fixed") {
                    window.setSize({runtime.settings().windowWidth, runtime.settings().windowHeight});
                } else {
                    // The world stays in logical coordinates and is uniformly fitted to the new viewport.
                    // Screen-space UI is resolved separately against the actual window size.
                    const float resizedWidth = static_cast<float>(resized->size.x);
                    const float resizedHeight = static_cast<float>(resized->size.y);
                    runtime.setViewportSize(resizedWidth, resizedHeight);
                }
            } else if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
                // A press is a one-time event; held state is also stored separately so `on tick`
                // scripts can implement continuous movement without spawning one task per key.
                const auto name = keyName(key->code);
                runtime.setKeyDown(name, true);
                if (key->code == sf::Keyboard::Key::Tab && key->control && runtime.settings().consoleEnabled) {
                    consoleDrawerOpen = !consoleDrawerOpen;
                    consoleFocused = consoleDrawerOpen;
                    if (consoleDrawerOpen) consoleCursor = consoleInput.size();
                } else if (consoleFocused) {
                    if (key->code == sf::Keyboard::Key::Escape) {
                        consoleFocused = false;
                        consoleDrawerOpen = false;
                    } else if (key->code == sf::Keyboard::Key::Enter) {
                        const std::string command = cx::trim(consoleInput);
                        if (!command.empty()) {
                            if (cx::lower(command) == "/quit" || cx::lower(command) == "/exit") running = false;
                            else {
                                commandHistory.push_back(command);
                                historyIndex = commandHistory.size();
                                runtime.executeConsole(command);
                                const auto diagnosticLines = printAndClearDiagnostics(runtime);
                                if (cx::lower(command) == "/clear") diagnosticHistory.clear();
                                refreshConsoleDisplay(diagnosticLines);
                            }
                        }
                        consoleInput.clear();
                        consoleCursor = 0;
                    } else if (key->code == sf::Keyboard::Key::Backspace) {
                        if (consoleCursor > 0 && consoleCursor <= consoleInput.size()) {
                            consoleInput.erase(consoleCursor - 1, 1);
                            --consoleCursor;
                        }
                    } else if (key->code == sf::Keyboard::Key::Delete) {
                        if (consoleCursor < consoleInput.size()) consoleInput.erase(consoleCursor, 1);
                    } else if (key->code == sf::Keyboard::Key::Left) {
                        if (consoleCursor > 0) --consoleCursor;
                    } else if (key->code == sf::Keyboard::Key::Right) {
                        if (consoleCursor < consoleInput.size()) ++consoleCursor;
                    } else if (key->code == sf::Keyboard::Key::Home) {
                        consoleCursor = 0;
                    } else if (key->code == sf::Keyboard::Key::End) {
                        consoleCursor = consoleInput.size();
                    } else if (key->code == sf::Keyboard::Key::PageUp) {
                        logScrollOffset = std::min(displayLogs.size(), logScrollOffset + 10);
                    } else if (key->code == sf::Keyboard::Key::PageDown) {
                        logScrollOffset = logScrollOffset > 10 ? logScrollOffset - 10 : 0;
                    } else if (key->code == sf::Keyboard::Key::Up && !commandHistory.empty()) {
                        if (historyIndex > 0) --historyIndex;
                        consoleInput = commandHistory[historyIndex];
                        consoleCursor = consoleInput.size();
                    } else if (key->code == sf::Keyboard::Key::Down && !commandHistory.empty()) {
                        if (historyIndex < commandHistory.size()) ++historyIndex;
                        consoleInput = historyIndex == commandHistory.size() ? "" : commandHistory[historyIndex];
                        consoleCursor = consoleInput.size();
                    }
                } else {
                    runtime.clearDiagnostics();
                    runtime.pressKey(name);
                    const auto diagnosticLines = printAndClearDiagnostics(runtime);
                    refreshConsoleDisplay(diagnosticLines);
                }
            } else if (const auto* releasedKey = event->getIf<sf::Event::KeyReleased>()) {
                runtime.setKeyDown(keyName(releasedKey->code), false);
            } else if (const auto* entered = event->getIf<sf::Event::TextEntered>()) {
                if (consoleFocused && entered->unicode >= 32 && entered->unicode < 127 && consoleInput.size() < 4000) {
                    consoleInput.insert(consoleCursor, 1, static_cast<char>(entered->unicode));
                    ++consoleCursor;
                }
            } else if (const auto* moved = event->getIf<sf::Event::MouseMoved>()) {
                currentMouseX = static_cast<float>(moved->position.x);
                currentMouseY = static_cast<float>(moved->position.y);
            } else if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>()) {
                currentMouseX = static_cast<float>(mouse->position.x);
                currentMouseY = static_cast<float>(mouse->position.y);
                if (mouse->button != sf::Mouse::Button::Left) continue;
                if (consoleDrawerOpen) continue; // The terminal drawer sits above the game UI.
                const float mx = currentMouseX;
                const float my = currentMouseY;
                // Button callbacks may close their canvas. Iterate a snapshot so erasing from the
                // runtime's open-canvas set cannot invalidate the event-dispatch iterator.
                const auto openCanvasSnapshot = runtime.openCanvases();
                for (const auto& openName : openCanvasSnapshot) {
                    const auto* canvas = runtime.findCanvas(openName);
                    if (!canvas) continue;
                    const auto resolvedElements = runtime.resolveCanvasElements(*canvas, static_cast<float>(window.getSize().x), static_cast<float>(window.getSize().y));
                    bool clicked = false;
                    for (const auto& element : resolvedElements) {
                        if (element.type == cx::CanvasElementType::Button && pointIn(mx, my, element)) {
                            runtime.clearDiagnostics();
                            runtime.clickButton(element.id);
                            const auto diagnosticLines = printAndClearDiagnostics(runtime);
                            refreshConsoleDisplay(diagnosticLines);
                            clicked = true;
                            break;
                        }
                    }
                    if (clicked) break;
                }
            }
        }
#ifdef CX_WITH_X11_DND
        for (const auto& drop : fileDropBridge.poll()) {
            runtime.clearDiagnostics();
            runtime.attachFile(drop.path, drop.x, drop.y);
            const auto diagnosticLines = printAndClearDiagnostics(runtime);
            refreshConsoleDisplay(diagnosticLines);
        }
#endif
        dispatchAudioRequests();

        // Accumulate real elapsed time, but advance game logic in fixed-size steps. This avoids
        // making movement depend directly on fluctuating render FPS. The step cap prevents a
        // long stall from causing an endless catch-up spiral; excess accumulated time is dropped.
        const auto now = Clock::now();
        double elapsed = std::chrono::duration<double>(now - previousTime).count();
        previousTime = now;
        elapsed = std::clamp(elapsed, 0.0, 0.25);
        accumulator += elapsed;
        const double step = 1.0 / static_cast<double>(std::max(1U, runtime.settings().tickRate));
        int steps = 0;
        while (accumulator >= step && steps < 5) {
            runtime.clearDiagnostics();
            runtime.tick();
            if (!runtime.diagnostics().empty()) {
                const auto diagnosticLines = printAndClearDiagnostics(runtime);
                // A repeated error from a tick handler should remain visible without
                // growing the drawer forever; avoid duplicate consecutive messages.
                refreshConsoleDisplay(diagnosticLines);
            }
            accumulator -= step;
            ++steps;
        }
        // Avoid a long catch-up spiral after the window is dragged or paused by the OS.
        if (steps == 5 && accumulator >= step) accumulator = 0.0;

        const auto& currentSettings = runtime.settings();
        if (!currentSettings.consoleEnabled) { consoleFocused = false; consoleDrawerOpen = false; }
        const float drawerTarget = consoleDrawerOpen ? 1.0F : 0.0F;
        consoleDrawerProgress += (drawerTarget - consoleDrawerProgress) * std::min(1.0F, static_cast<float>(elapsed) * 10.0F);
        if (currentSettings.vsync != lastVsync) {
            window.setVerticalSyncEnabled(currentSettings.vsync);
            lastVsync = currentSettings.vsync;
        }
        if (currentSettings.targetFps != lastTargetFps) {
            window.setFramerateLimit(currentSettings.targetFps);
            lastTargetFps = currentSettings.targetFps;
        }
        if (currentSettings.textureFilter != lastTextureFilter) {
            const bool smooth = currentSettings.textureFilter == "linear";
            for (auto& [id, texture] : textureCache) { (void)id; texture->setSmooth(smooth); }
            lastTextureFilter = currentSettings.textureFilter;
        }
        if (currentSettings.windowTitle != lastTitle) {
            window.setTitle(currentSettings.windowTitle);
            lastTitle = currentSettings.windowTitle;
        }
        if (currentSettings.resolutionMode != lastResolutionMode) {
            if (currentSettings.resolutionMode == "fixed") {
                window.setSize({currentSettings.windowWidth, currentSettings.windowHeight});
                window.setView(sf::View(sf::FloatRect({0.0F, 0.0F}, {static_cast<float>(currentSettings.windowWidth), static_cast<float>(currentSettings.windowHeight)})));
                runtime.setViewportSize(static_cast<float>(currentSettings.windowWidth), static_cast<float>(currentSettings.windowHeight));
                background = makeBackground(currentSettings.uiReferenceWidth, currentSettings.uiReferenceHeight);
            }
            lastResolutionMode = currentSettings.resolutionMode;
        }
        if (currentSettings.uiReferenceWidth != lastReferenceWidth || currentSettings.uiReferenceHeight != lastReferenceHeight) {
            background = makeBackground(currentSettings.uiReferenceWidth, currentSettings.uiReferenceHeight);
            lastReferenceWidth = currentSettings.uiReferenceWidth;
            lastReferenceHeight = currentSettings.uiReferenceHeight;
        }
        if (currentSettings.windowWidth != lastWidth || currentSettings.windowHeight != lastHeight) {
            window.setSize({currentSettings.windowWidth, currentSettings.windowHeight});
            window.setView(sf::View(sf::FloatRect({0.0F, 0.0F}, {static_cast<float>(currentSettings.windowWidth), static_cast<float>(currentSettings.windowHeight)})));
            runtime.setViewportSize(static_cast<float>(currentSettings.windowWidth), static_cast<float>(currentSettings.windowHeight));
            background = makeBackground(currentSettings.uiReferenceWidth, currentSettings.uiReferenceHeight);
            lastWidth = currentSettings.windowWidth;
            lastHeight = currentSettings.windowHeight;
        }
        if (window.getSize().x != 0 && window.getSize().y != 0) {
            window.setView(sf::View(sf::FloatRect({0.0F, 0.0F}, {static_cast<float>(window.getSize().x), static_cast<float>(window.getSize().y)})));
            runtime.setViewportSize(static_cast<float>(window.getSize().x), static_cast<float>(window.getSize().y));
        }
        if (runtime.logs() != lastRuntimeLogSnapshot) refreshConsoleDisplay({});

        if (roomRevision != runtime.rooms().revision()) rebuildRoomBatches();
        // Render world objects in a logical reference resolution. The viewport preserves aspect ratio;
        // UI is switched to physical window coordinates later so controls remain anchored to screen edges.
        window.clear(sf::Color(0, 0, 0));
        const float logicalWidth = static_cast<float>(std::max(1U, currentSettings.uiReferenceWidth));
        float activeTileSize = 32.0F;
        if (const auto* activeRoom = runtime.rooms().activeRoom())
            activeTileSize = static_cast<float>(std::max(1, activeRoom->tileSize));
        // The game is configured by the vertical number of tiles, not physical pixels.
        // The active room's tile size converts that coverage to world units; aspect ratio
        // then reveals more or less world horizontally without stretching a tile.
        const float logicalHeight = std::max(1.0F, currentSettings.worldViewHeightTiles * activeTileSize);
        const float actualWidth = std::max(1.0F, static_cast<float>(window.getSize().x));
        const float actualHeight = std::max(1.0F, static_cast<float>(window.getSize().y));
        const float actualAspect = actualWidth / actualHeight;
        // Keep world units square and preserve their apparent size. Wider windows reveal more world horizontally;
        // tall/narrow windows reveal less horizontal world instead of stretching tiles or letterboxing the view.
        const float worldViewWidth = std::max(1.0F, logicalHeight * actualAspect);
        float worldCenterX = logicalWidth * 0.5F;
        float worldCenterY = logicalHeight * 0.5F;
        const auto* activeRoomForCamera = runtime.rooms().activeRoom();
        float roomWorldWidth = 0.0F;
        float roomWorldHeight = 0.0F;
        if (activeRoomForCamera) {
            const auto projectedExtent = roomProjectedExtent(*activeRoomForCamera);
            roomWorldWidth = projectedExtent.x;
            roomWorldHeight = projectedExtent.y;
            worldCenterX = roomWorldWidth * 0.5F;
            worldCenterY = roomWorldHeight * 0.5F;
        }
        if (currentSettings.cameraFollow != "none") {
            const auto followed = runtime.sprites().find(currentSettings.cameraFollow);
            if (followed != runtime.sprites().end() && followed->second.visible) {
                if (activeRoomForCamera && isObliqueRoom(*activeRoomForCamera)) {
                    const auto foot = projectWorldFoot(*activeRoomForCamera, followed->second.x, followed->second.y,
                                                        followed->second.width, followed->second.height);
                    worldCenterX = foot.x;
                    worldCenterY = foot.y - followed->second.height * 0.5F;
                } else {
                    worldCenterX = followed->second.x + followed->second.width * 0.5F;
                    worldCenterY = followed->second.y + followed->second.height * 0.5F;
                }
                // Clamp the camera to the world when the room is larger than the view. If the room
                // is smaller, keep it centered so we never expose an arbitrary edge outside it.
                if (activeRoomForCamera) {
                    worldCenterX = roomWorldWidth <= worldViewWidth ? roomWorldWidth * 0.5F
                        : std::clamp(worldCenterX, worldViewWidth * 0.5F, roomWorldWidth - worldViewWidth * 0.5F);
                    worldCenterY = roomWorldHeight <= logicalHeight ? roomWorldHeight * 0.5F
                        : std::clamp(worldCenterY, logicalHeight * 0.5F, roomWorldHeight - logicalHeight * 0.5F);
                }
            }
        }
        sf::View worldView(sf::FloatRect({0.0F, 0.0F}, {worldViewWidth, logicalHeight}));
        worldView.setCenter({worldCenterX, worldCenterY});
        worldView.setViewport(sf::FloatRect({0.0F, 0.0F}, {1.0F, 1.0F}));
        window.setView(worldView);
        if (currentSettings.showDebugOverlay) {
            window.draw(background.data(), background.size(), sf::PrimitiveType::Triangles);
        }
        if (const auto* room = runtime.rooms().activeRoom()) {
            const float tile = static_cast<float>(room->tileSize);
            for (const auto& batch : roomBatches) {
                const auto textureIt = textureCache.find(batch.textureId);
                if (textureIt == textureCache.end() || batch.vertices.empty()) continue;
                sf::RenderStates states;
                states.texture = textureIt->second.get();
                window.draw(batch.vertices.data(), batch.vertices.size(), sf::PrimitiveType::Triangles, states);
            }
            for (const auto& overlay : backgroundOverlays) {
                const auto* texture = ensureTexture(overlay.textureId);
                if (texture) drawRoomTextureTile(window, *texture, *room, overlay.x, overlay.y);
            }
            // Lighting is a low-cost overlay rather than per-pixel material shading: first draw
            // translucent ambient darkness over the rendered room, then add a soft radial
            // or conical vertex mesh for each live light. This changes displayed colors,
            // but does not modify the source texture pixels.
            if (currentSettings.lightingEnabled) {
                const float lightingWorldWidth = worldViewWidth;
                const float lightingWorldHeight = logicalHeight;
                sf::RectangleShape ambientOverlay({lightingWorldWidth, lightingWorldHeight});
                ambientOverlay.setPosition({worldCenterX - lightingWorldWidth * 0.5F, worldCenterY - lightingWorldHeight * 0.5F});
                const float darkness = (1.0F - std::clamp(currentSettings.ambientLight, 0.0F, 1.0F)) * 178.0F;
                ambientOverlay.setFillColor(sf::Color(5, 8, 15, static_cast<std::uint8_t>(std::clamp(darkness, 0.0F, 178.0F))));
                window.draw(ambientOverlay);

                const std::string activeRoomName = runtime.rooms().activeRoomName();
                const std::size_t currentLightRevision = runtime.lightsRevision();
                const std::size_t currentRoomRevision = runtime.rooms().revision();
                const std::size_t currentBeamRevision = runtime.beamsRevision();
                if (cachedLightRevision != currentLightRevision ||
                    cachedBeamRevision != currentBeamRevision ||
                    cachedLightRoomRevision != currentRoomRevision ||
                    cachedLightQuality != currentSettings.lightingQuality ||
                    cachedPixelPerfect != currentSettings.pixelPerfect ||
                    cachedLightRoomName != activeRoomName ||
                    cachedTileSize != room->tileSize) {
                    cachedLightVertices.clear();
                    for (const auto& [name, light] : runtime.lights()) {
                        (void)name;
                        if (!light.roomName.empty() && light.roomName != activeRoomName) continue;
                        float lx = (light.xTiles + 0.5F) * tile;
                        float ly = (light.yTiles + 0.5F) * tile;
                        if (isObliqueRoom(*room)) {
                            lx = (light.xTiles - light.yTiles) * tile + static_cast<float>(room->height) * tile;
                            ly = (light.xTiles + light.yTiles) * tile * 0.5F + tile * 0.5F;
                        }
                        if (currentSettings.pixelPerfect) { lx = std::round(lx); ly = std::round(ly); }
                        appendLightBeam(cachedLightVertices, light, lx, ly, currentSettings.lightingQuality);
                    }
                    for (const auto& [name, beam] : runtime.beams()) {
                        (void)name;
                        if (!beam.endpointLightEnabled) continue;
                        if (!beam.roomName.empty() && beam.roomName != activeRoomName) continue;
                        const sf::Vector2f endpoint = projectWorldPoint(room, beam.end);
                        cx::LightSource endpointLight;
                        endpointLight.radius = beam.endpointLightRadius;
                        endpointLight.intensity = beam.endpointLightIntensity;
                        endpointLight.beamAngleDegrees = 360.0F;
                        endpointLight.red = beam.endpointLightRed;
                        endpointLight.green = beam.endpointLightGreen;
                        endpointLight.blue = beam.endpointLightBlue;
                        const float lx = currentSettings.pixelPerfect ? std::round(endpoint.x) : endpoint.x;
                        const float ly = currentSettings.pixelPerfect ? std::round(endpoint.y) : endpoint.y;
                        appendLightBeam(cachedLightVertices, endpointLight, lx, ly, currentSettings.lightingQuality);
                    }
                    cachedLightRevision = currentLightRevision;
                    cachedBeamRevision = currentBeamRevision;
                    cachedLightRoomRevision = currentRoomRevision;
                    cachedLightQuality = currentSettings.lightingQuality;
                    cachedPixelPerfect = currentSettings.pixelPerfect;
                    cachedLightRoomName = activeRoomName;
                    cachedTileSize = room->tileSize;
                }
                if (!cachedLightVertices.empty()) {
                    sf::RenderStates lightStates;
                    lightStates.blendMode = sf::BlendAdd;
                    window.draw(cachedLightVertices.data(), cachedLightVertices.size(), sf::PrimitiveType::Triangles, lightStates);
                }
            }
            // Background beams sit above the lit room surface but behind entities.
            drawBeamsForLayer(cx::BeamDrawLayer::Background, room);
            if (currentSettings.showDebugOverlay) {
                sf::CircleShape spawnMarker(std::max(3.0F, tile * 0.22F));
                spawnMarker.setOrigin({spawnMarker.getRadius(), spawnMarker.getRadius()});
                spawnMarker.setPosition({(static_cast<float>(runtime.rooms().activeEntrance().x) + 0.5F) * tile,
                                         (static_cast<float>(runtime.rooms().activeEntrance().y) + 0.5F) * tile});
                spawnMarker.setFillColor(sf::Color(255, 190, 90, 220));
                spawnMarker.setOutlineThickness(1.0F);
                spawnMarker.setOutlineColor(sf::Color(255, 241, 210));
                window.draw(spawnMarker);
            }
        } else if (currentSettings.showDebugOverlay) {
            drawText(window, font, "No active room. Use: enter room \"name\" at {entrance}",
                     28.0F, 140.0F, 16, sf::Color(148, 163, 184));
        }

        // Depth-sort moving objects and foreground cover tiles. A character behind a stone is occluded by it,
        // while the same character is drawn in front once its feet move below the stone's front edge.
        struct WorldDrawItem {
            float sortY;
            bool cover;
            const cx::SpriteObject* sprite;
            const ForegroundTile* tile;
            const cx::BeamEffect* beam;
        };
        std::vector<WorldDrawItem> drawItems;
        const auto* drawRoom = runtime.rooms().activeRoom();
        const float drawTileSize = drawRoom ? static_cast<float>(drawRoom->tileSize) : 32.0F;
        const bool obliqueDraw = drawRoom && isObliqueRoom(*drawRoom);
        drawItems.reserve(runtime.sprites().size() + foregroundTiles.size() + runtime.beams().size());
        for (const auto& cover : foregroundTiles) {
            const float depth = obliqueDraw
                ? (static_cast<float>(cover.x + cover.y) * drawTileSize * 0.5F + drawTileSize)
                : static_cast<float>(cover.y + 1) * drawTileSize;
            drawItems.push_back({depth + 0.01F, true, nullptr, &cover, nullptr});
        }
        const std::string drawRoomName = runtime.rooms().activeRoomName();
        for (const auto& [beamId, beam] : runtime.beams()) {
            (void)beamId;
            if (beam.layer != cx::BeamDrawLayer::World) continue;
            if (!beam.roomName.empty() && beam.roomName != drawRoomName) continue;
            const auto start = projectWorldPoint(drawRoom, beam.start);
            const auto end = projectWorldPoint(drawRoom, beam.end);
            drawItems.push_back({(start.y + end.y) * 0.5F, false, nullptr, nullptr, &beam});
        }
        for (const auto& [spriteId, spriteObject] : runtime.sprites()) {
            (void)spriteId;
            if (!spriteObject.visible) continue;
            float depth = spriteObject.y + spriteObject.height;
            if (obliqueDraw) depth = projectWorldFoot(*drawRoom, spriteObject.x, spriteObject.y,
                                                       spriteObject.width, spriteObject.height).y;
            drawItems.push_back({depth, false, &spriteObject, nullptr, nullptr});
        }
        std::stable_sort(drawItems.begin(), drawItems.end(), [](const WorldDrawItem& a, const WorldDrawItem& b) { return a.sortY < b.sortY; });
        for (const auto& item : drawItems) {
            if (item.cover && item.tile) {
                const auto* texture = ensureTexture(item.tile->textureId);
                if (texture && drawRoom) drawRoomTextureTile(window, *texture, *drawRoom, item.tile->x, item.tile->y);
                continue;
            }
            if (item.beam) {
                const auto* texture = ensureTexture(item.beam->textureId);
                if (texture) drawTexturedBeam(window, *texture, *item.beam, drawRoom, currentSettings.pixelPerfect);
                continue;
            }
            if (!item.sprite) continue;
            const auto& spriteObject=*item.sprite;
            const sf::Texture* texture = ensureTexture(spriteObject.textureId);
            if (!texture) continue;
            const auto textureSize = texture->getSize();
            if (textureSize.x == 0 || textureSize.y == 0) continue;
            sf::Sprite drawnSprite(*texture);
            float x = spriteObject.x; float y = spriteObject.y;
            if (currentSettings.pixelPerfect) { x = std::round(x); y = std::round(y); }
            if (obliqueDraw && drawRoom) {
                const auto foot = projectWorldFoot(*drawRoom, spriteObject.x, spriteObject.y,
                                                   spriteObject.width, spriteObject.height);
                x = foot.x - spriteObject.width * 0.5F;
                y = foot.y - spriteObject.height;
            }
            drawnSprite.setPosition({x, y});
            if (spriteObject.frameWidth > 0 && spriteObject.frameHeight > 0 &&
                spriteObject.frameWidth <= static_cast<int>(textureSize.x) && spriteObject.frameHeight <= static_cast<int>(textureSize.y)) {
                const int cols = std::max(1, spriteObject.frameColumns);
                const int rows = std::max(1, spriteObject.frameRows);
                const int frame = std::clamp(spriteObject.animationFrame, 0, cols-1);
                const int row = std::clamp(spriteObject.currentAnimation.empty()
                    ? spriteObject.facingRow : spriteObject.animationRow, 0, rows-1);
                drawnSprite.setTextureRect(sf::IntRect({frame*spriteObject.frameWidth,row*spriteObject.frameHeight},{spriteObject.frameWidth,spriteObject.frameHeight}));
                drawnSprite.setScale({spriteObject.width/static_cast<float>(spriteObject.frameWidth),spriteObject.height/static_cast<float>(spriteObject.frameHeight)});
            } else {
                drawnSprite.setScale({spriteObject.width/static_cast<float>(textureSize.x),spriteObject.height/static_cast<float>(textureSize.y)});
            }
            window.draw(drawnSprite);
        }
        drawBeamsForLayer(cx::BeamDrawLayer::Foreground, drawRoom);
        if (drawRoom) {
            for (const auto& overlay : aboveEntityOverlays) {
                const auto* texture = ensureTexture(overlay.textureId);
                if (texture) drawRoomTextureTile(window, *texture, *drawRoom, overlay.x, overlay.y);
            }
        }
        if (currentSettings.screenFlash > 0.001F) {
            sf::RectangleShape flash({worldViewWidth, logicalHeight});
            flash.setPosition({worldCenterX-worldViewWidth*0.5F,worldCenterY-logicalHeight*0.5F});
            flash.setFillColor(sf::Color(255,232,201,static_cast<std::uint8_t>(std::clamp(currentSettings.screenFlash*185.0F,0.0F,185.0F))));
            window.draw(flash);
        }

        // UI and terminal use physical viewport coordinates, independent of the world camera/view.
        const float windowH = static_cast<float>(window.getSize().y);
        const float windowW = static_cast<float>(window.getSize().x);
        sf::View uiView(sf::FloatRect({0.0F, 0.0F}, {windowW, windowH}));
        window.setView(uiView);

        // Optional diagnostic overlay is drawn over the world, never as permanent engine chrome.
        if (currentSettings.showDebugOverlay) {
            drawText(window, font, "CX Engine", 14.0F, 10.0F, 18, sf::Color(226, 232, 240));
            drawText(window, font, "FPS cap: " + (currentSettings.targetFps ? std::to_string(currentSettings.targetFps) : "off") +
                     "   |   VSync: " + (currentSettings.vsync ? "on" : "off") + "   |   Tick rate: " + std::to_string(currentSettings.tickRate),
                     14.0F, 34.0F, 12, sf::Color(125, 211, 252));
        }

        for (const auto& openName : runtime.openCanvases()) {
            const auto* canvas = runtime.findCanvas(openName);
            if (!canvas) continue;
            const auto resolvedElements = runtime.resolveCanvasElements(*canvas, windowW, windowH);
            for (const auto& element : resolvedElements) {
                if (element.type == cx::CanvasElementType::Panel || element.type == cx::CanvasElementType::Button) {
                    sf::RectangleShape shape({element.width, element.height});
                    shape.setPosition({element.x, element.y});
                    shape.setFillColor(sf::Color(element.red, element.green, element.blue, element.alpha));
                    if (element.type == cx::CanvasElementType::Button) {
                        shape.setOutlineThickness(1.0F);
                        shape.setOutlineColor(sf::Color(100, 116, 139));
                    }
                    window.draw(shape);
                }
                if (element.type == cx::CanvasElementType::ProgressBar) {
                    sf::RectangleShape barBack({element.width,element.height});
                    barBack.setPosition({element.x,element.y});
                    barBack.setFillColor(sf::Color(element.backgroundRed,element.backgroundGreen,element.backgroundBlue,element.backgroundAlpha));
                    window.draw(barBack);
                    const double value=runtime.getGlobal(element.valueVariable).numberOr(0.0);
                    const double maximum=runtime.getGlobal(element.maxVariable).numberOr(1.0);
                    const float ratio=maximum>0.0?static_cast<float>(std::clamp(value/maximum,0.0,1.0)):0.0F;
                    if(ratio>0.0F) { sf::RectangleShape barFill({std::max(0.0F,(element.width-4.0F)*ratio),std::max(0.0F,element.height-4.0F)}); barFill.setPosition({element.x+2.0F,element.y+2.0F}); barFill.setFillColor(sf::Color(element.red,element.green,element.blue,element.alpha)); window.draw(barFill); }
                }
                if (element.type == cx::CanvasElementType::Text) {
                    drawText(window, font, runtime.formatDisplayText(element.text), element.x, element.y, element.fontSize, sf::Color(241, 245, 249));
                } else if (element.type == cx::CanvasElementType::Button) {
                    drawText(window, font, runtime.formatDisplayText(element.text), element.x + 10.0F, element.y + 8.0F, 16, sf::Color(241, 245, 249));
                }
            }
        }

        // A compact black terminal drawer slides down from the top. No permanent bottom bar.
        if (currentSettings.consoleEnabled && consoleDrawerProgress > 0.001F) {
            const float drawerHeight = std::min(windowH * 0.74F, 380.0F);
            const float visibleHeight = drawerHeight * consoleDrawerProgress;
            const float drawerTop = -drawerHeight * (1.0F - consoleDrawerProgress);
            sf::RectangleShape drawer({windowW, drawerHeight});
            drawer.setPosition({0.0F, drawerTop});
            drawer.setFillColor(sf::Color(5, 8, 14, 205));
            drawer.setOutlineThickness(1.0F);
            drawer.setOutlineColor(sf::Color(86, 112, 148, 200));
            window.draw(drawer);
            if (visibleHeight > 24.0F) {
                drawText(window, font, "Ctrl+Tab toggles · Esc closes · PgUp/PgDn scroll", std::max(120.0F, windowW - 310.0F), drawerTop + 10.0F, 12, sf::Color(150, 164, 184));
                std::string lineWithCaret = consoleInput;
                lineWithCaret.insert(std::min(consoleCursor, lineWithCaret.size()), "|");
                drawText(window, font, "cx> " + lineWithCaret, 14.0F, drawerTop + 10.0F, 15, sf::Color(226, 232, 240));
                float drawerY = drawerTop + 36.0F;
                const std::size_t visibleCount = static_cast<std::size_t>(std::max(0.0F, (visibleHeight - 46.0F) / 17.0F));
                const std::size_t end = displayLogs.size() - std::min(logScrollOffset, displayLogs.size());
                const std::size_t begin = end > visibleCount ? end - visibleCount : 0;
                for (std::size_t i = begin; i < end && drawerY + 16.0F < drawerTop + visibleHeight; ++i) {
                    sf::Color logColor(183, 195, 211);
                    if (displayLogs[i].starts_with("[ERROR]")) logColor = sf::Color(255, 132, 145);
                    else if (displayLogs[i].starts_with("[WARNING]")) logColor = sf::Color(255, 202, 125);
                    else if (displayLogs[i].starts_with("  FIX:")) logColor = sf::Color(124, 203, 255);
                    drawText(window, font, displayLogs[i], 14.0F, drawerY, 12, logColor);
                    drawerY += 17.0F;
                }
                if (logScrollOffset > 0) drawText(window, font, "scrollback", std::max(14.0F, windowW - 92.0F), drawerTop + visibleHeight - 20.0F, 10, sf::Color(130, 150, 175));
            }
        }
        window.display();
    }
    return 0;
}
