# CX Engine Documentation

CX is a C++20 graphical 2D game engine with an approachable, Skript-inspired scripting language.

> **Released syntax (v0.10.14):** [Open the interactive CX Script & Runtime Reference](reference.html) — this searchable catalogue is pinned to the current published release.
>
> **v0.11.0 development:** Follow the [language design charter](language-design.md), [Skript syntax audit](SKRIPT_SYNTAX_AUDIT.md), and [development syntax guide](syntax.md) for the in-progress overhaul. These additions are not part of the published runtime yet.
>
> **New to CX?** Follow [Getting started](getting-started.md), then the [CX Script learning guide](learn-cx-script.md).

## Core documentation

- [Interactive HTML reference](reference.html) — searchable overview of the language and runtime.
- [Language design charter](language-design.md) — the English-first Skript-like grammar requirement for v0.11.0.
- [Skript syntax audit](SKRIPT_SYNTAX_AUDIT.md) — source-backed feature inventory and concrete syntax gaps.
- [Event context](event-context.md) — local values supplied by trigger, focus, projectile, tween, animation, and file-attachment handlers.
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
- [Testing and validation](testing.md) — CI syntax-checks every `.cx` example; CTest and graphical smoke tests cover separate behavior paths.
- [Runtime package guide](runtime-package.md)
- [Release format](release-format.md)
- [Roadmap](roadmap.md)
- [Project status](project-status.md)

Version-specific reference snapshots ship with their corresponding GitHub releases. The [v0.10.14-preview.5 legacy reference](versions/v0.10.14-preview.5/reference.html) is also preserved on this site.

Feature-specific contracts are documented under [features/](features/), including animation, tweens, collision responses, interaction focus, projectiles, room transitions, textured beams, and persistent storage.

## Repository and release status

The latest published runtime remains [v0.10.14-preview.8](https://github.com/Decitle/CX/releases/tag/v0.10.14-preview.8). The v0.11.0 Skript-like overhaul is being developed on an open draft branch and is not yet merged or released. Its latest verified development CI matrix passed all six native targets in [run #91](https://github.com/Decitle/CX/actions/runs/43491377925).

The searchable `reference.html` continues to describe v0.10.14 until the v0.11.0 language surface is accepted. The development syntax guide and audit are separate on purpose. Recent branch commits added count-prefix random list assignment, possessive property setters, and CI syntax checks for all `.cx` examples; the current-head CI run must pass before those additions are considered validated. For the exact repository/PR state and remaining release gates, see [Project status](project-status.md).

GitHub automatically provides Source code (zip) and Source code (tar.gz) snapshots for each tag. The release workflow uploads native runtime packages and a version-matched HTML reference.
