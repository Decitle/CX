

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
