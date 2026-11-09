# CX Engine starter runtime

This folder contains a native CX Engine build and a minimal starter project. It is intended to help you begin experimenting without installing a C++ compiler or creating a project layout by hand. Packages contain only executables, non-system shared runtime libraries, license notices, and the starter project; SDK headers, static development libraries, CMake metadata, and operating-system DLLs are deliberately excluded.

## Requirements

Use the package built for your operating system and CPU architecture. Do not replace a Windows executable with a Linux executable or mix libraries from another package. The game project files themselves are portable and can be reused with another supported native runtime.

The package includes the application libraries that can be redistributed with the build. It cannot include operating-system components, graphics drivers, or a desktop display server. On Linux Wayland sessions, the current SFML backend requires XWayland.

## Start the graphical engine

- Windows: run `bin/cx-engine.exe project` from PowerShell or Command Prompt.
- Linux/macOS: run `./bin/cx-engine ./project` from a terminal.

The included `project/` folder is a minimal editable starter, not a sample game.

## Check the project

- Windows: `bin/cx-cli.exe --check project`
- Linux/macOS: `./bin/cx-cli --check ./project`

The CLI checks scripts and reports file/line diagnostics. It does not display the game.

## Edit the starter

- `project/config.json` contains baseline window and engine settings.
- `project/scripts/main.cx` is the first script to edit.

Keep a backup of your project before experimenting with new engine builds. The engine does not overwrite existing project files during normal first-run scaffolding.

## Documentation

The matching source distribution and the project documentation describe the scripting language, project layout, and platform limitations. The `LICENSE` and `THIRD_PARTY_NOTICES.md` files describe redistribution terms for this runtime package.
