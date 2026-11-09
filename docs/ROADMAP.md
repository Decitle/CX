# CX Engine roadmap

This roadmap separates the current engineering checkpoint from the future product goals. It is intentionally written for people evaluating or contributing to the project, not as a promise that every feature will ship on a fixed date.

## Milestone 1 — Complete repository and reproducible builds

**Status: next major prerequisite**

- Import the complete engine implementation, CMake project, tests, tools, examples, and required documentation into the GitHub repository.
- Reconcile the repository README and project version with the exact source revision being committed.
- Standardize the SFML version and dependency acquisition path.
- Make a clean checkout build the graphical runtime and CLI using documented commands.
- Add CI for formatting/build/test and archive verification.

**Exit criteria:** a new contributor can clone the repository, build the graphical engine and CLI, run tests, and understand exactly which feature state the branch represents.

## Milestone 2 — Windows and cross-platform portability

**Status: planned; Windows code paths exist but are not yet verified**

- Build and test Windows x86_64 natively with MSVC.
- Validate save locking, safe replacement, paths, Unicode, input, fonts, and graphical behavior.
- Add Linux ARM64, Windows ARM64, and macOS x86_64/ARM64 native builds.
- Keep one shared project format across platform-specific native runtimes.

**Exit criteria:** the same starter and sample project open and run on every platform advertised in a release.

## Milestone 3 — First-run experience and structured releases

**Status: planned**

- Make the graphical runtime the obvious first-run path.
- Ship a minimal starter project with `config.json` and `scripts/main.cx`.
- Keep the CLI as a separate project-checking and automation feature.
- Publish source ZIP and TAR.GZ plus native runtime packages, checksums, build metadata, and dependency notices.
- Keep example games separate from the engine runtime.

**Exit criteria:** someone can download the matching runtime, launch a clean starter project, edit its script, validate it, and run it without first installing a compiler.

## Milestone 4 — Input, coordinates, editor, and scripting completeness

**Status: in progress**

- Finish the logical UI coordinate contract and verify aspect ratios, DPI, and display scaling.
- Complete consistent mouse and keyboard action behavior and rebinding.
- Expand Room Studio path/waypoint editing, undo/redo, validation, and round-trip coverage.
- Finish list/map and function semantics, task handles, cancellation, and timer behavior.
- Keep reload failure recovery and diagnostics covered by regression tests.

**Exit criteria:** common 2D game interactions and authoring tasks work without custom C++ patches or editor/file workarounds.

## Milestone 5 — Reusable gameplay and rendering quality

**Status: in progress**

- Expand collision from current static-tile/projectile/tween behavior toward robust moving-entity collision and simple physics.
- Improve animation state transitions, collision authoring, and entity/room ownership rules.
- Verify lighting visually and define effect/resource lifecycle behavior across reload and room transitions.
- Complete reusable UI widgets and project-owned menus.
- Build polished top-down and side-on sample scenes to expose missing engine capabilities.

**Exit criteria:** representative game slices can be authored with the same engine contracts rather than game-specific C++.

## Milestone 6 — Audio, profiling, and performance

**Status: planned**

- Add practical real-time audio playback/mixing controls and safe update/playback separation.
- Add frame/tick timing, memory, rendering, and handler-cost measurements.
- Create repeatable stress projects and profile on modest desktop hardware.
- Add culling/spatial indexing only where measurements justify them.
- Verify project packaging after moving a game to a clean machine or directory.

**Exit criteria:** performance and compatibility claims are backed by reproducible measurements and clean-machine checks.

## Version 1.0 readiness

A v1.0 label should wait until the repository is complete, the graphical first-run experience is straightforward, the shared project format is validated across advertised platforms, the editor and core gameplay systems cover common 2D needs, and every release artifact passes a clean extraction and launch test.

No calendar date is promised. The next critical path is repository completion, native Windows validation, and reproducible multi-platform packaging; broader game-engine completeness follows after those foundations.
