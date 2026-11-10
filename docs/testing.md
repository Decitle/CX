# Testing and validation

CX uses CTest for its C++ and Python regression suite. GitHub Actions [run #13](https://github.com/Decitle/CX/actions/runs/42743276494) passed the native build, CTest, and starter-project validation on all six configured targets: Linux x86_64/ARM64, Windows x86_64/ARM64, and macOS x86_64/ARM64. Graphical startup smoke tests passed on Linux and Windows runners; the current workflow does not run that smoke test on macOS.

CTest target counts vary by platform because the process-level save/recovery test and the Wayland display guard are Linux-only. Do not compare raw test counts between operating systems as though every target were identical.

## Build and run tests

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

For Visual Studio generators, add -C Release to CTest:

    ctest --test-dir build -C Release --output-on-failure

## What the suite covers

The current test targets cover the scripting runtime, project scaffolding, persistent-variable manifests, cross-platform threaded save locking, attack-pattern geometry, projectiles, collision channels, motion/tweens, textured-beam geometry, animation clips, room collision, interaction focus, deterministic random streams, delayed-task lifecycle, room transitions, Linux process-level save/recovery behavior, Room Studio server validation, Wayland startup guarding, and source/runtime packaging.

The process-level storage interruption tests are intentionally Linux-only because they use POSIX process and file-lock APIs. A separate cross-platform threaded-save test exercises the project lock and verifies that the final CSV/cache/manifest generation remains coherent; native Windows CI runs that test through the LockFileEx implementation. Multi-process Windows crash/recovery coverage remains a separate future improvement.

The Python packaging tests exercise source archive verification, Windows runtime ZIP contents, and release-reference tag pinning. They verify the expected package layout, bundled SFML license, manifest entries, checksum sidecar, exclusion of Windows system components, and correct versioned guide links. These checks validate packaging logic, not the architecture or launchability of a real Windows executable; native Windows CI covers the actual build and graphical startup smoke test.

## Latest Linux x86_64 validation

- Headless and SFML-enabled Release configurations: 19/19 CTest passed.
- Clean-source ZIP extraction with SFML 3.1.0: 19/19 CTest passed.
- Every top-level .cx example passed cx-cli --check.
- The minimal Linux runtime bundle launched for an eight-second Xvfb smoke test; ldd found no unresolved bundled dependencies.
- The no-XWayland guard regression confirmed that a Wayland session without DISPLAY exits before modifying/scaffolding a project.

These local build and package checks are Linux x86_64 results. See the native CI matrix for the other OS/architecture runner results.

## Graphical smoke tests

A passing CTest suite does not prove that a window opens or that rendering looks correct. CI separately launches the graphical runtime in a display-capable environment on Linux and Windows and confirms that it remains alive long enough to initialize a starter project. macOS graphical startup is not currently covered by that automated smoke step. Manual checks on physical displays are still needed for mouse behavior, scaling, fonts, resize behavior, compositors, and performance.

## Before a release

The tag-triggered release workflow runs a native build, CTest, starter-project validation, runtime dependency installation, and package creation for each of six targets. A separate documentation job creates a self-contained HTML reference from the exact tag. The publish job depends on all six runtime jobs and this versioned reference job. GitHub supplies its standard automatic source-code ZIP/TAR.GZ snapshots; the workflow uploads only six native runtime packages and the matching HTML reference.

The first preview.5 release attempt failed after the Windows ARM64 build and tests had passed: its install-time dependency scan reported five unresolved Windows system components, so publication was skipped. A targeted fix and packaging regression tests are now in main. The next preview tag must complete the entire release workflow before runtime packages should be described as published.
