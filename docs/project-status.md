# Project status

**Development checkpoint:** v0.10.14-preview.5 source line  
**Source repository:** complete curated source tree merged into main  
**Latest completed full native CI baseline:** [run #13](https://github.com/Decitle/CX/actions/runs/42743276494), all six configured targets passed on commit 6d0509131f13f41ba485328fe092366ddc4888ea  
**Release state:** the first preview.5 attempt built and tested all six targets but failed during Windows ARM64 dependency installation; the publish job was skipped and no runtime assets were attached. A targeted fix and regression tests are now in main; the next preview tag is required to validate the corrected workflow.

## Implemented foundation

The engine contains a C++20 core, parser and scripting runtime, local/event/transient/persistent variable lifetimes, function and loop support, waits and scheduled handlers, transactional script reload, project scaffolding, data-driven rooms, asset discovery, tile collision metadata, entities and hitboxes, collision channels, triggers, interaction focus, projectiles, tweens, animation clips, seeded random streams, textured-beam/effect geometry, canvas UI, lighting meshes, a graphical terminal, a separate CLI, persistent storage recovery, and a local Room Studio tool.

## Current verified checks

### Local validation

- Full Linux x86_64 SFML 3.1.0 graphical Release build: 19/19 CTest targets passed.
- Latest headless/core regression build: 18/18 CTest targets passed.
- Latest package-tool regression suite before the new Windows component fixtures and reference-packaging test: 8/8 passed.
- Room Studio HTTP integration suite: 9/9 passed.
- Clean source archive extraction, package manifests, and checksums have been verified.
- Linux x86_64 starter runtime passed clean extraction, dependency scan, CLI starter check, and virtual-display graphical startup smoke.

### Native platform matrix

The latest completed full CI matrix before this packaging fix was run #13:

| Target | Native build | CTest | Starter CLI check | Graphical startup smoke |
|---|---:|---:|---:|---:|
| Linux x86_64 | Pass | Pass | Pass | Pass (Xvfb) |
| Linux ARM64 | Pass | Pass | Pass | Pass (Xvfb) |
| Windows x86_64 | Pass | Pass | Pass | Pass |
| Windows ARM64 | Pass | Pass | Pass | Pass |
| macOS x86_64 | Pass | Pass | Pass | Not run by current workflow |
| macOS ARM64 | Pass | Pass | Pass | Not run by current workflow |

These are native CI results, not comprehensive physical-device QA. GPU drivers, display scaling, input devices, OS versions, and desktop/compositor configurations still need broader testing.

### First release attempt

The release workflow for v0.10.14-preview.5 passed the native build, CTest, and quickstart CLI check on all six platforms. Five runtime jobs completed their install/package steps. The Windows ARM64 job failed only during CMake's install-time runtime-dependency scan: it reported five unresolved Windows system DLL names (AzureAttestManager.dll, AzureAttestNormal.dll, HvsiFileTrust.dll, PdmUtilities.dll, and wpaxholder.dll). The publish job depends on all runtime jobs, so GitHub correctly skipped publication rather than attaching a partial set.

The fix uses exact-name pre-exclusions for those five system components and adds regression fixtures proving they are not copied into Windows runtime archives. It does not turn off dependency scanning or broadly ignore unknown DLLs. A new preview tag from the updated main is required; the existing preview.5 tag still points to the failing source revision.

## Runtime package quality

Runtime packages contain the graphical application, CLI, starter project, required application libraries, third-party notices, and manifest/checksum. They exclude the full SDK, static development libraries, headers, CMake metadata, and operating-system DLLs. The source repository remains the authoritative full buildable source.

GitHub automatically supplies the generic Source code (zip) and Source code (tar.gz) snapshots for every tag. The release workflow uploads only the six native runtime packages and a version-matched standalone HTML reference.

## Important unfinished areas

- Native Wayland backend; the current Linux SFML window backend relies on XWayland.
- General moving-body physics and broader collision response.
- More complete Room Studio tools and undo/redo.
- UI widget/menu coverage and coordinate/DPI verification across physical devices.
- Full real-time audio mixer and playback automation. SFML Audio is disabled in the current build configuration.
- Repeatable performance benchmarks and larger-world optimization.
- Further first-run/editor polish and the broader v1.0 feature-completion roadmap.

## Completion estimate

CX has a substantial usable foundation but is not yet v1.0 feature-complete. The main remaining risks are first-use/editor polish, moving-body physics, audio playback, performance profiling, physical-device QA, and proving that all six runtime packages can be built, extracted, and launched from the first successful tagged release. A version number is not a completion percentage.

## Download and first use

Use GitHub Releases for native runtime packages when the corrected release workflow has succeeded. Extract the package matching the computer's operating system and CPU architecture, then run its graphical executable with the included project folder. No C++ compiler is required for a runtime package. The cx-cli executable checks project scripts without opening a window.
