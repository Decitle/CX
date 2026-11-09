# Roadmap

This roadmap describes the work needed to make CX a dependable general-purpose graphical 2D engine. It is not a promise of fixed delivery dates.

## 1. Repository and reproducible builds — current priority

- Keep the full engine source, build configuration, tests, focused examples, and public documentation in the repository.
- Make a clean checkout build the graphical application and CLI.
- Pin dependencies and add native CI for Linux, Windows, and macOS.
- Keep source distributions free of handoff files, logs, generated builds, and compiled executables.

**Exit:** contributors can clone the repository, build the graphical engine, run the tests, and understand the project status.

## 2. Platform portability

- Establish Windows x86_64 as a native tested target.
- Add Linux ARM64, Windows ARM64, and macOS x86_64/ARM64 validation.
- Test persistent storage, paths, encoding, fonts, input, display scaling, and native runtime dependencies.
- Confirm that the same project data opens across validated targets.

**Exit:** each advertised target passes native build/test/launch checks.

## 3. First-run workflow and structured releases

- Polish the starter project and first-run messages.
- Publish source ZIP/TAR.GZ and per-platform starter runtime packages.
- Include the graphical runtime, CLI, config, main script, dependency notices, and checksums.
- Keep full sample games separate from the engine runtime.

**Exit:** a new user can download a matching runtime, open the starter project, edit a script, check it, and run it without installing a compiler.

## 4. Input, editor, and scripting completeness

- Complete a consistent logical UI coordinate contract across aspect ratios and DPI scales.
- Improve mouse/keyboard action handling and rebinding.
- Expand Room Studio path/waypoint tools, undo/redo, validation, and round-trip coverage.
- Finish list/map and task/timer semantics, cancellation, and clearer errors.

**Exit:** common authoring and interaction tasks do not require game-specific C++ workarounds.

## 5. Gameplay and rendering depth

- Expand moving-entity collision and simple physics beyond the current specialized collision systems.
- Improve animation state transitions and ownership/lifecycle rules.
- Verify lighting/effects visually across reloads and room transitions.
- Add reusable menus and UI widgets.
- Build polished top-down and side-on example slices to reveal missing engine functionality.

**Exit:** representative games can be implemented mostly through shared engine APIs and scripts.

## 6. Audio, profiling, and optimization

- Develop practical multi-voice audio playback/mixing controls.
- Add frame/tick, memory, rendering, and handler-cost profiling.
- Benchmark on modest hardware and optimize measured bottlenecks.
- Test clean-machine project transfer and packaging.

**Exit:** performance and compatibility claims have repeatable evidence.

## v1.0 readiness

A v1.0 release should wait until the repository is complete, graphical first-run is straightforward, platform claims are verified, common 2D gameplay and authoring workflows are dependable, and every release package passes clean extraction and launch tests.
