# CX Engine

**A C++20, 2D-first game runtime with a readable, Skript-inspired scripting language.**

CX Engine is a lightweight engine foundation built around a portable C++ core, an optional SFML graphical frontend, and a CLI for project checks and scripted runtime work. The project prioritizes understandable code, predictable behavior, focused regression tests, and a small dependency footprint.

> **Co-authorship disclosure:** CX Engine is co-authored by **Decitle and ChatGPT (OpenAI)**. The project is directed and maintained by Decitle. ChatGPT has contributed implementation work, tests, and technical documentation under Decitle's direction. AI-authored or AI-edited repository contributions are explicitly identified in commit messages and recorded in [the contribution log](docs/CHATGPT_CONTRIBUTIONS.md).

## Project status

The current engine release is **v0.10.11**, which adds the renderer-neutral textured-beam foundation and optional independent endpoint lighting. The GitHub repository is intentionally a selective source import: it currently contains the beam-effect module, focused tests, example, and technical documentation. It is **not yet a complete standalone engine source tree**. The complete tested source archive remains the authoritative build distribution while the repository import is expanded.

The repository remains private during development. Public release timing and any future visibility changes are Decitle's decision.

## Architecture

- **Core:** C++20 runtime, script parsing/execution, project and room data, entity/gameplay systems, persistence, and renderer-independent effect geometry.
- **Graphical frontend:** optional SFML 3 renderer and input/window layer.
- **CLI:** project checking and non-graphical runtime workflows.
- **Tools and docs:** Room Studio, `.cx` examples, implementation notes, and regression coverage.

The core is intended to remain usable without the graphical frontend. SFML types belong in the frontend, not in the renderer-neutral gameplay and geometry interfaces.

## Building

A complete source distribution is built with CMake. Once the full source tree is present, a core/CLI build without SFML uses:

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCX_ENABLE_SFML=OFF -DCX_BUILD_TESTS=ON
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

To include the optional SFML 3 graphical frontend, configure with `-DCX_ENABLE_SFML=ON` and set `-DSFML_DIR=/path/to/SFML/lib/cmake/SFML` when SFML is not installed in a standard location. Audio playback is optional and can be disabled with `-DCX_ENABLE_SFML_AUDIO=OFF`.

The current GitHub import is selective and does not yet contain the full CMake project, so it should not yet be treated as a standalone build checkout.

## Current source contribution

The current GitHub source slice is the renderer-neutral textured-beam foundation:

- `include/cx/effects.hpp` and `src/effects.cpp`: beam data contract and pure quad geometry builder.
- `tests/beam_effect_tests.cpp`: geometry, script/runtime, property validation, lifetime, and removal regressions.
- `examples/textured_beam.cx` and `docs/TEXTURED_BEAMS-v0.10.11.md`: example script and behavior contract.

This selective import is part of the v0.10.11 engine source. It is not a claim that all engine components have been mirrored into this repository.

## License

The repository currently includes a BSD 3-Clause license in [LICENSE](LICENSE). Review that file before redistributing this repository or its contents.

## Attribution and review

Changes pushed by ChatGPT are labeled in their commit subjects (for example, `AI-assisted by ChatGPT:`) and listed in `docs/CHATGPT_CONTRIBUTIONS.md`. Decitle remains responsible for reviewing changes and deciding what is accepted, released, licensed, or published.
