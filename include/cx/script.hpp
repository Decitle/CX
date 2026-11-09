#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cx {

// EventType describes *when* a handler runs. Event handlers are parsed into data
// first; Runtime decides when to execute them later.
enum class EventType {
    Load, Tick, Periodic, KeyPress, ButtonClick, FileAttach, ProjectileHit,
    TriggerEnter, TriggerExit, FocusEnter, FocusExit, TweenComplete, AnimationComplete
};
enum class StatementType {
    Set, SeedRandomStream, SetEntrance, LoadRoom, UnloadRoom, ReloadRoom, EnterRoom, DeleteVariable, PlaceTile, ClearTile,
    CreateLight, SetLight, RemoveLight, CreateBeam, SetBeam, RemoveBeam, CreateSprite, SetSprite, MoveSprite, DeleteSprite, PlaceOverlayTile, ClearOverlayTile,
    CreatePath, SetEntityPath, SetEntityProperty, DefineAnimation, PlayAnimation, StopAnimation,
    SpawnProjectile, SpawnProjectilePattern, MoveEntityInput, TweenEntity, CancelEntityTween, Add, Subtract, Toggle, Broadcast,
    If, LoopTimes, LoopList, While, Wait, ExitLoop, ContinueLoop, Return, OpenCanvas, CloseCanvas, CreateWave, AddWaveLayer, SetWaveEnvelope,
    NormalizeWave, SaveWave, VisualizeWave, PlaySound, Stop
};
enum class CanvasElementType { Panel, Text, Button, ProgressBar };
enum class CanvasLayoutMode { Absolute, Grid, Relative };

// Statement is the parser's small instruction record (similar to one node in an
// abstract syntax tree). `type` selects the command; the remaining fields hold
// its operands and, for blocks such as if/loop, nested statements. Source fields
// let runtime errors point back to the exact line the author wrote.
struct Statement {
    StatementType type{StatementType::Broadcast};
    std::string target;
    std::string expression;
    std::string property;
    // Additional operands used by the multi-part tween command.
    std::string secondaryExpression;
    std::string detailExpression;
    std::vector<Statement> body;
    std::vector<Statement> elseBody;
    int line{0};
    std::string source;
    std::string sourceLineText;
};

// A function stores its parameter names and parsed body. It does not execute while
// the file is being parsed; calls are evaluated by Runtime when a script reaches them.
struct ScriptFunction {
    std::string name;
    std::vector<std::string> parameters;
    std::vector<Statement> body;
    std::filesystem::path file;
    int line{0};
};

// A handler couples an event (for example `on tick:`) with the code to run for it.
// Periodic handlers also keep their requested interval and unit here.
struct EventHandler {
    EventType type{EventType::Load};
    std::string selector;
    // Paired handlers use both selectors: actor + trigger, or entity + animation.
    // Other event types leave the secondary selector empty.
    std::string secondarySelector;
    std::vector<Statement> body;
    std::filesystem::path file;
    int line{0};
    double intervalValue{0.0};
    std::string intervalUnit;
};

struct CanvasElement {
    CanvasElementType type{CanvasElementType::Panel};
    std::string id;
    std::string text;
    float x{0.0F};
    float y{0.0F};
    float width{0.0F};
    float height{0.0F};
    unsigned char red{40};
    unsigned char green{40};
    unsigned char blue{40};
    unsigned char alpha{220};
    unsigned int fontSize{20};
    std::string valueVariable;
    std::string maxVariable;
    unsigned char backgroundRed{20};
    unsigned char backgroundGreen{24};
    unsigned char backgroundBlue{32};
    unsigned char backgroundAlpha{240};
    int line{0};
    CanvasLayoutMode layoutMode{CanvasLayoutMode::Absolute};
    float gridX{0.0F};
    float gridY{0.0F};
    std::string relativeToId;
    std::string relativeSide;
    std::string anchor{"top_left"};
    float gap{8.0F};
};

struct CanvasDefinition {
    std::string name;
    std::vector<CanvasElement> elements;
    std::filesystem::path file;
    int line{0};
};

struct Diagnostic {
    bool warning{false};
    std::filesystem::path file;
    int line{0};
    std::string message;
    std::string suggestion;
    int column{1};
    int length{1};
    std::string sourceLineText;

    Diagnostic() = default;
    Diagnostic(bool isWarning, std::filesystem::path sourceFile, int sourceLine,
               std::string text, std::string fix = {}, int sourceColumn = 1,
               int sourceLength = 1, std::string sourceText = {})
        : warning(isWarning), file(std::move(sourceFile)), line(sourceLine),
          message(std::move(text)), suggestion(std::move(fix)), column(sourceColumn),
          length(sourceLength), sourceLineText(std::move(sourceText)) {}
};

// ScriptProgram is the complete parsed result from one project: startup statements,
// event handlers, canvas declarations, named functions, and diagnostics. Keeping
// this separate from Runtime lets the engine reject a bad script before activating it.
struct ScriptProgram {
    std::vector<Statement> bootstrap;
    std::vector<EventHandler> handlers;
    std::vector<CanvasDefinition> canvases;
    std::map<std::string, ScriptFunction> functions;
    std::vector<Diagnostic> diagnostics;
    std::size_t filesLoaded{0};
};

// ScriptParser converts source text/files into ScriptProgram data. It reports errors
// instead of trying to launch a partially parsed program.
class ScriptParser {
public:
    [[nodiscard]] ScriptProgram parseDirectory(const std::filesystem::path& directory) const;
    [[nodiscard]] ScriptProgram parseFile(const std::filesystem::path& file) const;
    [[nodiscard]] ScriptProgram parseText(const std::string& text,
                                          const std::filesystem::path& sourceName = "<memory>") const;
};

[[nodiscard]] std::string canonicalVariableName(std::string_view raw);
[[nodiscard]] std::string canonicalEventKey(std::string_view raw);

} // namespace cx
