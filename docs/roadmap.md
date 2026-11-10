# Roadmap

This roadmap describes the work needed to make CX a dependable general-purpose graphical 2D engine. It is not a promise of fixed delivery dates.

## 1. Repository and reproducible builds — baseline complete

- The complete curated engine source, build configuration, tests, focused examples, and public documentation are merged into `main`.
- A clean checkout is configured to build the graphical application and CLI.
- SFML 3.1.0 is pinned and fetched by CMake when no suitable installed package is found.
- Source packaging has an allowlist and guards against binaries, build output, caches, generated save state, and internal development records.

**Baseline evidence:** native CI run #10 passed build, CTest, and starter-project checks on all six configured targets. Ongoing changes still require normal review and CI.

## 2. Platform portability — native CI baseline complete

- Native CI targets pass on Linux x86_64/ARM64, Windows x86_64/ARM64, and macOS x86_64/ARM64.
- Linux and Windows graphical startup smoke tests pass in CI.
- The same starter-project data is checked on all six targets.

**Remaining work:** physical-device and display/DPI testing, real-world driver/compositor coverage, additional Windows process-recovery testing, and macOS graphical smoke coverage. No 32-bit target is currently planned or advertised; add one only if a real compatibility requirement justifies a separately tested build.

## 3. First-run workflow and structured releases — release validation pending

- Publish curated source ZIP/TAR.GZ and per-platform starter runtime packages.
- Include the graphical runtime, CLI, config, main script, dependency notices, and checksums.
- Keep full sample games separate from the engine runtime.
- Verify the first tag-triggered release matrix, including clean extraction and the attached assets.

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

A v1.0 release should wait until graphical first-run is straightforward, common 2D gameplay and authoring workflows are dependable, platform claims have appropriate physical-device coverage, and every release package passes clean extraction and launch tests.
