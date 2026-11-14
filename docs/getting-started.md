# Getting started

This guide assumes you want to make a small 2D game using CX Script. You do not need to write C++ to create a project, edit scripts, or run the graphical engine when you use a prebuilt runtime package.

## 1. Get the engine

Open the [CX download page](downloads.html) and select the package for your operating system and CPU architecture. Extract it to a writable folder. The package includes the graphical engine, CLI, required application libraries, notices, and a minimal editable project. See the [runtime package guide](runtime-package.md).

If you want to compile the engine yourself, use [the build guide](building.md). The normal source build creates both the graphical runtime and the separate CLI. A source clone is not a precompiled application.

## 2. Open the quickstart project

The source repository includes a small starter project in `examples/quickstart/`. In a runtime package, the single starter project lives directly inside `CX-Engine/`, alongside the graphical executable and CLI; there is no separate `project/` or `bin/` folder.

To launch the repository example from the source root:

```sh
./build/cx-engine examples/quickstart
```

On Windows, run `cx-engine.exe` from the configured build output folder and pass the path to `examples/quickstart`.

With a prebuilt runtime package, run the executable against the `CX-Engine/` folder from the extracted archive root:

- Windows: `CX-Engine\\cx-engine.exe CX-Engine`
- Linux/macOS: `./CX-Engine/cx-engine ./CX-Engine`

The included quickstart demonstrates a working project rather than an empty engine shell. An on-screen HUD shows the sample score and status. Press Space to increment the score and R to reset it; the HUD updates as the script changes its variables. The in-game terminal remains available for engine diagnostics and developer-log messages.

## 3. Create your own project

Create a new empty folder and launch the engine with that folder as the project path. CX can initialize a standard project layout when the folder is empty or contains an unambiguous root-level script. Existing files are not overwritten. For a manual project, create `config.json` and `scripts/main.cx` as shown in the quickstart.

## 4. Write a script

```skript
set {score} to 0
set {status_message} to "Press SPACE to score."

canvas "HUD":
    create panel "score_panel" at (20, 20) size (420, 100) color (12, 20, 32, 225)
    create text "Score: {score}" at (42, 35) font size 28
    create text "{status_message}" at (42, 73) font size 18

on load:
    open canvas "HUD"

on press key "SPACE":
    add 1 to {score}
    if {score} is at least 5:
        set {status_message} to "Milestone reached!"
```

The event header describes when a block runs. Indented statements are the actions. Variables in braces are values managed by the runtime. Canvas text interpolates their current values on screen, so the score changes without rebuilding the canvas. See the [CX Script learning guide](learn-cx-script.md) and [full syntax reference](syntax.md).

## 5. Check scripts

Before opening the graphical runtime, you can ask the CLI to parse every script in a project:

- Windows: `CX-Engine\\cx-cli.exe --check CX-Engine` (runtime package) or `build/Release/cx-cli.exe --check examples/quickstart` (source build)
- Linux/macOS: `./CX-Engine/cx-cli --check ./CX-Engine` (runtime package) or `./build/cx-cli --check examples/quickstart` (source build)

This checks syntax and emits file/line diagnostics. It does not prove that a behavior looks correct on screen.

## 6. Learn what to try next

The `examples/` folder has small independent examples for loops, lists, functions, entities, collision filters, triggers, interaction focus, projectiles, animation clips, tweens, lighting effects, and audio generation. Read one example at a time and check it with `cx-cli --check`.

## Troubleshooting

- If the graphical executable was not built, configure with the normal default (`CX_BUILD_GRAPHICAL_RUNTIME=ON`) and follow the [build guide](building.md).
- On Linux Wayland sessions, make sure XWayland is installed and available to the session. The current SFML backend does not open a native Wayland surface.
- If the game window opens but the project reports syntax errors, run the CLI check and correct the listed file and line.
- If the local room editor does not load, verify that Python 3 is installed and that you started its server from the project directory as described in [Room Studio](room-studio.md).
