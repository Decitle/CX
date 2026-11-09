# CX Engine

**A graphical 2D game engine built in C++20, with a readable, Skript-inspired scripting language.**

CX is being developed as a lightweight, scriptable foundation for 2D games. Its primary application is a graphical desktop runtime: the current frontend uses SFML 3 for windowing, drawing, and input, while the game rules and scripting runtime live in a renderer-neutral C++ core.

A separate command-line tool supports project checks, diagnostics, and automated workflows. It complements the graphical engine; it is not the product's primary interface.

## What CX is designed to provide

- **Graphical 2D runtime** — windows, rooms, tile and sprite rendering, input, camera behavior, UI canvases, lighting, and visual effects.
- **Readable game scripting** — a custom language inspired by the approachable event-and-action style of Skript, with variables, functions, loops, events, and diagnostics.
- **Reusable gameplay systems** — entities, hitboxes, collision channels, triggers, interaction focus, projectiles, deterministic random streams, tweens, and animation clips.
- **Room and project workflows** — data-driven room files, asset discovery, project initialization, and the Room Studio editor.
- **Developer CLI** — project validation and non-graphical checks for testing and automation.

These are project goals and current development areas, not a claim that every planned editor, physics, UI, audio, or portability feature is complete. See [Project status](docs/PROJECT_STATUS.md) and the [roadmap](docs/ROADMAP.md).

## Architecture

CX separates three responsibilities:

1. **Engine core:** C++20 scripting, project data, simulation, persistence, and renderer-independent gameplay/effect contracts.
2. **Graphical runtime:** the desktop application, currently implemented with SFML 3.
3. **Command-line tools:** project checking and headless workflows for development and automation.

The graphical runtime is the main way to use CX. The CLI is a separate supporting tool. Keeping the core independent from SFML makes it easier to test game logic and to maintain platform-specific graphical builds without tying gameplay APIs to a particular renderer.

## Current development and repository status

The wider engine has reached an internal **v0.10.14 development checkpoint**. On Linux x86_64, that checkpoint passed 17 CTest targets in headless and SFML-enabled Release configurations, plus a clean-source extraction build. The graphical runtime also passed a short headless-window smoke test. These checks are not a substitute for testing on every supported operating system or on a physical display.

**This GitHub repository is not yet a complete, standalone checkout of that engine.** The current `main` branch is a selective source import focused on the renderer-neutral textured-beam feature. It contains:

- `include/cx/effects.hpp` and `src/effects.cpp`
- `tests/beam_effect_tests.cpp`
- `examples/textured_beam.cx`
- The beam behavior documentation and AI-assisted contribution log in `docs/`

The complete engine build files and the rest of the implementation have not yet been imported here. As a result, this checkout cannot currently build the full graphical engine by itself. There are no published GitHub Release or downloadable runtime packages from this repository yet.

The development checkpoint and the contents of this GitHub repository are deliberately described separately so that readers can see exactly what is available here.

## Platform support

Linux x86_64 is the only platform with a recorded full-engine build and test result at the current development checkpoint. Windows support is a target, not a verified claim: the development code contains Windows-specific paths and MSVC configuration, but a native Windows build and runtime test are still required. Linux ARM64 and macOS builds also need validation.

See [Platform support](docs/PLATFORM_SUPPORT.md) for the evidence and target matrix.

## Documentation

The [documentation home](docs/index.md) is the starting point for the project overview, status, platform support, release format, and roadmap.

## Releases

When the repository contains the complete buildable engine and platform CI has passed, versioned GitHub Releases are intended to provide:

- Source code in both ZIP and TAR.GZ formats.
- Self-contained starter runtime packages for each validated operating-system/architecture combination.
- Checksums, build metadata, and dependency/license notices.

No such multi-platform release is published yet. Planned package contents and naming are described in [Release format](docs/RELEASE_FORMAT.md).

## License

CX currently uses the **BSD 3-Clause License**. The license is included in [LICENSE](../LICENSE). The license may be reconsidered before a public release, but no license change has been made.

## Contributions and attribution

Project direction and release decisions belong to the maintainer. AI-assisted repository contributions are recorded in [the contribution log](CHATGPT_CONTRIBUTIONS.md).
