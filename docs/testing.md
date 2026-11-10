# Testing and validation

CX uses CTest for its C++ and Python regression suite. GitHub Actions run #10 passed the native build, CTest, and starter-project validation on all six configured targets: Linux x86_64/ARM64, Windows x86_64/ARM64, and macOS x86_64/ARM64. Graphical startup smoke tests passed on Linux and Windows runners; the current workflow does not run that smoke test on macOS.

CTest target counts vary by platform because the process-level save/recovery test and the Wayland display guard are Linux-only. Do not compare raw test counts between operating systems as though every target were identical.

## Build and run tests

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

For Visual Studio generators, add `-C Release` to CTest:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## What the suite covers

The current test targets cover the scripting runtime, project scaffolding, persistent-variable manifests, cross-platform threaded save locking, attack-pattern geometry, projectiles, collision channels, motion/tweens, textured-beam geometry, animation clips, room collision, interaction focus, deterministic random streams, delayed-task lifecycle, room transitions, Linux process-level save/recovery behavior, Room Studio server validation, Wayland startup guarding, and release-source packaging.

The process-level storage interruption tests are intentionally Linux-only because they use POSIX process and file-lock APIs. A separate cross-platform threaded-save test exercises the project lock and verifies that the final CSV/cache/manifest generation remains coherent; native Windows CI runs that test through the `LockFileEx` implementation. Multi-process Windows crash/recovery coverage remains a separate future improvement.

The Python packaging test exercises the Windows runtime ZIP path with fixture `.exe` and `.dll` files. It verifies the expected package layout, bundled SFML license, manifest entries, and ZIP checksum sidecar on every platform. This validates packaging logic, not the validity or launchability of a native Windows executable; the latter is covered by the Windows CI build and graphical smoke test.

## Latest Linux x86_64 validation

- Headless and SFML-enabled Release configurations: **19/19 CTest passed**.
- Clean-source ZIP extraction with SFML 3.1.0: **19/19 CTest passed**.
- Every top-level `.cx` example passed `cx-cli --check`.
- The minimal Linux runtime bundle launched for an eight-second Xvfb smoke test; `ldd` found no unresolved bundled dependencies.
- The no-XWayland guard regression confirmed that a Wayland session without `DISPLAY` exits before modifying/scaffolding a project.

These local build and package checks are Linux x86_64 results. See the native CI matrix above for the other OS/architecture runner results.

## Graphical smoke tests

A passing CTest suite does not prove that a window opens or that rendering looks correct. CI separately launches the graphical runtime in a display-capable environment on Linux and Windows and confirms that it remains alive long enough to initialize a starter project. macOS graphical startup is not currently covered by that automated smoke step. Manual checks on physical displays are still needed for mouse behavior, scaling, fonts, resize behavior, compositors, and performance.

## Before a release

The tag-triggered release workflow performs native build, CTest, starter-project validation, runtime installation, and packaging for each of its six targets. The publish job depends on every source/runtime job, so a failed target prevents a partially populated formal release. After the workflow succeeds, check the release page for both curated source archives, six runtime packages, and their manifests/checksums.
