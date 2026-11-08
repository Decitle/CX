# CX Engine

**A C++20, 2D-first game runtime with a readable, Skript-inspired scripting language.**

CX Engine is a lightweight engine foundation built around a portable C++ core, an optional SFML graphical frontend, and a CLI for project checks and scripted runtime work. The project prioritizes understandable code, predictable behavior, focused regression tests, and a small dependency footprint.

> **Co-authorship disclosure:** CX Engine is co-authored by **Decitle and ChatGPT (OpenAI)**. The project is directed and maintained by Decitle. ChatGPT has contributed implementation work, tests, and technical documentation under Decitle's direction. AI-authored or AI-edited repository contributions are explicitly identified in commit messages and recorded in [the contribution log](docs/CHATGPT_CONTRIBUTIONS.md).

## Project status

CX Engine is in active development. The currently published source checkpoint is **v0.10.10**; the v0.10.11 textured-beam work is being validated. The GitHub source import is intentionally incremental: the initial tracked source contribution contains the beam-effect module, its focused tests, an example, and its technical documentation. It is **not yet a complete standalone engine source tree**. The full tested release archives remain the authoritative build distributions while the repository import is expanded.

The repository is private during development. Public release timing and any future changes to visibility are Decitle's decision.

## Architecture

- **Core:** C++20 runtime, script parsing/execution, project and room data, entity/gameplay systems, persistence, and renderer-independent effect geometry.
- **Graphical frontend:** optional SFML 3 renderer and input/window layer.
- **CLI:** project checking and non-graphical runtime workflows.
- **Tools and docs:** Room Studio, .cx examples, implementation notes, and regression coverage.

The core is intended to remain usable without the graphical frontend. SFML types belong in the frontend, not in the renderer-neutral gameplay and geometry interfaces.

## Building

A complete source distribution includes CMakeLists.txt and build.sh. Typical commands are:

    ./build.sh
    ./build.sh --core-only
    ./build.sh --graphical

A C++20 compiler, CMake, and Python 3 are required for the core and tooling. The graphical target additionally requires SFML 3 Graphics/Window/System and their host dependencies. Audio support is optional.

See the source-distribution README and docs/BUILDING_WITH_SFML.md in the complete source package for the tested build setup. Because the initial GitHub import is selective, the files currently tracked here should not yet be treated as a complete build checkout.

## Current source contribution

The first implementation slice checked into this repository is the renderer-neutral textured-beam foundation:

- include/cx/effects.hpp and src/effects.cpp: beam data contract and pure quad geometry builder.
- tests/beam_effect_tests.cpp: geometry, script/runtime, property validation, lifetime, and removal regressions.
- examples/textured_beam.cx and docs/TEXTURED_BEAMS-v0.10.11.md: example script and behavior contract.

This slice is part of the in-progress v0.10.11 development candidate. It is not a claim that the complete release has shipped.

## License

The repository currently includes a BSD 3-Clause license in [LICENSE](LICENSE). Review that file before redistributing this repository or its contents.

## Attribution and review

Changes pushed by ChatGPT are labeled in their commit subjects (for example, "AI-assisted by ChatGPT:") and listed in docs/CHATGPT_CONTRIBUTIONS.md. Decitle remains responsible for reviewing changes and deciding what is accepted, released, licensed, or published.
