# CX Engine

**A graphical 2D game engine built in C++20, with a readable, Skript-inspired scripting language.**

CX is a lightweight, scriptable foundation for 2D games. Its primary application is a native graphical runtime for drawing, input, rooms, game objects, user interfaces, and visual effects. Game behavior is written in CX Script, an approachable event-and-action language inspired by Minecraft Skript.

A separate command-line utility provides project validation, diagnostics, and automation. The CLI supports development; it is not a substitute for the graphical engine.

> **Development status:** The complete curated v0.10.14 source tree is being integrated on a dedicated review branch. Linux x86_64 has a recorded clean graphical build and 19/19 CTest baseline, including a clean-source build and tested starter runtime package. Native Windows, ARM64, and macOS build jobs are configured, but no successful native result is recorded yet. On Linux Wayland desktops, the current SFML window backend requires XWayland; CX does not yet have a native Wayland backend.

## Highlights

- **Graphical 2D runtime:** rooms, tile and sprite rendering, keyboard/mouse input, a camera, canvas UI, lighting meshes, and visual effects.
- **Readable scripting:** events, variables with different lifetimes, conditions, loops, functions, delayed tasks, and actionable diagnostics.
- **Gameplay foundations:** entities, hitboxes, collision channels, triggers, proximity/interaction focus, projectiles, animation clips, tweens, and seeded random streams.
- **Project workflows:** data-driven rooms, asset discovery, safe first-run project scaffolding, and a browser-based Room Studio helper.
- **Developer CLI:** script checking and non-graphical workflows for testing and automation.

CX is still under active development. Audio mixing, general-purpose moving-body physics, editor depth, profiling, and full cross-platform packaging remain in progress. See the [project status](docs/project-status.md) and [roadmap](docs/roadmap.md).

## Quick start

### Build the graphical engine

Requirements: CMake 3.24+, a C++20 compiler, Git, and the platform development tools needed by SFML 3.1.0. By default, CMake downloads the pinned SFML version if it is not already installed.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The graphical executable is `build/cx-engine` on Linux/macOS and `build/Release/cx-engine.exe` on a typical multi-configuration Windows generator. The CLI executable is `cx-cli` / `cx-cli.exe`.

### Run a project

```sh
./build/cx-engine examples/quickstart
```

On Windows, run the corresponding `cx-engine.exe` and pass the same project directory. See [Getting started](docs/getting-started.md) for IDE and Windows-specific notes.

### Check scripts without opening a window

```sh
./build/cx-cli --check examples/quickstart
```

### Build only the core and CLI

The normal build requires the graphical runtime. A headless core/CLI build is an explicit developer option:

```sh
cmake -S . -B build-headless -DCX_BUILD_GRAPHICAL_RUNTIME=OFF
cmake --build build-headless --parallel
ctest --test-dir build-headless --output-on-failure
```

## Repository layout

```text
include/cx/        Public engine headers
src/               Engine core, graphical host, and CLI implementation
tests/             Automated C++ and Python regression tests
examples/          Small script and project examples
tools/             Local Room Studio server and release tooling
docs/              User guide, language reference, and feature notes
.github/workflows/ Native CI and release automation
```

The repository intentionally excludes internal development journals, prompt logs, handoff bundles, old validation dumps, compiled binaries, build directories, and complete showcase games. Example scripts are included because they are useful to learn and test the engine; larger sample games are distributed separately.

## Documentation

Start at the [documentation home](docs/index.md). The [interactive language and runtime reference](docs/reference.html) is a self-contained HTML page with inline CSS and JavaScript. The Markdown pages cover setup, scripting, projects, architecture, platform behavior, and contribution workflows.

## Platform notes

- **Linux x86_64:** current verified development baseline.
- **Windows x86_64:** not yet verified; native build and runtime testing are required before a support claim.
- **Linux/Windows ARM64 and macOS:** native CI targets are configured; no successful native results have been recorded yet.
- **Wayland:** SFML 3.1.0 currently opens its Linux windows through XWayland. Install/enable XWayland on the compositor. Native Wayland support is not available in this SFML backend.

See [Platform support](docs/platform-support.md) for the evidence and limitations.

## Releases

Versioned releases are intended to include source ZIP and TAR.GZ archives plus native starter-runtime packages for every platform that passes CI. Each runtime package will contain the graphical engine, CLI, a minimal starter project (`config.json` and `scripts/main.cx`), required redistributable libraries, license notices, and checksums. No unsupported target will be published as though it were verified.

See [Release format](docs/release-format.md).

## License

CX Engine is currently distributed under the [BSD 3-Clause License](LICENSE).

## Contributions and attribution

AI-assisted repository changes are listed in [the contribution log](docs/CHATGPT_CONTRIBUTIONS.md). Project direction and release decisions remain with the project maintainer.
