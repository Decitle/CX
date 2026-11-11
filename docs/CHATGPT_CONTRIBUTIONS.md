

## 2026-11-09 — release readiness and documentation accuracy review

**Pushed by:** ChatGPT (OpenAI), at Decitle's request  
**Commit subject:** `AI-assisted by ChatGPT: Refresh release and contributor documentation`

- Updated the README and documentation landing page with the current six-target CI evidence, a clear source-clone explanation, and accurate release/download guidance.
- Corrected stale testing, platform-support, project-status, and roadmap statements that still described the Windows, ARM64, and macOS CI work as unverified.
- Documented the release workflow's all-or-nothing publication gate, the two curated source archives, six runtime targets, the additional GitHub-generated source links, and the absence of 32-bit targets.
- Clarified that native CI results are not physical-device QA and that the first tagged release workflow still needs to complete before all six curated runtime assets are considered published.
- Reviewed the tracked source tree and source packager: no compiled executables, shared libraries, build folders, editor caches, generated save files, handoff bundles, prompt logs, or private journals are tracked or admitted into curated source distributions.


## 2026-11-09 — refine the public README audience

**Pushed by:** ChatGPT (OpenAI), at Decitle's request  
**Commit subject:** `AI-assisted by ChatGPT: Refine the public project overview`

- Removed the Git cloning tutorial from the README; Git usage belongs to the expected contributor workflow rather than the primary project introduction.
- Kept the README focused on the engine's purpose, capabilities, current limitations, CMake build instructions, platform state, documentation, release downloads, and license.
- Updated the status line to cite the successful six-platform CI verification of the documentation refresh.


## 2026-11-09 — release packaging repair and documentation navigation refresh

**Pushed by:** ChatGPT (OpenAI), at Decitle's request  
**Commit subject:** AI-assisted by ChatGPT: Repair release packaging and refresh documentation

- Investigated the failed v0.10.14-preview.5 release workflow. All six native builds, CTest runs, and starter CLI checks passed; the Windows ARM64 install stage failed while resolving five Windows system DLL names, so the all-or-nothing release publication job was skipped.
- Added exact-name CMake dependency pre-exclusions and Windows runtime-packager exclusions for those five operating-system components, with regression fixtures confirming they are not copied into runtime packages.
- Simplified tagged release automation to attach six native starter-runtime packages and one version-matched standalone HTML reference. GitHub's automatic source-code ZIP and TAR.GZ snapshots remain the source downloads.
- Replaced the plain Pages landing page with a responsive documentation homepage whose primary action opens the interactive reference; added direct documentation-site and reference links to the README.
- Preserved the exact preview.5 HTML reference and refreshed the current catalogue's version framing, guide navigation, animation completion entry, duplicate animation entry, and random-choice description.
- Updated release, testing, platform, status, and roadmap documentation to distinguish successful build/test evidence from the incomplete first package publication.


## 2026-11-09 — public README polish and release-readiness review

**Pushed by:** ChatGPT (OpenAI), at Decitle's request  
**Commit subject:** `AI-assisted by ChatGPT: Polish public README for wider audience`

- Reworked the README opening to clearly identify CX as an open-source C++20 2D game engine and explain the role of CX Script.
- Added direct, prominent routes to documentation, the searchable reference, getting started, and releases; included a live CI badge.
- Organized the feature overview, build instructions, starter project, CLI checks, examples, platform matrix, known limitations, and release download behavior for prospective users.
- Clarified that the current examples are feature demonstrations rather than complete showcase games, and avoided claiming general-purpose physics, full editor functionality, or unverified runtime compatibility.
- Added natural project-specific terminology for discoverability without keyword stuffing. The GitHub repository About description and topic tags still require a manual edit because the connected GitHub actions do not expose repository metadata updates.


## 2026-11-09 — repair Windows ARM64 release dependency filtering

**Pushed by:** ChatGPT (OpenAI), at Decitle's request

- Investigated the failed v0.10.14-preview.6 release run. Five runtime targets and the version-matched HTML reference completed; Windows ARM64 alone failed at CMake's install-time runtime-dependency scan, which caused the release publication job to be skipped.
- Identified the root cause in CMake's documented Windows behavior: DLL dependency names are lowercased before matching PRE_EXCLUDE_REGEXES. The first fix used mixed-case regular-expression alternatives, so the intended Windows system DLL exclusions did not reliably match.
- Changed the CMake patterns to lowercase exact-name alternatives and added regression coverage for the matching behavior.
- Expanded ordinary native CI to install and package the starter runtime on all six platform/architecture targets, rather than packaging Windows x86_64 only.
- Added a final release-asset verifier requiring all six runtime archives, matching SHA-256 sidecars, and the release-specific HTML reference. It checks archive integrity, internal manifests and file hashes, required executables/project files, and version-pinned guide links before publication.
- Updated the public status and release documentation to distinguish the two failed preview attempts from the new packaging validation required before preview.7.


## 2026-11-10 — repair release publisher checkout and inspect preview.7 artifacts

**Pushed by:** ChatGPT (OpenAI), at Decitle's request

- Investigated preview.7 release run #3. All six native runtime packages and the version-matched HTML reference were produced and uploaded as Actions artifacts. The final publish job failed with exit code 2 because it invoked `tools/verify_release_assets.py` without checking out the tagged repository, so the script did not exist in the publisher's workspace.
- Added an explicit checkout of the release tag to the publish job before asset verification. This is a fix for the next tagged release workflow; it does not retroactively alter preview.7's already-completed run.
- Inspected the 100 MB Windows ARM64 runtime archive. It contains numerous Windows OS components (including `shell32.dll`, `windows.storage.dll`, and `d2d1.dll`) that should not be redistributed as application dependencies. It is excluded from the manual preview.7 asset set and requires a separate packaging-filter correction before a future release.
- Verified the Windows x86_64, Linux x86_64, and Linux ARM64 runtime archives: each contains both expected executables, a valid internal manifest with matching file sizes and SHA-256 hashes, and a matching external checksum sidecar.
- Verified the preview.7 standalone HTML reference and its version-pinned guide links. Prepared a manual upload bundle containing only the three validated runtime packages, their three checksum sidecars, and the matching reference.

## 2026-11-10 — simplify runtime archives and consolidate license notices

**Pushed by:** ChatGPT (OpenAI), at Decitle's request

- Used the user's repackaged Linux runtime archive as the target layout: a CX-Engine folder containing the executables, bundled runtime dependencies, and an in-place quickstart project, with no duplicate project directory at the archive root.
- Changed the runtime packager to flatten CMake's bin and lib install payloads into CX-Engine, preserve macOS frameworks under that directory, reject filename collisions, and place the runtime manifest beside the executables.
- Set Linux and macOS install RPATHs to match the new colocated-library layout; kept the release archives' platform-specific formats and checksum sidecars.
- Replaced the three-path runtime license presentation with one LICENSES.txt containing the complete CX Engine BSD 3-Clause text and the unmodified SFML zlib/libpng notice as separately labeled sections. The repository's source license files remain available in their normal locations.
- Updated runtime instructions, third-party disclosure, and release asset verification to require the new layout and reject the old bin structure or duplicate license files.
- Expanded Windows system-DLL exclusions to cover components found in the oversized preview.7 Windows ARM64 archive and added packaging regression fixtures for those names.
- Added Linux archive-layout coverage and updated the six-target release verifier fixtures to test the new package contract.


## 2026-11-10 — remove the duplicate root project from runtime archives

**Pushed by:** ChatGPT (OpenAI), at Decitle's request

- Corrected the package contract after the author clarified that the uploaded archive accidentally contained a duplicate top-level project/ folder. Runtime packages now contain exactly one starter project, directly inside CX-Engine/.
- Removed the second copy step from the runtime packager, updated the six-target release verifier to allow only CX-Engine/, README.md, and LICENSES.txt at the archive root, and added a regression test that rejects any top-level project/ directory.
- Updated package launch and check instructions so both the graphical runtime and CLI operate on CX-Engine/ as the project path.
- Expanded LICENSES.txt to include the applicable full notices for bundled FreeType, HarfBuzz, SheenBidi, QOI, cpp-unicodelib, glad, and stb_image components when their source is present in the FetchContent build tree. Host-provided dependencies are not treated as bundled files.
- Corrected the Windows packaging test to normalize CRLF/LF differences instead of assuming Unix line endings.
- Updated THIRD_PARTY_NOTICES.md and runtime documentation to make the one-project layout and consolidated third-party licensing explicit.
