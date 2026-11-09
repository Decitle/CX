# ChatGPT contribution and attribution log

This file records repository content authored or materially edited by ChatGPT (OpenAI) under the direction of Decitle. It is intended to make AI-assisted work visible during private development and if the repository is made public later.

Decitle directs and maintains the project. ChatGPT is an AI co-author/contributor; it is not represented as a human developer or as the owner of the repository.

## 2026-11-07 — initial source and project-documentation contribution

**Pushed by:** ChatGPT (OpenAI), at Decitle's request  
**Commit subject:** AI-assisted by ChatGPT: Add project overview and v0.10.11 beam-effect source slice

Files added or replaced in this contribution:

- README.md — professional project overview, build architecture, development status, BSD 3-Clause license pointer, and explicit co-authorship disclosure.
- include/cx/effects.hpp, src/effects.cpp — renderer-neutral beam data and pure textured-quad geometry builder from the v0.10.11 development candidate.
- tests/beam_effect_tests.cpp — focused geometry, script/runtime, property validation, lifetime, and removal tests.
- docs/TEXTURED_BEAMS-v0.10.11.md — beam API, rendering-order, endpoint-light, lifecycle, and limitation documentation.
- examples/textured_beam.cx — example script for a textured beam and independent endpoint light.

**Import status:** This was a selective initial import, not a complete mirror of the full CX Engine source distribution. The full engine build files and the rest of the implementation were not included. The repository remained incomplete until the full source import described below.

All future ChatGPT-authored or ChatGPT-edited pushes should use an explicit AI-assisted by ChatGPT commit subject and add a dated entry here. Decitle's own changes need not be attributed to ChatGPT.

## 2026-11-07 — README build-instruction correction

**Pushed by:** ChatGPT (OpenAI)  
**Commit subject:** `AI-assisted by ChatGPT: Correct source import status and CMake build guidance`

Corrected the project status to v0.10.11, clarified that the GitHub checkout was a selective non-buildable source import, and replaced an inaccurate helper-script example with direct CMake instructions for a future complete source checkout. No license or repository visibility settings were changed.

## 2026-11-08 — public-facing repository and documentation preparation

**Pushed by:** ChatGPT (OpenAI), at Decitle's request

Changes made:

- Rewrote `README.md` to present CX first as a graphical 2D game engine, with the CLI described as a separate supporting tool.
- Separated the wider v0.10.14 development checkpoint from the actual contents and readiness of the then-selective GitHub repository.
- Added `docs/index.md` as a documentation landing page and `docs/_config.yml` to prepare the folder for GitHub Pages.
- Added public-facing project status, platform-support, release-format, and roadmap pages.
- Explicitly recorded that the earlier GitHub checkout was not a complete buildable engine, that Windows and other non-Linux targets were not verified, and that no GitHub Release downloads were published.
- Added the planned source ZIP/TAR.GZ and native runtime package matrix as a future release design, not as existing assets.
- Kept the BSD 3-Clause license unchanged.

No repository visibility, license, GitHub Pages deployment, or release settings were changed. No executable or release asset was uploaded to GitHub in that contribution.

### Follow-up accuracy corrections

- Corrected the README's license link to the actual root-level `LICENSE` file.
- Changed the project status wording to refer only to the verified absence of published GitHub Releases; no claim was made about tag state.
- Removed an outdated sentence that referred to an external source ZIP as though it were part of the GitHub repository.

## 2026-11-08 — complete source integration and first-run polish

**Pushed by:** ChatGPT (OpenAI), at Decitle's request  
**Branch:** `complete-source-import-v0.10.14-review`  
**Commit subject:** `AI-assisted by ChatGPT: Import complete v0.10.14 engine source and CI`

Changes made:

- Replaced the earlier selective working tree with the complete curated engine repository: C++20 core, SFML graphical host, CLI, public headers, tests, examples, Room Studio, release tools, dependency notices, and user documentation.
- Added native CI workflows for Linux x86_64/ARM64, Windows x86_64/ARM64, and macOS x86_64/ARM64. These are configured targets, not verified support claims until native jobs pass.
- Added source/runtime packaging, SHA-256 manifests, an XWayland startup diagnostic, and a short-lived Windows x86_64 CI runtime artifact.
- Improved generated starter-project documentation and its commented script template; expanded scaffold tests for partially populated projects and idempotence.
- Verified Linux x86_64 with GCC 14.2.0 and SFML 3.1.0: 19/19 CTest; verified source package checks, the starter project's CLI validation, runtime dependency closure, and a six-second Xvfb startup smoke.

The branch is submitted for review and native CI. It is not merged into `main`, no tag or GitHub Release is created, the repository remains private, and the BSD 3-Clause license is unchanged.

## 2026-11-08 — native CI portability fixes

**Pushed by:** ChatGPT (OpenAI), at Decitle's request  
**Branch:** `complete-source-import-v0.10.14-review`  
**Commit subject:** `AI-assisted by ChatGPT: Fix portable floating parsing and Linux CI dependencies`

Changes prepared for the follow-up review commit:

- Added compile-time detection for floating-point `std::from_chars`; compilers with the overload retain that path, while Apple libc++ without it uses a classic-locale, full-token stream parser.
- Added strict value-conversion regressions for signs, exponents, whitespace, partial tokens, out-of-range values, NaN, and infinity.
- Added `libfreetype6-dev` and `libharfbuzz-dev` to both Linux CI and release workflow dependency setup, matching SFML Graphics' source-build requirements revealed by sequential configure runs.
- Added the direct `<sstream>` include to `src/diagnostics.cpp` after libc++ caught the prior reliance on a transitive include.
- Disabled cancel-in-progress for same-branch CI runs so a newer commit does not erase useful platform build/package evidence from a still-running matrix.
- Updated the preview changelog and platform/project status to distinguish Windows ARM64 compile evidence and partial macOS build evidence from full platform verification.

Local evidence: Linux graphical build 19/19 CTest; package tests 6/6; Room Studio HTTP tests 9/9; forced portable-fallback runtime conversion tests passed. The native CI rerun is required before marking any additional architecture verified. No merge, release, tag, license change, or repository visibility change is part of this contribution.


## 2026-11-09 — runtime package payload cleanup

**Pushed by:** ChatGPT (OpenAI), at Decitle's request  
**Branch:** `complete-source-import-v0.10.14-review`  
**Commit subject:** `AI-assisted by ChatGPT: Keep runtime packages free of SDK and Windows system files`

- Replaced whole-install-prefix copying with a runtime-only stage collector. Application archives now retain executables, shared runtime libraries, and macOS Frameworks, while omitting SDK headers, static libraries, CMake metadata, and development documentation.
- Added an explicit Windows system-DLL exclusion list plus generic API-set exclusions. Unknown application DLLs remain eligible for bundling.
- Expanded the Windows packaging regression to verify that a future SFML runtime DLL is retained while `kernel32.dll`, SDK headers, static libraries, and SDK docs are omitted.
- Local validation: 18/18 headless CTest targets and 6/6 packaging tests passed. A new native CI run is required before treating the newly slimmed Windows package as verified.


## 2026-11-09 — isolate graphical smoke project and reject generated starter state

**Pushed by:** ChatGPT (OpenAI), at Decitle's request  
**Branch:** `complete-source-import-v0.10.14-review`  
**Commit subject:** `AI-assisted by ChatGPT: Isolate smoke-test project and clean starter packages`

- Changed `tools/smoke_graphical.py` to launch a disposable copy of the supplied project so first-run scaffolding never writes into tracked examples.
- Added a package-time rejection guard for generated persistence files (`.variables.lock`, `variables.csv`, cache/manifest files, and temporary files) in the starter project.
- Added regression tests proving the smoke helper leaves its source project untouched and that the packager rejects generated save state.
- Local validation: 18/18 headless CTest targets, 8/8 packaging tests, and Python syntax compilation passed. A fresh native CI run is required before release packaging is considered verified.


## 2026-11-09 — complete source merge, native CI and release documentation status

**Pushed by:** ChatGPT (OpenAI), at Decitle's request  
**Repository:** `Decitle/CX`  
**Merge:** pull request #1, merge commit `ca03a61252dad4755ebaccb59fbbb24e0b52d241`  

- Merged the complete curated CX Engine source into `main` while preserving the existing repository history.
- Updated the README, project status, platform support, documentation landing page, release format, and changelog to reflect the six-platform native CI baseline and the actual remaining publication steps.
- Clarified that native CI validation is not physical-device QA, SFML's Linux window backend uses XWayland, and no official GitHub Release or Pages deployment is claimed until one is actually published/enabled.


## 2026-11-09 — final native CI verification status correction

**Pushed by:** ChatGPT (OpenAI), at Decitle's request  
**Commit subject:** `AI-assisted by ChatGPT: Record final six-platform CI pass`

- Updated the README, project status, platform support, and changelog to record that follow-up GitHub Actions run #7 passed all six native targets, including the final disposable smoke-project and generated-save-state protections.
- The successful Windows x86_64 job uploaded a clean runtime artifact; its inner ZIP was downloaded and inspected, its manifest hashes verified, and the package was confirmed free of system DLLs, SDK payload, and generated save state.
