# CX Engine Documentation

CX is a C++20 graphical 2D game engine with an approachable, Skript-inspired scripting language.

> **Start here:** [Open the interactive CX Script & Runtime Reference](reference.html) — search events, variables, expressions, statements, engine APIs, and known limitations.
>
> **New to CX?** Follow [Getting started](getting-started.md), then the [CX Script learning guide](learn-cx-script.md).

## Core documentation

- [Interactive HTML reference](reference.html) — searchable overview of the language and runtime.
- [Getting started](getting-started.md) — choose a runtime or build from source, launch the graphical engine, and open a starter project.
- [Full CX Script syntax guide](syntax.md) — detailed syntax and language behavior.
- [Engine settings](engine-settings.md) — supported settings, defaults, accepted values, and when they take effect.
- [World and entity systems](world-and-entities.md) — sprites, entities, collision, triggers, projectiles, tweens, and animation.
- [Projects and rooms](projects-and-rooms.md) — project structure, assets, room files, and initialization.
- [Room Studio](room-studio.md) — use the local browser-based room editor.
- [Building from source](building.md) — dependencies, CMake options, and platform-specific build notes.
- [Engine architecture](architecture.md) — core runtime, graphical host, CLI, and extension boundaries.

## Detailed project and feature guidance

- [Persistent variable recovery](variable-storage-recovery.md)
- [Skript compatibility notes](language-compatibility.md)
- [Learn the C++ source](understanding-the-cpp-source.md)
- [Platform support](platform-support.md)
- [Testing and validation](testing.md)
- [Runtime package guide](runtime-package.md)
- [Release format](release-format.md)
- [Roadmap](roadmap.md)
- [Project status](project-status.md)

Version-specific reference snapshots ship with their corresponding GitHub releases. The [v0.10.14-preview.5 legacy reference](versions/v0.10.14-preview.5/reference.html) is also preserved on this site.

Feature-specific contracts are documented under [features/](features/), including animation, tweens, collision responses, interaction focus, projectiles, room transitions, textured beams, and persistent storage.

## Repository and release status

The latest completed six-target native CI baseline before the current release-packaging fix is [run #13](https://github.com/Decitle/CX/actions/runs/42743276494). The first v0.10.14-preview.5 release attempt failed after the Windows ARM64 build and tests had passed, because its install-time dependency scan reported five Windows system DLL names unresolved. The release publish gate skipped the entire asset set. A narrow fix and regression coverage are now in main; a new preview tag is required to validate and publish the six runtime packages.

GitHub automatically provides Source code (zip) and Source code (tar.gz) snapshots for each tag. The release workflow uploads only native runtime packages and the version-matched HTML reference.
