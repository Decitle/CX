# Platform support

CX uses a C++20 core with a platform-specific graphical runtime. The project aims for one portable game-project format, but native binaries and native libraries must match the operating system and CPU architecture.

## Current native CI evidence

GitHub Actions run #6 completed successfully for the six configured native targets. The matrix built the graphical runtime and CLI, ran CTest, and checked the starter project on each architecture.

| Platform | Native CI state | Current evidence and limits |
|---|---|---|
| Linux x86_64 | **Pass** | Native build, CTest, starter CLI check, and Xvfb graphical startup smoke passed. A Linux x86_64 starter runtime has also passed local clean-extraction, dependency, CLI, and GUI smoke checks. |
| Linux ARM64 | **Pass** | Native build, CTest, starter CLI check, and Xvfb graphical startup smoke passed. Official runtime packaging is performed by the tagged release workflow. |
| Windows x86_64 | **Pass** | Native MSVC build, CTest, starter CLI check, and graphical startup smoke passed. The CI job also generates a short-lived Windows x64 runtime artifact. Final clean-packaging safeguards are being checked in follow-up CI. |
| Windows ARM64 | **Pass** | Native ARM64 build, CTest, starter CLI check, and graphical startup smoke passed. The tagged release workflow is responsible for producing the downloadable runtime package. |
| macOS x86_64 | **Pass** | Native build, CTest, and starter CLI check passed. The current CI workflow does not run a graphical startup smoke test on macOS. |
| macOS ARM64 | **Pass** | Native build, CTest, and starter CLI check passed. The current CI workflow does not run a graphical startup smoke test on macOS. |

The final smoke-test and generated-starter-state safeguards are being rerun on the merged source; see [GitHub Actions](https://github.com/Decitle/CX/actions) for the most recent exact result. These are native CI checks, not physical-device QA. Individual GPU drivers, display scaling, input devices, and desktop/compositor configurations can still differ.

## Wayland desktops

The graphical host currently uses SFML 3.1.0. SFML's Linux window backend does **not** currently provide a native Wayland implementation. On a Wayland session it uses XWayland. This is expected to work on common environments such as GNOME Wayland, KDE Plasma Wayland, Sway, and Hyprland when XWayland is installed, enabled, and the application receives a valid X11 display. Individual compositor configurations still need testing.

The CX-specific X11 file-drop adapter is optional. If X11 development libraries are unavailable at build time, file dropping is disabled and the in-game `/attach` command remains available. That adapter is not required for the game window itself.

A Wayland session without a working XWayland server cannot currently run the SFML graphical window. That is a limitation of the selected multimedia backend, not a special restriction in CX's project format. A native Wayland backend would require SFML to add one or CX to adopt a different window/input backend.

## Chromebook / ChromeOS

ChromeOS and Linux container configurations vary. CX has not been validated across all Chromebook Linux-container display arrangements. In particular, environments that do not provide an accessible XWayland display may fail to open the current SFML window. Do not infer Chromebook support from the fact that the same project works in a standard Linux desktop session.

## Portable project format

The portable project consists of `.cx` scripts, room JSON, assets, configuration, and documented data files. A player should be able to keep that project unchanged and run it with the native engine package matching their OS and architecture. A Windows executable itself cannot be swapped into a Linux package; the project data can be shared while the native executable and its libraries are platform-specific.
