# Testing and validation

CX uses CTest for its C++ and Python regression suite. The Linux x86_64 v0.10.14 graphical Release build passes 19/19 CTest targets, including a second 19/19 run after extracting the exact source ZIP into a clean directory and rebuilding against SFML 3.1.0. Native Windows, ARM64, and macOS results are not yet verified; workflows are configured but have not produced native results.

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

The current test targets cover the scripting runtime, project scaffolding, persistent-variable manifests, attack-pattern geometry, projectiles, collision channels, motion/tweens, textured-beam geometry, animation clips, room collision, interaction focus, deterministic random streams, delayed-task lifecycle, room transitions, process-level save/recovery behavior on Linux, Room Studio server validation, and release-source packaging.

The process-level storage interruption tests are intentionally Linux-only because they use POSIX process and file-lock APIs. A separate cross-platform threaded-save test exercises the project lock and verifies that the final CSV/cache/manifest generation remains coherent; native Windows CI runs that test through the `LockFileEx` implementation. Multi-process Windows crash/recovery coverage remains a separate future improvement.

The Python packaging test also exercises the Windows runtime ZIP path with fixture `.exe` and `.dll` files. It verifies the expected package layout, bundled SFML license, manifest entries, and ZIP checksum sidecar on every platform. This validates packaging logic, not the validity or launchability of a native Windows executable; the latter is covered by the Windows CI build and graphical smoke test.

## Latest Linux x86_64 validation

- Headless and SFML-enabled Release configurations: 19/19 CTest passed.
- Clean-source ZIP extraction with SFML 3.1.0: 19/19 CTest passed.
- Every top-level `.cx` example passed `cx-cli --check`.
- The minimal Linux runtime bundle launched for an eight-second Xvfb smoke test; `ldd` found no unresolved bundled dependencies.
- The no-XWayland guard regression confirmed that a Wayland session without `DISPLAY` exits before modifying/scaffolding a project.

These results are Linux x86_64 validation only; they do not establish Windows or other architecture support.

## Graphical smoke tests

A passing CTest suite does not prove that a window opens or that rendering looks correct. CI should separately launch the graphical runtime in a display-capable environment and confirm that it remains alive long enough to initialize a starter project. Manual checks on physical displays are still needed for mouse behavior, scaling, fonts, resize behavior, compositors, and performance.

## Before a release

1. Build the graphical application and CLI on a native runner for the target OS/architecture.
2. Run CTest.
3. Run the CLI against `examples/quickstart`.
4. Extract the package into a clean directory and resolve its runtime dependencies.
5. Launch the graphical runtime with the quickstart project.
6. Verify checksums and ensure no tests, build products, caches, or example game archives leak into the runtime package.
