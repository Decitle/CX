# Script reload state contract — v0.10.13

**Scope:** `Runtime::loadScripts()` and the state committed by a successful candidate startup.  
**Status:** audited against the v0.10.12 implementation; clarified and regression-tested in v0.10.13.  
**Purpose:** make `/reload` predictable for game authors and prevent a future refactor from accidentally carrying stale state into a new script program.

This document describes a **script reload**, not `reload room "name"`. A room reload changes one room definition inside the current runtime. A successful script reload replaces the active program and starts a new runtime session from the candidate scripts. The distinction matters: room transitions deliberately preserve most runtime-global objects; successful script reloads deliberately rebuild them.

## 1. The transaction in plain language

`Runtime::loadScripts()` does not parse new scripts directly into the currently running game. It creates a separate candidate runtime, loads the candidate `.cx` files, executes their top-level statements and `on load:` handlers, stages generated files, and checks for errors. Only after that startup is valid does it replace the active runtime state.

Think of this as preparing a new game session in a separate room before opening the door. If the rehearsal fails, the old game continues. If it succeeds, the candidate becomes the game that receives the next input and simulation tick.

The transaction has three important outcomes:

1. **Parse failure:** the current program/world remains live. `diagnostics_` is replaced with the attempted script's parse diagnostics so the author can see what needs fixing.
2. **Bootstrap, `on load`, or generated-output commit failure:** the candidate is discarded. The old program/world, pending waits, persistent in-memory values, settings, audio requests, and other live state stay active. `diagnostics_` is replaced with the candidate failure diagnostics. Staged output files are discarded when startup fails.
3. **Successful startup:** staged outputs are committed first, then candidate program/runtime state replaces the live state. The old session's delayed tasks and world objects do not migrate unless the candidate script creates equivalent new ones.

`/reload` uses the default `loadScripts(true)` behavior.

## 2. The `resetGlobals` choice

| Call | Ordinary globals | Engine settings | Persistent save files | Process-only `{^name}` values |
|---|---|---|---|---|
| `loadScripts()` / `loadScripts(true)` | Rebuilt from saved state, then candidate bootstrap/on-load runs | Reset to built-in defaults, then `config.json`, then candidate scripts may override | Read from the last saved generation; candidate startup changes may save after successful commit | Cleared |
| `loadScripts(false)` | Existing live values are copied into the candidate, then candidate startup may change them | Existing live settings are copied, then candidate scripts may override | Not reloaded from disk; pending unsaved global changes remain dirty and are saved on successful commit | Cleared |

### What the distinction means

- `true` is a fresh baseline from disk. An ordinary global that was changed only in memory and has not yet been saved can be replaced by its last saved value. `config.json` changes are picked up. The candidate's script statements still run after the config/save baseline and can override either.
- `false` carries the live ordinary globals and current `EngineSettings` into the new candidate. It also carries the live `variableStorageDirty_` flag. This last detail is important: a developer-console edit can be present in RAM but not yet in `variables.csv`; a successful `loadScripts(false)` must not forget that the value still needs saving. v0.10.13 adds a regression for that case.
- Both modes clear process-only `{^name}` values. They are shared between events during one process/session, but are not a save-game format and are not carried into a newly started script session.
- Event/function locals such as `{_name}` belong to an `ExecutionContext`. Old contexts point into the old program tree and cannot be migrated to the new one.

## 3. Successful reload state matrix

The candidate starts from fresh runtime-owned state. The candidate's bootstrap and `on load:` handlers can recreate anything the project wants at startup.

| State | After a successful script reload | How to restore it |
|---|---|---|
| Parsed `ScriptProgram` and handlers | Old program replaced by candidate program | Parse the updated `.cx` files; handlers are registered from that candidate |
| Ordinary globals | Preserved only with `resetGlobals=false`; otherwise loaded from persistent save | Candidate bootstrap and `on load:` can initialize/update values |
| `{^name}` process-only variables | Always cleared | Candidate startup can explicitly initialize new session values |
| Event/function locals and suspended execution contexts | Old contexts discarded | New events create fresh locals; startup `wait` commands create new candidate waits |
| Pending `wait` continuations | Old waits cancelled; waits created during candidate startup survive and are rebased to the committed room-context epoch | Put required startup scheduling in the new script's bootstrap/`on load:` |
| `every N` periodic schedule | Old deadlines discarded; each candidate periodic handler receives a new deadline after startup | Periodic handlers start their interval again; overdue old deadlines do not fire immediately |
| Seeded random streams | Old stream positions discarded; candidate seed statements initialize new streams | Run the new program's `seed random stream` statements during startup |
| Rooms / active room | Old room manager replaced by the candidate's loaded/entered room set | `load room` and `enter room` during bootstrap/`on load:` |
| Sprites, entities, projectiles | Old object instances removed; candidate startup objects become active | Recreate initial objects in the candidate startup code |
| Paths, tweens, projectile counters and entity-label IDs | Rebuilt/reset with the candidate runtime | Recreate paths and movement instructions during startup/events |
| Lights and beams | Old registrations removed; candidate startup registrations replace them | Create the desired lights/beams in the new startup |
| Trigger contacts and interaction focus | Cleared; no old-room exit callbacks are synthesized | The next simulation reconciliation finds new contacts/focus |
| Open canvases | Old open-canvas set replaced; only canvases opened by candidate startup are open | Reopen persistent/global menus in the new `on load:` |
| `AudioSystem` wave recipes and pending play requests | Old generated-wave definitions and queued play requests replaced; supported audio asset files are rediscovered | Recreate generated waves and queue new sounds during startup |
| Already-playing SFML sounds | Remain owned by the graphical host's active-sound list until their playback ends; Runtime reload does not explicitly stop them | This is current host behavior, not a promise of a future audio-device/session API |
| Held keyboard state | Cleared | The window host reports new press/release state after focus/input events |
| Screen flash | New candidate settings/state; previous fade is not migrated | Candidate startup may set `engine::screen_flash` |
| Simulation tick count | Resets for the new runtime session | No script restore mechanism currently exists |
| Logs | Existing runtime log history remains; candidate startup log lines are appended | Use `/clear` to deliberately clear history |
| Diagnostics | Replaced by the candidate's startup diagnostics on success | New warnings/errors are those produced by the candidate startup |
| Viewport dimensions and host resource limits | Remain owned by the live host/runtime object; the viewport size is passed into the candidate before startup | The host updates viewport dimensions as the window changes |
| Project root | Unchanged | A Runtime instance does not switch projects during reload |
| Staged generated output files | Candidate outputs are committed only if all startup work succeeds | Fix startup errors and retry; failed candidate outputs are discarded |

### Notes about audio

`AudioSystem` is runtime-owned. Resetting it removes generated wave recipes and any requests that have not yet been consumed by the graphical host, then scans `assets/audio/` again. A wave generated by script must be regenerated by the new startup if later scripts intend to play it. The SFML frontend separately owns `sf::Sound` objects that have already begun playback; the current host does not stop those active sounds just because the Runtime's `AudioSystem` is replaced. This is a boundary of today's implementation, not a general mixer-lifetime guarantee.

### Notes about viewports and limits

The host sets the current viewport size on the staged runtime before startup, so responsive canvas calculations start with the current dimensions. The live Runtime object's project path, viewport bookkeeping and instruction/log limits are not replaced by values from a second Runtime constructor. The new `EngineSettings` still control the host window/renderer after the reload succeeds.

## 4. Failure versus success

| Item | Failed candidate | Successful candidate |
|---|---|---|
| Current program and handlers | Preserved | Replaced |
| Current room/world entities/effects | Preserved | Replaced by candidate startup world |
| Current ordinary globals/settings | Preserved exactly | `resetGlobals` policy applies, then startup may change them |
| Transient variables / old waits | Preserved | Cleared/replaced |
| Periodic schedule | Preserved | Restarted from candidate deadlines |
| Audio requests and definitions | Preserved | Rebuilt from candidate startup and asset scan |
| Persistent save files / generated outputs | Candidate writes are staged and discarded if startup fails | Staged writes are committed before the runtime-state swap; subsequent persistent save follows normal storage rules |
| Log history | Remains; `/reload` command may append its own result message | Remains and candidate startup log lines are appended |
| Diagnostics | Replaced with candidate error diagnostics | Replaced with candidate startup warnings/diagnostics |

This means the error panel can describe the *failed attempt* while the old game continues underneath. That is intentional: diagnostics are feedback about the latest attempt, not a snapshot of all errors ever seen.

## 5. Test coverage

The contract is protected by focused regressions in the existing test executables:

- `tests/runtime_tests.cpp`: pending persistent-save dirty flag survives `loadScripts(false)`; globals/settings survive that mode; transient values, old sprites/lights/rooms/canvases/audio requests/held keys reset; new startup state is installed; log history survives; default `loadScripts(true)` reloads the saved value and changed `config.json` baseline.
- `tests/task_lifecycle_tests.cpp`: old waits are cancelled; waits created by the candidate's `on load:` survive the commit; a new periodic deadline begins after the successful reload instead of inheriting an overdue old deadline.
- `tests/random_stream_tests.cpp`: failed reload preserves the live random stream position; successful reload reconstructs streams from candidate seed statements.
- Existing `tests/runtime_tests.cpp` failure fixtures: syntax and runtime-error reloads preserve the last-good program, objects, settings and persistent save files.
- `tests/room_lifecycle_tests.cpp`: room-context changes remain distinct from script reload; room-only lifecycle behavior is not silently changed by this contract.

Run the full CTest suite after changing reload state or ownership. The CLI/core tests exercise the transaction without requiring SFML; separately build the optional graphical frontend when changing host-owned behavior.

## 6. Safe-reading path for beginners

Read these in order:

1. `include/cx/runtime.hpp` — public `Runtime::loadScripts(bool resetGlobals)` declaration and the state members grouped below it.
2. `src/runtime.cpp`, `Runtime::loadScripts` — candidate creation, parse, staged startup, checks, and commit.
3. `src/runtime.cpp`, `Runtime::resetRuntimeState` — which session-owned containers are cleared and which globals/settings are conditionally reset.
4. `Runtime::loadPersistentVariables` / `Runtime::savePersistentVariables` — how disk state enters/leaves the runtime and why the dirty flag matters.
5. `Runtime::tick`, `Runtime::resumeScheduledExecutions`, and the periodic-deadline loop — how a new session handles waits and periodic events.
6. `include/cx/audio.hpp` / `src/audio.cpp` and `src/sfml_main.cpp` — the boundary between Runtime's sound definitions/requests and host-owned active playback.
7. The regression fixtures listed above — small executable examples of each rule.

**Compatibility rule:** do not turn `loadScripts()` into a partial hot-reload/migration system without a separate design and migration contract. It currently means “start a new script runtime from a verified candidate, then replace the old session.”
