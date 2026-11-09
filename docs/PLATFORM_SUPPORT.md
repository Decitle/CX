# Platform support

CX aims to let a game project move between operating systems without rewriting its scripts, rooms, or assets. That requires more than compiling the same C++ code: file handling, timing, input, fonts, graphics, audio, dependencies, and project-path behavior must remain compatible.

## Verified status at development checkpoint v0.10.14

| Platform | Development status | Evidence |
|---|---|---|
| Linux x86_64 | **Verified development target** | GCC 14.2, SFML 3.1.0 graphical Release build, headless Release build, clean-source build, and 17/17 CTest in each recorded configuration |
| Windows x86_64 | **Not yet verified** | The wider development code contains `_WIN32` persistence paths, a Windows font fallback, and MSVC compile settings; no native Windows build/test result is recorded |
| Windows ARM64 | Not yet verified | No build or runtime evidence |
| Linux ARM64 | Not yet verified | No build or runtime evidence |
| macOS x86_64 | Not yet verified | No build or runtime evidence |
| macOS ARM64 | Not yet verified | No build or runtime evidence |

A conditional code path is evidence of design intent, not proof that a complete build or runtime works. The current GitHub checkout is also incomplete and cannot be used to perform the full-engine build described here.

## Planned target matrix

The release engineering target is:

- Windows x86_64
- Windows ARM64
- Linux x86_64
- Linux ARM64
- macOS x86_64
- macOS ARM64

The first platform milestone should establish Windows x86_64 alongside the existing Linux x86_64 baseline. ARM64 and macOS targets should be added as native CI builds and runtime checks become reliable. Unsupported or untested targets must remain clearly marked rather than silently appearing in release downloads.

32-bit x86 is not currently a planned target. It would add maintenance and dependency constraints without being a priority for the intended modern desktop hardware.

## Native binaries and portable game projects

A Windows executable cannot run directly on Linux, and a Linux executable cannot run directly on Windows. Each operating system and CPU architecture needs a matching native runtime.

The portable part is the **game project**:

- `.cx` scripts
- room and project data
- textures and other game assets
- configuration and other documented data files

A player downloads the runtime that matches their operating system and CPU architecture, places it beside the same game project, and launches it. The project's scripts and assets should not need to be rewritten for each target.

This compatibility promise must be backed by automated tests using the same example projects on each target. Platform-specific native APIs should stay inside small platform layers rather than leak into the scripting and project formats.

## Platform-specific validation still required

### Windows

- Build the graphical application and CLI with MSVC.
- Verify Windows-specific persistent-variable locking, safe writes, replacement, and recovery.
- Test Unicode paths, spaces in paths, and user-writable locations.
- Test display resizing, DPI scaling, keyboard/mouse input, font fallback, and window close behavior.
- Package and test the SFML runtime DLLs and any required compiler runtime components.

### Linux

- Keep x86_64 as the current baseline and add ARM64.
- Test both X11 and Wayland sessions where supported by the selected SFML build.
- Record the minimum supported distribution/runtime-library baseline.
- Bundle permitted third-party runtime libraries or document unavoidable system requirements; never imply that graphics drivers or the operating system itself are included.

### macOS

- Build native x86_64 and ARM64 outputs.
- Verify the app bundle, resource paths, fonts, input, window behavior, and required frameworks.
- Decide whether signed/notarized distribution is needed before calling the download convenient for general users.

## Release rule

A platform is listed as **supported** only after a native build, automated tests, a clean package extraction, starter-project launch, and a short graphical smoke test have passed on that platform. A cross-compiled binary without a native runtime check is not enough.
