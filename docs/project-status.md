# Project status

**Development checkpoint:** v0.10.14-preview.6 source line  
**Source repository:** complete curated source tree merged into main  
**Latest completed native build/test baseline:** [CI run #20](https://github.com/Decitle/CX/actions/runs/42756628417), all six configured targets passed builds, CTest, and starter-project CLI checks. This run predates the new all-target package-install checks.  
**Release state:** preview.5 and preview.6 have no uploaded runtime assets. Both release attempts passed native compilation and tests but failed in Windows ARM64 at CMake's install-time runtime-dependency scan. The first exclusion patch was incomplete because CMake lowercases Windows DLL names before applying PRE_EXCLUDE_REGEXES; the filter was written with mixed-case names.

## Implemented foundation

The engine contains a C++20 core, parser and scripting runtime, local/event/transient/persistent variable lifetimes, function and loop support, waits and scheduled handlers, transactional script reload, project scaffolding, data-driven rooms, asset discovery, tile collision metadata, entities and hitboxes, collision channels, triggers, interaction focus, projectiles, tweens, animation clips, seeded random streams, textured-beam/effect geometry, canvas UI, lighting meshes, a graphical terminal, a separate CLI, persistent storage recovery, and a local Room Studio tool.

## Current verified checks

### Native build and test baseline

The latest completed six-target build/test run is [run #20](https://github.com/Decitle/CX/actions/runs/42756628417). Linux x86_64/ARM64, Windows x86_64/ARM64, and macOS x86_64/ARM64 all passed native compilation, CTest, and starter-project CLI validation. Graphical startup smoke testing is present on Linux and Windows, not macOS.

This run did not exercise runtime installation/package creation on every target. The expanded CI workflow now adds those exact release steps to all six native jobs; the expanded matrix must pass before preview.7 is tagged.

### Release attempt history

- **v0.10.14-preview.5:** all six native builds and tests passed; Windows ARM64 failed at CMake install with unresolved Windows system DLL names, so publication was skipped.
- **v0.10.14-preview.6:** the same Windows ARM64 install stage failed again. Four DLL names remained unresolved: `AzureAttestManager.dll`, `AzureAttestNormal.dll`, `HvsiFileTrust.dll`, and `PdmUtilities.dll`. The version-matched HTML reference and the other five runtime jobs succeeded, but the all-or-nothing publish job was skipped.

CMake's Windows runtime-dependency scanner lowercases DLL names before matching `PRE_EXCLUDE_REGEXES`. The earlier filter used mixed-case names, so it did not match reliably. The corrected filter uses lowercase exact names. The runtime packager independently filters these five operating-system components from the final Windows package while preserving unknown application DLL dependencies.

### CI and publication safeguards

Ordinary CI previously packaged only Windows x86_64. It now exercises CMake installation and `tools/package_runtime.py` on every configured target: Windows, Linux, and macOS, each x86_64 and ARM64. The release publish job additionally verifies the complete assembled set of six runtime archives, six checksum sidecars, and one release-matched HTML reference. It checks archive integrity, internal manifests and hashes, required executables and starter-project files, and pinned documentation links before publishing.

The expanded six-target CI matrix must complete successfully before preview.7 is created. A successful ordinary build alone is no longer sufficient evidence for publishing runtime downloads.

## Runtime package quality

Runtime packages contain the graphical application, CLI, starter project, required application runtime libraries, third-party notices, and manifest/checksum. They exclude the full SDK: static development libraries, headers, CMake metadata, and SDK documentation. Operating-system DLLs are not redistributed. The source repository remains the authoritative full buildable source.

GitHub automatically supplies the generic Source code (zip) and Source code (tar.gz) snapshots for each tag. The CX release workflow uploads six native runtime packages and a version-matched standalone HTML reference; it does not upload duplicate curated source archives.

## Important unfinished areas

- Native Wayland backend; the current Linux SFML window backend relies on XWayland.
- General moving-body physics and broader collision response.
- More complete Room Studio tools and undo/redo.
- UI widget/menu coverage and coordinate/DPI verification across physical devices.
- Full real-time audio mixer and playback automation. SFML Audio is disabled in the current build configuration.
- Repeatable performance benchmarks and larger-world optimization.
- Further first-run/editor polish and the broader v1.0 feature-completion roadmap.

## Completion estimate

CX has a substantial usable foundation but is not yet v1.0 feature-complete. The main remaining risks are first-use/editor polish, moving-body physics, audio playback, performance profiling, physical-device QA, and proving that every runtime package can be built, extracted, and launched from a clean environment. A version number is not a completion percentage.

## Download and first use

The preview.5 and preview.6 release pages currently contain only GitHub's automatic source snapshots; neither has verified executable downloads. Do not treat those source snapshots as ready-to-run packages. Use the next preview's native runtime assets only after the release workflow completes successfully.
