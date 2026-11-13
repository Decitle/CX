#pragma once

#include "cx/script.hpp"
#include "cx/rooms.hpp"
#include "cx/value.hpp"
#include "cx/audio.hpp"
#include "cx/collision.hpp"
#include "cx/effects.hpp"

#include <cstdint>
#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cx {

// EngineSettings contains the defaults the host window/renderer reads. Script
// assignments to `engine::...` are validated by Runtime::applySetting before they
// update these fields.
struct EngineSettings {
    std::string windowTitle{"CX Engine | .cx Runtime"};
    unsigned int windowWidth{1280};
    unsigned int windowHeight{720};
    bool vsync{true};
    unsigned int targetFps{0};
    unsigned int tickRate{60};
    bool consoleEnabled{true};
    bool showDebugOverlay{false};
    std::string textureFilter{"nearest"}; // Pixel-art-safe point filtering by default.
    bool pixelPerfect{true};
    bool lightingEnabled{true};
    float ambientLight{0.72F};
    float screenFlash{0.0F};
    float uiScale{1.0F};
    std::string resolutionMode{"responsive"}; // responsive window/UI layout or fixed configured window size.
    unsigned int uiReferenceWidth{1280};
    unsigned int uiReferenceHeight{720};
    float worldViewHeightTiles{22.5F}; // Vertical world coverage measured in the active room tile units.
    std::string cameraFollow{"none"}; // Entity name or "none"; world camera follows the entity in physical world units.
    unsigned int lightingQuality{2};
    std::string fontPath{};
};

struct LightSource {
    std::string name;
    std::string roomName; // Empty means a global/world-space light.
    float xTiles{0.0F};
    float yTiles{0.0F};
    float radius{180.0F};
    float intensity{0.85F};
    float directionDegrees{0.0F};
    float beamAngleDegrees{55.0F};
    std::uint8_t red{255};
    std::uint8_t green{185};
    std::uint8_t blue{115};
};

// A named clip selects a contiguous inclusive range of a sprite sheet. A row of
// -1 follows the entity's current eight-way facing row; fixed rows are zero-based.
// Timing is expressed in animation frames per simulation second, not render frames.
struct AnimationClip {
    int firstFrame{0};
    int lastFrame{0};
    int row{0};
    double framesPerSecond{8.0};
    bool followsFacing{false};
    bool looping{false};
};

// Lightweight screen/world sprite used by the first entity API. Coordinates are pixels.
struct SpriteObject {
    std::string name;
    // Monotonic runtime identity distinguishes two different objects that reuse the
    // same script-visible name. Deferred events must never leak onto a replacement.
    std::uint64_t runtimeInstanceId{0};
    std::string textureId;
    float x{0.0F};
    float y{0.0F};
    float width{32.0F};
    float height{32.0F};
    float rotationDegrees{0.0F};
    bool visible{true};
    bool entity{false};
    bool projectile{false};
    bool projectileAnchor{false};
    bool projectileCanHit{true};
    bool projectileIgnoresWalls{false};
    bool orbiting{false};
    std::string orbitParent;
    float orbitRadiusPixels{0.0F};
    float orbitAngleRadians{0.0F};
    float orbitSpeedRadians{0.0F};
    float orbitElapsedSeconds{0.0F};
    float orbitReleaseAfterSeconds{1.2F};
    bool collisionEnabled{true};
    // Movement blockers are opt-in. Triggers, focus targets and ordinary entities
    // remain non-solid unless an author explicitly enables this flag.
    bool blocksMovement{false};
    TweenCollisionResponse tweenCollisionResponse{TweenCollisionResponse::Ignore};
    // Interaction is an authored gameplay relationship, independent from physical
    // collision response. Invisible/decorative objects can still be interactive.
    bool interactable{false};
    float interactionRangePixels{0.0F}; // Actor-side range; zero disables focus selection.
    // Classifications are author-defined metadata. Collision channels are compact
    // bitmasks so high-frequency filtering avoids string work in the update loop.
    // Classification strings are interned once per runtime. Individual objects keep
    // compact IDs so large projectile groups do not each allocate kind/team strings.
    std::uint32_t kindId{0};
    std::uint32_t teamId{0};
    std::vector<std::uint32_t> tagIds;
    std::uint32_t collisionLayer{CollisionActors};
    std::uint32_t collisionMask{AllCollisionChannels};
    std::uint32_t projectileCollisionMask{AllCollisionChannels};
    bool avoidSolidTiles{false};
    float hitboxWidth{0.0F};
    float hitboxHeight{0.0F};
    float hitboxOffsetX{0.0F};
    float hitboxOffsetY{0.0F};
    float pathSpeedTilesPerSecond{1.25F};
    float moveSpeedPixelsPerSecond{150.0F};
    std::string pathName;
    std::size_t pathWaypoint{0};
    std::vector<TilePoint> navigationTiles;
    std::size_t navigationWaypoint{0};
    TilePoint navigationGoal{-1, -1};
    float projectileVx{0.0F};
    float projectileVy{0.0F};
    float projectileSpeedTilesPerSecond{5.0F};
    int projectileDamage{1};
    float projectileAgeSeconds{0.0F};
    float projectileLifetimeSeconds{5.0F};
    std::string projectileOwner;
    std::string projectileTarget;
    // Projectiles bind to the target instance present at launch, not a future object
    // that happens to reuse the same name after a delete/respawn cycle.
    std::uint64_t projectileTargetInstanceId{0};
    int health{1};
    int maxHealth{1};
    int facingRow{0}; // Eight-way sheet: down, down-left, left, up-left, up, up-right, right, down-right.
    int frameWidth{0};
    int frameHeight{0};
    int frameColumns{1};
    int frameRows{1};
    int animationFrame{0};
    int animationTicks{0};
    std::map<std::string, AnimationClip> animationClips;
    std::string currentAnimation;
    int animationRow{0};
    double animationElapsedTicks{0.0};
    std::uint64_t animationPlaybackGeneration{0};
    bool animationPlaying{false};
    bool moving{false};
};

// All physical queries share the same effective hitbox rectangle. When no explicit
// hitbox exists, the sprite rectangle is used; offsets are relative to sprite origin.
[[nodiscard]] inline CollisionAabb spriteCollisionBoundsAt(const SpriteObject& object,
                                                            float x, float y) noexcept {
    const float width = object.hitboxWidth > 0.0F ? object.hitboxWidth : object.width;
    const float height = object.hitboxHeight > 0.0F ? object.hitboxHeight : object.height;
    const float offsetX = object.hitboxWidth > 0.0F ? object.hitboxOffsetX : 0.0F;
    const float offsetY = object.hitboxHeight > 0.0F ? object.hitboxOffsetY : 0.0F;
    return {x + offsetX, y + offsetY, x + offsetX + width, y + offsetY + height};
}
[[nodiscard]] inline CollisionAabb spriteCollisionBounds(const SpriteObject& object) noexcept {
    return spriteCollisionBoundsAt(object, object.x, object.y);
}

// Proximity and interaction focus share one effective hitbox-center convention.
// If no explicit hitbox is configured, the sprite rectangle is used.
[[nodiscard]] inline std::array<double, 2> spriteHitboxCenter(const SpriteObject& object) noexcept {
    const double width = object.hitboxWidth > 0.0F ? object.hitboxWidth : object.width;
    const double height = object.hitboxHeight > 0.0F ? object.hitboxHeight : object.height;
    const double offsetX = object.hitboxWidth > 0.0F ? object.hitboxOffsetX : 0.0F;
    const double offsetY = object.hitboxHeight > 0.0F ? object.hitboxOffsetY : 0.0F;
    return {static_cast<double>(object.x) + offsetX + width * 0.5,
            static_cast<double>(object.y) + offsetY + height * 0.5};
}

// A named path is a sequence of room-grid waypoints. Entity movement code consumes
// these points; rendering does not own or modify the path definition.
struct EntityPath {
    std::string name;
    std::vector<TilePoint> waypoints;
};



// Runtime owns the live game state and the script execution rules. The SFML frontend
// collects input, calls tick(), and draws the state; the CLI can use this same class
// without opening a window. This separation is a major reason most engine behavior
// can be tested on a machine without graphics libraries installed.
class Runtime {
public:
    // Creates the in-memory runtime for one project folder. Construction records the
    // path and initializes state; use loadScripts() to parse and start the project.
    explicit Runtime(std::filesystem::path projectRoot = ".");

    // Loads every .cx file under scripts/. A separate candidate runtime is parsed and
    // started first. Syntax/bootstrap/on-load/output-commit errors leave the current
    // game session intact; only a successful candidate replaces it. With resetGlobals
    // true (the default), globals and settings are rebuilt from config.json plus the
    // last saved persistent variables. With false, live globals/settings are carried
    // forward, including a pending persistent-save dirty flag. Both modes start a new
    // runtime session: transient variables, old waits/periodic deadlines, entities,
    // rooms, effects, canvases, audio requests and held-key state are rebuilt by startup.
    [[nodiscard]] bool loadScripts(bool resetGlobals = true);

    // Runs top-level script statements (commands outside an event/function block).
    // Normally called as part of loadScripts(), not every frame.
    void executeBootstrap();
    // Fires all program-level `on load:` handlers after bootstrap has completed.
    void fireLoad();
    // Advances exactly one simulation step. This runs game logic; it does not draw.
    // The host decides when to call it, usually at settings().tickRate updates/second.
    void tick();
    // Sends one key-press event to matching `on press key` handlers. This is an
    // edge/event notification; use setKeyDown()/keyDown() for held-key movement.
    void pressKey(std::string key);
    // Updates the set of keys currently held down by the host window.
    void setKeyDown(std::string key, bool down);
    // Clears every held key. A graphical host calls this when the OS sends a focus-lost
    // event, because the matching key-release events may not arrive after Alt-Tab.
    void clearHeldKeys() noexcept;
    // Returns whether a canonicalized key name is currently in that held-key set.
    [[nodiscard]] bool keyDown(std::string_view key) const;
    // Fires matching script button-click handlers for a canvas button identifier.
    void clickButton(std::string buttonId);
    // Fires the project file-attach event for an existing regular file and screen position.
    // A relative file path is resolved against the project folder.
    void attachFile(std::filesystem::path filePath, float mouseX, float mouseY);
    // Processes a developer-terminal command such as /reload, /help, or an inline action.
    void executeConsole(std::string command);
    // Parses and executes a small script command supplied by a developer tool/console.
    // This is not a replacement for loading the complete project with loadScripts().
    void executeInline(std::string command);

    // Read-only access to parsed program definitions and the settings consumed by the host.
    [[nodiscard]] const ScriptProgram& program() const noexcept { return program_; }
    [[nodiscard]] const EngineSettings& settings() const noexcept { return settings_; }
    [[nodiscard]] const std::map<std::string, LightSource>& lights() const noexcept { return lights_; }
    [[nodiscard]] const std::map<std::string, BeamEffect>& beams() const noexcept { return beams_; }
    [[nodiscard]] std::size_t beamsRevision() const noexcept { return beamsRevision_; }
    [[nodiscard]] const std::map<std::string, SpriteObject>& sprites() const noexcept { return sprites_; }
    // Returns the currently selected interactable for an actor, or an empty string.
    // Identity is checked so a deleted/replaced target is never exposed as current.
    [[nodiscard]] std::string focusedEntity(std::string_view actorName) const;
    [[nodiscard]] const std::map<std::string, EntityPath>& paths() const noexcept { return paths_; }
    [[nodiscard]] std::size_t lightsRevision() const noexcept { return lightsRevision_; }
    [[nodiscard]] const AudioSystem& audio() const noexcept { return audio_; }
    [[nodiscard]] AudioSystem& audio() noexcept { return audio_; }
    [[nodiscard]] const RoomManager& rooms() const noexcept { return rooms_; }
    [[nodiscard]] RoomManager& rooms() noexcept { return rooms_; }
    [[nodiscard]] const std::filesystem::path& projectRoot() const noexcept { return projectRoot_; }
    [[nodiscard]] const std::vector<std::string>& logs() const noexcept { return logs_; }
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept { return diagnostics_; }
    [[nodiscard]] const std::unordered_set<std::string>& openCanvases() const noexcept { return openCanvases_; }
    [[nodiscard]] const CanvasDefinition* findCanvas(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<CanvasElement> resolveCanvasElements(const CanvasDefinition& canvas, float viewportWidth, float viewportHeight) const;
    void setViewportSize(float width, float height) noexcept;
    // Reads one persistent global variable without needing an event ExecutionContext.
    // This is mainly for hosts/tests; scripts normally read variables through expressions.
    [[nodiscard]] Value getGlobal(std::string_view name) const;
    [[nodiscard]] std::string variableReport() const;
    // Resolves persistent/global variable placeholders for screen text, e.g. "HP: %{hp}%".
    [[nodiscard]] std::string formatDisplayText(std::string_view text) const;
    // True when the current diagnostic collection contains at least one error.
    [[nodiscard]] bool hasErrors() const noexcept;
    // Removes only developer log text; diagnostics are kept separately until cleared.
    void clearLogs() { logs_.clear(); }
    void clearDiagnostics() { diagnostics_.clear(); }
    // Adds a developer-facing runtime log entry, subject to the runtime's history cap.
    void log(std::string message);

private:
    using VariableMap = std::unordered_map<std::string, Value>;
    struct ListEntry { std::string index; Value value; };
    struct ExecutionFrame {
        const std::vector<Statement>* statements{nullptr};
        std::size_t nextIndex{0};
        std::size_t repetitions{1};
        std::size_t iteration{1};
        bool loopFrame{false};
        bool whileLoop{false};
        const Statement* owner{nullptr};
        bool listLoop{false};
        std::vector<ListEntry> listEntries;
    };
    struct ExecutionContext {
        VariableMap locals;
        std::vector<ExecutionFrame> frames;
        std::vector<std::size_t> loopNumbers;
        std::vector<Value> loopValues;
        std::vector<std::string> loopIndexes;
        bool stopped{false};
        bool suspended{false};
        bool allowSuspend{true};
        bool returned{false};
        Value returnValue{};
        std::chrono::steady_clock::time_point wakeAt{};
        // A suspended `wait` belongs to the active room context in which it was
        // reached. Scene changes invalidate old continuations before they can act
        // on a different room; the epoch is refreshed by each subsequent `wait`.
        std::uint64_t roomContextRevision{0};
    };

    // Trigger contacts are keyed by runtime identity rather than script name.
    // Replacing an entity with another object of the same name therefore produces
    // a real exit for the old instance and an enter for the new one.
    struct TriggerContact {
        std::string actorName;
        std::string triggerName;
    };
    using TriggerPairId = std::pair<std::uint64_t, std::uint64_t>;

    // One actor may focus one target. Runtime IDs, not names, own the relationship.
    struct InteractionFocus {
        std::string actorName;
        std::string targetName;
        std::uint64_t actorInstanceId{0};
        std::uint64_t targetInstanceId{0};
    };

    // Named deterministic random streams isolate unrelated game systems. The
    // stream state is runtime-local and is reconstructed from script seed commands
    // during successful startup/reload; it is not an implicit process-global RNG.
    struct RandomStreamState {
        std::uint64_t state{0};
    };

    // A tween owns one entity instance for a fixed number of simulation ticks.
    // It remains a scripted position primitive; an optional collision response can
    // now stop or slide it against static tiles and explicitly solid entities.
    struct EntityTweenCompletion {
        std::string name;
        std::uint64_t instanceId{0};
        std::uint64_t roomRevision{0};
        float finalX{0.0F};
        float finalY{0.0F};
    };
    struct EntityTween {
        std::uint64_t entityInstanceId{0};
        float startX{0.0F};
        float startY{0.0F};
        float targetX{0.0F};
        float targetY{0.0F};
        float arcHeightPixels{0.0F};
        std::uint64_t durationTicks{1};
        std::uint64_t elapsedTicks{0};
        std::uint64_t roomContextRevision{0};
        std::string easing{"linear"};
    };
    struct AnimationCompletion {
        std::string entityName;
        std::string animationName;
        std::uint64_t entityInstanceId{0};
        std::uint64_t playbackGeneration{0};
        std::uint64_t roomContextRevision{0};
        int finalFrame{0};
        int finalRow{0};
    };

    std::filesystem::path projectRoot_;
    RoomManager rooms_;
    ScriptProgram program_;
    VariableMap globals_;
    VariableMap transient_; // {^name}: persistent only for this process, shared across events.
    AudioSystem audio_;
    bool variableStorageDirty_{false};
    std::unordered_set<std::string> openCanvases_;
    std::vector<std::string> logs_;
    std::vector<Diagnostic> diagnostics_;
    std::map<std::string, LightSource> lights_;
    std::map<std::string, BeamEffect> beams_;
    std::map<std::string, SpriteObject> sprites_;
    std::map<std::string, EntityPath> paths_;
    std::map<std::string, EntityTween> entityTweens_;
    // Retained scratch storage avoids allocating a completion vector on every tick.
    std::vector<EntityTweenCompletion> pendingTweenCompletions_;
    // Reused each update so finished clips can be dispatched after sprite iteration.
    // Event handlers may safely delete/recreate entities without invalidating the loop.
    std::vector<AnimationCompletion> pendingAnimationCompletions_;
    std::map<TriggerPairId, TriggerContact> activeTriggerPairs_;
    std::map<std::uint64_t, InteractionFocus> activeInteractionFocus_;
    std::uint64_t interactionFocusRoomContextRevision_{0};
    std::map<std::string, RandomStreamState> randomStreams_;
    std::unordered_map<std::string, std::uint32_t> entityLabelIds_;
    std::uint32_t nextEntityLabelId_{1};
    std::uint64_t projectileCounter_{0};
    std::uint64_t spriteInstanceCounter_{0};
    // Monotonic epoch for the active scene context. Deferred projectile impacts
    // are valid only for the context in which their candidates were gathered.
    std::uint64_t roomContextRevision_{0};
    std::uint64_t triggerPairsRoomContextRevision_{0};
    std::size_t activeProjectileCount_{0};
    bool projectileLimitDiagnosticIssued_{false};
    std::size_t projectileLimitRequiredCapacity_{1};
    static constexpr std::size_t maximumActiveProjectiles_{4096};
    std::unordered_set<std::string> keysDown_;
    std::size_t lightsRevision_{0};
    std::size_t beamsRevision_{0};
    EngineSettings settings_;
    std::size_t tickCount_{0};
    // Bounds stored message records; individual strings may contain embedded newlines.
    std::size_t maximumLogEntries_{500};
    float viewportWidth_{1280.0F};
    float viewportHeight_{720.0F};
    std::deque<ExecutionContext> scheduledExecutions_;
    std::vector<std::chrono::steady_clock::time_point> periodicNextRun_;
    // Script startup is staged in a separate Runtime. Files created by startup
    // commands are written to a temporary directory and only committed after
    // bootstrap and all on-load handlers finish without runtime errors.
    struct StagedOutputFile {
        std::filesystem::path temporaryPath;
        std::filesystem::path destinationPath;
    };
    bool stagingLoad_{false};
    std::filesystem::path stagedOutputDirectory_;
    std::vector<StagedOutputFile> stagedOutputFiles_;
    std::size_t maximumInstructionsPerResume_{100000};
    std::size_t maximumCountedLoopIterations_{1000000};
    std::size_t functionCallDepth_{0};

    void fire(EventType type, std::string_view selector = {});
    void fireTriggerEvent(EventType type, const TriggerContact& contact);
    void fireInteractionFocusEvent(EventType type, const InteractionFocus& focus);
    void updateInteractionFocus();
    [[nodiscard]] std::optional<InteractionFocus> selectInteractionTarget(const SpriteObject& actor) const;
    void fireTweenComplete(std::string_view entityName, std::uint64_t instanceId,
                           std::uint64_t roomRevision, float finalX, float finalY);
    void updateEntityTweens();
    struct MovementBlockerSnapshot {
        std::uint64_t instanceId{0};
        std::uint32_t collisionLayer{0};
        std::uint32_t collisionMask{AllCollisionChannels};
        CollisionAabb bounds;
    };
    [[nodiscard]] bool movementSegmentBlocked(
        const SpriteObject& mover, float fromX, float fromY, float toX, float toY,
        const std::vector<MovementBlockerSnapshot>& blockers) const;
    void fireAnimationComplete(const AnimationCompletion& completion);
    void fireFileAttach(const std::filesystem::path& filePath, float mouseX, float mouseY);
    void updateEntities();
    void updateTriggerEvents();
    void updateProjectiles(float deltaSeconds, float tileSizePixels);
    [[nodiscard]] std::uint32_t internEntityLabel(std::string_view label);
    [[nodiscard]] std::uint32_t findEntityLabel(std::string_view label) const;
    void executeProjectileStatement(const Statement& statement);
    void reportProjectileLimitOnce(const Statement& statement, std::size_t requiredCapacity, bool wholePattern);
    [[nodiscard]] bool spritesOverlap(std::string_view left, std::string_view right) const;
    [[nodiscard]] std::vector<TilePoint> findTilePath(TilePoint start, TilePoint goal) const;
    void fireProjectileHit(std::string_view targetEntity, std::string_view projectileName, int damage);
    // Shared construction keeps single shots and pattern shots consistent. Callers
    // validate the named entities, texture, speed and damage before reaching here.
    void spawnProjectileAtAngle(const SpriteObject& owner, std::string_view targetEntity,
                                std::uint64_t targetInstanceId, std::string_view textureId, float speedTilesPerSecond,
                                int damage, float angleRadians);
    [[nodiscard]] bool positionHitsSolidTile(const SpriteObject& object, float x, float y) const;
    void loadPersistentVariables();
    void savePersistentVariables();
    [[nodiscard]] bool makeStagedOutputPath(const std::filesystem::path& destination,
                                            std::filesystem::path& temporaryPath,
                                            std::string& error);
    [[nodiscard]] bool commitStagedOutputs(std::string& error);
    void discardStagedOutputs() noexcept;
    [[nodiscard]] std::string resolveVariableName(std::string_view rawName, const ExecutionContext& context) const;
    void executeStatements(const std::vector<Statement>& statements, ExecutionContext& context);
    void runExecution(ExecutionContext& context);
    void resumeScheduledExecutions();
    [[nodiscard]] Value evaluateValue(std::string_view expression, const ExecutionContext& context, std::string& error);
    [[nodiscard]] bool evaluateCondition(std::string_view expression, const ExecutionContext& context, bool& result, std::string& error);
    [[nodiscard]] Value getVariable(std::string_view name, const ExecutionContext& context) const;
    [[nodiscard]] std::vector<ListEntry> listEntries(std::string_view wildcardName, const ExecutionContext& context) const;
    void clearList(std::string_view wildcardName, ExecutionContext& context, const Statement* origin);
    void appendListValue(std::string_view wildcardName, Value value, ExecutionContext& context, const Statement* origin);
    void setVariable(std::string_view name, Value value, ExecutionContext& context, const Statement* origin = nullptr);
    [[nodiscard]] bool applySetting(std::string_view name, const Value& value, std::string& error);
    [[nodiscard]] bool executeSimpleConsole(std::string_view command);
    void resetRuntimeState(bool resetGlobals);
    void loadConfig();
    void runtimeError(const Statement& statement, std::string message, std::string suggestion = {});
};

} // namespace cx
