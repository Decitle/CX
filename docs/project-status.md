# Project status

**Development checkpoint:** v0.10.14-preview.5 candidate  
**Source repository:** complete curated source tree merged into `main`  
**Latest completed native CI matrix:** run #10, all six configured targets passed on commit `a690dc023a072c6d5de797e276128491505a1d2b`  
**Release pipeline:** source ZIP/TAR.GZ plus six native runtime packages are configured; the first tag-triggered release run must complete successfully before those curated assets are considered published.

## Implemented foundation

The engine contains a C++20 core, parser and scripting runtime, local/event/transient/persistent variable lifetimes, function and loop support, waits and scheduled handlers, transactional script reload, project scaffolding, data-driven rooms, asset discovery, tile collision metadata, entities and hitboxes, collision channels, triggers, interaction focus, projectiles, tweens, animation clips, seeded random streams, textured-beam/effect geometry, canvas UI, lighting meshes, a graphical terminal, a separate CLI, persistent storage recovery, and a local Room Studio tool.

## Current verified checks

### Local validation

- Full Linux x86_64 SFML 3.1.0 graphical Release build: **19/19 CTest targets passed**.
- Latest headless/core regression build: **18/18 CTest targets passed**.
- Latest package-tool regression suite: **8/8 passed**.
- Room Studio HTTP integration suite: **9/9 passed**.
- Clean source archive extraction, package manifests, and checksums verified.
- Linux x86_64 starter runtime passed a clean extraction, dependency scan, CLI starter check, and virtual-display graphical startup smoke.

### Native platform matrix

GitHub Actions run #10 completed successfully for all six configured targets:

| Target | Native build | CTest | Starter CLI check | Graphical smoke in CI |
|---|---:|---:|---:|---:|
| Linux x86_64 | Pass | Pass | Pass | Pass (Xvfb) |
| Linux ARM64 | Pass | Pass | Pass | Pass (Xvfb) |
| Windows x86_64 | Pass | Pass | Pass | Pass |
| Windows ARM64 | Pass | Pass | Pass | Pass |
| macOS x86_64 | Pass | Pass | Pass | Not run by current workflow |
| macOS ARM64 | Pass | Pass | Pass | Not run by current workflow |

See [GitHub Actions](https://github.com/Decitle/CX/actions/runs/42566278962) for the recorded run and the [Actions history](https://github.com/Decitle/CX/actions) for newer runs. A green CI result establishes that the configured build and tests passed on those runners; it does not replace physical-device testing on every Windows, macOS, Linux distribution, compositor, or Chromebook.

### Runtime package quality

The runtime packager copies executables and application runtime libraries rather than an entire SDK install tree. Windows operating-system DLLs, headers, static development libraries, CMake metadata, and SDK documentation are excluded. The packager rejects generated save/cache/lock/temp files in the starter project. Graphical smoke tests use a disposable copy so first-run scaffolding cannot pollute the tracked example.

The regular CI workflow uploads a short-lived Windows x86_64 starter package artifact. Official versioned releases are built separately by the release workflow. That workflow has six native runtime targets and an all-or-nothing publish dependency: if a source or runtime job fails, the Release publication job is skipped until the failure is resolved.

## Important unfinished areas

- Native Wayland backend; the current Linux SFML window backend relies on XWayland.
- General moving-body physics and broader collision response.
- More complete Room Studio tools and undo/redo.
- UI widget/menu coverage and coordinate/DPI verification across physical devices.
- Full real-time audio mixer and playback automation. SFML Audio is disabled in the current build configuration.
- Repeatable performance benchmarks and larger-world optimization.
- Further first-run/editor polish and the broader v1.0 feature-completion roadmap.

## Completion estimate

The project has a substantial usable foundation, but it is not yet v1.0 feature-complete. Native build/test coverage is now much broader; the main remaining risks are user-experience polish, moving-body physics, audio playback, performance profiling, physical-device QA, and proving the first tagged multi-platform packaging run. The version number is not a completion percentage.

## Download and first use

Use [GitHub Releases](https://github.com/Decitle/CX/releases) for a versioned runtime package when one is available. Extract the package matching the computer's operating system and CPU architecture, then run the included graphical executable with the included `project/` folder. No C++ compiler is required for a runtime package. The `cx-cli` executable is available for checking project scripts without opening a window.
