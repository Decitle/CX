# Testing and validation

CX uses CTest for its C++ and Python regression suite. The development branch is validated on six native targets: Linux x86_64/ARM64, Windows x86_64/ARM64, and macOS x86_64/ARM64. The latest completed six-target green run is [run #103](https://github.com/Decitle/CX/actions/runs/43563396396), which passed build, CTest, example syntax checks, and runtime packaging on all six platforms for branch head `bd0c5a3`. Linux and Windows also passed graphical startup smoke tests. The event-context runtime assertions described below are a subsequent increment and require a fresh run on their new branch head.

The CI workflow runs CTest on every native target. Graphical startup smoke tests run on Linux and Windows runners; macOS graphical startup is not currently covered by that automated smoke step. Native build/test evidence is separate from physical-device checks of rendering, scaling, mouse behavior, compositors, and performance.

## Build and run tests

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel
    ctest --test-dir build --output-on-failure

For Visual Studio generators, add -C Release to CTest:

    ctest --test-dir build -C Release --output-on-failure

## What the suite covers

The current CTest targets cover the scripting runtime, project scaffolding, persistent-variable manifests, cross-platform threaded save locking, attack-pattern geometry, projectiles, collision channels, motion/tweens, textured-beam geometry, animation clips, room collision, interaction focus, deterministic random streams, language expressions, typed object references, event syntax and runtime event-context payloads, variable-effect syntax, room transitions, loops, screen effects, delayed-task lifecycle, the positive/negative subsystem syntax corpus, and room transitions. Linux-only tests additionally cover process-level save/recovery behavior and Wayland startup guarding. The suite also includes Room Studio server validation and source/runtime packaging tests.

The process-level storage interruption tests are intentionally Linux-only because they use POSIX process and file-lock APIs. A separate cross-platform threaded-save test exercises the project lock and verifies that the final CSV/cache/manifest generation remains coherent; native Windows CI runs that test through the LockFileEx implementation. Multi-process Windows crash/recovery coverage remains a separate future improvement.

The subsystem syntax corpus checks both canonical word-spaced forms and legacy aliases, verifies normalized parser fields, and rejects malformed values, ambiguous anchors, and event headers missing required selectors. Runtime integration tests separately assert every documented payload field across trigger, focus, projectile-hit, tween-completion, animation-completion and file-attachment events. The file-attachment test also verifies that event locals survive a `wait` suspension and are available after resumption. Existing lifecycle cases exercise object replacement/deletion and room-context invalidation; parser acceptance alone is not treated as proof of runtime behavior.

The Python packaging tests exercise source archive verification, Windows runtime ZIP contents, and release-reference tag pinning. They verify the expected package layout, bundled SFML license, manifest entries, checksum sidecar, exclusion of Windows system components, and correct versioned guide links. These checks validate packaging logic, not the architecture or launchability of a real Windows executable; native Windows CI covers the actual build and graphical startup smoke test.

## Syntax-check every example script

The development CI workflow runs:

    python tools/check_examples.py --cli ./build/cx-cli --examples examples

On Windows CI, the equivalent command points to ./build/Release/cx-cli.exe. The helper finds every .cx file under examples/. A directory containing a scripts/ folder is checked as one multi-file project; each remaining standalone .cx file is copied into a fresh temporary project and checked independently.

This invokes cx-cli --check, which parses script files and reports syntax diagnostics without starting the graphical runtime or modifying the example project. It is a parser/grammar check, not proof that assets exist, runtime expressions execute successfully, or a window renders. Runtime semantics are covered by CTest; graphical initialization is covered separately by the smoke tests. This separation makes failures easier to diagnose.

## Graphical smoke tests

A passing CTest suite does not prove that a window opens or that rendering looks correct. CI separately launches the graphical runtime in a display-capable environment on Linux and Windows and confirms that it remains alive long enough to initialize a starter project. macOS graphical startup is not currently covered by that automated smoke step. Manual checks on physical displays are still needed for mouse behavior, scaling, fonts, resize behavior, compositors, and performance.

## Before a release

The tag-triggered release workflow runs a native build, CTest, starter-project validation, runtime dependency installation, and package creation for each of six targets. A separate documentation job creates a self-contained HTML reference from the exact tag. The publish job depends on all six runtime jobs and this versioned reference job. GitHub supplies its standard automatic source-code ZIP/TAR.GZ snapshots; the workflow uploads only six native runtime packages and the matching HTML reference.

The published v0.10.14-preview.8 release workflow completed successfully in [run 42961733306](https://github.com/Decitle/CX/actions/runs/42961733306). The v0.11.0 development branch is not ready to tag: the remaining event/subsystem grammar audit, expanded syntax corpus, beginner/example alignment, final-head six-target validation, compatibility review, and final clean-extraction/launch checks remain release gates.
