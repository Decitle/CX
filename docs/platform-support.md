# Platform support

CX uses a C++20 core with a platform-specific graphical runtime. The project aims for one portable game-project format, but native binaries and native libraries must match the operating system and CPU architecture.

## Current native CI evidence

GitHub Actions [run #13](https://github.com/Decitle/CX/actions/runs/42743276494) completed successfully for the six configured native targets before the latest release-packaging fix. Each job built the graphical runtime and CLI, ran CTest, and checked the starter project.

| Platform | Native build / CTest / starter check | Graphical startup smoke |
|---|---|---|
| Linux x86_64 | Pass | Pass (Xvfb) |
| Linux ARM64 | Pass | Pass (Xvfb) |
| Windows x86_64 | Pass | Pass |
| Windows ARM64 | Pass | Pass |
| macOS x86_64 | Pass | Not run by current workflow |
| macOS ARM64 | Pass | Not run by current workflow |

These are native CI checks, not physical-device QA. Individual GPU drivers, display scaling, input devices, compositor settings, and specific OS versions can still differ.

## Release packaging status

The first release workflow attempt built and tested all six platforms. The Windows ARM64 post-build install step failed when CMake's dependency scanner encountered five unresolved Windows system components; the other five runtime package jobs completed. Because release publication waits for every runtime job, no partial runtime downloads were attached.

The current main branch adds exact-name exclusions for those five unresolved Windows system DLLs and regression coverage in the runtime packager. Unknown application DLL dependencies are still treated as package content rather than being ignored. A new preview tag is required to run the corrected package workflow.

## Supported package targets

The release matrix targets Windows x86_64 and ARM64, Linux x86_64 and ARM64, and macOS x86_64 and ARM64. There are no 32-bit Windows or Linux packages in the current matrix. 32-bit support would require its own compiler/dependency configuration and native or equivalent runtime validation; it is not implied by the x86_64 builds.

## Wayland desktops

The graphical host currently uses SFML 3.1.0. SFML's Linux window backend does not currently provide a native Wayland implementation. On a Wayland session it uses XWayland. This is expected to work on common environments such as GNOME Wayland, KDE Plasma Wayland, Sway, and Hyprland when XWayland is installed, enabled, and the application receives a valid X11 display. Individual compositor configurations still need testing.

The CX-specific X11 file-drop adapter is optional. If X11 development libraries are unavailable at build time, file dropping is disabled and the in-game /attach command remains available. That adapter is not required for the game window itself.

A Wayland session without a working XWayland server cannot currently run the SFML graphical window. That is a limitation of the selected multimedia backend, not a special restriction in CX's project format. A native Wayland backend would require SFML to add one or CX to adopt a different window/input backend.

## Chromebook / ChromeOS

ChromeOS and Linux container configurations vary. CX has not been validated across all Chromebook Linux-container display arrangements. In particular, environments that do not provide an accessible XWayland display may fail to open the current SFML window. Do not infer Chromebook support from the fact that the same project works in a standard Linux desktop session.

## Portable project format

The portable project consists of .cx scripts, room JSON, assets, configuration, and documented data files. A player should be able to keep that project unchanged and run it with the native engine package matching their OS and architecture. A Windows executable itself cannot be swapped into a Linux package; the project data can be shared while the native executable and its libraries are platform-specific.
