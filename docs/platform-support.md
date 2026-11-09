# Platform support

CX uses a C++20 core with a platform-specific graphical runtime. The project aims for one portable game-project format, but native binaries and native libraries must match the operating system and CPU architecture.

## Current evidence

| Platform | State | Evidence / limitation |
|---|---|---|
| Linux x86_64 | **Verified development baseline** | GCC 14.2 Release core/CLI and SFML 3.1.0 graphical build, exact source-archive clean extraction build, 19/19 CTest, and a minimal Linux starter runtime smoke test. Physical-device testing is still useful. |
| Windows x86_64 | **Not yet verified** | Windows-specific persistent storage code, MSVC compile settings, Windows font paths, and a native CI job are present, but no native compile or runtime result has been recorded. Do not treat it as supported until native CI and graphical smoke tests pass. |
| Linux ARM64 | Planned / unverified | No completed native runtime validation yet. |
| Windows ARM64 | Planned / unverified | No completed native runtime validation yet. |
| macOS x86_64 | Planned / unverified | No completed native runtime validation yet. |
| macOS ARM64 | Planned / unverified | No completed native runtime validation yet. |

A six-target CI matrix is configured in `.github/workflows/ci.yml`. Native Windows, ARM64, and macOS results must still be produced and reviewed before those targets are advertised as supported. An architecture is not a supported download until the build, tests, package extraction, starter launch, and runtime dependency checks all pass on that architecture.

## Wayland desktops

The graphical host currently uses SFML 3.1.0. SFML's Linux window backend does **not** currently provide a native Wayland implementation. On a Wayland session it uses XWayland. This is expected to work on common environments such as GNOME Wayland, KDE Plasma Wayland, Sway, and Hyprland when XWayland is installed, enabled, and the application receives a valid X11 display. Individual compositor configurations still need testing.

The CX-specific X11 file-drop adapter is optional. If X11 development libraries are unavailable at build time, file dropping is disabled and the in-game `/attach` command remains available. That adapter is not required for the game window itself.

A Wayland session without a working XWayland server cannot currently run the SFML graphical window. That is a limitation of the selected multimedia backend, not a special restriction in CX's project format. A native Wayland backend would require SFML to add one or CX to adopt a different window/input backend.

## Chromebook / ChromeOS

ChromeOS and Linux container configurations vary. CX has not been validated across all Chromebook Linux-container display arrangements. In particular, environments that do not provide an accessible XWayland display may fail to open the current SFML window. Do not infer Chromebook support from the fact that the same project works in a standard Linux desktop session.

## Portable project format

The portable project consists of `.cx` scripts, room JSON, assets, configuration, and documented data files. A player should be able to keep that project unchanged and run it with the native engine package matching their OS and architecture. A Windows executable itself cannot be swapped into a Linux package; the project data can be shared while the native executable and its libraries are platform-specific.
