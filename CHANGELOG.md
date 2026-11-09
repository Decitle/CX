# Changelog

## v0.10.14-preview.5 — native runtime packaging cleanup (2026-11-09)

- Tightened starter runtime packaging to include only executable files and required non-system shared runtime libraries. SFML headers, static libraries, CMake package metadata, and SDK documentation are not copied into application downloads.
- Excluded known Windows operating-system DLLs from runtime archives. Windows provides these components; redistributing them beside the application is unnecessary and can be inappropriate.
- Added regression coverage proving Windows runtime ZIPs retain an application SFML DLL if present while excluding Windows system DLLs and development-only files.
- Changed graphical smoke tests to launch a disposable copy of the starter project, preventing generated save/lock files from contaminating the tracked example and release package.
- Added a packaging guard that refuses starter projects containing generated persistent state or temporary files.
- Local validation: 18/18 headless CTest targets and 6/6 packaging tests passed. The native Windows x64 build, CTest, starter-project check, and graphical startup smoke passed in GitHub Actions; its first package artifact was inspected and exposed the over-broad stage copy corrected here.
- The new runtime-package cleanup requires a fresh native CI run before any new runtime ZIP is treated as release-ready.

## v0.10.14-preview.3 — native CI portability corrections (2026-11-08)

- Added a portable floating-point parsing path for standard libraries that do not provide the floating-point `std::from_chars` overload. Implementations with that overload retain the original fast, locale-independent path; the fallback uses the classic locale and still requires a complete token.
- Expanded value-conversion regressions for scientific notation, fractional values, trimming rules, leading plus signs, partial numbers, overflow, NaN, and infinity.
- Added the FreeType and HarfBuzz development packages to Linux CI and release-runner setup, matching SFML Graphics' source-build dependencies. Sequential configure attempts identified both required packages; the workflow now installs both.
- Added the missing direct `<sstream>` include to `src/diagnostics.cpp`. Apple's libc++ build exposed this header-hygiene defect after the floating-point parsing fix allowed compilation to progress further.
- The first native CI run exposed the macOS libc++ portability issue; its next run compiled with the fallback. The same run showed Linux's font-stack dependencies were incomplete, prompting the FreeType and HarfBuzz workflow fixes.
- Windows ARM64 completed its native compile in the first matrix run, but its CTest, graphical smoke, and packaging steps were canceled when the follow-up source commit superseded that run. Windows x64 configuration succeeded but the build did not finish before cancellation.
- Rebuilt locally on Linux x86_64: 19/19 CTest, 6/6 package tests, and 9/9 Room Studio HTTP tests. A separate headless build forced the portable floating-point fallback and passed its runtime conversion tests.
- No engine project format, script syntax, persistent save format, license, or repository visibility change.

## v0.10.14-preview.2 — repository integration and first-run polish (2026-11-08)

- Consolidated the complete curated v0.10.14 engine source, examples, tools, tests, and public documentation for a dedicated GitHub source-import review branch.
- Added native CI targets for Linux, Windows, and macOS architectures, with an ephemeral Windows x86_64 starter-runtime artifact on successful CI runs. These non-Linux targets remain unverified until their native jobs complete.
- Kept the graphical SFML 3.1.0 runtime as the normal build product; the headless core/CLI path is an explicit build option.
- Added an actionable Linux Wayland/XWayland startup check before project scaffolding can create files.
- Improved the generated project README with run and read-only script-check commands, and added a commented `on load:` starter example without imposing game behavior.
- Expanded scaffold regressions for empty projects, partially populated projects, preservation of user files, and repeat-call idempotence.
- Verified the graphical Linux x86_64 build and all 19 CTest targets; verified the Linux starter runtime's manifest, shared-library closure, CLI check, and six-second Xvfb startup smoke.
- No public GitHub Release or v1.0 release is part of this preview.

## v0.10.13 — complete the successful script-reload state contract (2026-11-07)

- Audited `Runtime::loadScripts()` as a transactional new-session replacement, distinct from `reload room`. Documented what survives and what resets for both `loadScripts(true)` and `loadScripts(false)`.
- Fixed a persistence bug: `loadScripts(false)` now carries the live `variableStorageDirty_` flag along with ordinary globals/settings. A console edit made just before a successful no-global-reset reload is no longer left in memory without being written to the persistent save.
- Explicitly clears the old pending animation-completion scratch list when a new runtime session is committed/reset. It cannot dispatch old events into the new session.
- Added reload regressions for pending persistent saves, globals/settings versus process-only values, room/entity/light/canvas/audio/held-key replacement, log/diagnostic behavior, fresh config/save loading, and restarted periodic deadlines. Existing wait and random-stream reload tests remain part of the contract.
- Added `docs/SCRIPT_RELOAD_STATE-v0.10.13.md` and expanded the beginner C++ guide's explanation of `Runtime::loadScripts()`.
- No script syntax, project format or save-file format changed. No GitHub write was made.
## v0.10.12 — clear held input when the window loses focus (2026-11-07)

- Added `Runtime::clearHeldKeys()`, which removes every key from the host-provided held-key snapshot without generating synthetic key-press events.
- The SFML frontend now calls it on `sf::Event::FocusLost`. This prevents movement or other `key "..." is down` logic from remaining active when the operating system does not deliver matching key-release events after Alt-Tab or another focus change.
- Added a regression to `cx-runtime-tests`: two held keys are cleared together and movement stops on the following fixed simulation tick.
- Added plain-language API and learning-guide notes explaining the difference between a one-time key press and continuous held-key state. No script syntax or save format changed.
- Validation: GCC 14.2 Release build with SFML disabled, 16/16 CTest; SFML 3.1.0 graphical executable also compiled with audio disabled. A graphical Xvfb smoke check is recorded in `VALIDATION-v0.10.12.md`; this is not physical-device QA.
## v0.10.11 maintenance — global event contract and beginner C++ guide (2026-11-07)

- Clarified the existing distinction between program-wide event handlers and callbacks tied to a captured room/object snapshot. Added a regression assertion that later matching key handlers still run after an earlier handler changes rooms; no dispatch behavior was changed.
- Added function-level plain-language guidance for parser, startup/reload, event dispatch, interpreter frames, waits, variables, rooms, interaction focus, entity tweens, projectiles, collision and simulation tick ordering.
- Expanded public `Runtime` API comments and the source comment audit. Engine version, script syntax, project format and executable behavior remain v0.10.11.
- Verification: fresh GCC 14.2 Release build; 16/16 Linux CTest tests; 16 bundled script examples; HTML parsing; and Python source bytecode compilation. See `VALIDATION-v0.10.11.md`.
## v0.10.10 — collision-aware entity tweens

- Added opt-in `blocks movement` entity metadata and `ignore`, `stop`, or `slide` response modes for deterministic entity tweens. `ignore` remains the default, preserving older scripts.
- `stop` uses swept AABB checks against resolved solid room tiles, the active room boundary, and collision-enabled ordinary entities explicitly marked as blockers. Entity contacts use mutual collision masks; projectiles are not movement blockers.
- `slide` resolves horizontal then vertical travel independently and continues the tween timeline; completion locals report the actual position. `stop` cancels at the last safe tick position without a completion event.
- Kept trigger and interaction-focus lifecycle independent from physical blocking. Triggers/focus targets/decorative entities remain non-blocking unless explicitly opted in.
- Added shared swept-AABB geometry and parser/runtime integration regressions for tile tunneling, room boundaries, entity blockers, mutual mask rejection, slide coordinates, legacy pass-through, and trigger/focus coexistence.
- Limits: this is scripted tween collision response, not general physics or collision-aware input/path navigation. Moving entity blockers are sampled once at the start of each update tick; continuous moving-vs-moving resolution is future work.
- Validation: Release and Debug suites passed 14/14 each; focused ASan/UBSan collision and motion tests passed 2/2; all 15 examples passed syntax checking. See `VALIDATION-v0.10.10.md`.
## v0.10.8 — deterministic interaction focus and lifecycle-safe selection

- Added per-actor interaction ranges and explicit per-target `interactable` opt-in, with nearest eligible target selection using shared hitbox-center geometry and mutual collision-channel filters.
- Added `on entity "actor" focus enter entity "target":` and corresponding `focus exit` events, plus the condition `entity "actor" is focused on entity "target"` for script-owned input actions.
- Focus is independent of visibility and physical collision enabled state. Exact-radius candidates are included; exact distance ties resolve by normalized entity name. The engine does not choose keys, prompts, line-of-sight, or activation behavior.
- Focus relationships and dispatch are bound to runtime instance identities and room-context revisions. Callback deletion/replacement/room changes invalidate later handlers; the live focused-on query rechecks range, position, hitbox, masks and current target identity immediately.
- Focus-enter dispatch is revalidated after every handler against the actor's currently selected eligible target. If a callback changes range, position, hitbox, masks, target opt-in, or introduces a nearer candidate, later handlers for the stale enter event are suppressed. Focus-exit is treated as a completed transition but still stops on identity replacement/deletion or room change.
- Added `cx-interaction-focus-tests` and `docs/INTERACTION_FOCUS-v0.10.8.md` / `examples/interaction_focus.cx`. Linux CTest now has thirteen targets.
## v0.10.7 — named sprite animation clips and lifecycle-safe completion

- Added `define animation`, `play animation`, and `stop animation` for named clips over an entity's configured sprite sheet. Frame ranges are inclusive and zero-based; rows may be fixed or follow the existing eight-way facing order.
- Playback advances on the fixed simulation tick, with deterministic fractional-tick frame accumulation. Clips can play once or loop; one-shot playback holds its final frame for a complete frame interval and then emits one completion event.
- Added `on animation complete "clip" for entity "name":` with entity, clip, final-frame and final-row locals. Completion dispatch occurs after sprite traversal and validates runtime entity identity, playback generation and room context before each handler.
- A completion callback that restarts/stops/redefines playback, replaces the entity, or changes room context invalidates remaining callbacks from the old completion. Named clips take precedence over legacy eight-way movement animation; the legacy cycle remains the fallback when no named clip is selected.
- Added `cx-animation-tests` and `examples/animation_clips.cx`; coverage includes fixed/facing rows, exact frame timing, one-shot final-frame hold, looping, invalid definitions, missing clips, same-name entity replacement and callback-driven playback restart.
- Corrected the searchable HTML reference catalogue so the named clip entry is present once and the legacy sheet-setup entry no longer claims named clips are only planned.
- Boundaries: no blending, animation graph, automatic state selection, frame markers/hitboxes, asset importer, or animation editor. See `docs/ANIMATION_CLIPS-v0.10.7.md` and `VALIDATION-v0.10.7.md`.
## v0.10.6 — explicit room and asset tile-collision metadata

- Added optional `assets/collision.json` with a project-wide texture default and per-texture `solid`/`none` overrides. The entire manifest is validated before applying; malformed manifests warn and retain the legacy filename-based defaults.
- Added optional per-cell `tileCollision` matrices to room JSON (`inherit`, `solid`, `none`, or `null`/inherit). Explicit solid cells can block even when visually empty; explicit non-solid cells can override a solid texture. Legacy rooms without the property remain valid.
- Centralized resolved tile solidity in `RoomDefinition::isSolidTile` and reused it for actor/tile checks, projectile swept-tile checks and waypoint pathfinding. Collision remains independent of transparent visual overlays.
- Added Room Studio collision painting/visualization, inherit reset by right-click, palette defaults, undo/resize/load/save integration, and server-side matrix validation. Invalid edits do not replace the saved room.
- Added `cx-room-collision-tests`, Python Room Studio regressions, and JSON examples. Linux CTest now has 11 targets.
- Limit: this is static tile solidity metadata, not swept actor movement, dynamic collision response, per-pixel geometry, one-way platforms, tile triggers or rigid-body physics. Existing entity tweens still pass through tiles.
- Verification: see `VALIDATION-v0.10.6.md`.
## v0.10.5 — deterministic entity tweens and completion events

- Added `tween entity "name" to (x, y) over duration seconds easing ...` with `linear`, `ease_in`, `ease_out`, and `ease_in_out` curves.
- Added optional `with arc height N` for a deterministic parabolic upward offset; the apex occurs halfway through the tween.
- Added `cancel tween for entity "name"` and `on tween complete for entity "name":` with `_tween_entity`, `_tween_x`, and `_tween_y` event locals.
- Tween duration is converted to a fixed number of simulation ticks when started. Starting a new tween replaces the old one. Position assignment, manual movement, active directional input, path assignment, deletion and same-name replacement cancel it. Room-context changes discard stale tweens without firing completion.
- Completion events verify the original entity instance and scene context between handlers; deleting/replacing the entity from a handler safely stops later callbacks.
- Added `src/runtime_motion.cpp`, `cx-motion-tests`, and `examples/entity_tween.cx`. The motion module uses one compact per-entity record and constant-time interpolation per active tween; regression coverage includes invalid duration/arc rejection without replacing an in-progress valid tween. No performance claim is made without a benchmark.
- Deliberate limits: this is scripted position interpolation, not physics or swept collision; tweens pass through solid tiles and other entities. Projectiles and plain sprites are not tween targets.
- Validation: see `VALIDATION-v0.10.5.md`.
## v0.10.14 — developer experience and first-run readiness (2026-11-08)

- Expanded `/help` into a readable command reference covering every implemented runtime console utility, argument shape, graphical-terminal navigation, and CLI-only exit handling.
- Updated `cx-cli --help` so it no longer omits supported `/settings`, `/sprites`, `/paths`, or `/lights` commands.
- Added regression coverage for every advertised console utility, the `help` alias, and the 500-entry runtime log retention limit.
- Renamed the private runtime log cap member to `maximumLogEntries_` to match the actual vector-entry bound; runtime behavior is unchanged.
- Updated `docs/ENGINE_SETTINGS.md` against all 21 `EngineSettings` fields and `Runtime::applySetting`, including defaults, type/range, units, and when the graphical host applies each setting.
- Expanded the C++ learning guide with detailed beginner-oriented explanations of the developer console, settings validation, canvas layout, bounded logs, and GUI diagnostics.
- The v0.10.13 handoff source manifest contained one stale `.pyc` entry absent from the standalone source archive; the next package manifest will be regenerated directly from the exact package contents and explicitly reject cache files.
- No script syntax, save-file format, gameplay semantics, GitHub state, or repository metadata changed.
## v0.10.11 maintenance — event-dispatch contract and C++ learning guide (2026-11-07)

- Clarified the distinction between program-level events (`on load`, `on tick`, key and button handlers) and snapshot-based gameplay callbacks (triggers, projectiles, focus, tweens and animations). Program-level matching handlers continue in source order when an earlier handler changes rooms; snapshot-based callbacks validate their captured room/object identity.
- Added a room-lifecycle regression that proves a later `on press key "B"` handler still runs after the earlier handler enters another room. This locks down existing behavior; no script syntax or production event behavior changed.
- Added beginner-oriented comments to the global event dispatcher and its room-bound counterpart.
- Expanded `docs/LEARNING_THE_CPP_SOURCE.md` into a function-by-function walkthrough of parsing, runtime startup, event dispatch, interpretation, expressions, variable scopes, rooms, storage, the CLI and graphical host.
- Kept the engine version at v0.10.11 because the update changes tests/comments/documentation only; the quick binary package remains unchanged.
- Validation and clean-extraction results are recorded in `VALIDATION-v0.10.11.md`.
## v0.10.11 maintenance — room-transition lifecycle audit (2026-11-07)

- Added `docs/ROOM_TRANSITION_LIFECYCLE-v0.10.11.md`, recording active-room revision changes and the current lifecycle of entities, projectiles, tweens, animations, trigger contacts, interaction focus, lights/beams, canvases, periodic handlers, waits and transactional script reloads.
- Added `tests/room_lifecycle_tests.cpp` and `cx-room-lifecycle-tests`. The new regression proves that world entities and in-flight projectiles are retained, room-tagged effects remain tagged, canvases remain open, periodic schedules continue, and timed beams age while hidden.
- Documented one outstanding dispatch consistency question: generic event handlers currently continue through a same-event handler list after a room change, while transition-sensitive callback batches suppress stale handlers.
- No production runtime semantics, project/save formats, script syntax or engine version changed. The Linux GCC Release suite is now 16/16.
- The local SFML 3.1.0 binary development package was initialized outside the source tree; the optional graphical target built and passed a five-second Xvfb startup smoke. No GitHub writes were made in this update.
## v0.10.11 — renderer-neutral textured beams and endpoint lighting

- Added a renderer-independent `BeamEffect` model and pure beam-quad geometry builder. Endpoints are world-space pixels; width is constant; texture U repeats according to a configurable world-pixel repeat distance.
- Added `create beam`, `set beam`, and `remove beam` with candidate-copy property validation, active-room scoping, tint/opacity, three draw layers, fixed-tick lifetime, and independent endpoint-light settings.
- Integrated the SFML frontend: oblique-room endpoint projection, shared texture caching, stable world-depth insertion, and endpoint radial-light geometry in the existing cached additive light pass.
- Added `cx-beam-effect-tests`, covering geometry/UVs, invalid inputs, script authoring, endpoint-light settings, transactional property updates, texture lookup, lifetime expiry, and removal.
- Scope boundary: straight constant-width 2D quads and an optional radial endpoint light only; no volumetric lighting, bloom, shadow occlusion, curved/tapered beams, collision, or saved-game serialization.
- Validation: Release with SFML and Debug/no-zlib configurations each passed all 15 Linux CTest targets. See `VALIDATION-v0.10.11.md` for full remaining release checks.
## v0.10.9 — explicitly seeded deterministic random streams

- Added runtime-local named SplitMix64 streams using `seed random stream "name" with integer`; no implicit wall-clock seed is used.
- Added `random_float("stream")` in `[0, 1)`, rejection-sampled inclusive `random_int("stream", minimum, maximum)`, and `random_choice("stream", option1, ...)` (up to 256 options, original value type preserved).
- `random_choice` lazily evaluates only the chosen expression, preventing unselected expressions from consuming unrelated random streams or reporting unrelated runtime errors. Single-option choice does not advance its stream; equal-bound integer draws do.
- Stream states are runtime-local, reset from startup seed commands on successful reload, and kept unchanged by failed reload. They are not persisted to `variables.csv`; seed values must be safe integers.
- Added golden sequence, reproducibility, stream independence, lazy evaluation, range validation and reload-state regressions.
- Deterministic pseudo-randomness is not cryptographic and does not itself guarantee determinism for unrelated external/time-dependent game systems.
## v0.10.4 — geometric entity proximity queries

- Added `entity "a" is within 72 pixels of entity "b"` as a condition, with a variable or numeric expression for the radius.
- Distance is inclusive, center-to-center, and uses each entity's effective hitbox center or sprite rectangle center.
- Proximity queries are independent of visibility, collision-enabled state and collision layer/mask filtering; they report distance, not contact eligibility.
- Missing/non-entity references evaluate false. Negative, non-finite and non-numeric radii produce a clear diagnostic.
- Added regression coverage for exact-radius boundaries, dynamic ranges, negation, missing entities, collision-mask independence, same-entity zero distance and invalid negative radii.
- Added `examples/entity_proximity.cx` and `docs/ENTITY_PROXIMITY-v0.10.4.md`.
## v0.10.3 — named trigger enter/exit events

- Added `on entity "actor" enter trigger "area":` and `on entity "actor" exit trigger "area":` event headers with `_trigger_actor` and `_trigger_entity` local values.
- Trigger contacts require an `actors` entity and a separate `triggers` entity, collision-enabled AABBs, and mutually compatible layer/mask settings.
- Contacts are keyed by monotonic runtime instance IDs. Deleting/recreating an entity with the same name closes the old contact and opens a new one; exits are delivered before enters for replacement instances.
- Scene-context transitions clear contact state without firing cross-room exits; handler-triggered room changes stop dispatching stale transitions.
- Added lifecycle tests for one-shot enter, stable overlap, separation/re-entry, same-name replacement, event locals, mask-based exits, deletion inside an enter callback and room-context reset.
- Scans only actor/trigger pairs explicitly named by event handlers instead of checking every possible actor/trigger combination; this is a bounded design improvement, not a benchmark claim.
- Deliberate limits: endpoint sampling only (not swept trigger movement), named pairs only, and no radius query, general collision response, projectile trigger dispatch or tile-authored triggers.
- Validation: current test results are recorded in `VALIDATION-v0.10.3.md`.
## v0.10.2 — entity classifications and collision filters

- Added author-defined entity `kind`, `team`, and up to 32 tags, with script conditions to query those classifications.
- Added eight named collision channels, multi-layer assignments, and symmetric layer/mask filtering for AABB overlap queries and named-target projectile impacts.
- Added an outgoing projectile mask on the firing entity. Generated projectiles use the `projectiles` layer and inherit the owner's team and projectile mask; targets must also accept `projectiles`.
- Preserved legacy behavior by defaulting existing entity/sprite masks to `all`; solid room-tile collision remains a separate system.
- Interned classification labels per runtime. `SpriteObject` measured 440 bytes after this refinement, versus 520 bytes for the first string/set prototype; this is an object-size comparison, not a full runtime benchmark.
- Added `cx-collision-tests` and `examples/entity_collision_filters.cx`; Linux now registers nine CTest targets. The Room Studio integration test timeout is 60 seconds to avoid false timeouts on slow/loaded runners.
- Trigger enter/exit events, tag/team target selection, multiple-target acquisition, rigid-body response and advanced animated formations remain future work.
- Validation: `VALIDATION-v0.10.2.md`.
## v0.10.1 — first-run project scaffolding and reference redesign

- Added conservative runtime project initialization for an empty folder, an existing `scripts/` tree containing at least one `.cx` file (including an empty file), or one unambiguous root-level `.cx` file. The original root file is preserved and copied under `scripts/` with its original filename. Existing files are not overwritten; multiple root scripts are not guessed.
- Confirmed `main.cx` is optional. The runtime loads every `.cx` under `scripts/` recursively in stable path order; top-level statements are bootstrap, then `on load:` handlers execute. `config.json` is optional baseline configuration; script settings override it.
- Generated a minimal, comment-only starter and short config for a truly empty folder. No player-facing game dialogue, tutorial, narration or prompts are generated. Standard window/tick/UI defaults are not repeated in every project.
- Fresh projects do not write empty `variables.csv`, `variables.cxv` or `variables.meta`; persistence begins on the first actual persistent save.
- Rebuilt `docs.html` as a standalone searchable reference catalogue with 103 entries across 14 Skript-inspired color-coded categories, always-visible search, category filters, behavior/limitations and a separate learning path. The parser/runtime inventory remains subject to ongoing documentation coverage audits.
- Added `cx-project-scaffold-tests`, taking the Linux CTest suite to eight targets. The GUI font loader now skips missing optional candidates before asking SFML to load them, avoiding expected missing-file warnings on a fresh project.
- Added project-initialization and bullet-hell rendering architecture notes. Glow/bloom, shader lighting, beams, geometric emitters, orbit/release formations, entity classifications and collision masks remained future work at the v0.10.1 checkpoint.
- Validation: `VALIDATION-v0.10.1.md`.
## v0.10.0 — verify persistent save generations

- Added the versioned `variables.meta` sidecar with a monotonically increasing generation and byte-size/FNV-1a 64-bit fingerprints for the authoritative CSV and compressed cache. The fingerprints detect accidental corruption/mismatch; they are not cryptographic authentication.
- `variables.meta` is atomically replaced only after both `variables.csv` and `variables.cxv` have been committed. The previous manifest therefore continues to identify the last complete cache generation after a partial save.
- A present CSV remains authoritative, even when manually edited. When CSV is absent, manifest-backed projects now refuse cache reconstruction unless the exact cache bytes match the last committed generation. Legacy projects without a manifest remain readable and receive generation metadata on cache reconstruction or their next successful save.
- Added a dedicated `cx-storage-manifest-tests` target for generation increments/overflow, hand-edited CSV precedence, stale-cache warnings, mismatch/malformed-manifest refusal, missing-data diagnostics, and verified recovery. Splitting these cases from the broad runtime suite keeps every C++ file below 100 KB.
- Validation: `VALIDATION-v0.10.0.md`. The CSV and cache are still sequential file replacements; this is not a multi-file filesystem transaction and does not make backup pairs automatically recoverable.
## v0.9.8 — revalidate queued projectile contacts after callback mutation

- Queued projectile hit candidates now retain the projectile's AABB at the sampled contact point. Before applying each queued impact, the resolver checks that the original target instance still exists, collision remains enabled, and the target's current hitbox still overlaps that sampled contact bounds.
- If an earlier hit callback moves the target away, moves its hitbox away, or disables collision, later queued impacts against that target are discarded without diagnostics. A small move that still overlaps the sampled projectile contact remains valid.
- This is stale-candidate validation, not a new sweep: entities moved into a projectile's previously unoccupied path are not retroactively hit, and the path is not reswept after callbacks. Existing target-instance and room-context checks remain in force.
- Added regression coverage for moved-away targets, still-overlapping movement, collision disablement, and hitbox-offset mutation.
- SFML 3.1.0 Graphics/Window/System and the compact binary dependency workflow from v0.9.7 are preserved.
- Validation: `VALIDATION-v0.9.8.md`.
## v0.9.7 — exercise save interruption and recovery in separate processes

- Added a Linux-only `cx-storage-process-tests` integration target. It starts with a valid save, starts a second writer in a child process, waits until the authoritative CSV has been replaced, then uses a FIFO barrier to terminate that real writer before cache replacement. No production failpoint or public test hook was added.
- The test asserts the interrupted on-disk generation byte-for-byte: the CSV is the new complete generation, the cache remains the previous generation, and both `.bak` files retain the previous generation. A new process proves that normal startup trusts the present CSV rather than reviving the stale cache.
- The test also exercises the documented, deliberate manual rollback of the paired `.bak` files with the engine closed, then confirms a fresh process reads the prior value. Failure cleanup now kills/reaps a blocked child if a regression assertion fails.
- Expanded the storage-recovery guide with post-interruption states and safe backup restoration guidance. Kept the scope precise: process termination is tested on Linux; power-loss/fsync durability, cross-process concurrent writers, Windows process interruption, and automated restore are not covered.
- No script syntax, save format, or production storage policy changed. CSV remains authoritative whenever present; `.bak` files remain manual recovery copies.
- Added `CX_ENABLE_SFML_AUDIO` and the `build.sh --no-audio` switch so the SFML graphics/window/system runtime can be built when optional codec dependencies for SFML Audio are unavailable. Added `build.sh --sfml-dir PATH` to select a prebuilt SFML CMake package explicitly.
- Verified SFML 3.1.0 using the supplied Linux x86_64 GCC binary distribution: CMake found Graphics/Window/System, compiled and linked `cx-engine` plus the X11 file-drop bridge, and the resulting executable had no unresolved shared-library dependencies with the SFML library directory on `LD_LIBRARY_PATH`. SFML Audio was disabled for this check because this runner does not provide `libFLAC.so.12`.
- A headless Xvfb launch remained alive for the four-second smoke interval without project startup errors. This is not a physical-display rendering/input validation. A deprecation warning remains for `sf::Text::findCharacterPos` under SFML 3.1; its behavior is retained for compatibility with the configured SFML 3.0.2 fetch fallback.
- Validation: `VALIDATION-v0.9.7.md`.
## v0.9.6 — bind suspended waits to their room context

- `wait` continuations now capture the active room-context revision at suspension. Successful room transitions, active-room reload/unload, and committed script/runtime reloads invalidate waits suspended in an older context.
- Continuations are checked when dequeued and immediately before each ready continuation runs, so an earlier due continuation cannot change rooms and let later same-tick continuations run stale. Invalid pending contexts are removed without waiting for their deadline.
- A continuation can transition rooms and continue normally; if it later reaches `wait`, it captures the new room context. Waits created during successful staged `on load` are rebased to the committed epoch, while pre-reload waits are replaced along with the previous runtime as before.
- Added regression coverage for pending cancellation, same-tick ready queue invalidation, a second wait after transition, staged `on load` wait retention, and failed-versus-successful reload wait behavior. No new syntax or save format.
- Validation results and limits: `VALIDATION-v0.9.6.md`.
## v0.9.5 — cancel stale queued impacts after active-room context changes

- Added a runtime-owned room-context revision that remains monotonic across transactional script/runtime reload commits, independent of the staged `RoomManager` revision counter.
- Successful room entry, active-room reload, and active-room unload invalidate pending projectile impacts gathered from the prior scene context. Operations on non-active rooms do not invalidate the active context; failed operations do not advance this revision.
- The hit callback that changes context completes normally. Once it returns, later matching handlers for the same event and other impacts queued by that projectile update are discarded before stale damage/callback work can run.
- Added regressions for room entry, active-room reload, and active-room unload, including same-event multi-handler cancellation; a complementary case proves that reloading a non-active room does not invalidate queued hits. The tests also verify the handler continues after the transition statement.
- No new CX syntax, script-facing API, save format, or collision solver was added. Swept collision remains bounded sampling, and projectiles/entities are not globally cleared merely because a room changes.
- Validation: Release build and all four CTest targets passed with SFML disabled; exact packaged-archive and additional configuration results are recorded in `VALIDATION-v0.9.5.md`.
## v0.9.4 — specify sampled wall-versus-target precedence

- Made the existing per-sample collision precedence explicit in `runtime_projectiles.cpp`: the earliest sampled contact along the path wins; at a single sample, a solid tile is checked before the named target, so the wall blocks the target hit when both overlap there.
- Added `testTargetHitBeforeWallUsesEarlierSampledContact` and `testSolidWallWinsWhenTargetOverlapsSameWallSample`. The former checks that a target reached before a farther wall is hit; the latter checks that the wall wins a same-sample overlap.
- This is a contract-and-regression hardening release, not a new collision solver. The movement path is still tested with bounded samples; contacts between sample positions are approximate, and this release does not claim analytical continuous collision detection.
- No CX syntax, public API, projectile target-binding behavior, or save format changed.
- Verification: GCC 14.2 Debug with zlib, GCC 14.2 Release with zlib, and GCC 14.2 Debug with zlib disabled each pass all four CTest targets. The exact source ZIP must also pass a fresh Release build/CTest after packaging; results and environment limits are recorded in `VALIDATION-v0.9.4.md`.
## v0.9.3 — instance-safe deferred projectile impacts

- Added a monotonic runtime-instance ID to sprite/entity objects. Script-visible names can be reused after deletion, but deferred work can now distinguish the original object from a replacement with the same name.
- Queued projectile impacts record the exact target instance they collided with. If an earlier hit callback deletes that target and creates a new entity using the same name, later queued hits against the old object are discarded instead of damaging the replacement.
- Single-target projectiles also capture the target instance at launch. If that object is removed or replaced during flight, the projectile is discarded on its next update instead of reacquiring a new object by name. This keeps shot targeting predictable and frees an active-projectile slot promptly.
- Added `testQueuedHitsDoNotTransferToReplacementEntityWithSameName` and `testInFlightProjectileDoesNotRetargetReplacementEntityWithSameName`. The queued-impact test was first run against the v0.9.2 logic and failed for the expected reason; both tests pass after instance identity was added.
- The identity counter is reset with fresh runtime state, copied when a staged script reload successfully commits, and assigned whenever a script creates/replaces a sprite or the engine spawns a projectile.
- No new scripting syntax or project API is introduced. Existing entity and projectile behaviors remain name-addressable from scripts.
- Verification: GCC 14.2 Debug with zlib, GCC 14.2 Release with zlib, and GCC 14.2 Debug with zlib explicitly disabled each built and passed all 4 CTest targets. Seven focused examples plus the starter and two demo projects passed CLI syntax checks. The optional SFML target was disabled; platform and test limits are in `VALIDATION-v0.9.3.md`.
## v0.9.2 — chronological projectile impacts and correct lifetime boundary

- Resolve queued projectile impacts by the sampled time they contact their named target, not by lexicographic projectile ID. A stable sort keeps same-sample ties deterministic.
- Move a projectile through its remaining lifetime during the final tick and test that segment for target/tile collision before removing it. Previously, the update could expire a projectile before applying its last movement step, shortening the usable range by up to one tick and skipping an end-of-life impact.
- Added regression coverage proving three shots that arrive at different points within a single tick resolve nearest/earliest first, and that a shot can hit on its lifetime-ending tick.
- No new script syntax or configurable lifetime was introduced. Swept collision remains bounded sampling; target impact order is based on sampled time, not an exact continuous-collision solution.
- Verification: GCC 14.2 Debug with zlib, GCC 14.2 Release with zlib, and GCC 14.2 Debug with zlib disabled each passed CTest 4/4. The optional SFML target was disabled in this environment; see `VALIDATION-v0.9.2.md`.
## v0.9.1 — deterministic projectile hit resolution and collision/visibility separation

- Changed shared AABB overlap semantics so `visible` controls drawing only; invisible objects remain collidable unless collision is independently disabled.
- Separated projectile impact detection from damage/event execution. The runtime gathers impacts while traversing sprites, removes consumed shots safely, then applies damage and runs `on projectile hit entity` handlers afterward.
- Damage now resolves one impact at a time in stable projectile-name order. Each callback observes health immediately after its own hit. Later queued impacts are skipped if an earlier hit has defeated the target or an earlier callback has removed it.
- Added regression coverage for hidden targets that remain valid projectile targets and three simultaneous hits against a two-health target (including per-hit health visibility, target deletion and suppression of stale death callbacks).
- Fixed generated projectile IDs colliding with user-authored sprite names such as `projectile_1`. Spawn now skips occupied IDs and counts only successfully inserted projectiles, keeping the global cap synchronized with live objects. A regression also checks deletion releases one slot and later shots continue with unique IDs.
- No generalized collision masks, arbitrary target filters, analytical continuous collision solver or new projectile-pattern shapes were added. The existing 64-shot pattern and 4096-live-projectile limits remain unchanged.
- Verification: GCC 14.2 Debug with zlib passed CTest 4/4. Clean-extraction Release and no-zlib builds are also tested; see `VALIDATION-v0.9.1.md`. The SFML target was disabled for these core tests and is not freshly verified in this environment.
## v0.9.0 — first reusable projectile attack patterns and swept projectile collision

- Added a small, renderer-independent pattern geometry module for radial and fan projectile directions. The radial pattern distributes shots around a full circle from the target-aim direction; fan patterns are distributed over a bounded 0–180 degree arc.
- Added script syntax for radial/fan bursts, with parser checks for counts, spread, speed, and damage. Pattern creation validates the complete command before inserting any projectile.
- Split the former large runtime file into `src/runtime_statements.cpp` (statement execution), `src/runtime_expressions.cpp` (value/condition evaluation), and `src/runtime_projectiles.cpp` (projectile commands and movement/collision). Pure geometry lives in `src/attack_patterns.cpp`. This keeps the primary source files focused, below 100 KB each, and easier to study.
- Added sampled swept collision against solid tiles and the named target, fixing cases where a fast projectile could jump past a narrow target or tile between tick endpoints. The hot loop compares object references rather than repeatedly normalizing names and searching the sprite map.
- Added a maximum of 64 shots per pattern and 4096 simultaneously active projectiles. Over-cap patterns are rejected as a whole with an actionable diagnostic; warnings are de-duplicated until there is enough room for the rejected volley, preventing repeated `on tick:` log flooding. Projectiles are counted down when deleted, expired, hit, or replaced.
- Reserved the deferred-removal and hit-work vectors from the bounded live-projectile count once per update, avoiding repeated vector growth when dense volleys expire together. This is a low-risk allocation reduction, not a measured throughput claim.
- Added separate pure-geometry and projectile-runtime test binaries, keeping focused tests out of the long general runtime test source. Added `examples/projectile_patterns.cx` and `docs/PROJECTILE_PATTERNS.md`.
- Remaining limits are explicit: no projectile perimeter placement, time-varying/orbiting/expanding formations, general target filters/collision masks, exposed wall bypass or separate target/wall persistence settings yet.
- Verification: GCC 14.2 Debug with zlib and without zlib, plus GCC 14.2 Release with zlib, each built and passed CTest 4/4; `examples/projectile_patterns.cx` passed CLI syntax validation; embedded documentation JavaScript passed `node --check`. SFML is disabled/unavailable in the audit environment; run GUI smoke tests on the target device after graphical changes.
## v0.8.9 — persistent-save failure-injection coverage

- Added deterministic filesystem fault-injection tests for failure while preparing the CSV backup, failure while preparing the compressed-cache backup, and failure opening the cache temporary file after the authoritative CSV has committed.
- The tests avoid permission-bit tricks that behave differently under privileged CI. They use blocked directory destinations at specific write stages and assert the original files, backup files, and fixture contents are preserved.
- Added a fresh-load recovery assertion after a deliberately partial save: the next load reads the newly committed authoritative CSV, while the older compressed cache and both `.bak` snapshots remain available for manual diagnosis/recovery.
- No save-format or normal successful-save behavior was intentionally changed; this is a test-and-contract milestone that tightens our evidence around v0.8.8 behavior.
- Verification: GCC 14.2.0 Debug core/CLI/test builds pass with zlib enabled and explicitly disabled; each CTest run passed 2/2 suites. SFML was disabled for persistence-focused verification.
## v0.8.8 — persistent-variable CSV integrity and one-generation recovery backups

- Fixed a CSV round-trip bug that double-escaped backslashes after string escaping, corrupting values containing real newlines, literal `\n` / `\r` sequences, and path-like backslashes across save/load cycles.
- Made quoted CSV parsing reject stray text after a closing quote and quotes embedded in unquoted fields instead of silently interpreting malformed rows.
- Aggregated line-specific CSV warnings (showing up to eight rows plus a count for further errors), validates `none` rows, trims numeric fields, and resets stale warning/error strings on successful calls.
- Preserved the CSV-authoritative rule: malformed CSV rows can be skipped with a warning, but the stale cache is never used to resurrect deleted variables. A malformed decoded cache table now fails recovery and does not fabricate a partial `variables.csv`.
- Added a 256 MiB file-read bound for CSV/cache files, propagates path-inspection errors rather than treating inaccessible files as absent, and reports when the CSV was committed but cache replacement failed.
- Before replacing an existing regular save, atomically stores the previous CSV/cache bytes in `variables.csv.bak` and `variables.cxv.bak`. These one-generation copies are manual recovery aids; automatic backup restoration remains a future decision. Runtime startup no longer immediately overwrites a malformed authoritative CSV merely to refresh the cache.
- Added regression tests for exact string/name round-trips, malformed row aggregation, malformed-cache-table rejection, one-generation backups, partial save reporting, and retaining malformed CSV bytes during warning-only startup.
- Verification: GCC 14.2.0 Debug builds with zlib enabled and explicitly disabled each passed CTest 2/2. SFML was disabled for persistence-focused tests; Windows and Release remain unverified in this environment.
## v0.8.7 — portable compressed-cache recovery

- Fixed a reproduced persistence bug in the no-zlib build: the fallback writer emitted codec marker `0`, while the loader rejected marker `0` before reaching its own fallback decoder. Existing tests failed when `variables.csv` was absent and the engine tried to recover from the cache.
- Defined the cache header marker as a codec identifier: `0` is the built-in PackBits-style codec and `1` is zlib/DEFLATE. The reader now supports codec 0 in both zlib-enabled and no-zlib builds; codec 1 gives a clear error when zlib is unavailable.
- Tightened fallback decoding bounds and rejects truncated packets, output beyond the declared size, and trailing compressed payload rather than accepting damaged cache data.
- Added `testPortableVariableCacheLoadsInEveryBuild`, which creates a fallback-format cache fixture and checks restored string/number values, CSV reconstruction, and truncated-cache failure.
- Verification: reproduced the failure against the pre-fix code in a no-zlib Debug build (CTest 1/2); after the fix, both no-zlib and zlib-enabled Debug builds pass CTest 2/2. The SFML target was disabled for these storage-focused builds and was not re-tested.
- The CSV remains authoritative when present. This release does not add a complete backup scheme, crash-durable two-file transaction, cross-process lock, or automated repair of malformed CSV rows; those remain open roadmap work.
## v0.8.6 — educational source and beginner language path

- Added a commented first-steps script and a beginner-first CX Script guide emphasizing simple events, clear variable lifetime, and the difference between one-time input events and continuous tick updates.
- Added a guided C++ source-reading path and glossary so a beginner can move from CX Script to the implementation gradually.
- Expanded comments around public data structures and the parser/runtime/storage/room/audio/diagnostic/frontend boundaries. Comments focus on data flow, lifetime, failure behavior, and non-obvious algorithms rather than narrating every line.
- Added a source-commenting standard. Future code changes must update relevant comments and the comment audit in the same task; comments are not a substitute for tests.
- No intended runtime or language behavior changed in this educational pass.
- Verification: GCC 14.2.0 Debug core/CLI/tests build succeeded; CTest passed 2/2; all six focused script examples passed CLI checks; embedded documentation JavaScript, Python syntax and shell syntax checks passed. The exact source ZIP was also cleanly extracted, rebuilt, and passed CTest 2/2. SFML target was not built in this environment; the project owner reports that the graphical runtime works on their device, while feature-specific GUI tests remain a separate gate.
## v0.8.5 — non-destructive persistent-file replacement failure

- Removed the fallback that deleted a destination after `rename` failed and then retried. A failed replacement now reports an error and cleans up the temporary file without intentionally deleting the existing destination.
- On POSIX, replacement continues to use same-directory `std::filesystem::rename`, which replaces an existing regular file atomically. On Windows, use `MoveFileExW` with replacement/write-through flags rather than deleting the old file first.
- Added `testVariableStorageSavePreservesUnexpectedDestination`: a `variables.csv` directory containing a sentinel file causes the save to fail; the directory and contents must remain intact and the temporary file must be cleaned up.
- This is a narrow filesystem-safety fix. It does not yet solve truncated/corrupted CSV recovery, cache divergence after a partial two-file save, directory fsync/power-loss guarantees, concurrent writers, or stale-cache policy. Those remain on the persistent-storage roadmap.
- Verification: Clang 17 Debug `cx-cli` and `cx-tests` build succeeded; CTest passed 2/2. Existing three signedness-conversion warnings remain. Windows API code is source-reviewed but not built/tested on Windows in this environment; SFML GUI target was not rebuilt here.
## v0.8.4 — transactional script initialization and runtime-error reload rollback

- Changed script loading to build and initialize a candidate `Runtime` separately. A syntactically valid candidate that raises a runtime error during bootstrap or an `on load` handler no longer replaces the currently active program, variables, settings, rooms, entities, lights, audio state, or scheduled executions.
- Prevented the candidate runtime from writing persistent variables before startup succeeds. On successful startup, persistent variables are saved after state commit.
- Staged `save wave` and `visualize wave` outputs in a temporary directory during candidate startup, then prepared destination-side temporary files and backup/rename steps before committing generated outputs. Failed loads discard staged outputs; commit failures attempt to restore prior files and keep the old runtime active.
- Added `testFailedRuntimeReloadPreservesRuntimeAndFiles`, which checks failed bootstrap rollback, continued execution of the old tick handler, persistent-save preservation, and preservation of an existing WAV destination after the candidate attempted to overwrite it.
- Verification: fresh Clang 17 Debug build of `cx-cli` and `cx-tests` succeeded; CTest passed 2/2. All five focused `.cx` examples and all three project archives passed CLI syntax checks. Three pre-existing sign-conversion warnings remain. GCC Release build and a fresh SFML build were not verified in this environment.
- Graphical-runtime status: project owner reports the existing graphical runtime launches and works as expected on their target device; this release's container verification did not re-build the SFML target.
## v0.8.2 — Eclipse Cathedral showcase project

- Added `demo-eclipse-cathedral/`, a new standalone playable project centered on the current native lighting, room, entity, path, projectile, and canvas systems. The earlier `demo-canyon-boss/` project remains intact and separate.
- Added 82 hand-built pixel-art texture assets, including two 4×8 animation sheets, a portal, projectiles, decorative tiles, aligned 4×4 / 6×6 transparent sigil mosaics, and cached autotile edge/corner overlays.
- Added two 40×24 rooms with authored overlay layers and texture scales, a multi-room transition, waypoint-patrolling two-phase boss, solid cover pillars, projectile hit callbacks, health bars, victory/return flow, colored radial/conical lights, and a Lighting Lab for ambient intensity, light quality, and lighting toggles.
- Added `run-eclipse-demo.sh`, feature map documentation, and an updated roadmap/docs/CLI/CMake version for the v0.8.2 source bundle.
- Validation: v0.8.2 core/CLI build and CTest are run on the packaged tree; both showcase and older demo scripts pass syntax checks; the new CLI runtime smoke test enters the arena, triggers phase II, defeats the boss, returns to the gallery, and toggles lighting. The SFML graphical target remains unverified in this environment because SFML 3 is unavailable.
## v0.8.1 — safe room reload and Room Studio validation

- Made `reload room` transactional: the candidate file is fully read and validated before replacing the live room. Invalid or partially written room files now leave the currently loaded/active room, entrance, and room revision intact so the user can repair the file and retry.
- Hardened the Room Studio HTTP API: room dimensions and entrance coordinates must be integer JSON values (booleans/fractions are rejected rather than silently coerced), and room saves use unique same-directory temporary files followed by atomic replacement.
- Added a C++ regression test for failed/successful room reload transitions and Python HTTP integration tests for room save/load round-trips, overlay stacking/layers, projection, scales, entrances, and invalid numeric input.
- Fixed stale v0.7.0 CLI version banners, added direct includes required by the SFML frontend, and synchronized current release/version references in the docs and bundled demo instructions.
- Verification in this environment: C++ core/CLI builds and CTest suites pass; graphical SFML 3 target remains uncompiled because SFML 3 is not installed, so graphical runtime behavior is still pending target-machine validation.
## v0.8.0 — reload safety, room overlay tools, oblique projection and docs polish

- Syntax-invalid `/reload` is transactional: parsing happens before the active program is replaced, so a syntax error keeps the running handlers and live runtime state. Added regression coverage for this guarantee.
- Added `/settings`, `/sprites`, `/paths`, and `/lights` runtime inspection commands and documented the utility terminal commands.
- Added room `projection` metadata (`orthogonal` or `oblique` / isometric alias), consistent logical grid coordinates, transparent tile overlay records, and support for multiple different overlays per cell and layer.
- Expanded Room Studio with projection selection, overlay painting/erasing/layer selection, previewed oblique diamond cells, transparent overlay preview, save/load round-tripping, and preservation of projection in the UI when switching rooms.
- Added `assets/autotiles.json` plus transparent edge, outer-corner, and inner-corner texture assets. Room batch rebuilds cache the generated terrain seam overlays; no autotile calculation runs every frame.
- Clarified draw-order semantics: `background` is over base tiles but behind entities (terrain seams), `foreground` is depth-sorted with entities, and `above_entities` covers all entities.
- Updated the searchable documentation for publication as a static website and added detailed engine-setting and website-publishing guides.
- Core build and automated tests pass; root, starter, and Lanternfall scripts pass syntax checking. The SFML graphical target could not be compiled/visually verified in this environment because SFML 3 is unavailable.
## v0.7.0 — Lanternfall, first world/entity layer

- Added world viewport height in tile units (`engine::world_view_height_tiles`) and camera follow (`engine::camera_follow`); the world view width follows window aspect ratio rather than stretching the scene.
- Added sprite-backed entities with health, max health, configurable offset AABB hitboxes, 8-way spritesheet animation, and continuous WASD movement in pixels per second.
- Added named tile waypoint paths, speed in tiles per second, and low-overhead four-neighbor BFS obstacle routing around solid tiles.
- Added directed projectiles with target-at-launch aim, solid-tile obstruction, expiry, damage, and `on projectile hit entity` callbacks.
- Added runtime cover placement/clearing and added the **Lanternfall: Canyon Warden** demo: grassy canyon, worn dirt trail, light-marked star objective, room transition, boss patrol, cover stones, projectile combat, health UI, flash effects, and a script-defined F2 view/settings menu.
- Updated `README.md`, `docs/SYNTAX.md`, `docs/ARCHITECTURE_ROADMAP.md`, and the offline searchable `docs.html` to distinguish present capabilities from future work.

### Verification

The dependency-free core/CLI compiles and all core runtime tests pass. The demo project passes `cx-cli --check`, and a CLI smoke run triggered the star overlap and changed rooms to `boss_arena` with the Warden created. The actual SFML graphical target could not be built/visually tested in this environment because SFML 3 is unavailable; please run the Arch graphical build before relying on the demo's final visual behavior.
## v0.6.2 — compact terminal and logical-resolution viewport

- Changed the in-game terminal shortcut from Ctrl+Alt+T to Ctrl+Tab. It now appears as a compact black panel sliding down from the top; removed the permanent bottom terminal strip and Tab-to-focus behavior. Escape closes the open drawer. `engine::console_enabled` still disables the in-game terminal entirely.
- Made the clean dark game view the default by disabling the debug overlay by default. Room tiles now start at world coordinate (0, 0) instead of reserving space for the old engine header/terminal.
- Added a logical world viewport using `engine::ui_reference_width` and `engine::ui_reference_height`, uniformly scaled to fit the actual window without stretching sprites/tiles; differing aspect ratios use letterboxing. Screen-space canvas UI remains separately resolved against the actual window.
- Added an F2 Display and UI Settings canvas to the bundled project and starter: 1280×720, 1600×900, and 1920×1080 presets; responsive resizing; UI scale presets; and VSync toggle. These settings apply immediately for the session, while `config.json` remains the next-launch baseline.
- Added F1–F12 key-name mapping for graphical key events and regression tests for script-disabled terminal settings and UI-driven window settings.
- The core, CLI, and tests build cleanly; root and starter scripts pass syntax checks. The optional SFML GUI remains uncompiled in this environment because SFML 3 is not installed.
## v0.6.1

- Added minimal named sprite objects in the runtime: create, position, size, texture replacement, visibility, movement, and deletion.
- Added key-down/key-up state and `if key "W" is down:` conditions for continuous movement from `on tick:`.
- Added `engine::show_debug_overlay` so a game can use a clean blank-void background instead of the engine label and diagnostic grid.
- Added `starter-project/`, a minimal textured player project with WASD movement, reset, hide/show, and the Ctrl+Alt+T terminal shortcut.
- Expanded runtime tests for texture-backed sprite objects and held-key movement.
- Graphical SFML target remains unverified in this build environment because SFML 3 is not installed here.
## v0.6.0 — collections, functions, responsive canvas layout

- Added list variables with indexed and named keys, list literals, list copying, append/remove/clear/delete operations, list wildcard iteration, and `loop-value` / `loop-index`.
- Added synchronous user-defined functions with positional parameters and `return`, plus arithmetic expression parsing with parentheses and standard precedence.
- Added a renderer-independent canvas layout resolver for 0–128 grid positions, nine anchors, relative panel/button placement, viewport-responsive sizing, and uniform global UI scaling.
- Added responsive/fixed resolution settings and the SFML frontend's resize path.
- Added the graphical terminal drawer hotkey (`Ctrl+Alt+T`) with cursor insertion/editing, Home/End, Delete/Backspace, history, and Escape close behavior.
- Expanded `docs.html` with a full project tree, script/file discovery order, how assets become variables, terminal controls, and the roadmap for 3D/audio/plugin architecture.
- Added architecture notes on entities/world simulation, separating a future 3D renderer from the 2D path, mixer buses, native extension safety, and VST3 host requirements.
- Made relative placement accept both natural concise forms such as `above button "hotbar"` and explicit forms such as `to the left of button "map_btn"`.
- Added regression coverage and example projects for list/function behavior and responsive UI placement.

### Verification

The dependency-free core and CLI were clean-built in Release mode. The runtime test suite passes, the root project scripts and every `.cx` example pass syntax checks, the list/function example executes with the expected output, the room-editor Python source passes bytecode compilation, and the searchable offline docs' embedded JavaScript passes `node --check`.

The optional SFML graphical target was not compiled or visually exercised in this environment because SFML 3 is not installed. Consequently, the graphical terminal shortcut and resizable-window behavior still require local build/runtime validation. Full entity/physics/world simulation, mixer routing/automation, native module loading, and VST hosting remain future work.
## v0.10.11 maintenance — clean-extraction lifecycle audit closeout (2026-11-07)

- Rebuilt the exact refreshed source ZIP from a clean extraction and passed all 16 CTest targets, including the new room-lifecycle regression.
- Checked all 16 bundled example scripts together (43 event handlers, 2 canvases), plus the separate Cave-to-Arena sample (8/8 structural tests and CLI validation).
- Reconfirmed the optional SFML 3.1.0 Graphics/Window/System target builds from the local minimal development package with Audio disabled, resolves linked SFML libraries, and starts under Xvfb.
- No production runtime source or quick executable changed. GitHub remained read-only in Development 02; the local audit/test contribution is disclosed in `docs/CHATGPT_CONTRIBUTIONS.md`.
- The next numbered session is dedicated to a substantial Cave-to-Ancient-Arena prototype-game quality pass, based on the user's feedback that the current screenshots are not satisfactory.
