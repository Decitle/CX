

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
