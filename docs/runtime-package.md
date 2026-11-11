# CX Engine runtime package

This archive is a portable starter distribution for one operating system and CPU architecture. It includes the native graphical runtime, the command-line checker, two copies of the starter project, the libraries required by that build, and one consolidated license file. It does not include C++ headers, static development libraries, CMake metadata, build caches, or operating-system DLLs.

## Package layout

- **CX-Engine/** contains cx-engine, cx-cli, any redistributable shared runtime libraries, and a ready-to-run copy of the starter project.
- **project/** is a second clean starter project at the archive root. Copy it to create a separate game project or edit it in place.
- **LICENSES.txt** contains the complete CX Engine BSD 3-Clause license and the complete SFML zlib/libpng license in clearly separated sections.
- CX-Engine/runtime-manifest.json records the version, target, file sizes, and SHA-256 hashes of the packaged files.

Use the archive built for your operating system and CPU architecture. Do not mix binaries or shared libraries from different platform packages. The project files themselves are portable between supported runtime targets.

## Start the graphical engine

From the extracted archive's root directory:

- Windows: CX-Engine\cx-engine.exe project
- Linux/macOS: ./CX-Engine/cx-engine ./project

To run the quickstart project stored directly inside the engine folder instead, pass CX-Engine as the project path:

- Windows: CX-Engine\cx-engine.exe CX-Engine
- Linux/macOS: ./CX-Engine/cx-engine ./CX-Engine

The engine folder's config.json and scripts/ are a self-contained quickstart. The separate root project/ folder is the recommended starting point for a project you intend to edit independently.

The graphical runtime includes only application libraries that can be redistributed with the build. It does not include operating-system components, graphics drivers, or a desktop display server. On Linux Wayland sessions, the current SFML backend requires XWayland.

## Check a project

- Windows: CX-Engine\cx-cli.exe --check project
- Linux/macOS: ./CX-Engine/cx-cli --check ./project

The CLI checks scripts and reports file/line diagnostics. It does not display the game. Run the same command with CX-Engine instead of project to check the built-in quickstart.

## Edit the starter

- project/config.json contains the baseline window and engine settings.
- project/scripts/main.cx is the first script to edit.
- CX-Engine/config.json and CX-Engine/scripts/main.cx are the built-in quickstart copy.

Keep a backup of your project before experimenting with new engine builds. The engine does not overwrite existing project files during normal first-run scaffolding.

## Documentation and licensing

The version-matched online documentation describes the scripting language, project layout, and platform limitations. LICENSES.txt is the single runtime-package location for CX Engine and SFML license terms. The SFML notice is reproduced in full; the licenses remain separate and are not replaced by one another.
