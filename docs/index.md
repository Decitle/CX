# CX Engine Documentation

CX is a graphical 2D game engine built around a C++20 runtime and a readable, Skript-inspired scripting language. This documentation is written for game creators, contributors, and developers evaluating the engine.

## Start here

- [Getting started](getting-started.md) — download a runtime or build from source, launch the graphical runtime, and open the starter project.
- [Building from source](building.md) — dependencies, CMake options, and platform-specific build notes.
- [CX Script guide](learn-cx-script.md) — write your first event-driven game logic without needing C++.
- [Interactive HTML reference](reference.html) — a searchable, self-contained reference page.
- [Projects and rooms](projects-and-rooms.md) — project structure, assets, room files, and initialization.
- [Room Studio](room-studio.md) — use the local browser-based room editor.
- [Engine architecture](architecture.md) — understand the core, graphical host, CLI, and extension boundaries.
- [Platform support](platform-support.md) — native CI results and platform-specific limits.
- [Testing](testing.md) — run the automated suite and understand its coverage.
- [Release format](release-format.md) — source archives, native runtime packages, and release automation.
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

## Repository and release status

The complete curated source tree is merged into `main`. GitHub Actions run #10 passed the six-target native CI matrix on commit `a690dc023a072c6d5de797e276128491505a1d2b`. The first planned preview tag is `v0.10.14-preview.5`; check [GitHub Releases](https://github.com/Decitle/CX/releases) for the current publication and asset status. The tag-triggered release workflow must complete all source and runtime jobs before it attaches the curated downloads.
