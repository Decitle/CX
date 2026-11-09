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

**Import status:** This is a selective initial import, not a complete mirror of the full CX Engine source distribution. The full engine source and build files were not part of this selective repository import. The repository remains incomplete until the complete, buildable project is checked in and verified.

All future ChatGPT-authored or ChatGPT-edited pushes should use an explicit AI-assisted by ChatGPT commit subject and add a dated entry here. Decitle's own changes need not be attributed to ChatGPT.


## 2026-11-07 — README build-instruction correction

**Pushed by:** ChatGPT (OpenAI)  
**Commit subject:** `AI-assisted by ChatGPT: Correct source import status and CMake build guidance`

Corrected the project status to v0.10.11, clarified that the GitHub checkout is a selective non-buildable source import, and replaced an inaccurate helper-script example with direct CMake instructions for a future complete source checkout. No license or repository visibility settings were changed.

## 2026-11-08 — public-facing repository and documentation preparation

**Pushed by:** ChatGPT (OpenAI), at Decitle's request

Changes made:

- Rewrote `README.md` to present CX first as a graphical 2D game engine, with the CLI described as a separate supporting tool.
- Separated the wider v0.10.14 development checkpoint from the actual contents and readiness of this selective GitHub repository.
- Added `docs/index.md` as a documentation landing page and `docs/_config.yml` to prepare the folder for GitHub Pages.
- Added public-facing project status, platform-support, release-format, and roadmap pages.
- Explicitly recorded that the current GitHub checkout is not a complete buildable engine, that Windows and other non-Linux targets are not verified, and that no GitHub Release downloads are published yet.
- Added the planned source ZIP/TAR.GZ and native runtime package matrix as a future release design, not as existing assets.
- Kept the BSD 3-Clause license unchanged.

No repository visibility, license, GitHub Pages deployment, or release settings were changed. No executable or release asset was uploaded to GitHub in this contribution.


### Follow-up accuracy corrections

- Corrected the README's license link to the actual root-level `LICENSE` file.
- Changed the project status wording to refer only to the verified absence of published GitHub Releases; no claim is made about tag state.
- Removed an outdated sentence that referred to an external source ZIP as though it were part of the GitHub repository.
