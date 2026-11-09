# Contributing to CX Engine

Thank you for taking an interest in CX. Contributions should improve a reusable graphical 2D engine, not introduce a game-specific dependency into the core.

## Before making a change

1. Read the [README](README.md), [architecture overview](docs/architecture.md), and relevant feature documentation.
2. Check the [project status](docs/project-status.md) so proposals distinguish current behavior from planned work.
3. Search existing issues and code for the same behavior before introducing a second API.

## Build and test

Use the normal graphical build and run the full suite:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

A headless build is useful for core changes, but it is not a replacement for testing the graphical runtime when a change touches rendering, input, windows, or user-facing behavior.

## Code guidelines

- Keep script/runtime logic independent from SFML where practical.
- Keep operating-system APIs inside narrow platform-specific adapters.
- Add regression tests for bug fixes and new language/runtime behavior.
- Use deterministic tests; avoid depending on wall-clock timing where a fake clock or bounded wait is possible.
- Preserve project data and user saves on failed loads, reloads, or editor operations.
- Document syntax, defaults, side effects, and limitations when adding user-facing APIs.
- Keep code comments focused on intent, invariants, ownership, and data flow rather than narrating obvious syntax.

## Examples and documentation

Small examples that demonstrate one feature belong under `examples/`. Large sample games should be separate projects or release assets. User documentation belongs under `docs/`; do not commit private planning notes, prompt logs, handoff packages, local validation dumps, or generated build output.

## Pull requests

Describe the problem, behavior change, platform(s) tested, commands used, and any remaining limitations. Include screenshots or short recordings only when they demonstrate actual output from the tested build. Do not claim support for a platform that has not passed a native build and runtime check.
