# Learn C++ by reading the CX Engine source

This guide explains the C++ side of CX Engine without assuming you already know C++. You can use CX Script without reading any C++; this is a map for when you want to understand how the engine itself works or safely experiment with it.

The goal is not to memorize the whole codebase. Learn the small chain that turns one line of `.cx` into a change on screen, then follow one feature at a time.

## 1. Build a safe starting point

1. Keep an untouched copy of the source ZIP. Make experiments in a separate extracted folder.
2. Read `README.md` first. For the dependency-free engine/CLI, configure with `-DCX_BUILD_GRAPHICAL_RUNTIME=OFF`; this keeps the first build small and avoids needing a graphics library.
3. Build the tests and run `ctest --test-dir build --output-on-failure`.
4. Keep the last passing build. When you change behavior, add or update a test before deciding that the change is finished.

The existing comments aim to explain purpose, data flow, assumptions, ownership and non-obvious decisions. They do not narrate every punctuation mark. A comment is most useful when it explains *why* code exists or what later code relies on.

## 2. The whole engine in one picture

A script file is not executed directly as text. It passes through a few stages:

```text
Your .cx files
    |
    v
ScriptParser::parseDirectory()
    |  reads files, removes comments, normalizes indentation,
    |  and creates Statement / EventHandler records
    v
ScriptProgram
    |  data describing what the script asked for
    v
Runtime::loadScripts()
    |  prepares a separate candidate runtime, runs startup,
    |  and commits it only if startup succeeds
    v
Runtime state
    |  entities, rooms, variables, lights, beams, waits, etc.
    v
Runtime::pressKey() / Runtime::tick()
    |  picks the handlers that should run now
    v
Runtime::executeStatements() -> Runtime::runExecution()
    |  interprets the prepared instructions
    v
Runtime::evaluateValue() / Runtime::evaluateCondition()
    |  calculate values and decide true/false conditions
    v
Updated runtime state
    |
    v
The SFML host reads that state and draws it
```

The most important separation is this: **the runtime decides what the game means; the graphical host decides how to display it.** The CLI and SFML window use the same `Runtime` class. This is why most game rules can be tested without opening a window.

## 3. Start with the data types, not the huge functions

### `include/cx/value.hpp` and `src/value.cpp` — a script value

A CX variable can contain no value, a Boolean (`true`/`false`), a number, or text. The C++ `Value` class stores one of those choices with `std::variant`.

Read these functions first:

- `Value::isNull`, `isBool`, `isNumber`, `isString`: ask which kind of value is currently stored.
- `Value::truthy`: decide how a value behaves when a condition needs true or false.
- `Value::numberOr`: get a number, or a fallback number when the value is not numeric.
- `Value::toString`: turn a value into printable text.
- `Value::fromString`: interpret text read from a save file as a typed value.

A `Value` is deliberately small. It does not know where a variable is stored; `Runtime` makes that decision.

### `include/cx/script.hpp` — parsed instructions as ordinary data

The parser creates these records:

- `Statement`: one command such as `set`, `move`, `wait`, or `if`. `type` says which command it is; the other fields store its arguments.
- `EventHandler`: an event header plus the commands that belong under it.
- `ScriptFunction`: a named group of commands and its parameter names.
- `ScriptProgram`: the finished collection of startup statements, event handlers, canvases, functions and diagnostics.

Think of a `Statement` as a recipe card. Writing the recipe card does not cook anything. The runtime reads it later and performs the action.

## 4. How the parser turns text into instructions

Main file: `src/parser.cpp`.

| Function | Plain-language job | Why it exists |
|---|---|---|
| `stripComment` | Removes a `# comment` from a line, except when the `#` is inside quotes. | `broadcast "#welcome"` must keep its text. |
| `prepareLines` | Reads the file line by line, records original line numbers, removes blank/comment-only lines and converts indentation into consistent logical levels. | The rest of the parser can understand blocks without caring whether the author used two spaces, four spaces or tabs. |
| `splitComma` | Splits a list of arguments at commas that are outside quoted text. | A comma inside `"hello, world"` is text, not another argument. |
| `parseStatement` | Recognizes one command and fills a `Statement` record. | It decides whether a line is a `set`, an `if`, a movement command, etc.; it does not run that command. |
| `parseStatements` | Collects indented lines into a block, including nested `if`/loop bodies. | It preserves which commands belong inside which block. |
| `parseEventHeader` | Recognizes event headings such as `on tick:` or `on press key "E":`. | It tells the program when a block should run. |
| `ScriptParser::parseText` | Parses the contents of one script text and assembles its declarations. | This is the main entry point for one file or an in-memory script. |
| `ScriptParser::parseFile` | Reads one file and passes its contents to `parseText`. | File access stays separate from parsing rules. |
| `ScriptParser::parseDirectory` | Finds every `.cx` file under `scripts/`, sorts paths, and combines their parsed results. | File load order is stable and reproducible. |

When a parser function detects something invalid, it adds a `Diagnostic` with the source filename and line number. It does not try to guess what the author meant. This is why error messages can point back to your script.

## 5. How startup and reload work

Main file: `src/runtime.cpp`.

### `Runtime::loadScripts(bool resetGlobals)`

This is one of the most important functions in the engine. It does not edit the running game in place. It builds a second, temporary `Runtime`, tests the new scripts there, and only swaps that candidate into the live game after startup succeeds.

The steps are:

1. `ensureProjectScaffold` checks that the project has the folders the engine expects.
2. `ScriptParser::parseDirectory` reads every `.cx` file under `scripts/` and builds a new `ScriptProgram`.
3. A separate candidate `Runtime` is created. It discovers assets, chooses the requested global/settings baseline, and starts with fresh runtime-owned world state.
4. The candidate runs top-level/bootstrap statements and then its `on load:` handlers. This is where a project normally creates its starting room, player, lights, menu and other first-session objects.
5. Generated files are staged, and diagnostics are checked. A candidate that reports an error is thrown away; the old running game is not replaced.
6. On success, staged file outputs are committed and the candidate's program/state becomes the live session. Startup log lines are appended to the existing log history.

There are two ways to call it:

- `loadScripts()` or `loadScripts(true)` means **load a fresh baseline from disk**. Globals are rebuilt from the saved persistent variables, and settings restart from engine defaults plus `config.json`. Then the new scripts run and can override either. An ordinary global that was changed only in memory and never saved may return to its last saved value.
- `loadScripts(false)` means **keep the current ordinary globals and settings** while replacing the script/world session. The values are copied into the candidate. It also carries the “there is an unsaved persistent change” flag, so a console edit made immediately before this call is still saved instead of being forgotten. This was a subtle bug fixed in v0.10.13.

Both modes start a new session. `{^name}` process-only variables, event-local `{_name}` values, old waits, old periodic deadlines, entities, rooms, effects, canvas-open state, queued audio requests and held keys do not simply migrate. The new `on load:` can recreate the pieces the project wants. Logs remain as history; diagnostics describe the most recent attempt. Already-playing SFML sounds are held by the graphical host and may finish normally even though Runtime's queued audio requests are replaced.

Why go to this trouble? Imagine editing a script while a game is running and accidentally typing an invalid command. A failed reload should not erase the last working game state. The candidate is like a rehearsal: the new version must start successfully before it replaces the currently running one.

For the complete state-by-state table, failure matrix, test coverage and a beginner source-reading route, see [`features/script-reload.md`](features/script-reload.md).

### `Runtime::resetRuntimeState`

Clears or rebuilds runtime-owned data during startup. It separates things that belong to one running game session from data that the caller deliberately preserves, such as globals when the relevant option says not to reset them.

### `Runtime::executeBootstrap` and `Runtime::fireLoad`

Bootstrap statements are ordinary top-level commands that run during startup. `fireLoad` then dispatches handlers declared with `on load:`. They are separate phases; a project does not need a special filename such as `main.cx` for either one.

## 6. How a key press or game tick runs a script

### `Runtime::pressKey`

The graphical host calls this when a key-press event occurs. It is an event, not a held-key check. For continuous movement, the host updates the held-key set through `setKeyDown`, and a script can query it with `keyDown`. The held-key functions and the focus-loss reset are explained just below.

### `Runtime::setKeyDown`, `Runtime::keyDown` and `Runtime::clearHeldKeys`

The window host owns the real keyboard events, but scripts need a simple, reliable way to ask whether a key is currently held. These three functions connect those two sides:

- `setKeyDown(name, true)` records that the host saw a key press. `setKeyDown(name, false)` removes that one key when the host sees its release. The key name is normalized before it is stored, so the runtime compares a consistent name rather than whatever spelling a frontend happened to use.
- `keyDown(name)` asks whether that normalized key is in the held-key set right now. It does not wait for a new press event; that is why an `on tick:` handler can use it to keep moving an actor while a key stays down.
- `clearHeldKeys()` removes every held key at once. It exists for a specific operating-system edge case: when a user presses a key and then Alt-Tabs away, the window may lose focus before it receives that key's release event. Without a reset, the game can keep thinking the key is down. The SFML host calls `clearHeldKeys()` on `sf::Event::FocusLost`, so the next simulation tick sees no held keys. The function only clears the held-state set; it does not create synthetic `on press key` or `on release key` script events.

A useful mental model is: `pressKey()` says **“this key was pressed just now”**; `setKeyDown()` updates **“this key is currently held”**; `keyDown()` asks the current-state question; and `clearHeldKeys()` is the host's emergency reset when it can no longer trust that it received all release events.

### `Runtime::tick`

This advances one logic step. It is not the draw function. A host calls it at the configured simulation rate. It resumes ready `wait` continuations, fires `on tick:` handlers, advances timed effects/entities, updates triggers/focus and checks periodic schedules. The renderer draws separately.

### `Runtime::fire`

This dispatches program-level events such as load, tick, key press and button click. It walks the matching handlers in their stored order. These handlers belong to the loaded program, not to one room. Therefore, when the first handler for a key enters another room, later handlers for that same key press still run.

That is different from a callback that carries a snapshot of a room-specific event. `fireTriggerEvent`, `fireProjectileHit`, and the focus/tween/animation completion dispatchers validate room revisions and object identities so they do not deliver stale contacts after a scene change or object replacement. See `features/room-transition-lifecycle.md` for the detailed contract and the v0.10.11 maintenance clarification test.

## 7. The interpreter: commands versus calculations

### `Runtime::executeStatements`

This starts an interpreter execution context for a prepared list of statements. If the context is new, it creates the first execution frame. It then hands control to `runExecution`. If a `wait` suspends the context, the context is saved in the runtime's scheduled queue rather than blocking the game thread.

### `Runtime::runExecution` — `src/runtime_statements.cpp`

This is the command interpreter. It reads the next `Statement` in the current execution frame and uses `switch (statement.type)` to decide what to do. For example:

- `StatementType::Set` evaluates the right-hand side and updates a variable.
- `StatementType::EnterRoom` asks the room manager to activate a room.
- `StatementType::LoopTimes` creates/updates an execution frame so a block can repeat.
- `StatementType::Wait` stores a wake-up time and suspends the context.
- `StatementType::SpawnProjectile` hands the prepared command to the projectile module.

A frame is the interpreter's bookmark: it remembers which block is running and which statement should be next. Nested blocks/functions add frames rather than asking C++ to recursively call one giant interpreter function for every script command.

The instruction and loop limits are deliberate guardrails. A malformed or accidental infinite script loop should produce a diagnostic instead of monopolizing the game update forever.

### `Runtime::evaluateValue` and `Runtime::evaluateCondition` — `src/runtime_expressions.cpp`

These answer different questions:

- `evaluateValue("2 + 3")` returns a typed value (number 5).
- `evaluateCondition("{health} > 0")` returns whether a condition is true or false.

They are separate from `runExecution` so arithmetic and condition rules are shared by many commands. Both can report an error through the `error` output parameter. In C++, an output parameter is an ordinary function argument that the function fills in for its caller.

### `Runtime::resumeScheduledExecutions`

A script's `wait 1 second` must not call C++ `sleep`, because that would freeze input and drawing. Instead the interpreter saves its bookmark, local values and wake-up deadline. On later ticks, this function checks the deadline and resumes the bookmark. A room transition or successful script replacement can invalidate an old continuation; the room-context revision is the guard used for that rule.

## 8. Variables and their lifetimes

The variable storage code lives in `Runtime` and `VariableStorage`:

| Script spelling | C++ storage | How long it lasts |
|---|---|---|
| `{score}` | `Runtime::globals_` | Persistent; saved to the project. |
| `{^combo}` | `Runtime::transient_` | Until this runtime process is replaced/exits; not written to persistent save files. |
| `{_damage}` | `ExecutionContext::locals` | One event/function execution context. A delayed `wait` keeps that context while suspended. |

Important functions:

- `resolveVariableName`: normalizes a script variable name and resolves special asset/audio placeholders.
- `getVariable`: looks up a value in the correct scope and returns `none` when no value is present.
- `setVariable`: routes a write to the correct scope and records persistent data as dirty when needed.
- `listEntries`, `appendListValue`, and `clearList`: handle list-style names such as `{inventory::*}`.
- `VariableStorage::load`: reads the editable CSV/cache files and validates them before filling the destination map.
- `VariableStorage::save`: writes the persistent map back to the project using the storage safeguards described in `variable-storage-recovery.md` and `features/variable-storage-durability.md`.

Persistent saving is intentionally separate from transient/event-local variables. Do not copy every runtime variable into the save file.

## 9. Rooms, collision, movement and rendering

### Rooms — `include/cx/rooms.hpp`, `src/rooms.cpp`

- `RoomManager::readRoomFile` reads and validates a candidate JSON room into a temporary `RoomDefinition`.
- `RoomManager::loadRoom` adds that validated room to the loaded-room collection.
- `RoomManager::enterRoom` changes the active room and its entrance after the destination is ready.
- `RoomDefinition::tileAt` reads the texture ID stored in one tile cell.
- `RoomDefinition::isSolidTile` resolves whether a cell blocks movement using per-cell overrides and the texture collision registry.

The grid coordinates are integers measured in tiles. Rendering later converts those coordinates to pixels. Keeping that distinction prevents a room's data format from depending on window size.

### Collision and movement

`include/cx/collision.hpp` defines collision bounds and shared classification/channel values. `src/collision.cpp` contains the pure geometry/filtering helpers. Runtime-specific movement and projectile code calls those helpers but owns the actual game state. A collision check answers a question such as “do these two boxes overlap?”; it does not automatically move an object unless the calling system implements a response.

### Graphical host — `src/sfml_main.cpp`

The SFML file owns the window, polls events, tracks held input, uses a fixed-step accumulator to call `Runtime::tick`, and draws the state exposed by the runtime. It should not contain separate copies of script rules. If a rule needs to work identically in the CLI and GUI, it belongs in `cx_core`, not only in `sfml_main.cpp`.

The CLI host (`src/cli_main.cpp`) uses the same `Runtime` but has no game renderer. `cx-cli --check` parses and validates a project without opening a window or intentionally creating project files.

### Interaction focus, tweens and projectiles — the systems that make objects feel alive

These systems are split into separate `.cpp` files so the main interpreter does not have to contain every detail of movement and collision.

**Interaction focus** (`src/runtime_interaction.cpp`):

- `Runtime::selectInteractionTarget(actor)` looks for the nearest eligible object the actor can interact with. It checks that the actor is a real entity, that its interaction range is valid, that the target is marked `interactable`, and that the two objects' collision layers/masks allow interaction. It measures from hitbox centers, not sprite corners. If two targets are exactly equally close, the normalized target name breaks the tie so the result does not depend on accidental map iteration.
- `Runtime::updateInteractionFocus()` compares what each actor was focused on with what it should focus on now. It sends a focus-exit event for the old target and a focus-enter event for the new one. This is why a script can react to focus entering/leaving without constantly running its own distance loop.
- `Runtime::focusedEntity(actorName)` is a safe query for the currently focused target. It rechecks the actor, target identity and current eligibility rather than blindly returning an old name.
- `Runtime::fireInteractionFocusEvent(...)` runs the appropriate handlers. Because those handlers can move/delete objects or enter a room, the function checks that the room and the exact object instances are still the same before continuing to later handlers.

**Entity tweens** (`src/runtime_motion.cpp`):

- `Runtime::updateEntityTweens()` advances every active tween by one simulation tick. It calculates normalized progress from elapsed ticks/duration, applies the selected easing curve, computes the desired position, optionally checks movement collision, and queues completion events.
- `Runtime::movementSegmentBlocked(...)` asks whether an entity's hitbox would cross a solid tile, room edge or eligible blocking entity between two positions. It tests the *whole movement segment*, not just the final point, which helps prevent fast tweens passing through thin obstacles.
- `Runtime::fireTweenComplete(...)` sends the one-shot completion event only while the original entity instance and room context still exist. A replacement entity with the same name is not treated as the old one.

**Projectiles** (`src/runtime_projectiles.cpp`):

- `Runtime::executeProjectileStatement(...)` interprets a parsed projectile command and validates its named owner/target, texture, speed, damage and pattern-specific options before spawning anything.
- `Runtime::spawnProjectileAtAngle(...)` is the shared low-level constructor for one projectile at a chosen angle. It initializes position, velocity, target identity, hitbox, collision settings, damage and lifetime. Higher-level single-shot and pattern commands reuse it so they behave consistently.
- `Runtime::updateProjectiles(deltaSeconds, tileSizePixels)` moves projectiles, samples points along their paths for wall/target contacts, and queues the hits. It removes expired or blocked shots, sorts sampled hits by impact time, and only then applies damage and fires script events. Deferring callbacks until after the object-map traversal lets those callbacks delete entities without invalidating the traversal.
- `Runtime::fireProjectileHit(...)` supplies the event-local values to matching hit handlers. The hit dispatcher verifies the target identity so an old projectile cannot damage a newly spawned entity that merely reused the same name.

**Room movement and paths** (mostly `src/runtime.cpp`):

- `Runtime::positionHitsSolidTile(object, x, y)` checks an object's collision box at one candidate position against the active room's solid tiles and room boundary.
- `Runtime::findTilePath(start, goal)` uses the room's tile collision rules to find a walkable route. The route is a list of tile coordinates; the movement code follows it over time.
- `Runtime::updateEntities()` coordinates ordinary entity movement and calls the tween/projectile update work as part of the simulation. The exact order matters: collision and callbacks are not merely drawing effects.

**How one tick is ordered.** `Runtime::tick()` first increments the tick counter and clears per-tick movement flags. It resumes `wait` continuations, runs `on tick` handlers, ages timed beams, updates entities/tweens/projectiles/animations, evaluates trigger contacts, updates interaction focus, fades screen flash, and checks periodic handlers. This order is intentional: for example, trigger/focus events are based on the state after entity movement for that tick. The graphical host then renders the resulting state separately.

## 10. The storage boundary

`src/variable_storage.cpp` has more low-level file handling than most modules because save data deserves special care.

- `parseCsv` reads text rows into typed values and rejects malformed structure rather than inventing values.
- `compressCache` / `decompressCache` translate the editable variable representation into/from the optional compact cache format.
- `writeAtomic` writes a replacement through a temporary file before the destination is replaced; it has platform-specific durability steps.
- `SaveLock` coordinates cooperating engine processes. Older versions or external editors do not honor this lock, so do not let an old engine and a new engine write one project at the same time.
- `SaveManifest` records the generation and byte fingerprints of a matching CSV/cache pair. The fingerprints detect mismatches or accidental damage; they are not a security signature.

Read the comments in `variable_storage.cpp` before changing this area. A save bug can destroy a user's game progress, so behavior changes need failure-injection tests, not just a successful save test.

## 11. Tests are executable explanations

Tests under `tests/` are the closest thing the engine has to a precise behavior contract. Each focused executable has a small purpose:

- `runtime_tests.cpp`: language/runtime behavior across variables, functions, loops, waits, settings and other general script operations.
- `project_scaffold_tests.cpp`: safe creation/recognition of new project folders.
- `variable_storage_manifest_tests.cpp` and `storage_process_tests.cpp`: file format, recovery, interrupted writes, locks and competing writers.
- `projectile_runtime_tests.cpp`, `collision_runtime_tests.cpp`, `motion_runtime_tests.cpp`: moving objects, hits and tween collision rules.
- `interaction_focus_tests.cpp`, `room_collision_tests.cpp`, `room_lifecycle_tests.cpp`: focus eligibility, room collision metadata and scene transition contracts.
- `animation_runtime_tests.cpp`, `beam_effect_tests.cpp`, `attack_pattern_tests.cpp`, `random_stream_tests.cpp`, `task_lifecycle_tests.cpp`: focused feature contracts.
- `test_room_editor_server.py`: Room Studio save/load/validation API behavior.

When a test fails, read its assertion message first, then the fixture script, then the implementation path. The fixture usually shows a small `.cx` program and the assertion states the intended engine behavior.

## 12. A few C++ words in plain language

- **Header (`.hpp`)**: a file that tells other C++ files which types and functions exist. It usually declares them without containing the full implementation.
- **Implementation (`.cpp`)**: where the work done by a function is written.
- **Namespace (`cx`)**: a named group that keeps engine names from colliding with names in unrelated libraries.
- **Function parameter**: a value supplied to a function when you call it.
- **Return value**: what a function gives back after it finishes. `bool` usually represents success/failure or true/false.
- **Reference (`T&`)**: another way to refer to an existing object without making a copy.
- **Pointer (`T*`)**: a value that stores an object's address. It can be empty (`nullptr`); check before using it.
- **`const`**: says that code using this reference/value cannot change the referenced object through that access path.
- **`std::string_view`**: a lightweight read-only view of text. It does not own the characters, so do not store it beyond the lifetime of the text it views.
- **`std::vector`**: a resizable ordered list.
- **`std::map` / `std::unordered_map`**: collections that associate a key with a value; the first keeps keys sorted, the second is optimized for lookup and has no guaranteed iteration order.
- **`std::optional<T>`**: either a `T` value or “nothing”. It is useful when absence is normal, not necessarily an error.
- **`std::variant`**: one value selected from a fixed list of possible types.
- **`[[nodiscard]]`**: a compiler hint that the return value matters; ignoring it may hide a mistake.
- **`noexcept`**: a promise that the function will not let a C++ exception escape.
- **RAII**: resources such as files/locks are tied to object lifetime so cleanup happens through normal C++ object destruction.
- **Regression test**: a test that makes sure a bug or behavior already fixed does not quietly return.

## 13. A safe first code-reading exercise

1. Open `examples/first_steps.cx` and find one command you recognize.
2. Find its `StatementType` in `include/cx/script.hpp`.
3. Find the corresponding `case StatementType::...` inside `Runtime::runExecution` in `src/runtime_statements.cpp`.
4. Follow any call it makes into `Runtime`, `RoomManager` or another module.
5. Make a comment-only experiment in a copy, build, and run the tests.
6. Only change behavior after identifying the expected result and adding a small regression test.

You can learn this codebase one path at a time. There is no need to understand all 9,000-plus lines before making a useful change.

## v0.10.14 — following the developer console and settings

This section follows the path taken when a person types a command into the terminal. It is intentionally slower and more literal than the code comments, so a beginner can read the functions in order without needing to understand the whole engine first.

### 1. `Runtime::executeConsole` — the front door for typed commands

**File:** `src/runtime.cpp`  
**Job:** receive one line of text from either the graphical terminal or `cx-cli` and decide what kind of line it is.

In plain language, this function is a reception desk. It does not itself know how to draw a window, and it does not implement every game-language command. It makes two decisions:

1. Clear `diagnostics_`, because a new command should not display an error left over from the previous command.
2. Ask `executeSimpleConsole()` whether the line is one of the engine's developer utilities (such as `/help` or `/settings`). If that function returns `true`, the line was handled and the work is finished. If it returns `false`, the line is passed to `executeInline()` and parsed as a normal `.cx` statement.

The `bool` return value is important. **`true` means “this was one of the terminal utilities.” `false` means “I did not recognize it as a utility; let the script parser try.”** It does not mean that a game command succeeded or failed.

### 2. `Runtime::executeSimpleConsole` — the utility-command dispatcher

**File:** `src/runtime.cpp`  
**Job:** recognize terminal utilities that are not part of the `.cx` scripting language.

The function trims spaces from the beginning/end of the input and makes a lowercase copy for comparing command names. That means `/HELP` and `/help` are treated the same. It then checks the command against the supported utility list.

For `/help`, the function writes a short catalogue into the normal runtime log. Each catalogue line explains one utility and its expected arguments. The `help` spelling without a slash is an alias for `/help`. A regression test in `tests/runtime_tests.cpp` searches the output for every required utility, so a command that disappears from the help text is noticed by the tests.

For the other utilities, the dispatcher asks the runtime for information and writes the answer to the log:

- `/vars` calls `variableReport()` to format global and RAM-only variables.
- `/settings` reads the current `settings_` object and prints its values. It does not change them.
- `/sprites`, `/paths`, and `/lights` inspect the corresponding runtime collections.
- `/rooms` asks the room manager which rooms are loaded and which one is active.
- `/textures` reads texture IDs discovered by the room/asset manager.
- `/status` prints simple counts such as loaded script files, handlers, variables and ticks.
- `/clear` clears the runtime log vector.
- `/tick` calls `tick()` once. This is a manual logic step, not a request to draw one frame.
- `/press KEY` and `/click BUTTON_ID` simulate input through the same runtime-facing input functions used by the graphical host.
- `/attach "PATH" X Y` sends a file-attachment event to the runtime at a supplied window coordinate. The path should be quoted when it contains spaces.
- `/reload` calls `loadScripts(true)`, which stages a fresh script session from project files. If the new session fails, the previous live runtime is retained.

`/quit`, `quit`, and `exit` are **not** runtime commands. `src/cli_main.cpp` recognizes them before calling `executeConsole()` so they close the command-line program. The graphical game closes through its window close control. This difference is why the help text labels these as CLI-only.

### 3. `Runtime::log` — keeping terminal messages from growing forever

**File:** `src/runtime.cpp`; limit declared in `include/cx/runtime.hpp`  
**Job:** append a message to `logs_` while keeping the retained message collection bounded.

`logs_` is a vector of strings. Each call to `log()` appends one message. If the number of stored message entries grows beyond `maximumLogEntries_` (500), the oldest entries are erased from the beginning. This prevents a long-running game or repeated debug command from using more and more memory just to retain old terminal output.

A message string can contain embedded newline characters, so 500 is a limit on **message entries**, not a guarantee that exactly 500 visual text rows are displayed. The graphical drawer applies its own display-row limits after formatting and combining logs with diagnostics. Neither history is intended to be an audit log saved permanently to disk.

### 4. `Runtime::applySetting` — checking a setting before accepting it

**File:** `src/runtime.cpp`  
**Job:** translate an `engine::...` assignment into a change to the typed `EngineSettings` structure.

The script language represents setting values as general `Value` objects. The renderer, however, wants particular C++ types: a `bool` for VSync, an unsigned whole number for the tick rate, a floating-point number for ambient brightness, and a string for the window title. `applySetting()` is the boundary between those two worlds.

It first makes the setting name lowercase. Then it compares that name to the supported list. Depending on the setting, it checks the type and range before changing the matching member in `settings_`. For example:

- `window_width` must be a whole number between 320 and 7680.
- `tick_rate` must be a whole number between 1 and 1000.
- `ambient_light` must be a finite number from 0 to 1.
- `texture_filter` must be the text `nearest` or `linear`.
- `vsync` must be an actual boolean, not the text `"true"`.

If a value is invalid, the function fills the `error` string and returns `false`. The caller turns that into a diagnostic. The important safety property is that a rejected value does not partially update the setting. This function is why the reference in `engine-settings.md` must match the actual code, not just what a user interface happens to show.

### 5. `Runtime::loadConfig` — applying the optional project defaults

**File:** `src/runtime.cpp`  
**Job:** read `config.json` before the script's own top-level statements run.

This is a deliberately small settings-file reader. It reads one key/value pair per line, converts strings/booleans/numbers into `Value` objects, and passes each value through `applySetting()`. It reports malformed lines and duplicate/unknown settings instead of silently guessing. It is not intended to read arbitrary nested JSON structures.

The order matters: config values are the project's baseline, then sorted script bootstrap statements can override them. A setting placed in a script therefore wins over the same setting from `config.json` when that startup session loads.

### 6. `Runtime::resolveCanvasElements` — converting layout into screen coordinates

**File:** `src/runtime.cpp`  
**Job:** turn a canvas definition into element rectangles that the renderer can draw in the current window.

A script describes a button or label in a reference design space. The actual window may be wider, taller, or smaller than that design. This function compares the current viewport size with the configured UI reference width/height, calculates scale factors, and creates a separate vector of resolved elements. It does not draw those elements and it does not change the original canvas definition.

For absolute layouts it scales the original X and Y coordinates. For grid/anchor layouts it converts the 0–128 grid point to a pixel position and shifts the element based on its chosen anchor (for example, center or bottom-right). For relative layouts it looks up an earlier element in the already-resolved vector and positions the new element beside or below it. `ui_scale` multiplies the element dimensions/font size, not the world tile coordinates.

### 7. The graphical host's diagnostic path

**File:** `src/sfml_main.cpp`  
**Important functions/lambdas:** `printAndClearDiagnostics()` and `refreshConsoleDisplay`.

The runtime stores structured `Diagnostic` objects: severity, file, line, message, suggestion and sometimes the original source text. The graphical terminal needs printable strings, so `printAndClearDiagnostics()` converts the structured diagnostics into readable lines, also prints them to the parent terminal, clears the runtime's temporary diagnostic list, and returns the formatted lines to the window code.

`refreshConsoleDisplay` keeps a separate `diagnosticHistory` vector. It avoids appending the same consecutive diagnostic batch twice, trims that history to 300 display lines, combines it with the latest runtime logs, then trims the combined drawer view to 500 entries/lines. When an error is present, it automatically opens and focuses the drawer so the user does not have to guess why a command or script stopped. This is a display-history limit; it does not modify the saved project files or make the diagnostics permanent.

### A practical debugging route

When something does not work, use this order:

1. Read the exact error and its source path/line. Do not edit unrelated systems yet.
2. Read the `HOW TO FIX` suggestion, then open that exact line in the `.cx` file.
3. Run `cx-cli --check /path/to/project` to parse all scripts without opening a graphical window.
4. If the error is fixed, launch the project again or use `/reload` to build a fresh script session.
5. Use `/status`, `/vars`, `/rooms`, `/sprites`, `/settings`, `/paths`, or `/lights` to inspect runtime state before assuming the renderer is at fault.

The best learning habit is to follow one value from the script statement, into the parser's `Statement`, into the runtime function that evaluates it, and finally to the state the graphical host reads. Avoid trying to understand every module at once.


### 8. `appendDiagnostic` and `appendDiagnostics` — keep error reports useful and bounded

**Files:** `include/cx/diagnostics.hpp`, `src/diagnostics.cpp` (used by `src/parser.cpp`, `src/runtime.cpp` and `src/runtime_statements.cpp`)

**Job:** add a diagnostic message without allowing a broken script or repeated runtime error to grow a vector forever.

A `Diagnostic` is a small record describing one thing the engine wants to tell the developer. It contains whether the message is a warning, which file it came from, a line number, a human-readable explanation, and a suggested fix. `std::vector<Diagnostic>` is simply a growable list of those records. Before this change, a parser could append a diagnostic for every invalid line and a running script could append an error on every tick. The GUI only displayed a bounded portion of that history; the underlying runtime list itself could still grow.

`maximumDiagnosticEntries` sets one shared maximum of 256 records. `appendDiagnostic()` is the only supported way to add one record now. It behaves like this:

1. While fewer than 255 records exist, it adds the new real diagnostic.
2. When the list reaches 255, the next insertion adds a special final error saying the 256-record limit was reached and more problems are hidden.
3. Once that notice exists, further diagnostics are ignored until the caller clears/replaces the list. It does not keep allocating records.

Why reserve the final slot? Simply cutting off errors would make the displayed list look complete even though more errors existed. The summary is deliberately an **error**, not a warning, so a script with hidden parser errors cannot accidentally be treated as valid. The fix suggestion tells the developer to correct the first group or split a very large source into smaller pieces, then run the check again.

`appendDiagnostics()` handles a whole second list. It loops through that list and passes every record through `appendDiagnostic()`. This matters when the parser merges diagnostics from one `.cx` file into the full project result: the combined project list must have the same limit as each individual parser list. Both a const-reference version (copy records) and an rvalue version (move records when the source list is no longer needed) are provided.

The test `testDiagnosticHistoryIsBoundedForParserAndRuntime()` makes 400 invalid top-level declarations and then runs a tick handler that creates 400 runtime errors. It checks that both paths stop at exactly 256, that the last entry is the suppression notice, and that the engine still reports an error. This is a regression test: it makes sure a future refactor cannot accidentally reintroduce unbounded diagnostic growth.

### 9. How to follow a diagnostic from its source to the screen

A common path is: parser/runtime code detects a problem → `appendDiagnostic()` stores the structured record → `hasErrors()` decides whether it is safe to continue → `printDiagnostic()` formats it for a terminal → the SFML host may copy the formatted lines into its bounded drawer history. The data record is separate from the text shown to the user. That separation lets the CLI and graphical window explain the same error without having two different parsers or error rules.
