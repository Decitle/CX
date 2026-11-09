# CX Engine Documentation

CX is a graphical 2D game engine built around a C++20 runtime and a readable, Skript-inspired scripting language. This documentation is written for players, game creators, contributors, and developers evaluating the engine.

## Start here

- [Getting started](getting-started.md) — build the engine, launch the graphical runtime, and open the starter project.
- [Building from source](building.md) — dependencies, CMake options, and platform-specific build notes.
- [CX Script guide](learn-cx-script.md) — write your first event-driven game logic without needing C++.
- [Interactive HTML reference](reference.html) — a searchable, self-contained web page.
- [Language and runtime reference](reference.html) — searchable reference for syntax, events, functions, world, graphics, UI, settings, and diagnostics.
- [Projects and rooms](projects-and-rooms.md) — project structure, assets, room files, and initialization.
- [Room Studio](room-studio.md) — use the local browser-based room editor.
- [Engine architecture](architecture.md) — understand the core, graphical host, CLI, and extension boundaries.
- [Platform support](platform-support.md) — native CI results and platform-specific limits.
- [Testing](testing.md) — run the automated suite and understand its coverage.
- [Release format](release-format.md) — automated source and native runtime release packages.
- [Runtime package guide](runtime-package.md) — launch the included starter workspace and understand its contents.
- [Roadmap](roadmap.md) — development priorities and v1.0 exit criteria.

## Detailed references

- [Full syntax reference](syntax.md)
- [Engine settings](engine-settings.md)
- [World and entity systems](world-and-entities.md)
- [Persistent variable recovery](variable-storage-recovery.md)
- [Skript compatibility notes](language-compatibility.md)
- [Learn the C++ source](understanding-the-cpp-source.md)

Feature-level details live in [the feature guides](features/).

## Project status

The complete curated engine source is merged into `main`. GitHub Actions run #6 passed the six-target native CI matrix (Linux x86_64/ARM64, Windows x86_64/ARM64, macOS x86_64/ARM64). Build, CTest, and starter-project checks passed on all six targets; graphical startup smoke tests passed on Linux and Windows runners. The official pre-release tag and GitHub Release have not yet been published. Read [Project status](project-status.md) and [Platform support](platform-support.md) for evidence and limitations.
