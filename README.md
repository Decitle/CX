# CX Engine

**A graphical 2D game engine built in C++20, with a readable, Skript-inspired scripting language.**

CX is a lightweight, scriptable foundation for 2D games. Its primary application is a native graphical runtime for drawing, input, rooms, game objects, user interfaces, and visual effects. Game behavior is written in CX Script, an approachable event-and-action language inspired by Minecraft Skript.

A separate command-line utility provides project validation, diagnostics, and automation. The CLI supports development; it is not a substitute for the graphical engine.

> **Repository status:** The complete curated source tree is merged into `main`. The latest completed six-target CI baseline is [run #13](https://github.com/Decitle/CX/actions/runs/42743276494); all six targets passed native builds, CTest, and starter-project validation. The first v0.10.14-preview.5 release attempt passed those checks on all six targets but failed during the Windows ARM64 install-time dependency scan, so the all-or-nothing publication job was skipped. A narrowly scoped fix and regression coverage are now in `main`; the next preview tag is required to validate the corrected packaging workflow.

> **Documentation:** [Open the CX Engine documentation site](https://decitle.github.io/CX/) · [Open the interactive language & runtime reference](https://decitle.github.io/CX/reference.html) · [Getting started](docs/getting-started.md)

## Highlights

- **Graphical 2D runtime:** rooms, tile and sprite rendering, keyboard/mouse input, a camera, canvas UI, lighting meshes, and visual effects.
- **Readable scripting:** events, variables with different lifetimes, conditions, loops, functions, delayed tasks, and actionable diagnostics.
- **Gameplay foundations:** entities, hitboxes, collision channels, triggers, proximity/interaction focus, projectiles, animation clips, tweens, and seeded random streams.
- **Project workflows:** data-driven rooms, asset discovery, safe first-run project scaffolding, and a browser-based Room Studio helper.
- **Developer CLI:** script checking and non-graphical workflows for testing and automation.

CX is still under active development and is not a v1.0-complete engine. General moving-body physics, richer editor workflows, audio playback, profiling, and physical-device compatibility testing remain areas for improvement. SFML Audio playback is disabled by default in the current build configuration. See the [project status](docs/project-status.md) and [roadmap](docs/roadmap.md).

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

**[Open CX Engine Documentation](https://decitle.github.io/CX/)** · **[Open the interactive language and runtime reference](https://decitle.github.io/CX/reference.html)**

New to CX? Start with [Getting started](docs/getting-started.md) and the [CX Script learning guide](docs/learn-cx-script.md). The detailed [syntax reference](docs/syntax.md), [engine settings](docs/engine-settings.md), [world/entity guide](docs/world-and-entities.md), and focused feature guides document the exact behavior and limitations.

## Platform notes

- **Linux x86_64 and ARM64:** native CI build, CTest, starter-project validation, and virtual-display startup smoke passed.
- **Windows x86_64 and ARM64:** native CI build, CTest, starter-project validation, and graphical startup smoke passed.
- **macOS x86_64 and ARM64:** native CI build, CTest, and starter-project validation passed. The current workflow does not run the graphical smoke test on macOS.
- These are six 64-bit/ARM64 CI targets. No 32-bit Windows or Linux runtime is currently built or advertised.
- **Wayland:** SFML 3.1.0 opens Linux windows through XWayland. Install/enable XWayland on the compositor; native Wayland support is not available in this backend.

See [Platform support](docs/platform-support.md) for the evidence and limitations.

## Downloads

Use [GitHub Releases](https://github.com/Decitle/CX/releases) for versioned downloads. GitHub already provides automatic source-code ZIP and TAR.GZ snapshots for every tag, so the CX workflow does not upload a duplicate curated source pair. After all six native runtime jobs and the versioned-documentation job pass, the release workflow attaches six starter-runtime packages and a standalone HTML reference for that exact release. Choose the runtime matching the computer's operating system and CPU architecture; project files are portable, but native executables and libraries are not interchangeable.

See [Release format](docs/release-format.md) for exact package names, contents, and validation rules.

## License

CX Engine is currently distributed under the [BSD 3-Clause License](LICENSE).

## Contributions and attribution

AI-assisted repository changes are listed in [the contribution log](docs/CHATGPT_CONTRIBUTIONS.md). Project direction and release decisions remain with the project maintainer.
