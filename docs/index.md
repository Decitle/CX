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
- [Platform support](platform-support.md) — what has actually been built and what still needs verification.
- [Testing](testing.md) — run the automated suite and understand its coverage.
- [Release format](release-format.md) — planned source and starter-runtime downloads.
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

The v0.10.14 development baseline has a functioning Linux x86_64 graphical build and a clean-source test run of 19/19 CTest targets. Native Windows and additional architecture jobs are configured in the CI workflow, but no successful native results have been recorded yet. SFML's Linux window backend uses XWayland on Wayland desktops; a native Wayland backend is not currently provided. Read [Project status](project-status.md) before treating any future platform or feature as supported.
