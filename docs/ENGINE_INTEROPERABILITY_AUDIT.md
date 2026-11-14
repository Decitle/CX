# Engine interoperability audit

This page tracks whether CX systems merely work in isolation or have been shown to work together. A green subsystem test is not automatically evidence that a full game scenario works.

The goal is not to promise that every imaginable game can be built from a finite feature list. The goal is a modular 2D engine whose language, state model, event lifecycle, rendering, simulation, assets, and packaging compose predictably enough to support a broad range of game designs.

## Status vocabulary

- **Integrated** — an automated test exercises the systems together and checks their observable result.
- **Lifecycle-tested** — the interaction or invalidation boundary is tested, but not necessarily as part of a complete game scenario.
- **Separate coverage** — both sides have tests, but there is no direct end-to-end test for their combined workflow.
- **Gap** — the capability or combined behavior is not implemented or has not been verified.

## Current integration evidence

| Systems that meet | Evidence in the repository | Status | Remaining limitation |
|---|---|---|---|
| Script variables → canvas text → rendered HUD | testCanvasTextInterpolatesLiveGlobalValues checks initial values, a key-event update, and an inline update; first-steps and quickstart display live score text. | **Integrated** | Physical display scaling and accessibility behavior still need device-level checks. |
| Canvas buttons → event handlers → game rules → visible state | Number Hunt uses button IDs, click events, random values, conditions, score/attempt state, and live canvas text. testFirstGameNumberHuntRoundLifecycle plays a full wrong/correct/replay sequence. | **Integrated** | Number Hunt is deliberately small; it is not yet the packaged first-game acceptance run. |
| File attachment → hit-tested canvas button → event locals → suspended handler | event_context_runtime_tests checks absolute path resolution, pointer coordinates, matching/empty button IDs, and local values after a wait. | **Integrated** | The automated test calls the public host API; it does not exercise each desktop file-drop bridge. |
| Tween motion → collision filters → trigger/focus eligibility | testTweenCollisionLeavesTriggerAndFocusTargetsNonBlocking exercises a moving entity alongside trigger and focus targets. | **Integrated** | Broader scenario coverage and performance under many moving entities remain open. |
| Tween completion → event payload → captured arrival values | Motion runtime tests check completion identity and final coordinates. | **Integrated** | More examples should show the completion contract in gameplay code. |
| Animation playback → completion event → captured animation data | Animation runtime tests check one-shot completion, looping behavior, and invalidation after replacement/restart. | **Integrated** | Combined animation-plus-room-transition-plus-reload scenarios are not one end-to-end fixture yet. |
| Projectile movement → collision layers/masks → ordered impacts | Projectile and collision tests exercise mutual filtering, sampled impact order, replacement identity, and skipped stale impacts. | **Integrated** | The tests are focused simulation fixtures rather than a complete genre sample. |
| Projectile impacts → entity replacement/deletion → room transitions | Projectile tests cover same-name replacement, queued-hit cancellation, target movement, collision changes, and room-context cancellation. | **Lifecycle-tested** | A single scenario spanning all event families is not yet present. |
| Entity tags/kind/team → conditions → gameplay selection | Collision runtime tests cover classification queries, tag mutation, and tag conditions. | **Integrated** | There is not yet a complete inventory/quest/faction sample showing these systems in a larger design. |
| Focus/trigger relationships → entity identity → callback mutation | Focus and collision tests cover replacement, deletion, eligibility changes, deterministic nearest-target selection, and room resets. | **Lifecycle-tested** | General wildcard events are intentionally absent until filtering and dispatch costs can be bounded. |
| Room changes → waits/periodic handlers → continuation validity | task_lifecycle_tests checks cancellation on room-context changes, due waits, re-waits, successful/failed reload behavior, and periodic deadline restarts. | **Lifecycle-tested** | The scheduler needs a broader shared contract if named/cancellable tasks or scene-independent jobs are introduced. |
| Room changes → tweens, projectiles, trigger contacts, and focus | Motion, projectile, collision, interaction-focus, and room lifecycle tests cover the relevant cleanup boundaries. | **Lifecycle-tested** | Coverage is spread across subsystem fixtures; a single room-transition integration scenario would improve confidence. |
| Global/RAM variables → CSV/cache/manifest persistence → reload/recovery | Runtime and storage tests cover persistent vs RAM-only values, CSV/cache consistency, backups, malformed inputs, interrupted commits, and reload behavior. | **Integrated** | A full game save/checkpoint workflow spanning rooms, inventory lists, and UI restore is not yet a sample project. |
| Config values → script overrides → runtime settings | testConfigAndScriptOverride checks the precedence rule. | **Integrated** | A single settings contract should eventually cover defaults, reload behavior, UI scaling, and platform-specific constraints. |
| Audio asset discovery → waveform generation → audio commands | Runtime tests cover asset variables and generated waves/noise; feature examples cover import and playback requests. | **Separate coverage** | CI disables SFML audio output. A true import → preview → playback → stop/volume workflow needs a controlled audio test backend. |
| Screen effects → game-state transitions | Screen-effect tests cover individual effect behavior. | **Separate coverage** | Fade timing, room transitions, UI layering, and interruption/cancellation need a combined scenario. |
| Project scripts → CLI validation → runtime package | CI checks all script examples and packages a starter project on six targets. | **Integrated** | Number Hunt and its tutorial still need a clean download/extract/launch check using the packaged runtime. |

## v0.11.0: make the existing engine coherent

The 0.11.0 acceptance target is language and contract consistency, not a large expansion of engine subsystems.

1. Choose one canonical English/Skript-like form for every currently supported event, expression, condition, effect, loop, function, variable form, and property operation.
2. Preserve older accepted spellings as tested aliases where practical. Where an old spelling cannot be preserved, provide a clear diagnostic and migration note.
3. Make block structure and control flow predictable: nested conditions, else-if, loops, loop-local lifetimes, function returns, and waits must have explicit parser/runtime tests.
4. Make every event payload and object-reference lifetime rule explicit. Callbacks must not use stale objects or cross room boundaries accidentally.
5. Ensure every subsystem's public syntax fits the same variable, condition, object, and event-context model.
6. Provide integrated, runnable examples for the major public interaction contracts without pretending one sample proves every possible combination.
7. Keep the searchable reference pinned to v0.10.14 until the accepted v0.11.0 grammar, examples, and compatibility list agree.

## v0.12.0: broaden game-scenario composition

The larger goal—making a wide range of specific game scenarios possible and ensuring systems work well together—deserves a dedicated milestone after the language surface stabilizes.

Candidate vertical slices:

- **Exploration/adventure:** room transitions, persistent inventory/list state, named entrances, entity interaction, focus, dialogue/status UI, and saved progress.
- **Platforming/precision movement:** input sampling, fixed-tick movement, collision response, moving platforms, camera behavior, and respawn/reset lifecycle.
- **Puzzle game:** canvas controls, variable-driven puzzle state, timers, room transitions, animation cues, and save/restore.
- **Arcade simulation:** spawning/despawning, bounded entity and effect counts, collision filters, deterministic random streams, scoring, and restart.
- **Presentation-heavy game:** animation state, tween completion, screen fades, audio cues, pause/resume, and cancellation when scenes change.

These are proposed acceptance scenarios, not claims that every listed system already exists. Each should have a runnable project, explicit subsystem ownership rules, runtime tests, and a package/launch check.

## Architectural contracts to settle before expanding

- **Time and scheduling:** define the relationship between fixed simulation ticks, rendered frames, waits, periodic events, and future named/cancellable tasks. Every task must declare whether it survives a room transition, reload, pause, or object deletion.
- **World and room ownership:** distinguish persistent world objects/state from room-scoped objects, room resources, and transient event payloads.
- **Event dispatch:** define ordering, identity validation, payload lifetime, cancellation, and bounded filtering for callbacks that mutate or replace the object being processed.
- **State and persistence:** clarify which values are global, local, temporary, object-owned, or saved; define how save/checkpoint operations coordinate with room transitions and reloads.
- **UI and input:** make canvas/button events, keyboard/mouse input, focus, hit-testing, scaling, and state-driven display updates share a predictable coordinate and lifecycle model.
- **Audio:** decide whether tracks, buses, playback handles, and volume automation are needed; design them together with pause, scene changes, and asynchronous asset loading rather than adding isolated commands.
- **Assets and packaging:** use one predictable asset-resolution contract across scripts, runtime loading, import tools, and packaged projects; validate failures before partial state is committed.

## Rules for future work

- Prefer a small cross-system test over several unrelated tests when it proves a real user workflow.
- Keep focused unit/regression tests too; vertical slices do not replace precise tests.
- Test failure paths: replacement, deletion, reload, room changes, cancellation, missing assets, and invalid input.
- Record untested combinations as gaps. Do not infer integration from two green subsystem suites.
- Add a new engine concept only after identifying which subsystem owns it, its lifetime, how scripts observe failure, and how it behaves during reload and room changes.
