# Engine architecture

CX separates gameplay rules from the platform-specific graphical host. The CLI and graphical application use the same core runtime, which keeps script behavior and project validation consistent.

## Main layers

### Engine core (`cx_core`)

The core owns values, parsing, script programs, execution, project data, rooms, entities, gameplay systems, diagnostics, persistence, and renderer-neutral effect descriptions. It does not include SFML types in its public gameplay interfaces.

### Graphical host (`cx-engine`)

The graphical host uses SFML 3.1.0 for the desktop window, drawing, input, texture/font loading, and optional audio playback. The host forwards input events to the core, advances the simulation at its configured tick rate, and renders the current state. It is the main application for playing and developing 2D projects.

### Developer CLI (`cx-cli`)

The CLI uses the same core but does not create a graphics window. It is useful for syntax checks, diagnostics, script/runtime automation, and CI. It is a supporting developer tool, not the main game runtime.

### Room Studio (`tools/`)

Room Studio is a small browser-based local authoring tool backed by a Python standard-library HTTP server. It is an optional helper for editing project room data. The server binds to localhost and must not be exposed as a public service.

## Important boundaries

- Script parsing and execution should not depend on SFML.
- World state and effect geometry should be testable without creating a window.
- Operating-system integration (file locking, durable writes, input/window backend, file drop) stays in small platform-specific layers.
- Project data should not contain absolute machine-specific paths where a relative asset/project path is sufficient.
- Optional graphical features should degrade clearly; the graphical runtime itself is not optional in the normal product build.

## Source map

| Path | Responsibility |
|---|---|
| `include/cx/` | Public core headers and data contracts |
| `src/parser.cpp` | Script parsing and source diagnostics |
| `src/runtime*.cpp` | Script execution and gameplay runtime |
| `src/rooms.cpp` | Room/project data and transitions |
| `src/variable_storage.cpp` | Persistent variable formats and platform-safe writes |
| `src/sfml_main.cpp` | Graphical window/input/rendering host |
| `src/cli_main.cpp` | Developer CLI host |
| `src/x11_file_drop.cpp` | Optional X11/XWayland file-drop bridge |
| `tests/` | Unit and integration regression tests |
