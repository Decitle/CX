# Building CX Engine

## Requirements

- CMake 3.24 or newer.
- A compiler with C++20 support (MSVC 2022+, GCC 13+, or Clang 16+ are recommended starting points).
- Git, when CMake needs to fetch SFML 3.1.0.
- SFML 3.1.0 development dependencies for the graphical application. The default configuration fetches the pinned SFML release when no installed package is found.
- Python 3 for the Room Studio helper and Python-based test coverage.

The normal build includes the graphical runtime. It fails clearly if SFML cannot be found or fetched; it will not silently produce only the CLI and claim the graphical engine built.

## Linux

Install your distribution's compiler, CMake, Git, Python 3, X11/XWayland development libraries, OpenGL development files, and the audio dependencies required by SFML if you want playback enabled. On Debian/Ubuntu runners, the CI workflow installs the required packages. Distribution package names vary.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

If the SFML audio development dependencies are unavailable, you can disable only graphical playback while keeping the graphical renderer/input frontend:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCX_ENABLE_SFML_AUDIO=OFF
```

## Wayland desktops

The current Linux graphical frontend uses SFML 3.1.0. SFML does not yet provide a native Wayland window backend; on Wayland desktops it opens through **XWayland**. This normally works on GNOME, KDE Plasma, Sway, Hyprland, and other compositors when XWayland is installed and enabled. A compositor/session without a working XWayland server cannot currently open the CX graphical window.

The optional file-drop bridge uses X11/XWayland. If the X11 development headers were unavailable at build time, native file dropping is omitted; the in-game `/attach` command remains available. This optional feature does not determine whether the graphical window can open.

## Windows

Use Visual Studio 2022 or newer with the C++ desktop workload, CMake, and Git. From a Developer PowerShell or terminal where MSVC is available:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The first configure fetches SFML 3.1.0 if it is not already installed. The resulting graphical application is `build\\Release\\cx-engine.exe` with Visual Studio generators. GitHub Actions run #10 passed native Windows x86_64 and ARM64 builds, CTest, and starter-project validation; the graphical startup smoke test also passed on both Windows runners.

A successful regular `windows-x86_64` CI run uploads a downloadable starter-runtime ZIP. Open the repository's **Actions** tab, select the successful **Build and test** run, and download the `CXEngine-Windows-x86_64-...` artifact. It is a short-lived testing artifact (14-day retention), not a versioned GitHub Release. For normal use, prefer the versioned runtime package on the [Releases page](https://github.com/Decitle/CX/releases).

## macOS

Install Xcode command-line tools, CMake, and Git, then use the same configure/build/test sequence as Linux. Native Intel and Apple Silicon builds are separate targets in the CI matrix. The latest native matrix passed build, CTest, and starter-project validation on both; graphical startup smoke is not currently automated on macOS.

## CMake options

| Option | Default | Purpose |
|---|---:|---|
| `CX_BUILD_GRAPHICAL_RUNTIME` | `ON` | Build the graphical desktop application. This is the normal product build. |
| `CX_FETCH_SFML` | `ON` | Fetch the pinned SFML 3.1.0 release if no suitable package is installed. |
| `CX_ENABLE_SFML_AUDIO` | `OFF` | Enable SFML audio playback when the dependency is available. |
| `CX_ENABLE_X11_FILE_DROP` | `ON` | Build the optional X11/XWayland native file-drop adapter on Linux. |
| `CX_BUILD_TESTS` | `ON` | Build the C++ and Python regression suite. |

A headless core/CLI build is available for server-side tests and development, but must be explicitly requested:

```sh
cmake -S . -B build-headless -DCX_BUILD_GRAPHICAL_RUNTIME=OFF
```

## Installed SFML package

If you install SFML yourself, set `SFML_DIR` to the directory containing `SFMLConfig.cmake`:

```sh
cmake -S . -B build -DSFML_DIR=/path/to/SFML/lib/cmake/SFML -DCX_FETCH_SFML=OFF
```

## Build outputs

- `cx-engine` — the graphical 2D runtime.
- `cx-cli` — project checks and non-graphical diagnostics.
- `cx_core` — the shared C++ engine core library.
- CTest executables — regression tests; not part of user runtime packages.

Use `cmake --build build --config Release --target help` to inspect targets for the generator in use.
