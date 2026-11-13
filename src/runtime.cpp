#include "cx/runtime.hpp"

#include "cx/diagnostics.hpp"
#include "cx/value.hpp"
#include "cx/variable_storage.hpp"
#include "cx/project_scaffold.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <fstream>
#include <limits>
#include <optional>
#include <regex>
#include <sstream>
#include <utility>
#include <queue>
#include <random>
#include <tuple>

namespace cx {
namespace {

// Runtime is the interpreter: it owns variable scopes and game state, evaluates
// expressions, schedules waiting scripts, and applies commands. It deliberately does
// not draw a window; the graphical frontend reads its state through the public API.

std::string unquote(std::string text) {
    text = trim(text);
    if (text.size() < 2 || text.front() != '"' || text.back() != '"') return text;
    std::string out;
    out.reserve(text.size() - 2);
    bool escaped = false;
    for (std::size_t i = 1; i + 1 < text.size(); ++i) {
        const char c = text[i];
        if (escaped) {
            switch (c) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                default: out.push_back(c); break;
            }
            escaped = false;
        } else if (c == '\\') escaped = true;
        else out.push_back(c);
    }
    if (escaped) out.push_back('\\');
    return out;
}

bool isQuoted(std::string_view value) {
    return value.size() >= 2 && value.front() == '"' && value.back() == '"';
}

bool isListWildcard(std::string_view name) {
    return name.ends_with("::*");
}

std::chrono::duration<double> periodicDuration(const EventHandler& handler, unsigned int tickRate) {
    const auto unit = lower(handler.intervalUnit);
    double seconds = handler.intervalValue;
    if (unit == "ms" || unit.starts_with("msec") || unit.starts_with("millisecond")) seconds *= 0.001;
    else if (unit == "m" || unit.starts_with("min")) seconds *= 60.0;
    else if (unit == "h" || unit.starts_with("hour") || unit.starts_with("hr")) seconds *= 3600.0;
    else if (unit == "t" || unit.starts_with("tick")) seconds *= 1.0 / static_cast<double>(std::max(1U, tickRate));
    return std::chrono::duration<double>(seconds);
}

bool validVariableName(std::string_view name) {
    // This validation runs on assignment and interpolation, including inside on tick.
    // A direct character scan avoids regex overhead on this hot path.
    if (name.empty()) return false;
    const auto first = static_cast<unsigned char>(name.front());
    if (!(std::isalpha(first) || first == '_' || first == '$' || first == '^' || first == '@')) return false;
    for (const char raw : name.substr(1)) {
        const auto ch = static_cast<unsigned char>(raw);
        if (!(std::isalnum(ch) || ch == '_' || ch == ':')) return false;
    }
    return true;
}

} // namespace

Runtime::Runtime(std::filesystem::path projectRoot)
    : projectRoot_(std::move(projectRoot)), rooms_(projectRoot_) {
    resetRuntimeState(true);
}

std::uint32_t Runtime::internEntityLabel(std::string_view label) {
    const std::string normalized = lower(trim(label));
    const auto found = entityLabelIds_.find(normalized);
    if (found != entityLabelIds_.end()) return found->second;
    if (normalized.empty() || nextEntityLabelId_ == 0U) return 0U;
    const std::uint32_t assigned = nextEntityLabelId_++;
    entityLabelIds_.emplace(normalized, assigned);
    return assigned;
}

std::uint32_t Runtime::findEntityLabel(std::string_view label) const {
    const std::string normalized = lower(trim(label));
    const auto found = entityLabelIds_.find(normalized);
    return found == entityLabelIds_.end() ? 0U : found->second;
}

// Reload uses a prepare-then-commit pattern. First parse the candidate, then boot it in
// a separate Runtime while persistent writes and generated files are staged. Only after
// all startup work succeeds do we replace the live state. A typo or startup error thus
// leaves the last working game running so the author can fix the script and retry.
bool Runtime::loadScripts(bool resetGlobals) {
    const auto scaffold = ensureProjectScaffold(projectRoot_);
    if (!scaffold.error.empty()) {
        appendDiagnostic(diagnostics_, {false, projectRoot_, 0,
            "The project structure could not be prepared: " + scaffold.error,
            "Check that this is a writable game-project folder. Existing files are never overwritten.", 1, 1});
        return false;
    }
    ScriptParser parser;
    // Parse and execute the replacement in an isolated runtime first. Syntax
    // and runtime failures must not replace the program currently in use.
    ScriptProgram candidate = parser.parseDirectory(projectRoot_ / "scripts");
    if (cx::hasErrors(candidate.diagnostics)) {
        diagnostics_ = std::move(candidate.diagnostics);
        return false;
    }

    Runtime staged(projectRoot_);
    staged.diagnostics_.clear();
    staged.stagingLoad_ = true;
    staged.lightsRevision_ = lightsRevision_;
    staged.beamsRevision_ = beamsRevision_;
    staged.setViewportSize(viewportWidth_, viewportHeight_);
    const bool preservePendingVariableSave = !resetGlobals && variableStorageDirty_;
    if (!resetGlobals) {
        // Preserve the existing global values/settings, matching the previous
        // loadScripts(false) contract. Transient values are still reset.
        staged.globals_ = globals_;
        staged.settings_ = settings_;
    }
    staged.program_ = std::move(candidate);
    staged.diagnostics_ = staged.program_.diagnostics;
    staged.periodicNextRun_.assign(staged.program_.handlers.size(), std::chrono::steady_clock::time_point{});
    staged.resetRuntimeState(resetGlobals);
    // resetRuntimeState() clears the dirty flag as part of a fresh session. When
    // globals are intentionally carried over, preserve whether those live values
    // still need to be written to disk; otherwise a console edit made just before
    // loadScripts(false) could remain in RAM but be forgotten by the new runtime.
    if (!resetGlobals) staged.variableStorageDirty_ = preservePendingVariableSave;
    if (!staged.hasErrors()) staged.executeBootstrap();
    if (!staged.hasErrors()) staged.fireLoad();

    const auto loadedAt = std::chrono::steady_clock::now();
    if (staged.periodicNextRun_.size() != staged.program_.handlers.size())
        staged.periodicNextRun_.assign(staged.program_.handlers.size(), std::chrono::steady_clock::time_point{});
    for (std::size_t i = 0; i < staged.program_.handlers.size(); ++i) {
        if (staged.program_.handlers[i].type == EventType::Periodic) {
            staged.periodicNextRun_[i] = loadedAt + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                periodicDuration(staged.program_.handlers[i], staged.settings_.tickRate));
        }
    }

    if (staged.hasErrors()) {
        diagnostics_ = std::move(staged.diagnostics_);
        staged.discardStagedOutputs();
        return false;
    }

    std::string outputCommitError;
    if (!staged.commitStagedOutputs(outputCommitError)) {
        appendDiagnostic(staged.diagnostics_, {false, projectRoot_, 0,
            "Could not commit files generated during script startup: " + outputCommitError,
            "The previous runtime remains active. Check output paths and permissions, then retry /reload.", 1, 1});
        diagnostics_ = std::move(staged.diagnostics_);
        staged.discardStagedOutputs();
        return false;
    }
    staged.discardStagedOutputs();

    // Commit the fully initialized state. This is a new runtime session, not a
    // room-only reload: old waits, periodic deadlines, entities, effects, audio
    // requests and other candidate-owned state are replaced by the new startup.
    // Any old scheduled executions point into the old program tree, so they are
    // intentionally discarded here.
    program_ = std::move(staged.program_);
    globals_ = std::move(staged.globals_);
    transient_ = std::move(staged.transient_);
    audio_ = std::move(staged.audio_);
    variableStorageDirty_ = staged.variableStorageDirty_;
    openCanvases_ = std::move(staged.openCanvases_);
    diagnostics_ = std::move(staged.diagnostics_);
    rooms_ = std::move(staged.rooms_);
    // A successful script reload replaces the live room/runtime state. Advance the
    // epoch independently of RoomManager's own revision because the staged manager
    // starts its counters from zero.
    ++roomContextRevision_;
    activeTriggerPairs_.clear();
    triggerPairsRoomContextRevision_ = roomContextRevision_;
    activeInteractionFocus_.clear();
    interactionFocusRoomContextRevision_ = roomContextRevision_;
    randomStreams_ = std::move(staged.randomStreams_);
    lights_ = std::move(staged.lights_);
    beams_ = std::move(staged.beams_);
    sprites_ = std::move(staged.sprites_);
    paths_ = std::move(staged.paths_);
    entityTweens_ = std::move(staged.entityTweens_);
    pendingTweenCompletions_.clear();
    pendingAnimationCompletions_.clear();
    for (auto& [name, tween] : entityTweens_) {
        (void)name;
        tween.roomContextRevision = roomContextRevision_;
    }
    entityLabelIds_ = std::move(staged.entityLabelIds_);
    nextEntityLabelId_ = staged.nextEntityLabelId_;
    projectileCounter_ = staged.projectileCounter_;
    spriteInstanceCounter_ = staged.spriteInstanceCounter_;
    activeProjectileCount_ = staged.activeProjectileCount_;
    projectileLimitDiagnosticIssued_ = staged.projectileLimitDiagnosticIssued_;
    projectileLimitRequiredCapacity_ = staged.projectileLimitRequiredCapacity_;
    keysDown_ = std::move(staged.keysDown_);
    lightsRevision_ = staged.lightsRevision_;
    beamsRevision_ = staged.beamsRevision_;
    settings_ = std::move(staged.settings_);
    tickCount_ = staged.tickCount_;
    scheduledExecutions_ = std::move(staged.scheduledExecutions_);
    // Startup waits belong to the candidate runtime that just committed. Rebase
    // them to the newly committed live epoch; old live-runtime waits were replaced.
    for (auto& context : scheduledExecutions_) context.roomContextRevision = roomContextRevision_;
    periodicNextRun_ = std::move(staged.periodicNextRun_);
    functionCallDepth_ = 0;
    for (auto line : staged.logs_) {
        const auto replaceFirst = [&](std::string_view from, std::string_view to) {
            const auto position = line.find(from);
            if (position != std::string::npos) line.replace(position, from.size(), to);
        };
        replaceFirst("Prepared WAV audio for `", "Saved WAV audio to `");
        replaceFirst("Prepared waveform preview for `", "Saved waveform preview to `");
        log(std::move(line));
    }
    savePersistentVariables();
    return !hasErrors();
}

void Runtime::resetRuntimeState(bool resetGlobals) {
    scheduledExecutions_.clear();
    openCanvases_.clear();
    rooms_.reset();
    lights_.clear();
    beams_.clear();
    ++beamsRevision_;
    sprites_.clear();
    paths_.clear();
    entityTweens_.clear();
    pendingTweenCompletions_.clear();
    pendingAnimationCompletions_.clear();
    activeTriggerPairs_.clear();
    triggerPairsRoomContextRevision_ = roomContextRevision_;
    activeInteractionFocus_.clear();
    interactionFocusRoomContextRevision_ = roomContextRevision_;
    randomStreams_.clear();
    // Ordinary Skript-style random expressions use this runtime-local default
    // stream. Authors can explicitly seed "default" for reproducible runs, or
    // use named streams to isolate deterministic systems from one another.
    std::uint64_t defaultRandomSeed = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    defaultRandomSeed ^= static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));
    try {
        std::random_device entropy;
        defaultRandomSeed ^= (static_cast<std::uint64_t>(entropy()) << 32U) ^
                             static_cast<std::uint64_t>(entropy());
    } catch (...) {
        // The clock/address mix is a portable fallback for environments without
        // a usable operating-system entropy source. Gameplay RNG is not crypto.
    }
    randomStreams_.emplace("default", RandomStreamState{defaultRandomSeed});
    entityLabelIds_.clear();
    nextEntityLabelId_ = 1;
    projectileCounter_ = 0;
    spriteInstanceCounter_ = 0;
    activeProjectileCount_ = 0;
    projectileLimitDiagnosticIssued_ = false;
    projectileLimitRequiredCapacity_ = 1;
    keysDown_.clear();
    audio_.reset();
    std::string audioWarning;
    if (!audio_.discoverAssets(projectRoot_, audioWarning) && !audioWarning.empty()) {
        appendDiagnostic(diagnostics_, {true, projectRoot_ / "assets" / "audio", 0, audioWarning,
            "Use unique filenames and place WAV, OGG, FLAC, AIFF, or AIF files in assets/audio/.", 1, 1});
    } else if (!audioWarning.empty()) {
        appendDiagnostic(diagnostics_, {true, projectRoot_ / "assets" / "audio", 0, audioWarning,
            "Rename one of the files so every audio asset has a unique base filename.", 1, 1});
    }
    transient_.clear();
    variableStorageDirty_ = false;
    ++lightsRevision_;
    tickCount_ = 0;
    if (resetGlobals) {
        globals_.clear();
        settings_ = EngineSettings{};
        loadConfig(); // Config is the baseline; scripts run afterward and may override it.
        globals_["engine::window_title"] = settings_.windowTitle;
        globals_["engine::window_width"] = static_cast<double>(settings_.windowWidth);
        globals_["engine::window_height"] = static_cast<double>(settings_.windowHeight);
        globals_["engine::vsync"] = settings_.vsync;
        globals_["engine::target_fps"] = static_cast<double>(settings_.targetFps);
        globals_["engine::tick_rate"] = static_cast<double>(settings_.tickRate);
        globals_["engine::console_enabled"] = settings_.consoleEnabled;
        globals_["engine::show_debug_overlay"] = settings_.showDebugOverlay;
        globals_["engine::texture_filter"] = settings_.textureFilter;
        globals_["engine::pixel_perfect"] = settings_.pixelPerfect;
        globals_["engine::lighting_enabled"] = settings_.lightingEnabled;
        globals_["engine::screen_flash"] = static_cast<double>(settings_.screenFlash);
        globals_["engine::ambient_light"] = static_cast<double>(settings_.ambientLight);
        globals_["engine::lighting_quality"] = static_cast<double>(settings_.lightingQuality);
        globals_["engine::ui_scale"] = static_cast<double>(settings_.uiScale);
        globals_["engine::ui_reference_width"] = static_cast<double>(settings_.uiReferenceWidth);
        globals_["engine::ui_reference_height"] = static_cast<double>(settings_.uiReferenceHeight);
        globals_["engine::world_view_height_tiles"] = static_cast<double>(settings_.worldViewHeightTiles);
        globals_["engine::camera_follow"] = settings_.cameraFollow;
        globals_["engine::resolution_mode"] = settings_.resolutionMode;
        globals_["engine::font_path"] = settings_.fontPath;
        loadPersistentVariables();
    }
}

void Runtime::loadConfig() {
    const auto filePath = projectRoot_ / "config.json";
    std::error_code ec;
    if (!std::filesystem::exists(filePath, ec)) return; // The file is intentionally optional.
    std::ifstream input(filePath);
    if (!input) {
        appendDiagnostic(diagnostics_, {false, filePath, 0, "The config file exists but could not be read.",
                                "Check file permissions, or remove config.json to use the built-in defaults.", 1, 1});
        return;
    }
    static const std::regex entryPattern(R"CFG(^\s*"([^"\n]+)"\s*:\s*(.*?)\s*,?\s*$)CFG");
    static const std::regex primitivePattern(R"CFG(^(true|false|null|-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)$)CFG", std::regex::icase);
    std::string line;
    int lineNumber = 0;
    std::unordered_set<std::string> seen;
    while (std::getline(input, line)) {
        ++lineNumber;
        const auto cleaned = trim(line);
        if (cleaned.empty() || cleaned == "{" || cleaned == "}" || cleaned == "},") continue;
        std::smatch match;
        if (!std::regex_match(cleaned, match, entryPattern)) {
            appendDiagnostic(diagnostics_, {false, filePath, lineNumber,
                "This config line is not a valid JSON key/value pair.",
                "Use one quoted key per line, for example: \"window_width\": 1280,", 1, static_cast<int>(std::max<std::size_t>(1, cleaned.size())), cleaned});
            continue;
        }
        const auto key = lower(trim(match[1].str()));
        auto rawValue = trim(match[2].str());
        if (rawValue.empty()) {
            appendDiagnostic(diagnostics_, {false, filePath, lineNumber, "The config value is missing.",
                                    "Give the option a value, e.g. \"vsync\": true", 1, static_cast<int>(cleaned.size()), cleaned});
            continue;
        }
        if (!seen.insert(key).second) {
            appendDiagnostic(diagnostics_, {false, filePath, lineNumber, "Config option `" + key + "` appears more than once.",
                                    "Keep only one entry for each option.", 1, static_cast<int>(key.size()), cleaned});
            continue;
        }
        Value value;
        if (isQuoted(rawValue)) {
            value = Value(unquote(rawValue));
        } else {
            if (!std::regex_match(rawValue, primitivePattern)) {
                appendDiagnostic(diagnostics_, {false, filePath, lineNumber, "This config value is not a valid JSON string, boolean, number, or null.",
                                        "Text must be quoted, booleans use true/false, and numbers must not be quoted.", 1, static_cast<int>(rawValue.size()), cleaned});
                continue;
            }
            value = Value::fromString(rawValue);
        }
        std::string name = key.starts_with("engine::") ? key : "engine::" + key;
        std::string error;
        if (!applySetting(name, value, error)) {
            appendDiagnostic(diagnostics_, {false, filePath, lineNumber, error,
                "Supported options: window_title, window_width, window_height, vsync, target_fps, tick_rate, console_enabled, show_debug_overlay, texture_filter, pixel_perfect, lighting_enabled, screen_flash, ambient_light, lighting_quality, ui_scale, ui_reference_width, ui_reference_height, world_view_height_tiles, camera_follow, resolution_mode, font_path.",
                1, static_cast<int>(key.size()), cleaned});
        }
    }
}

std::string Runtime::focusedEntity(std::string_view actorName) const {
    if (interactionFocusRoomContextRevision_ != roomContextRevision_) return {};

    const auto actor = sprites_.find(lower(trim(actorName)));
    if (actor == sprites_.end() || !actor->second.entity || actor->second.projectile) return {};
    const auto focus = activeInteractionFocus_.find(actor->second.runtimeInstanceId);
    if (focus == activeInteractionFocus_.end() ||
        focus->second.actorName != actor->first ||
        focus->second.actorInstanceId != actor->second.runtimeInstanceId) {
        return {};
    }

    const auto target = sprites_.find(focus->second.targetName);
    if (target == sprites_.end() || target->second.runtimeInstanceId != focus->second.targetInstanceId ||
        !target->second.entity || target->second.projectile || !target->second.interactable) {
        return {};
    }

    // A callback or script can change range, position, hitbox or channel masks
    // between focus-update passes. Do not expose a relationship that is no longer
    // the actor's currently selected eligible target just because the next tick has
    // not reconciled the enter/exit event yet.
    const auto currentBest = selectInteractionTarget(actor->second);
    if (!currentBest || currentBest->targetName != focus->second.targetName ||
        currentBest->targetInstanceId != focus->second.targetInstanceId) {
        return {};
    }
    return focus->second.targetName;
}

bool Runtime::hasErrors() const noexcept { return cx::hasErrors(diagnostics_); }

void Runtime::executeBootstrap() {
    ExecutionContext context;
    executeStatements(program_.bootstrap, context);
    savePersistentVariables();
}

void Runtime::fireLoad() { fire(EventType::Load); }

// One logic step. The host controls how often this is called (the graphical host uses
// a fixed-step accumulator); tick updates scripts and game objects, not the rendering.
// Keeping simulation separate from drawing makes behavior easier to test and reason about.
void Runtime::tick() {
    ++tickCount_;
    for (auto& [name, object] : sprites_) { (void)name; object.moving = false; }
    resumeScheduledExecutions();
    fire(EventType::Tick);
    const float effectDt = 1.0F / static_cast<float>(std::max(1U, settings_.tickRate));
    bool expiredBeam = false;
    for (auto it = beams_.begin(); it != beams_.end();) {
        BeamEffect& beam = it->second;
        if (beam.lifetimeSeconds > 0.0F) {
            beam.ageSeconds = std::min(beam.lifetimeSeconds, beam.ageSeconds + effectDt);
            if (beam.ageSeconds + 1.0e-6F >= beam.lifetimeSeconds) {
                it = beams_.erase(it);
                expiredBeam = true;
                continue;
            }
        }
        ++it;
    }
    if (expiredBeam) ++beamsRevision_;
    updateEntities();
    updateTriggerEvents();
    updateInteractionFocus();
    settings_.screenFlash *= 0.82F;
    if(settings_.screenFlash < 0.01F) settings_.screenFlash=0.0F;

    // Periodic handlers are scheduled by deadline rather than kept alive in a tight loop.
    // If rendering or the host stalls, run once and schedule from now; never burst-catch-up.
    if (periodicNextRun_.size() != program_.handlers.size())
        periodicNextRun_.assign(program_.handlers.size(), std::chrono::steady_clock::time_point{});
    const auto now = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < program_.handlers.size(); ++i) {
        const auto& handler = program_.handlers[i];
        if (handler.type != EventType::Periodic) continue;
        if (periodicNextRun_[i] == std::chrono::steady_clock::time_point{}) {
            periodicNextRun_[i] = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                periodicDuration(handler, settings_.tickRate));
            continue;
        }
        if (now < periodicNextRun_[i]) continue;
        periodicNextRun_[i] = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            periodicDuration(handler, settings_.tickRate));
        ExecutionContext context;
        executeStatements(handler.body, context);
        savePersistentVariables();
    }
}

void Runtime::pressKey(std::string key) { fire(EventType::KeyPress, canonicalEventKey(key)); }

void Runtime::setKeyDown(std::string key, bool down) {
    const std::string canonical = canonicalEventKey(key);
    if (down) keysDown_.insert(canonical);
    else keysDown_.erase(canonical);
}

// Clear the host's held-key snapshot after focus is lost. Operating systems are not
// required to send a KeyReleased event for every key that was down when focus changed.
void Runtime::clearHeldKeys() noexcept {
    keysDown_.clear();
}

bool Runtime::keyDown(std::string_view key) const {
    return keysDown_.contains(canonicalEventKey(key));
}

void Runtime::clickButton(std::string buttonId) { fire(EventType::ButtonClick, lower(trim(buttonId))); }

void Runtime::attachFile(std::filesystem::path filePath, float mouseX, float mouseY) {
    if (filePath.is_relative()) filePath = projectRoot_ / filePath;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(filePath, ec) || ec) {
        appendDiagnostic(diagnostics_, {false, "<file attach>", 0, "The attached path is not a readable regular file: `" + filePath.string() + "`.",
            "Drag a file (not a folder) onto an open canvas button, or use `/attach \"/path/to/file\" x y` to test the event.", 1, 1});
        return;
    }
    fireFileAttach(std::filesystem::absolute(filePath, ec), mouseX, mouseY);
}

// Dispatch an event that belongs to the loaded program as a whole (for example,
// a key press, button click, load event, or logic tick). These handlers are not
// attached to one room. If an earlier matching handler changes the active room,
// later matching handlers still receive this same original program-level event.
// Events that carry a room-specific snapshot (trigger/projectile/focus/tween/animation
// contacts) use dedicated dispatchers and validate their captured room/object identity.
void Runtime::fire(EventType type, std::string_view selector) {
    for (const auto& handler : program_.handlers) {
        if (handler.type != type) continue;
        if (type == EventType::KeyPress && canonicalEventKey(selector) != handler.selector) continue;
        if (type == EventType::ButtonClick && lower(selector) != lower(handler.selector)) continue;
        if (type == EventType::ProjectileHit && lower(selector) != lower(handler.selector)) continue;
        ExecutionContext context; // A fresh local-variable scope for every individual event firing.
        executeStatements(handler.body, context);
        savePersistentVariables();
    }
}

// Trigger handlers describe a contact sampled from one room context. Unlike the
// program-level dispatcher above, this event has a room-specific payload: once a
// callback changes rooms, the remaining handlers must not receive the stale contact.
void Runtime::fireTriggerEvent(EventType type, const TriggerContact& contact) {
    const std::uint64_t eventRoomContextRevision = roomContextRevision_;
    for (const auto& handler : program_.handlers) {
        if (handler.type != type || handler.selector != contact.actorName ||
            handler.secondarySelector != contact.triggerName) continue;

        ExecutionContext context;
        context.locals["_trigger_actor"] = Value(contact.actorName);
        context.locals["_trigger_entity"] = Value(contact.triggerName);
        executeStatements(handler.body, context);
        savePersistentVariables();

        // A handler may transition rooms. Do not run later handlers against
        // contact data gathered from the previous room context.
        if (roomContextRevision_ != eventRoomContextRevision) break;
    }
}

bool Runtime::positionHitsSolidTile(const SpriteObject& object, float x, float y) const {
    const auto* room=rooms_.activeRoom();
    if (!room) return false;
    const float tile=static_cast<float>(std::max(1,room->tileSize));
    const float w=object.hitboxWidth>0?object.hitboxWidth:object.width;
    const float h=object.hitboxHeight>0?object.hitboxHeight:object.height;
    const float ox=object.hitboxWidth>0?object.hitboxOffsetX:0.0F;
    const float oy=object.hitboxHeight>0?object.hitboxOffsetY:0.0F;
    const float left=x+ox, top=y+oy, right=left+w-0.001F, bottom=top+h-0.001F;
    if (left < 0 || top < 0 || right >= static_cast<float>(room->width)*tile || bottom >= static_cast<float>(room->height)*tile) return true;
    const int x0=std::max(0,static_cast<int>(std::floor(left/tile)));
    const int y0=std::max(0,static_cast<int>(std::floor(top/tile)));
    const int x1=std::min(room->width-1,static_cast<int>(std::floor(right/tile)));
    const int y1=std::min(room->height-1,static_cast<int>(std::floor(bottom/tile)));
    for(int ty=y0;ty<=y1;++ty) for(int tx=x0;tx<=x1;++tx) {
        if (room->isSolidTile(tx, ty, rooms_.textures())) return true;
    }
    return false;
}

// Breadth-first search (BFS) finds a shortest path on an unweighted four-neighbor tile
// grid. `parent` doubles as a visited marker: -1 means unseen, otherwise it stores the
// previous cell's flat index. This is intentionally simple; moving actors are not blockers.
std::vector<TilePoint> Runtime::findTilePath(TilePoint start, TilePoint goal) const {
    const auto* room=rooms_.activeRoom();
    if (!room) return {goal};
    auto inside=[&](TilePoint p){return p.x>=0&&p.y>=0&&p.x<room->width&&p.y<room->height;};
    if(!inside(start)||!inside(goal)) return {};
    auto blocked=[&](TilePoint p){ return room->isSolidTile(p.x, p.y, rooms_.textures()); };
    if(blocked(goal)) return {};
    const int count=room->width*room->height;
    std::vector<int> parent(static_cast<std::size_t>(count),-1);
    std::queue<TilePoint> q;
    auto idx=[&](TilePoint p){return p.y*room->width+p.x;};
    q.push(start); parent[static_cast<std::size_t>(idx(start))]=idx(start);
    constexpr int dx[4]={1,-1,0,0}; constexpr int dy[4]={0,0,1,-1};
    while(!q.empty()) {
        const TilePoint cur=q.front(); q.pop();
        if(cur.x==goal.x&&cur.y==goal.y) break;
        for(int d=0;d<4;++d) {
            TilePoint n{cur.x+dx[d],cur.y+dy[d]}; if(!inside(n)||blocked(n)) continue;
            const int ni=idx(n); if(parent[static_cast<std::size_t>(ni)]!=-1) continue;
            parent[static_cast<std::size_t>(ni)]=idx(cur); q.push(n);
        }
    }
    const int goalIdx=idx(goal);
    if(parent[static_cast<std::size_t>(goalIdx)]==-1) return {};
    std::vector<TilePoint> rev;
    int at=goalIdx;
    for(int steps=0;steps<count;++steps) {
        rev.push_back({at%room->width,at/room->width});
        const int p=parent[static_cast<std::size_t>(at)]; if(p==at) break; at=p;
    }
    std::reverse(rev.begin(),rev.end());
    return rev;
}

void Runtime::fireProjectileHit(std::string_view targetName, std::string_view projectileName, int damage) {
    const std::string target=lower(trim(targetName));
    const std::string projectile=lower(trim(projectileName));
    globals_["entity::"+target+"::health"] = Value(sprites_.contains(target) ? sprites_.at(target).health : 0);
    const std::uint64_t eventRoomContextRevision = roomContextRevision_;
    for(const auto& handler:program_.handlers) {
        if(handler.type!=EventType::ProjectileHit || lower(handler.selector)!=target) continue;
        ExecutionContext context;
        context.locals["_hit_entity"]=Value(target);
        context.locals["_hit_projectile"]=Value(projectile);
        context.locals["_hit_damage"]=Value(damage);
        executeStatements(handler.body,context);
        savePersistentVariables();
        // Let the handler that changed scenes finish, but never dispatch another
        // handler belonging to the old-scene hit after its context has gone stale.
        if (roomContextRevision_ != eventRoomContextRevision) break;
    }
}

void Runtime::updateEntities() {
    updateEntityTweens();
    const float dt=1.0F/static_cast<float>(std::max(1U,settings_.tickRate));
    const auto* room=rooms_.activeRoom();
    const float tile=room?static_cast<float>(std::max(1,room->tileSize)):32.0F;
    auto steer=[&](SpriteObject& entity,float dx,float dy,float maxDistance) {
        const float len=std::sqrt(dx*dx+dy*dy);
        if(len<0.001F) return true;
        const float step=std::min(maxDistance,len);
        const float mx=dx/len*step,my=dy/len*step;
        if(std::abs(mx)>0.001F || std::abs(my)>0.001F) {
            entity.moving=true;
            if(std::abs(mx)>0.001F && std::abs(my)>0.001F) entity.facingRow=(mx<0?(my<0?3:1):(my<0?5:7));
            else if(std::abs(mx)>0.001F) entity.facingRow=mx<0?2:6;
            else entity.facingRow=my<0?4:0;
        }
        if(entity.avoidSolidTiles) {
            if(!positionHitsSolidTile(entity,entity.x+mx,entity.y)) entity.x+=mx;
            if(!positionHitsSolidTile(entity,entity.x,entity.y+my)) entity.y+=my;
        } else { entity.x+=mx; entity.y+=my; }
        return len<=maxDistance+0.001F;
    };

    // Follow named tile paths. When obstacle avoidance is enabled, breadth-first search
    // (BFS) over four-neighbor room cells routes each segment around cells marked solid
    // by the texture registry; it is not A* and does not account for moving obstacles.
    for(auto& [id,entity]:sprites_) {
        (void)id;
        if(!entity.entity || entity.projectile || entity.pathName.empty() || entityTweens_.contains(id)) continue;
        const auto pathIt=paths_.find(entity.pathName);
        if(pathIt==paths_.end() || pathIt->second.waypoints.empty()) { entity.pathName.clear(); continue; }
        const auto& route=pathIt->second.waypoints;
        entity.pathWaypoint%=route.size();
        const TilePoint goal=route[entity.pathWaypoint];
        const TilePoint current{static_cast<int>(std::floor((entity.x+entity.width*0.5F)/tile)),static_cast<int>(std::floor((entity.y+entity.height*0.5F)/tile))};
        float tx=(static_cast<float>(goal.x)+0.5F)*tile-entity.width*0.5F;
        float ty=(static_cast<float>(goal.y)+0.5F)*tile-entity.height*0.5F;
        bool segmentDone=false;
        if(entity.avoidSolidTiles && room) {
            if(entity.navigationGoal.x!=goal.x || entity.navigationGoal.y!=goal.y || entity.navigationTiles.empty()) {
                entity.navigationTiles=findTilePath(current,goal);
                entity.navigationWaypoint=entity.navigationTiles.size()>1?1:0;
                entity.navigationGoal=goal;
            }
            if(entity.navigationTiles.empty()) continue;
            if(entity.navigationWaypoint>=entity.navigationTiles.size()) segmentDone=true;
            else {
                const auto nav=entity.navigationTiles[entity.navigationWaypoint];
                tx=(static_cast<float>(nav.x)+0.5F)*tile-entity.width*0.5F;
                ty=(static_cast<float>(nav.y)+0.5F)*tile-entity.height*0.5F;
                segmentDone=steer(entity,tx-entity.x,ty-entity.y,entity.pathSpeedTilesPerSecond*tile*dt);
                if(segmentDone && entity.navigationWaypoint+1<entity.navigationTiles.size()) { ++entity.navigationWaypoint; segmentDone=false; }
                else if(segmentDone) segmentDone=true;
            }
        } else segmentDone=steer(entity,tx-entity.x,ty-entity.y,entity.pathSpeedTilesPerSecond*tile*dt);
        if(segmentDone) {
            entity.pathWaypoint=(entity.pathWaypoint+1)%route.size();
            entity.navigationTiles.clear(); entity.navigationWaypoint=0; entity.navigationGoal={-1,-1};
        }
    }

    updateProjectiles(dt, tile);

    // Named clips take precedence over the legacy movement loop. Their time base is
    // the simulation tick, so frame changes do not depend on render rate or stalls.
    pendingAnimationCompletions_.clear();
    if (pendingAnimationCompletions_.capacity() < sprites_.size())
        pendingAnimationCompletions_.reserve(sprites_.size());
    for(auto& [id,entity]:sprites_) {
        (void)id;
        if(!entity.entity || entity.frameWidth<=0 || entity.frameHeight<=0 || entity.frameColumns<=1 || entity.frameRows<1) continue;
        entity.facingRow=std::clamp(entity.facingRow,0,entity.frameRows-1);
        if (!entity.currentAnimation.empty()) {
            const auto clipIt = entity.animationClips.find(entity.currentAnimation);
            if (clipIt == entity.animationClips.end()) {
                // A sheet/clip change invalidated this selection. Do not leave a
                // stale name that could accidentally refer to a later definition.
                entity.currentAnimation.clear();
                entity.animationPlaying = false;
                entity.animationElapsedTicks = 0.0;
                ++entity.animationPlaybackGeneration;
            } else {
                const AnimationClip& clip = clipIt->second;
                entity.animationRow = clip.followsFacing
                    ? entity.facingRow : std::clamp(clip.row, 0, entity.frameRows - 1);
                if (entity.animationPlaying) {
                    const double frameDurationTicks = static_cast<double>(std::max(1U, settings_.tickRate)) /
                        std::max(0.1, clip.framesPerSecond);
                    entity.animationElapsedTicks += 1.0;
                    if (entity.animationElapsedTicks + 1.0e-9 >= frameDurationTicks) {
                        entity.animationElapsedTicks = std::max(0.0, entity.animationElapsedTicks - frameDurationTicks);
                        if (entity.animationFrame < clip.lastFrame) {
                            ++entity.animationFrame;
                        } else if (clip.looping) {
                            entity.animationFrame = clip.firstFrame;
                        } else {
                            // Completion is recorded only after the final frame has
                            // remained visible for one complete frame interval.
                            entity.animationPlaying = false;
                            entity.animationElapsedTicks = 0.0;
                            pendingAnimationCompletions_.push_back({
                                entity.name, entity.currentAnimation, entity.runtimeInstanceId,
                                entity.animationPlaybackGeneration, roomContextRevision_,
                                entity.animationFrame, entity.animationRow});
                        }
                    }
                }
                continue; // A selected custom clip holds its frame after a one-shot ends.
            }
        }
        // Backwards-compatible eight-way walk cycle. It remains active only when
        // no named clip is selected, so scripted acting cannot be overwritten.
        if(entity.moving) {
            if(++entity.animationTicks>=7) { entity.animationTicks=0; entity.animationFrame=(entity.animationFrame+1)%entity.frameColumns; }
        } else { entity.animationFrame=0; entity.animationTicks=0; }
    }
    // Dispatch only after iteration; event handlers may delete or replace sprites.
    for (const auto& completion : pendingAnimationCompletions_)
        fireAnimationComplete(completion);
}

void Runtime::updateTriggerEvents() {
    // A room transition discards contacts from the old scene without firing exits:
    // exits belong to an entity leaving a trigger in the same active world, not to
    // tearing down one room and constructing another.
    if (triggerPairsRoomContextRevision_ != roomContextRevision_) {
        activeTriggerPairs_.clear();
        triggerPairsRoomContextRevision_ = roomContextRevision_;
    }

    // The syntax intentionally names both endpoints, so do not build a full
    // actor-by-trigger Cartesian product every tick. Walk the (usually small)
    // handler list instead: work scales with authored trigger pairs, not with all
    // possible combinations of entities in a busy room. No temporary selector
    // strings or per-tick watcher container is needed.
    std::map<TriggerPairId, TriggerContact> currentPairs;
    for (const auto& handler : program_.handlers) {
        if (handler.type != EventType::TriggerEnter && handler.type != EventType::TriggerExit) continue;
        const auto actorIt = sprites_.find(handler.selector);
        const auto triggerIt = sprites_.find(handler.secondarySelector);
        if (actorIt == sprites_.end() || triggerIt == sprites_.end()) continue;
        const auto& actor = actorIt->second;
        const auto& trigger = triggerIt->second;
        if (!actor.entity || actor.projectile ||
            (actor.collisionLayer & CollisionActors) == 0U ||
            (actor.collisionLayer & CollisionTriggers) != 0U) continue;
        if (!trigger.entity || trigger.projectile ||
            (trigger.collisionLayer & CollisionTriggers) == 0U) continue;
        if (actor.runtimeInstanceId == trigger.runtimeInstanceId) continue;
        const TriggerPairId pairId{actor.runtimeInstanceId, trigger.runtimeInstanceId};
        if (currentPairs.contains(pairId)) continue; // enter + exit handlers can share one pair.
        // spritesOverlap is the shared AABB + mutual-channel-filter query. It
        // intentionally ignores visibility, just like projectile collision.
        if (!spritesOverlap(handler.selector, handler.secondarySelector)) continue;
        currentPairs.emplace(pairId, TriggerContact{handler.selector, handler.secondarySelector});
    }

    const auto contactStillOverlaps = [this](const TriggerPairId& pairId,
                                              const TriggerContact& contact) {
        const auto actor = sprites_.find(contact.actorName);
        const auto trigger = sprites_.find(contact.triggerName);
        if (actor == sprites_.end() || trigger == sprites_.end()) return false;
        if (actor->second.runtimeInstanceId != pairId.first ||
            trigger->second.runtimeInstanceId != pairId.second) return false;
        if (!actor->second.entity || actor->second.projectile ||
            (actor->second.collisionLayer & CollisionActors) == 0U ||
            (actor->second.collisionLayer & CollisionTriggers) != 0U) return false;
        if (!trigger->second.entity || trigger->second.projectile ||
            (trigger->second.collisionLayer & CollisionTriggers) == 0U) return false;
        return spritesOverlap(contact.actorName, contact.triggerName);
    };

    // Work from a snapshot because callbacks are allowed to create, delete, move,
    // or reclassify objects. Validate each transition immediately before dispatch
    // so an earlier callback cannot cause a stale enter/exit later in this batch.
    const auto previousPairs = activeTriggerPairs_;
    for (const auto& [pairId, contact] : previousPairs) {
        if (currentPairs.contains(pairId)) continue;
        if (contactStillOverlaps(pairId, contact)) continue;

        activeTriggerPairs_.erase(pairId);
        fireTriggerEvent(EventType::TriggerExit, contact);
        if (roomContextRevision_ != triggerPairsRoomContextRevision_) {
            activeTriggerPairs_.clear();
            triggerPairsRoomContextRevision_ = roomContextRevision_;
            return;
        }
    }

    for (const auto& [pairId, contact] : currentPairs) {
        if (previousPairs.contains(pairId) || activeTriggerPairs_.contains(pairId)) continue;
        if (!contactStillOverlaps(pairId, contact)) continue;

        // Register before the callback: if the enter handler removes the actor or
        // trigger, the following tick can deliver the matching exit exactly once.
        activeTriggerPairs_.emplace(pairId, contact);
        fireTriggerEvent(EventType::TriggerEnter, contact);
        if (roomContextRevision_ != triggerPairsRoomContextRevision_) {
            activeTriggerPairs_.clear();
            triggerPairsRoomContextRevision_ = roomContextRevision_;
            return;
        }
    }
}

void Runtime::fireFileAttach(const std::filesystem::path& filePath, float mouseX, float mouseY) {
    std::string buttonUnderCursor;
    for (const auto& canvasName : openCanvases_) {
        const auto* canvas = findCanvas(canvasName);
        if (!canvas) continue;
        for (const auto& element : canvas->elements) {
            if (element.type == CanvasElementType::Button && mouseX >= element.x && mouseY >= element.y &&
                mouseX <= element.x + element.width && mouseY <= element.y + element.height) {
                buttonUnderCursor = element.id;
            }
        }
    }
    for (const auto& handler : program_.handlers) {
        if (handler.type != EventType::FileAttach) continue;
        ExecutionContext context;
        context.locals["_attached_file"] = Value(filePath.string());
        context.locals["_mouse_x"] = Value(static_cast<double>(mouseX));
        context.locals["_mouse_y"] = Value(static_cast<double>(mouseY));
        context.locals["_button_under_cursor"] = Value(buttonUnderCursor);
        executeStatements(handler.body, context);
        savePersistentVariables();
    }
    if (buttonUnderCursor.empty()) log("Attached file received, but the cursor was not over an open canvas button.");
    else log("Attached file `" + filePath.filename().string() + "` over button `" + buttonUnderCursor + "`.");
}

void Runtime::loadPersistentVariables() {
    VariableStorage::VariableMap loaded;
    std::string warning;
    if (!VariableStorage::load(projectRoot_, loaded, warning)) {
        appendDiagnostic(diagnostics_, {true, projectRoot_ / "variables.csv", 0, warning,
            "Close the engine and repair or remove variables.csv. If it is missing, variables.cxv is the compressed backup.", 1, 1});
        return;
    }
    for (auto& [name, value] : loaded) {
        if (name.starts_with("engine::") || name.starts_with("room::") || name.starts_with("entrance::") || name.starts_with("entity::")) continue;
        globals_[name] = std::move(value);
    }
    if (!warning.empty()) appendDiagnostic(diagnostics_, {true, projectRoot_ / "variables.csv", 0, warning,
        "Check the named file before editing it. variables.csv is authoritative when present; preserve variables.csv.bak and variables.cxv.bak before manual repair.", 1, 1});
    // A clean existing save can refresh its cache after bootstrap. A brand-new project,
    // however, should not acquire three empty persistence files merely because it launched
    // once. If neither data representation exists, wait until the first persistent value is
    // actually assigned. A manifest without either data file is already rejected by load().
    std::error_code fileCheck;
    const bool hasCsv = std::filesystem::exists(projectRoot_ / "variables.csv", fileCheck);
    if (fileCheck) {
        warning = "Could not inspect variables.csv after loading: " + fileCheck.message();
    }
    fileCheck.clear();
    const bool hasCache = std::filesystem::exists(projectRoot_ / "variables.cxv", fileCheck);
    if (fileCheck) {
        if (!warning.empty()) warning += '\n';
        warning += "Could not inspect variables.cxv after loading: " + fileCheck.message();
    }
    // If malformed rows were skipped, do not serialize the partial in-memory map over
    // the user's source file. Existing clean saves may be refreshed; brand-new projects wait.
    variableStorageDirty_ = warning.empty() && (hasCsv || hasCache);
}

void Runtime::savePersistentVariables() {
    // The candidate runtime must never overwrite the live save before its
    // bootstrap and on-load handlers have succeeded.
    if (stagingLoad_) return;
    if (!variableStorageDirty_) return;
    VariableStorage::VariableMap persistent;
    for (const auto& [name, value] : globals_) {
        if (name.starts_with("engine::") || name.starts_with("room::") || name.starts_with("entrance::") || name.starts_with("entity::") ||
            name.starts_with("_") || name.starts_with("^") || name.starts_with("$")) continue;
        persistent.emplace(name, value);
    }
    std::string error;
    if (!VariableStorage::save(projectRoot_, persistent, error)) {
        appendDiagnostic(diagnostics_, {true, projectRoot_ / "variables.csv", 0, "Could not save persistent variables: " + error,
            "Check that the project folder is writable. Variables remain available until the process exits.", 1, 1});
    } else variableStorageDirty_ = false;
}

bool Runtime::makeStagedOutputPath(const std::filesystem::path& destination,
                                   std::filesystem::path& temporaryPath,
                                   std::string& error) {
    static std::atomic<std::uint64_t> sequence{0};
    if (stagedOutputDirectory_.empty()) {
        std::error_code ec;
        const auto tempRoot = std::filesystem::temp_directory_path(ec);
        if (ec) {
            error = "Could not locate the system temporary directory: " + ec.message();
            return false;
        }
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        bool created = false;
        for (int attempt = 0; attempt < 16; ++attempt) {
            auto candidateDir = tempRoot / ("cxengine-load-" + std::to_string(timestamp) + "-" +
                std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
            ec.clear();
            if (std::filesystem::create_directory(candidateDir, ec)) {
                stagedOutputDirectory_ = std::move(candidateDir);
                created = true;
                break;
            }
            if (ec && ec != std::errc::file_exists) {
                error = "Could not create a temporary directory for startup outputs: " + ec.message();
                return false;
            }
        }
        if (!created) {
            error = "Could not reserve a unique temporary directory for startup outputs.";
            return false;
        }
    }

    temporaryPath = stagedOutputDirectory_ /
        ("output-" + std::to_string(stagedOutputFiles_.size()) + destination.extension().string());
    return true;
}

bool Runtime::commitStagedOutputs(std::string& error) {
    if (stagedOutputFiles_.empty()) return true;

    // Keep only the final write to a given destination, matching normal script
    // execution order while avoiding repeated backup/replace cycles.
    std::vector<StagedOutputFile> outputs;
    std::unordered_set<std::string> destinations;
    for (auto it = stagedOutputFiles_.rbegin(); it != stagedOutputFiles_.rend(); ++it) {
        std::error_code ec;
        auto absolute = std::filesystem::absolute(it->destinationPath, ec);
        if (ec) {
            error = "Could not resolve output path `" + it->destinationPath.string() + "`: " + ec.message();
            return false;
        }
        absolute = absolute.lexically_normal();
        if (!destinations.insert(absolute.string()).second) continue;
        outputs.push_back({it->temporaryPath, std::move(absolute)});
    }
    std::reverse(outputs.begin(), outputs.end());

    struct PreparedOutput {
        std::filesystem::path destination;
        std::filesystem::path stagedSibling;
        std::filesystem::path backup;
        bool hadOriginal{false};
        bool backedUp{false};
        bool installed{false};
    };
    static std::atomic<std::uint64_t> sequence{0};
    const auto token = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
        std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
    std::vector<PreparedOutput> prepared;
    prepared.reserve(outputs.size());

    const auto cleanupPrepared = [&]() {
        for (const auto& item : prepared) {
            std::error_code ec;
            if (!item.stagedSibling.empty()) std::filesystem::remove(item.stagedSibling, ec);
        }
    };
    const auto rollback = [&]() {
        for (auto it = prepared.rbegin(); it != prepared.rend(); ++it) {
            std::error_code ec;
            if (it->installed) std::filesystem::remove(it->destination, ec);
            if (it->backedUp) {
                ec.clear();
                std::filesystem::rename(it->backup, it->destination, ec);
            }
        }
        cleanupPrepared();
    };

    // Prepare all replacement files beside their destinations. This keeps the
    // final rename on the destination filesystem and avoids exposing partial
    // WAV/SVG output. No destination is replaced during this phase.
    for (std::size_t i = 0; i < outputs.size(); ++i) {
        const auto& output = outputs[i];
        PreparedOutput item;
        item.destination = output.destinationPath;
        auto parent = item.destination.parent_path();
        if (parent.empty()) parent = ".";
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "Could not create output directory `" + parent.string() + "`: " + ec.message();
            cleanupPrepared();
            return false;
        }
        const auto base = item.destination.filename().string() + ".cxengine-" + token + "-" + std::to_string(i);
        item.stagedSibling = parent / (base + ".tmp");
        item.backup = parent / (base + ".bak");
        const auto status = std::filesystem::symlink_status(item.destination, ec);
        if (ec && ec != std::errc::no_such_file_or_directory) {
            error = "Could not inspect output destination `" + item.destination.string() + "`: " + ec.message();
            prepared.push_back(std::move(item));
            cleanupPrepared();
            return false;
        }
        if (!ec && std::filesystem::exists(status)) {
            if (!std::filesystem::is_regular_file(status)) {
                error = "Output destination `" + item.destination.string() + "` exists and is not a regular file.";
                prepared.push_back(std::move(item));
                cleanupPrepared();
                return false;
            }
            item.hadOriginal = true;
        }
        ec.clear();
        if (!std::filesystem::copy_file(output.temporaryPath, item.stagedSibling,
                                        std::filesystem::copy_options::none, ec)) {
            error = "Could not prepare generated output for `" + item.destination.string() + "`: " +
                (ec ? ec.message() : std::string("copy operation failed"));
            prepared.push_back(std::move(item));
            cleanupPrepared();
            return false;
        }
        prepared.push_back(std::move(item));
    }

    // Back up each existing file immediately before replacement. On any rename
    // failure, restore all already changed destinations in reverse order.
    for (auto& item : prepared) {
        std::error_code ec;
        if (item.hadOriginal) {
            std::filesystem::rename(item.destination, item.backup, ec);
            if (ec) {
                error = "Could not protect existing output `" + item.destination.string() + "`: " + ec.message();
                rollback();
                return false;
            }
            item.backedUp = true;
        }
        ec.clear();
        std::filesystem::rename(item.stagedSibling, item.destination, ec);
        if (ec) {
            error = "Could not install generated output `" + item.destination.string() + "`: " + ec.message();
            rollback();
            return false;
        }
        item.installed = true;
    }

    for (const auto& item : prepared) {
        if (item.backedUp) {
            std::error_code ec;
            std::filesystem::remove(item.backup, ec);
        }
    }
    return true;
}

void Runtime::discardStagedOutputs() noexcept {
    std::error_code ec;
    if (!stagedOutputDirectory_.empty()) std::filesystem::remove_all(stagedOutputDirectory_, ec);
    stagedOutputFiles_.clear();
    stagedOutputDirectory_.clear();
}

void Runtime::executeStatements(const std::vector<Statement>& statements, ExecutionContext& context) {
    if (context.frames.empty()) context.frames.push_back({&statements, 0, 1, 1, false, false, nullptr, false, {}});
    runExecution(context);
    if (context.suspended) scheduledExecutions_.push_back(std::move(context));
}

// A `wait` suspends an execution context rather than blocking the game thread. Each tick
// resumes contexts whose deadline has passed; the saved program counter/local variables
// let the script continue at the statement after `wait`.
void Runtime::resumeScheduledExecutions() {
    const auto now = std::chrono::steady_clock::now();
    std::deque<ExecutionContext> pending;
    std::deque<ExecutionContext> ready;
    while (!scheduledExecutions_.empty()) {
        auto context = std::move(scheduledExecutions_.front());
        scheduledExecutions_.pop_front();
        // Discard stale contexts even if their deadline has not arrived. This keeps
        // an old-room coroutine from lingering until its original wake time.
        if (context.roomContextRevision != roomContextRevision_) continue;
        if (context.wakeAt <= now) ready.push_back(std::move(context));
        else pending.push_back(std::move(context));
    }
    scheduledExecutions_ = std::move(pending);
    while (!ready.empty()) {
        auto context = std::move(ready.front());
        ready.pop_front();
        // A preceding ready continuation may itself have changed the room. Recheck
        // immediately before execution so two waits due on the same tick cannot
        // cross a scene boundary in queue order.
        if (context.roomContextRevision != roomContextRevision_) continue;
        context.suspended = false;
        runExecution(context);
        if (context.suspended) scheduledExecutions_.push_back(std::move(context));
        savePersistentVariables();
    }
    // A ready continuation can invalidate entries that were still waiting in the
    // pending deque when it was built above. Remove those now rather than next tick.
    for (auto it = scheduledExecutions_.begin(); it != scheduledExecutions_.end();) {
        if (it->roomContextRevision != roomContextRevision_) it = scheduledExecutions_.erase(it);
        else ++it;
    }
}

// Interpret a prepared list of statements from left to right. Nested blocks and function
// calls reuse the same interpreter rules, while each event/function context owns its locals.
std::string Runtime::resolveVariableName(std::string_view rawName, const ExecutionContext& context) const {
    std::string name = canonicalVariableName(rawName);
    std::size_t search = 0;
    while ((search = name.find("%{", search)) != std::string::npos) {
        const auto innerEnd = name.find("}%", search + 2);
        if (innerEnd == std::string::npos) break;
        const std::string innerName = canonicalVariableName(std::string_view(name).substr(search + 1, innerEnd - search));
        Value value;
        if (innerName.starts_with("$")) {
            const auto* texture = rooms_.textures().find(std::string_view(innerName).substr(1));
            if (texture) value = Value(texture->id);
        } else if (innerName.starts_with("@")) {
            const auto* audioPath = audio_.findAssetPath(std::string_view(innerName).substr(1));
            if (audioPath) value = Value(audioPath->generic_string());
        } else value = getVariable(innerName, context);
        const std::string replacement = value.toString();
        name.replace(search, innerEnd + 2 - search, replacement);
        search += replacement.size();
    }
    return lower(name);
}

Value Runtime::getVariable(std::string_view rawName, const ExecutionContext& context) const {
    const auto name = resolveVariableName(rawName, context);
    if (!name.empty() && name.front() == '_') {
        const auto it = context.locals.find(name);
        return it == context.locals.end() ? Value{} : it->second;
    }
    if (!name.empty() && name.front() == '^') {
        const auto it = transient_.find(name.substr(1));
        return it == transient_.end() ? Value{} : it->second;
    }
    const auto it = globals_.find(name);
    return it == globals_.end() ? Value{} : it->second;
}

std::vector<Runtime::ListEntry> Runtime::listEntries(std::string_view rawWildcard, const ExecutionContext& context) const {
    const std::string wildcard = resolveVariableName(rawWildcard, context);
    if (!isListWildcard(wildcard)) return {};
    const std::string root = wildcard.substr(0, wildcard.size() - 1); // includes the trailing ::
    std::vector<ListEntry> result;
    auto collect = [&](const VariableMap& values, std::string_view prefix, bool prefixMatches) {
        for (const auto& [name, value] : values) {
            if (!name.starts_with(prefix)) continue;
            const auto index = name.substr(prefix.size());
            if (index.empty()) continue;
            result.push_back({index, value});
        }
        (void)prefixMatches;
    };
    if (wildcard.starts_with("_")) {
        collect(context.locals, root, true);
    } else if (wildcard.starts_with("^")) {
        const std::string transientRoot = root.substr(1);
        collect(transient_, transientRoot, true);
    } else {
        collect(globals_, root, true);
    }
    auto numericKey = [](std::string_view key, std::uint64_t& value) {
        if (key.empty()) return false;
        value = 0;
        for (const unsigned char c : key) {
            if (!std::isdigit(c)) return false;
            value = value * 10U + static_cast<std::uint64_t>(c - '0');
        }
        return true;
    };
    std::sort(result.begin(), result.end(), [&](const ListEntry& a, const ListEntry& b) {
        std::uint64_t ai = 0, bi = 0;
        const bool an = numericKey(a.index, ai), bn = numericKey(b.index, bi);
        if (an && bn) return ai < bi;
        if (an != bn) return an; // integer indices first, then named keys
        return a.index < b.index;
    });
    return result;
}

void Runtime::clearList(std::string_view rawWildcard, ExecutionContext& context, const Statement* origin) {
    const std::string wildcard = resolveVariableName(rawWildcard, context);
    if (!isListWildcard(wildcard)) {
        if (origin) runtimeError(*origin, "A list wildcard must end with `::*`.", "Use `clear {inventory::*}` to remove all list entries.");
        return;
    }
    const std::string prefix = wildcard.substr(0, wildcard.size() - 1);
    auto eraseMatching = [&](VariableMap& values, std::string_view start) {
        bool changed = false;
        for (auto it = values.begin(); it != values.end();) {
            if (it->first.starts_with(start) && it->first.size() > start.size()) {
                it = values.erase(it); changed = true;
            } else ++it;
        }
        return changed;
    };
    if (wildcard.starts_with("_")) (void)eraseMatching(context.locals, prefix);
    else if (wildcard.starts_with("^")) (void)eraseMatching(transient_, prefix.substr(1));
    else if (eraseMatching(globals_, prefix)) variableStorageDirty_ = true;
}

void Runtime::appendListValue(std::string_view rawWildcard, Value value, ExecutionContext& context, const Statement* origin) {
    const std::string wildcard = resolveVariableName(rawWildcard, context);
    if (!isListWildcard(wildcard)) {
        if (origin) runtimeError(*origin, "A list must be named with a `::*` wildcard when appending.", "Use `add \\\"potion\\\" to {inventory::*}`.");
        return;
    }
    const auto entries = listEntries(wildcard, context);
    std::uint64_t maxIndex = 0;
    for (const auto& entry : entries) {
        std::uint64_t n = 0;
        if (!entry.index.empty() && std::all_of(entry.index.begin(), entry.index.end(), [](unsigned char c) { return std::isdigit(c); })) {
            try { n = std::stoull(entry.index); } catch (...) { n = 0; }
            maxIndex = std::max(maxIndex, n);
        }
    }
    const std::string target = wildcard.substr(0, wildcard.size() - 1) + std::to_string(maxIndex + 1U);
    setVariable(target, std::move(value), context, origin);
}

// Assignment mirrors resolveVariableName's lifetime rules. Local values belong to the
// current event/function context; caret variables live only until process exit; ordinary
// globals are marked dirty so the next safe save can persist them. Built-in engine settings
// are validated through applySetting instead of being stored as ordinary game variables.
void Runtime::setVariable(std::string_view rawName, Value value, ExecutionContext& context, const Statement* origin) {
    const auto name = resolveVariableName(rawName, context);
    if (name.starts_with("$")) {
        if (origin) runtimeError(*origin, "Texture variables like {$stone_up} are read-only.",
                                 "Add or rename an image in assets/textures/; its file name becomes the texture variable.");
        return;
    }
    if (name.starts_with("@")) {
        if (origin) runtimeError(*origin, "Audio asset variables like {@click} are read-only.",
                                 "Add or rename an audio file in assets/audio/; its file name becomes the audio variable.");
        return;
    }
    if (!validVariableName(name)) {
        if (origin) runtimeError(*origin, "Invalid variable name: `{ " + name + " }`.", "Variable names may use letters, numbers, underscores, and colons. Example: {player_score}");
        return;
    }
    if (!name.empty() && name.front() == '_') {
        context.locals[name] = std::move(value);
        return;
    }
    if (!name.empty() && name.front() == '^') {
        transient_[name.substr(1)] = std::move(value);
        return;
    }
    if (name.starts_with("engine::")) {
        std::string error;
        if (!applySetting(name, value, error)) {
            if (origin) runtimeError(*origin, error, "Supported settings: engine::window_title, engine::window_width, engine::window_height, engine::vsync, engine::target_fps, engine::tick_rate, engine::console_enabled, engine::show_debug_overlay, engine::texture_filter, engine::pixel_perfect, engine::lighting_enabled, engine::screen_flash, engine::ambient_light, engine::lighting_quality, engine::ui_scale, engine::ui_reference_width, engine::ui_reference_height, engine::world_view_height_tiles, engine::camera_follow, engine::resolution_mode, engine::font_path");
            else appendDiagnostic(diagnostics_, {false, "<console>", 0, error, "Check the setting name and value type.", 1, 1});
            return;
        }
    }
    globals_[name] = std::move(value);
    if (!name.starts_with("engine::") && !name.starts_with("room::") && !name.starts_with("entrance::") && !name.starts_with("entity::")) variableStorageDirty_ = true;
}

bool Runtime::applySetting(std::string_view nameView, const Value& value, std::string& error) {
    const std::string name = lower(nameView);
    auto unsignedSetting = [&](unsigned int& target, unsigned int minimum, unsigned int maximum, std::string_view label) {
        const double number = value.numberOr(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(number) || std::floor(number) != number || number < minimum || number > maximum) {
            error = "`" + name + "` must be a whole number from " + std::to_string(minimum) + " to " + std::to_string(maximum) + ".";
            return false;
        }
        target = static_cast<unsigned int>(number);
        (void)label;
        return true;
    };
    if (name == "engine::window_title") {
        if (!value.isString()) { error = "`engine::window_title` must be text, for example: set engine::window_title to \"My Game\"."; return false; }
        settings_.windowTitle = value.toString();
    } else if (name == "engine::window_width") {
        if (!unsignedSetting(settings_.windowWidth, 320, 7680, "window width")) return false;
    } else if (name == "engine::window_height") {
        if (!unsignedSetting(settings_.windowHeight, 240, 4320, "window height")) return false;
    } else if (name == "engine::vsync") {
        if (!value.isBool()) { error = "`engine::vsync` must be true or false."; return false; }
        settings_.vsync = value.truthy();
    } else if (name == "engine::target_fps") {
        if (!unsignedSetting(settings_.targetFps, 0, 1000, "target FPS")) return false;
    } else if (name == "engine::tick_rate") {
        if (!unsignedSetting(settings_.tickRate, 1, 1000, "tick rate")) return false;
    } else if (name == "engine::console_enabled") {
        if (!value.isBool()) { error = "`engine::console_enabled` must be true or false."; return false; }
        settings_.consoleEnabled = value.truthy();
    } else if (name == "engine::show_debug_overlay") {
        if (!value.isBool()) { error = "`engine::show_debug_overlay` must be true or false."; return false; }
        settings_.showDebugOverlay = value.truthy();
    } else if (name == "engine::texture_filter") {
        if (!value.isString()) { error = "`engine::texture_filter` must be `\"nearest\"` or `\"linear\"`."; return false; }
        const std::string filter = lower(trim(value.toString()));
        if (filter != "nearest" && filter != "linear") {
            error = "Unknown texture filter `" + filter + "`. Use `\"nearest\"` for crisp pixel art or `\"linear\"` for smooth textures.";
            return false;
        }
        settings_.textureFilter = filter;
    } else if (name == "engine::pixel_perfect") {
        if (!value.isBool()) { error = "`engine::pixel_perfect` must be true or false."; return false; }
        settings_.pixelPerfect = value.truthy();
    } else if (name == "engine::lighting_enabled") {
        if (!value.isBool()) { error = "`engine::lighting_enabled` must be true or false."; return false; }
        settings_.lightingEnabled = value.truthy();
    } else if (name == "engine::screen_flash") {
        const double number=value.numberOr(std::numeric_limits<double>::quiet_NaN());
        if(!std::isfinite(number)||number<0.0||number>1.0) { error="`engine::screen_flash` must be between 0 (off) and 1 (full flash)."; return false; }
        settings_.screenFlash=static_cast<float>(number);
    } else if (name == "engine::ambient_light") {
        const double number = value.numberOr(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(number) || number < 0.0 || number > 1.0) {
            error = "`engine::ambient_light` must be a number from 0 (dark) to 1 (fully bright).";
            return false;
        }
        settings_.ambientLight = static_cast<float>(number);
    } else if (name == "engine::lighting_quality") {
        if (!unsignedSetting(settings_.lightingQuality, 1, 3, "lighting quality")) return false;
    } else if (name == "engine::ui_scale") {
        const double number = value.numberOr(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(number) || number < 0.25 || number > 4.0) {
            error = "`engine::ui_scale` must be between 0.25 and 4.0.";
            return false;
        }
        settings_.uiScale = static_cast<float>(number);
    } else if (name == "engine::ui_reference_width") {
        if (!unsignedSetting(settings_.uiReferenceWidth, 320, 7680, "UI reference width")) return false;
    } else if (name == "engine::ui_reference_height") {
        if (!unsignedSetting(settings_.uiReferenceHeight, 240, 4320, "UI reference height")) return false;
    } else if (name == "engine::world_view_height_tiles") {
        const double number = value.numberOr(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(number) || number < 6.0 || number > 100.0) {
            error = "`engine::world_view_height_tiles` must be from 6 to 100 tiles.";
            return false;
        }
        settings_.worldViewHeightTiles = static_cast<float>(number);
    } else if (name == "engine::camera_follow") {
        if (!value.isString()) { error = "`engine::camera_follow` must be an entity name or \"none\"."; return false; }
        settings_.cameraFollow = lower(trim(value.toString()));
        if (settings_.cameraFollow == "") settings_.cameraFollow = "none";
    } else if (name == "engine::resolution_mode") {
        if (!value.isString()) { error = "`engine::resolution_mode` must be `\"responsive\"` or `\"fixed\"`."; return false; }
        const auto mode = lower(trim(value.toString()));
        if (mode != "responsive" && mode != "fixed") { error = "Unknown resolution mode. Use `responsive` to adapt UI to the resizable window, or `fixed` to keep the configured window size."; return false; }
        settings_.resolutionMode = mode;
    } else if (name == "engine::font_path") {
        if (!value.isString()) { error = "`engine::font_path` must be a path string, e.g. `set engine::font_path to \"assets/fonts/NerdFontPropo.ttf\"`."; return false; }
        settings_.fontPath = value.toString();
    } else {
        error = "Unknown engine setting: `" + name + "`.";
        return false;
    }
    return true;
}

void Runtime::runtimeError(const Statement& statement, std::string message, std::string suggestion) {
    Diagnostic diagnostic{false, std::filesystem::path(statement.source), statement.line,
                          std::move(message), std::move(suggestion), 1,
                          static_cast<int>(std::max<std::size_t>(1, statement.expression.size()))};
    diagnostic.sourceLineText = statement.sourceLineText;
    appendDiagnostic(diagnostics_, std::move(diagnostic));
}

const CanvasDefinition* Runtime::findCanvas(std::string_view name) const noexcept {
    const auto wanted = lower(trim(name));
    for (const auto& canvas : program_.canvases) if (lower(canvas.name) == wanted) return &canvas;
    return nullptr;
}

void Runtime::setViewportSize(float width, float height) noexcept {
    if (std::isfinite(width) && width > 0.0F) viewportWidth_ = width;
    if (std::isfinite(height) && height > 0.0F) viewportHeight_ = height;
}

std::vector<CanvasElement> Runtime::resolveCanvasElements(const CanvasDefinition& canvas, float viewportWidth, float viewportHeight) const {
    const float winW = std::max(1.0F, viewportWidth);
    const float winH = std::max(1.0F, viewportHeight);
    const float refW = static_cast<float>(std::max(1U, settings_.uiReferenceWidth));
    const float refH = static_cast<float>(std::max(1U, settings_.uiReferenceHeight));
    const float scaleX = winW / refW;
    const float scaleY = winH / refH;
    const float sizeScale = std::max(0.1F, std::min(scaleX, scaleY) * settings_.uiScale);
    std::vector<CanvasElement> resolved;
    resolved.reserve(canvas.elements.size());
    for (const auto& source : canvas.elements) {
        CanvasElement element = source;
        element.width = source.width * sizeScale;
        element.height = source.height * sizeScale;
        element.fontSize = static_cast<unsigned int>(std::clamp(std::lround(static_cast<double>(source.fontSize) * sizeScale), 1L, 256L));
        element.gap = source.gap * sizeScale;
        if (source.layoutMode == CanvasLayoutMode::Absolute) {
            element.x = source.x * scaleX;
            element.y = source.y * scaleY;
        } else if (source.layoutMode == CanvasLayoutMode::Grid) {
            const float pointX = std::clamp(source.gridX / 128.0F, 0.0F, 1.0F) * winW;
            const float pointY = std::clamp(source.gridY / 128.0F, 0.0F, 1.0F) * winH;
            std::string anchor = lower(source.anchor);
            if (anchor == "auto") {
                const std::string horizontal = source.gridX <= 32.0F ? "left" : (source.gridX >= 96.0F ? "right" : "center");
                const std::string vertical = source.gridY <= 32.0F ? "top" : (source.gridY >= 96.0F ? "bottom" : "center");
                anchor = vertical + "_" + horizontal;
            }
            const bool right = anchor.ends_with("right");
            const bool centerX = anchor == "top_center" || anchor == "center" || anchor == "bottom_center";
            const bool bottom = anchor.starts_with("bottom");
            const bool centerY = anchor == "center_left" || anchor == "center" || anchor == "center_right";
            element.x = pointX - (right ? element.width : (centerX ? element.width * 0.5F : 0.0F));
            element.y = pointY - (bottom ? element.height : (centerY ? element.height * 0.5F : 0.0F));
        } else if (source.layoutMode == CanvasLayoutMode::Relative) {
            const auto target = std::find_if(resolved.begin(), resolved.end(), [&](const CanvasElement& candidate) {
                return lower(candidate.id) == lower(source.relativeToId);
            });
            if (target == resolved.end()) {
                // The parser reports missing/forward references. Keep a safe location if a program was constructed manually.
                element.x = source.x * scaleX;
                element.y = source.y * scaleY;
            } else {
                const auto side = lower(source.relativeSide);
                if (side == "left") {
                    element.x = target->x - element.width - element.gap;
                    element.y = target->y + (target->height - element.height) * 0.5F;
                } else if (side == "right") {
                    element.x = target->x + target->width + element.gap;
                    element.y = target->y + (target->height - element.height) * 0.5F;
                } else if (side == "above") {
                    element.x = target->x + (target->width - element.width) * 0.5F;
                    element.y = target->y - element.height - element.gap;
                } else {
                    element.x = target->x + (target->width - element.width) * 0.5F;
                    element.y = target->y + target->height + element.gap;
                }
            }
        }
        resolved.push_back(std::move(element));
    }
    return resolved;
}

Value Runtime::getGlobal(std::string_view rawName) const {
    const auto name = canonicalVariableName(rawName);
    if (!name.empty() && name.front() == '^') {
        const auto it = transient_.find(name.substr(1));
        return it == transient_.end() ? Value{} : it->second;
    }
    const auto it = globals_.find(name);
    return it == globals_.end() ? Value{} : it->second;
}

std::string Runtime::formatDisplayText(std::string_view rawText) const {
    // UI text is formatted every render, so global variables stay current
    // without rebuilding the canvas. This intentionally does not execute code
    // or mutate variables; it is a read-only display interpolation helper.
    std::string output;
    output.reserve(rawText.size());
    auto replacementFor = [&](std::string_view rawName) -> std::string {
        const std::string name = canonicalVariableName(rawName);
        if (name.empty() || name.front() == '$' || name.front() == '@' || name.front() == '_')
            return "{" + std::string(rawName) + "}";
        const Value value = getGlobal(name);
        if (value.isNull()) return "";
        return value.toString();
    };
    for (std::size_t i = 0; i < rawText.size();) {
        // Familiar Skript spelling: "HP: %{hp}%".
        if (rawText[i] == '%' && i + 1 < rawText.size() && rawText[i + 1] == '{') {
            const std::size_t closeBrace = rawText.find('}', i + 2);
            if (closeBrace != std::string_view::npos && closeBrace + 1 < rawText.size() && rawText[closeBrace + 1] == '%') {
                output += replacementFor(rawText.substr(i + 2, closeBrace - (i + 2)));
                i = closeBrace + 2;
                continue;
            }
        }
        // Convenient plain spelling: "Score: {score}". Rich-text color tags
        // use angle brackets and are therefore untouched by this branch.
        if (rawText[i] == '{') {
            const std::size_t closeBrace = rawText.find('}', i + 1);
            if (closeBrace != std::string_view::npos) {
                const auto rawName = rawText.substr(i + 1, closeBrace - (i + 1));
                const std::string name = canonicalVariableName(rawName);
                if (!name.empty() && (std::isalnum(static_cast<unsigned char>(name.front())) || name.front() == '^')) {
                    output += replacementFor(rawName);
                    i = closeBrace + 1;
                    continue;
                }
            }
        }
        output.push_back(rawText[i++]);
    }
    return output;
}

std::string Runtime::variableReport() const {
    std::vector<std::pair<std::string, Value>> sorted(globals_.begin(), globals_.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::ostringstream out;
    for (const auto& [name, value] : sorted) out << "{" << name << "} = " << value.toString() << '\n';
    std::vector<std::pair<std::string, Value>> volatileSorted(transient_.begin(), transient_.end());
    std::sort(volatileSorted.begin(), volatileSorted.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [name, value] : volatileSorted) out << "{^" << name << "} = " << value.toString() << "  [RAM only]\n";
    if (out.str().empty()) return "No global or RAM-only variables are set.";
    return out.str();
}

void Runtime::log(std::string message) {
    logs_.push_back(std::move(message));
    if (logs_.size() > maximumLogEntries_) logs_.erase(logs_.begin(), logs_.begin() + static_cast<std::ptrdiff_t>(logs_.size() - maximumLogEntries_));
}

bool Runtime::executeSimpleConsole(std::string_view rawCommand) {
    const std::string command = trim(rawCommand);
    const std::string lowered = lower(command);
    if (lowered == "/help" || lowered == "help") {
        // Keep this user-facing command catalogue close to the dispatcher above it.
        // The regression test checks every entry so a new command cannot silently be
        // omitted from help (or accidentally advertised after its implementation is gone).
        static constexpr std::array<std::string_view, 19> helpLines{{
            "Console commands (enter one command per line):",
            "  /help (or help)             Show this command reference.",
            "  /reload                     Reload .cx scripts as a new runtime session.",
            "  /vars                       List global and RAM-only variables.",
            "  /settings                   Show the current engine:: setting values.",
            "  /sprites                    List current sprites and entities.",
            "  /paths                      List named movement paths.",
            "  /lights                     List registered light sources.",
            "  /rooms                      List loaded rooms and identify the active room.",
            "  /textures                   List IDs discovered in assets/textures/.",
            "  /status                     Show script, handler, variable and tick counts.",
            "  /clear                      Clear the runtime log output.",
            "  /tick                       Run one logic tick (useful for manual testing).",
            "  /press KEY                  Simulate a key press, for example /press SPACE.",
            "  /click BUTTON_ID            Simulate a click on a canvas button by its ID.",
            "  /attach \"PATH\" X Y         Simulate dropping a file at window position X,Y.",
            "Type a CX statement directly, e.g. set {score} to 10 or broadcast \"Hello!\".",
            "Graphical terminal: Ctrl+Tab toggles it; PageUp/PageDown scroll the output.",
            "CLI only: /quit, quit or exit closes cx-cli; these are not CX script commands."
        }};
        for (const auto line : helpLines) log(std::string(line));
        log("Errors include their source location and a suggested correction.");
        return true;
    }
    if (lowered == "/vars") { log(variableReport()); return true; }
    if (lowered == "/rooms") {
        const auto loaded = rooms_.loadedRoomNames();
        log("Active room: " + (rooms_.activeRoomName().empty() ? std::string("(none)") : rooms_.activeRoomName()));
        if (loaded.empty()) log("No rooms are loaded. Use `load room \"name\"` or `enter room \"name\" at {entrance}`.");
        else for (const auto& name : loaded) {
            const auto* room = rooms_.loadedRoom(name);
            log(std::string(name == rooms_.activeRoomName() ? "* " : "  ") + name + (room ? " (" + std::to_string(room->width) + " x " + std::to_string(room->height) + ")" : ""));
        }
        return true;
    }
    if (lowered == "/textures") {
        const auto& textures = rooms_.textures().all();
        if (textures.empty()) log("No image textures found in assets/textures/.");
        else {
            std::string line = "Texture IDs: ";
            for (std::size_t i = 0; i < textures.size(); ++i) {
                if (i) line += ", ";
                line += "{$" + textures[i].id + "}";
            }
            log(std::move(line));
        }
        return true;
    }
    if (lowered == "/settings") {
        const auto& s = settings_;
        log("Engine settings (current runtime values):");
        log("  window_title = \"" + s.windowTitle + "\"");
        log("  window_width = " + std::to_string(s.windowWidth) + " | window_height = " + std::to_string(s.windowHeight));
        log("  resolution_mode = \"" + s.resolutionMode + "\" | ui_reference_width = " + std::to_string(s.uiReferenceWidth) + " | ui_reference_height = " + std::to_string(s.uiReferenceHeight));
        log("  vsync = " + std::string(s.vsync ? "true" : "false") + " | target_fps = " + std::to_string(s.targetFps) + " | tick_rate = " + std::to_string(s.tickRate));
        log("  console_enabled = " + std::string(s.consoleEnabled ? "true" : "false") + " | show_debug_overlay = " + std::string(s.showDebugOverlay ? "true" : "false"));
        log("  texture_filter = \"" + s.textureFilter + "\" | pixel_perfect = " + std::string(s.pixelPerfect ? "true" : "false"));
        log("  lighting_enabled = " + std::string(s.lightingEnabled ? "true" : "false") + " | ambient_light = " + std::to_string(s.ambientLight) + " | lighting_quality = " + std::to_string(s.lightingQuality) + " | screen_flash = " + std::to_string(s.screenFlash));
        log("  ui_scale = " + std::to_string(s.uiScale) + " | world_view_height_tiles = " + std::to_string(s.worldViewHeightTiles));
        log("  camera_follow = \"" + s.cameraFollow + "\" | font_path = \"" + (s.fontPath.empty() ? std::string("(auto)") : s.fontPath) + "\"");
        return true;
    }
    if (lowered == "/sprites") {
        if (sprites_.empty()) { log("No sprites or entities currently exist."); return true; }
        log("Sprites/entities:");
        for (const auto& [id, sprite] : sprites_) {
            log("  " + id + (sprite.entity ? " [entity]" : " [sprite]") + " texture={$" + sprite.textureId + "} pos=(" + std::to_string(sprite.x) + ", " + std::to_string(sprite.y) + ") size=(" + std::to_string(sprite.width) + ", " + std::to_string(sprite.height) + ")" + (sprite.visible ? " visible" : " hidden") + (sprite.projectile ? " projectile" : ""));
        }
        return true;
    }
    if (lowered == "/paths") {
        if (paths_.empty()) { log("No named paths are registered."); return true; }
        log("Named paths:");
        for (const auto& [id, path] : paths_) {
            std::string line = "  " + id + " (" + std::to_string(path.waypoints.size()) + " waypoints): ";
            for (std::size_t i = 0; i < path.waypoints.size(); ++i) {
                if (i) line += " -> ";
                line += "(" + std::to_string(path.waypoints[i].x) + "," + std::to_string(path.waypoints[i].y) + ")";
            }
            log(std::move(line));
        }
        return true;
    }
    if (lowered == "/lights") {
        if (lights_.empty()) { log("No light sources are registered."); return true; }
        log("Light sources:");
        for (const auto& [id, light] : lights_) {
            log("  " + id + " room=\"" + (light.roomName.empty() ? std::string("(global)") : light.roomName) + "\" tile=(" + std::to_string(light.xTiles) + "," + std::to_string(light.yTiles) + ") radius=" + std::to_string(light.radius) + " intensity=" + std::to_string(light.intensity) + " direction=" + std::to_string(light.directionDegrees) + "° beam=" + std::to_string(light.beamAngleDegrees) + "°");
        }
        return true;
    }
    if (lowered == "/clear") { logs_.clear(); return true; }
    if (lowered == "/status") {
        log("Scripts: " + std::to_string(program_.filesLoaded) + " | handlers: " + std::to_string(program_.handlers.size()) +
            " | global variables: " + std::to_string(globals_.size()) + " | ticks: " + std::to_string(tickCount_));
        return true;
    }
    if (lowered == "/tick") { tick(); return true; }
    if (lowered.starts_with("/press ")) { pressKey(trim(std::string_view(command).substr(7))); return true; }
    if (lowered.starts_with("/click ")) { clickButton(trim(std::string_view(command).substr(7))); return true; }
    if (lowered.starts_with("/attach ")) {
        std::string rest = trim(std::string_view(command).substr(8));
        const auto firstSpace = rest.find_last_of(' ');
        const auto secondSpace = firstSpace == std::string::npos ? std::string::npos : rest.find_last_of(' ', firstSpace - 1);
        if (firstSpace == std::string::npos || secondSpace == std::string::npos) {
            log("Usage: /attach \"/path/to/file\" mouse_x mouse_y (simulates dropping a file at that window position).");
            return true;
        }
        try {
            const float x = std::stof(rest.substr(secondSpace + 1, firstSpace - secondSpace - 1));
            const float y = std::stof(rest.substr(firstSpace + 1));
            attachFile(unquote(rest.substr(0, secondSpace)), x, y);
        } catch (...) { log("Usage: /attach \"/path/to/file\" mouse_x mouse_y"); }
        return true;
    }
    if (lowered == "/reload") {
        const bool loaded = loadScripts(true);
        if (loaded) log("Reloaded scripts successfully.");
        else log("Reload failed. Fix the errors printed above, then run /reload again.");
        return true;
    }
    return false;
}

void Runtime::executeConsole(std::string command) {
    diagnostics_.clear();
    if (executeSimpleConsole(command)) return;
    executeInline(std::move(command));
}

void Runtime::executeInline(std::string command) {
    ScriptParser parser;
    auto inlineProgram = parser.parseText(command, "<console>");
    diagnostics_ = inlineProgram.diagnostics;
    for (auto& diagnostic : diagnostics_) diagnostic.sourceLineText = command;
    if (hasErrors()) return;
    if (!inlineProgram.handlers.empty() || !inlineProgram.canvases.empty()) {
        appendDiagnostic(diagnostics_, {false, "<console>", 0, "The terminal accepts commands, not event or canvas declarations.",
            "Put `on load:`, `on tick:`, and `canvas \"Name\":` declarations in a .cx file, then run /reload.", 1, 1});
        return;
    }
    ExecutionContext context;
    context.allowSuspend = false; // The parsed terminal command owns a temporary AST.
    executeStatements(inlineProgram.bootstrap, context);
}

} // namespace cx
