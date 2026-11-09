# Getting started

This guide assumes you want to make a small 2D game using CX Script. You do not need to write C++ to create a project, edit scripts, or run the graphical engine.

## 1. Get the engine

Until the first multi-platform release is published, build from this repository using [the build guide](building.md). The normal build creates both the graphical runtime and the separate CLI.

## 2. Open the quickstart project

The repository includes a small starter project in `examples/quickstart/`:

```text
quickstart/
├── config.json
└── scripts/
    └── main.cx
```

Launch it from the repository root:

```sh
./build/cx-engine examples/quickstart
```

On Windows, run `cx-engine.exe` from the configured build output folder and pass the path to `examples/quickstart`.

The first window contains a working project rather than an empty engine shell. Press Space to increment the sample score and R to reset it. The in-game terminal can display script messages and engine diagnostics.

## 3. Create your own project

Create a new empty folder and launch the engine with that folder as the project path. CX can initialize a standard project layout when the folder is empty or contains an unambiguous root-level script. Existing files are not overwritten. For a manual project, create `config.json` and `scripts/main.cx` as shown in the quickstart.

## 4. Write a script

```skript
set {score} to 0

on load:
    broadcast "Welcome! Press SPACE."

on press key "SPACE":
    add 1 to {score}
    broadcast "Score: {score}"
```

The event header describes when a block runs. Indented statements are the actions. Variables in braces are values managed by the runtime. See the [CX Script learning guide](learn-cx-script.md) and [full syntax reference](syntax.md).

## 5. Check scripts

Before opening the graphical runtime, you can ask the CLI to parse every script in a project:

```sh
./build/cx-cli --check examples/quickstart
```

This checks syntax and emits file/line diagnostics. It does not prove that a behavior looks correct on screen.

## 6. Learn what to try next

The `examples/` folder has small independent examples for loops, lists, functions, entities, collision filters, triggers, interaction focus, projectiles, animation clips, tweens, lighting effects, and audio generation. Read one example at a time and check it with `cx-cli --check`.

## Troubleshooting

- If the graphical executable was not built, configure with the normal default (`CX_BUILD_GRAPHICAL_RUNTIME=ON`) and follow the [build guide](building.md).
- On Linux Wayland sessions, make sure XWayland is installed and available to the session. The current SFML backend does not open a native Wayland surface.
- If the game window opens but the project reports syntax errors, run the CLI check and correct the listed file and line.
- If the local room editor does not load, verify that Python 3 is installed and that you started its server from the project directory as described in [Room Studio](room-studio.md).
