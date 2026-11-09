# Project status

**Development checkpoint:** v0.10.14-preview.5 (pre-release candidate; no official release tag yet)  
**Source repository:** complete curated source tree merged into `main`  
**Native CI baseline:** all six configured platform/architecture jobs passed build, CTest, and starter-project validation in run #6  
**Latest source-safety verification:** follow-up CI run #7 passed all six targets, including the final disposable-smoke-project and generated-save-state safeguards  
**Release status:** release workflow is configured; no GitHub Release or downloadable official release assets have been published yet

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

GitHub Actions run #6 completed successfully for all six configured targets:

| Target | Native build | CTest | Starter CLI check | Graphical smoke in CI |
|---|---:|---:|---:|---:|
| Linux x86_64 | Pass | Pass | Pass | Pass (Xvfb) |
| Linux ARM64 | Pass | Pass | Pass | Pass (Xvfb) |
| Windows x86_64 | Pass | Pass | Pass | Pass |
| Windows ARM64 | Pass | Pass | Pass | Pass |
| macOS x86_64 | Pass | Pass | Pass | Not run by current workflow |
| macOS ARM64 | Pass | Pass | Pass | Not run by current workflow |

Run #7 passed the exact merged source revision across all six native targets, including the final package/smoke safeguards. See [GitHub Actions](https://github.com/Decitle/CX/actions) for the recorded jobs and future changes. A green CI result establishes that the configured build and tests passed on those runners; it does not replace physical-device testing on every Windows, macOS, Linux distribution, compositor, or Chromebook.

### Runtime package quality

The runtime packager copies executables and application runtime libraries rather than an entire SDK install tree. Windows operating-system DLLs, headers, static development libraries, CMake metadata, and SDK documentation are excluded. The packager rejects generated save/cache/lock/temp files in the starter project. Graphical smoke tests use a disposable copy so first-run scaffolding cannot pollute the tracked example.

The CI workflow uploads a short-lived Windows x86_64 starter package artifact. Official versioned releases are built separately by the release workflow and include source archives plus each native runtime package whose release job succeeds.

## Important unfinished areas

- Native Wayland backend; the current Linux SFML window backend relies on XWayland.
- General moving-body physics and broader collision response.
- More complete Room Studio tools and undo/redo.
- UI widget/menu coverage and coordinate/DPI verification across physical devices.
- Full real-time audio mixer and playback automation. SFML Audio is disabled in the current build configuration.
- Repeatable performance benchmarks and larger-world optimization.
- Further first-run/editor polish and the broader v1.0 feature-completion roadmap.

## Completion estimate

The project has a substantial usable foundation, but it is not yet v1.0 feature-complete. Native build/test coverage is now much broader; the main remaining risks are user-experience polish, moving-body physics, audio playback, performance profiling, physical-device QA, and the final release/download process. The version number is not a completion percentage.

## Download and first use

When the first versioned release is published, download the runtime matching the computer's operating system and CPU architecture from [GitHub Releases](https://github.com/Decitle/CX/releases/latest). Extract the package and run the included graphical executable with the included `project/` folder. No C++ compiler is required for a runtime package. The `cx-cli` executable is available for checking project scripts without opening a window.
