# Roadmap

**Product goal:** make a lightweight, reusable 2D game engine whose scripting workflow feels familiar to Minecraft Skript users: readable event-driven commands, approachable variables and functions, quick feedback, and no C++ requirement for ordinary game logic. CX is not a Minecraft server API or a feature-for-feature Skript clone. It should be more capable in its intended 2D-game domain, with its own clear and well-tested language contracts.

The roadmap is dependency-ordered, not a delivery-date promise. A feature is not complete merely because its parser accepts an example.

## Current checkpoint — v0.10.14-preview.8

- Preview.8 is published with six native runtime packages: Windows, Linux and macOS, each x86_64 and ARM64.
- The all-target native build/test workflow passed in [run 42959350359](https://github.com/Decitle/CX/actions/runs/42959350359).
- The tagged package assembly, archive verification and release publication passed in [run 42961733306](https://github.com/Decitle/CX/actions/runs/42961733306).
- The version-matched reference attached to Preview.8 is currently named `0_CX-Docs-v0.10.14-preview.8.html`. The project owner renamed that asset manually after it was published. The new release generator/verifier change makes this naming automatic for future tags.
- The post-Preview.8 download-page and release-reference changes are on `main`; their new CI runs must pass before a future release tag is created.

## Established engine foundation

### Language and runtime

- Source-aware parsing and file/line diagnostics; candidate script reload is transactional.
- Persistent globals `{name}`, execution-local `{_name}`, and process-lifetime temporary `{^name}` values.
- Conditions, arithmetic/boolean expressions, string interpolation, lists, functions, counted/condition/list loops, `exit loop`, `continue loop`, `wait`, and periodic `every` handlers.
- Input, lifecycle, entity, trigger, tween, animation and UI-related events.
- Explicitly seeded, deterministic named random streams: `random_float`, inclusive unbiased `random_int`, and type-preserving `random_choice`. Stream isolation, validation, reload behavior and golden vectors have regression coverage. This is pseudo-randomness, not cryptographic randomness; stream state is currently transient rather than saved with a game.
- CLI syntax validation and runtime utilities, bounded diagnostic/log storage, project scaffolding and persistent-variable recovery.

### 2D engine systems

- Project/room loading, texture and audio asset discovery, sprites/entities, hitboxes, collision channels, tile collision metadata, triggers and interaction focus.
- Scripted entity motion/tweens, named animation clips, projectiles and tested radial/fan patterns.
- Screen-space canvas UI, lighting meshes, textured beams and endpoint lighting.
- Optional SFML graphical host kept separate from the graphics-independent core and CLI; Room Studio is an optional localhost authoring tool.

These are substantial foundations, not a claim that every general-purpose 2D workflow is already finished. The individual syntax and feature guides define the actual guarantees and limitations.

## Active milestone — beginner-first onboarding and first-project success

**This is the next firm goal.** Before adding another large subsystem, prove that a new user who knows Minecraft Skript can get CX running, understand the project structure, change a script, see the result, and recover from a mistake without needing C++ or a compiler.

### Work items

1. **One clear download route**
   - Put a prominent Download button beside the Documentation and GitHub routes in the README.
   - Provide a small Pages download page with browser-based OS detection, a visible manual OS/architecture selector, and one direct runtime download link.
   - Keep checksums and source snapshots available on GitHub Releases, but do not present a `.sha256` file as a primary download choice.
   - This page is now on `main`; verify the deployed page, API lookup, target selection and fallback behavior before calling it complete.

2. **Exact first-run instructions**
   - Keep the package layout and launch commands identical across the README, download page, Getting Started guide and generated runtime README.
   - Test a clean extraction: the runtime is launched against the sole `CX-Engine/` folder, where the executable and starter project are colocated.
   - Make the distinction between a prebuilt runtime, a source checkout and a game project unmistakable.

3. **A small guided first game**
   - Add one complete, intentionally small 2D example project with annotated CX Script.
   - Teach the user to find the script, change a variable or input action, use a condition and a loop/function, run the CLI checker, then launch the graphical runtime.
   - Keep it a real runnable project, not a long list of isolated command snippets. Do not inject generic tutorial dialogue into the engine; all in-game copy belongs to the example game.

4. **A tested new-user path**
   - Test an empty project folder and a partially populated project folder; existing user files must remain untouched.
   - Exercise script errors, source/line diagnostics, correcting the error, CLI validation and successful/failed reload.
   - Walk through the same steps from a clean runtime package, not only from a developer build.
   - Record exact platform and display limitations. Native CI is not the same as physical-device visual/input QA.

### Exit criteria

A beginner can start from the Download button, select or confirm their system, extract and launch the runtime, understand where the project and script live, make a small meaningful change, validate and run it, and fix a deliberately introduced syntax error by following the diagnostic. No compiler, C++ source changes or manual library setup should be required for this path.

## Next engineering stages

### Scripting completeness and maintainability

- Named task handles and explicit task cancellation, with stable rules for reload, room transitions, completion and error propagation.
- Lightweight module/import boundaries and predictable name resolution across multiple scripts.
- Clearer value/type/schema diagnostics and deliberate serialization rules for nested or map-like data.
- A documented event-payload convention and a consistent set of common string, numeric and collection helpers.
- Decide whether save-game/replay use cases require persisted random-stream state or a saved world-seed/subsystem-seed contract.

Do not add these as a broad language redesign. Each needs a small script example, an explicit behavior/lifetime/error contract, and regression tests.

### Gameplay and authoring depth

- General moving-body physics and broader collision response beyond the current specialized tile avoidance, tween and projectile collision paths.
- Consistent logical UI coordinates and DPI/aspect-ratio behavior on physical displays.
- More complete Room Studio path/waypoint editing, undo/redo and lossless round-trip coverage.
- Reusable menus/widgets and polished top-down and side-on sample slices that expose gaps in shared engine APIs.

### Audio, performance and platform quality

- Practical multi-voice audio playback/mixing and script-level control; keep real-time callback work allocation-free and separate from script execution.
- Frame/tick, allocation/memory, rendering and handler-cost profiling.
- Repeatable low-end hardware benchmarks, including the project's modest Linux Chromebook target; do not infer performance from CI runners.
- Physical-device and display/DPI testing on supported operating systems. Linux Wayland currently requires XWayland; the native Wayland backend is not implemented.
- Broader clean-machine installation and project-transfer testing.

## v1.0 readiness gates

A stable 1.0 should wait until all of these are true:

1. The beginner-first onboarding and first-game path passes the exit criteria above.
2. Common top-down and side-on 2D game workflows can be built mostly through shared CX APIs and scripts without one-off engine workarounds.
3. Scripting task/module/data semantics have explicit contracts and reliable error handling.
4. Input, UI coordinates, collision and rendering behavior are tested on real supported devices, not only CI.
5. Audio and performance claims are backed by repeatable measurements.
6. Every release package passes all-target build, tests, clean extraction, launch and version-matched documentation verification.

No percentage-complete or fixed delivery date is implied by the version number.
