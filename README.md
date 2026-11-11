# CX Engine

**An open-source C++20 2D game engine with a readable, Skript-inspired scripting language.**

CX Engine is a cross-platform engine for building sprite-based 2D games. It combines a native desktop runtime with **CX Script**, an approachable, event-driven language inspired by Minecraft Skript. Game logic can be written in scripts while the engine's core, parser, and runtime are implemented in modern C++.

[![Build and test](https://github.com/Decitle/CX/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Decitle/CX/actions/workflows/ci.yml)
[![Download CX Engine](https://img.shields.io/badge/Download-CX%20Engine-168b78)](https://decitle.github.io/CX/downloads.html)
[![Documentation](https://img.shields.io/badge/Documentation-Online-5277c3)](https://decitle.github.io/CX/)
[![GitHub repository](https://img.shields.io/badge/GitHub-Repository-24292f?logo=github)](https://github.com/Decitle/CX)
[![License: BSD 3-Clause](https://img.shields.io/badge/license-BSD--3--Clause-blue.svg)](LICENSE)

**[Download CX](https://decitle.github.io/CX/downloads.html)** · **[Documentation](https://decitle.github.io/CX/)** · **[Interactive reference](https://decitle.github.io/CX/reference.html)** · **[Getting started](docs/getting-started.md)**

> **Project status:** CX is an experimental pre-release and remains under active development. The current native CI matrix covers Windows, Linux, and macOS on x86_64 and ARM64. Versioned runtime packages are published through GitHub Releases after every required release job succeeds.

## What CX provides

- **Native 2D runtime:** desktop windowing, sprite and tile rendering, keyboard and mouse input, camera movement, and a project-based game workflow.
- **CX Script:** event handlers, variables with distinct lifetimes, conditions, loops, functions, scheduled tasks, and source-aware diagnostics.
- **Gameplay systems:** rooms, sprites and entities, hitboxes, collision channels, tile avoidance, triggers, interaction focus, projectiles, tweens, and named animation clips.
- **UI and visual effects:** screen-space canvas elements, lighting, textured beams, and renderer-backed effects.
- **Project tools:** automatic asset discovery, project scaffolding, a separate command-line validator, and an optional local browser-based Room Studio helper.

CX is intended to make common 2D gameplay logic approachable without hiding the underlying engine. C++ remains available for engine development and future extensions.

## Quick start

### Get a runtime

Open the [CX download page](https://decitle.github.io/CX/downloads.html). It detects your operating system where the browser allows it, offers a manual OS/architecture selector, and links directly to the matching runtime archive. The page keeps checksum sidecars out of the main download choice; they remain available on the full [GitHub release page](https://github.com/Decitle/CX/releases) for anyone who wants to verify a download. A runtime package contains the graphical engine, the separate CLI, required application libraries, notices, and a minimal editable starter project.

A source checkout is not a precompiled application. To build CX yourself, use the steps below or follow the [full build guide](docs/building.md).

### Build from source

Requirements: CMake 3.24 or newer, a C++20 compiler, Git, and the platform development tools required by SFML 3.1.0. By default, CMake fetches the pinned SFML version when a suitable installation is not already available.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The graphical executable is `build/cx-engine` on Linux/macOS and commonly `build/Release/cx-engine.exe` on Windows. The separate CLI executable is `cx-cli` or `cx-cli.exe`.

### Run the starter project

```sh
./build/cx-engine examples/quickstart
```

The starter project demonstrates a simple event-driven script. Press Space to increment its score and R to reset it. On Windows, run the corresponding `cx-engine.exe` and pass the same project directory.

### Validate scripts from the command line

```sh
./build/cx-cli --check examples/quickstart
```

The CLI checks project scripts and reports syntax or project diagnostics without opening a graphics window. It uses the same core runtime as the graphical application.

### Build only the core and CLI

A headless build is available for server-side development and script validation. It is an explicit option; the normal build includes the graphical runtime.

```sh
cmake -S . -B build-headless -DCX_BUILD_GRAPHICAL_RUNTIME=OFF
cmake --build build-headless --parallel
ctest --test-dir build-headless --output-on-failure
```

## Examples

The `examples/` directory contains a runnable quickstart project and focused demonstrations of CX Script, lists and functions, timing, entities, collision filters, triggers, interaction focus, projectiles, animation, tweens, UI layout, lighting effects, and procedural audio.

These examples are intentionally small and feature-focused. Complete showcase games are a separate planned addition.

## Documentation

The documentation site is the main learning path:

- **[Interactive reference](https://decitle.github.io/CX/reference.html):** searchable catalogue of language events, variables, expressions, statements, engine APIs, settings, and limitations.
- **[Getting started](https://decitle.github.io/CX/getting-started.html):** install or build the engine, launch a project, and write a first script.
- **[Learn CX Script](https://decitle.github.io/CX/learn-cx-script.html):** a guided introduction to the scripting language.
- **[Full syntax guide](https://decitle.github.io/CX/syntax.html):** language syntax, execution behavior, variable lifetimes, and error handling.
- **[World and entity systems](https://decitle.github.io/CX/world-and-entities.html):** rooms, sprites, entities, movement, collision, triggers, projectiles, and animation.
- **[Engine settings](https://decitle.github.io/CX/engine-settings.html):** supported settings, defaults, accepted values, and when they take effect.
- **[Roadmap and project status](https://decitle.github.io/CX/roadmap.html):** current development priorities and known gaps.

Each tagged release also includes a standalone HTML reference generated from that exact version. Its links to the detailed guides are pinned to the same tag, making it possible to consult the documentation that matches an older engine release.

## Platform support and current limitations

Native CI targets are configured for:

| Operating system | x86_64 | ARM64 |
|---|---:|---:|
| Windows | Yes | Yes |
| Linux | Yes | Yes |
| macOS | Yes | Yes |

The six targets are built and tested on native CI runners. Linux graphical startup is smoke-tested under a virtual display. CI results are useful compatibility evidence, but they are not a substitute for testing on physical computers, graphics drivers, and desktop configurations.

Current limitations include:

- The Linux SFML window backend uses XWayland; a Wayland desktop needs an available XWayland server.
- SFML audio playback is disabled by default in the current build configuration.
- General-purpose moving-body physics and broad collision response are still under development; existing collision, trigger, and projectile systems have defined, narrower contracts.
- Room Studio is an optional local authoring helper, not a full visual game editor.
- No 32-bit Windows or Linux runtime packages are provided.

See [Platform support](docs/platform-support.md), [Project status](docs/project-status.md), and the [roadmap](docs/roadmap.md) for more detail.

## Downloads

Use the [CX download page](https://decitle.github.io/CX/downloads.html) for a platform-aware direct runtime download, or open [GitHub Releases](https://github.com/Decitle/CX/releases) to inspect release notes, all six packages, SHA-256 checksum sidecars, and GitHub's automatic source-code ZIP and TAR.GZ snapshots. The CX workflow does not upload duplicate curated source archives.

The release workflow builds six platform-specific starter-runtime packages and a version-matched HTML reference. It attaches those assets only after all six native runtime jobs and the documentation job succeed. Download the package matching the operating system and CPU architecture; native executables and libraries are not interchangeable between platforms.

See the [release format guide](docs/release-format.md) for package contents, naming, and validation details.

## License

CX Engine is distributed under the [BSD 3-Clause License](LICENSE). Third-party notices and the SFML license are included in the repository and runtime packages.
