# CX Engine — project status

**Development checkpoint:** v0.10.14  
**Repository branch:** `main`  
**Repository state:** selective source import; not yet a complete standalone build  
**Release state:** no tagged engine release or published runtime downloads

## Two different status views

The engine's wider development tree has reached v0.10.14. The GitHub repository currently contains only a small selection of that work. These are separate facts:

- **Engine development:** the broader project has a functioning C++20 core, a graphical SFML 3 runtime, a separate CLI, script and room systems, entity/gameplay features, persistence, tests, and a Room Studio workflow.
- **This repository:** the current checkout contains the renderer-neutral textured-beam data/geometry feature, its focused tests, one script example, and the supporting documentation. It does not contain the complete runtime or CMake build project.

The repository is being expanded toward a complete, reproducible, public project. Until that work is finished, the current checkout should not be presented as a downloadable or independently buildable engine.

## Current development baseline

The v0.10.14 Linux x86_64 development build has recorded:

- 17 CTest targets passing in headless Release.
- 17 CTest targets passing in an SFML-enabled Release build.
- 17 CTest targets passing after extracting and rebuilding a clean source package.
- A short graphical runtime smoke test in a virtual display environment.
- Packaging-manifest and ZIP integrity checks for the Linux quick package.

These checks establish a useful Linux development baseline. They do not establish Windows, ARM64, or macOS support, and a virtual display smoke test is not equivalent to physical-device input, rendering, or performance validation.

## Implemented feature areas in the wider development tree

The broader engine currently includes:

- A C++20 script parser and runtime with diagnostics, local/event variables, persistent variables, functions, loops, waits, scheduled handlers, and transactional script reload.
- Data-driven projects and rooms, tile rendering, room transitions, entrances, overlays, oblique projection, asset discovery, and a Room Studio editing workflow.
- Sprite-backed entities, hitboxes, collision channels, triggers, proximity and interaction focus.
- Direct projectiles, radial/fan direction patterns, tweens, named animation clips, and textured beams with independent endpoint lighting.
- A graphical terminal, canvas/UI system, settings, camera behavior, lighting meshes, and project validation through a separate CLI.

This is not the same as saying the engine is feature-complete. The roadmap below records major gaps.

## Important unfinished areas

- A complete, polished first-run experience and project-owned starter template.
- Consistent coordinate, input, DPI, and UI behavior across a wider display matrix.
- More complete scripting semantics and task/timer management.
- General moving-entity collision and physics, beyond the existing static-tile, projectile, and tween-specific behavior.
- More complete Room Studio authoring, especially paths, waypoints, and broader undo/redo.
- More visual verification and resource-lifecycle rules for lighting/effects.
- More complete reusable UI widgets and menus.
- A real-time audio mixer with playback controls and automation.
- Profiling, repeatable performance benchmarks, and larger-world optimization.
- Native builds, tests, and self-contained runtime packages across target operating systems and CPU architectures.

## Repository readiness checklist

| Area | Current state |
|---|---|
| Public-facing project overview | Updated |
| Focused source contribution and tests | Present |
| Full engine source in this repository | Not yet present |
| Build system in this repository | Not yet present |
| Continuous integration | Not configured |
| Native Windows build | Not verified |
| Linux x86_64 full-engine validation | Recorded in development baseline |
| Linux ARM64 / Windows ARM64 | Not verified |
| macOS x86_64 / ARM64 | Not verified |
| Source ZIP and TAR.GZ release assets | Planned; not published |
| Per-platform starter runtime packages | Planned; not published |
| Documentation site files | Prepared in `docs/`; Pages deployment not enabled |

## What counts as a release-ready engine

Before calling a public version ready, the repository should contain the complete buildable source, a reproducible dependency/build process, regression tests on every claimed target, a working graphical starter project, clear runtime packaging, and user-facing documentation. A successful compile alone is not sufficient: a clean extracted package must launch, open a starter project, report errors clearly, and preserve the project's files.
