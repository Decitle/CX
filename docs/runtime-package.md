# CX Engine runtime package

This archive is a portable starter distribution for one operating system and CPU architecture. It includes the native graphical runtime, the command-line checker, one ready-to-run starter project, the libraries required by that build, and one consolidated license file. It does not include C++ headers, static development libraries, CMake metadata, build caches, or operating-system DLLs.

## Package layout

- CX-Engine/ contains cx-engine, cx-cli, redistributable shared runtime libraries, and the starter project. The project files are directly in this folder; there is no second project directory.
- README.md contains these package-level startup instructions.
- LICENSES.txt contains the complete CX Engine BSD 3-Clause license, SFML's zlib/libpng license, and applicable notices for third-party components included in the build.
- CX-Engine/runtime-manifest.json records the version, target, file sizes, and SHA-256 hashes of the packaged files.

Use the archive built for your operating system and CPU architecture. Do not mix binaries or shared libraries from different platform packages.

## Start the graphical engine

From the extracted archive's root directory:

- Windows: CX-Engine\cx-engine.exe CX-Engine
- Linux/macOS: ./CX-Engine/cx-engine ./CX-Engine

The project is contained directly in CX-Engine/, alongside the executables and any redistributable shared libraries. The engine reads its configuration and scripts from that directory.

## Check the included project

- Windows: CX-Engine\cx-cli.exe --check CX-Engine
- Linux/macOS: ./CX-Engine/cx-cli --check ./CX-Engine

The CLI checks scripts and reports file/line diagnostics. It does not display the game.

## Edit the project

- CX-Engine/config.json contains the baseline window and engine settings.
- CX-Engine/scripts/main.cx is the first script to edit.
- Additional rooms, textures, audio, fonts, and project-local tools can be added in the documented project folders.

Keep a backup of your project before experimenting with new engine builds. The engine does not overwrite existing project files during normal first-run scaffolding.

## Requirements and documentation

The graphical runtime includes only application libraries that can be redistributed with the build. It does not include operating-system components, graphics drivers, or a desktop display server. On Linux Wayland sessions, the current SFML backend requires XWayland.

The version-matched online documentation describes the scripting language, project layout, and platform limitations. LICENSES.txt is the single runtime-package location for CX Engine and applicable third-party license terms; each license remains clearly separated and reproduced in full where required.
