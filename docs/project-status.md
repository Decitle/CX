# Project status

**Current published release:** [v0.10.14-preview.8](https://github.com/Decitle/CX/releases/tag/v0.10.14-preview.8)  
**Release validation:** [six-target build and test](https://github.com/Decitle/CX/actions/runs/42959350359) and [tagged package/publish workflow](https://github.com/Decitle/CX/actions/runs/42961733306) both succeeded.  
**Post-Preview.8 changes:** release-reference filename automation, the guided download page, updated first-run instructions and the beginner-first roadmap are committed to `main`. The Pages deployment for the current documentation set has succeeded. The corrected six-platform CI run is queued behind an earlier native matrix; do not tag another release until the current `main` head has a successful run.

## Published runtime state

Preview.8 includes six native runtime archives:

- Windows x86_64 and ARM64
- Linux x86_64 and ARM64
- macOS x86_64 and ARM64

The tagged release workflow verified the six archives, their SHA-256 sidecars, internal runtime manifests, package paths and the version-matched HTML reference before publishing. GitHub also provides its automatic source-code ZIP and TAR.GZ snapshots.

The Preview.8 HTML asset currently has the filename `0_CX-Docs-v0.10.14-preview.8.html` because the project owner manually renamed it after publication. Commit `917a43b` changes the reference generator, release verifier, tests and release-note URL so future releases generate and require the `0_CX-Docs-v{version}.html` convention automatically. For stable versions, the full version identifier naturally omits the `-preview.N` suffix.

## Implemented foundation

CX currently has a C++20 core, source-aware parser and runtime, three variable lifetimes, conditions and expressions, functions, lists, loops, non-blocking waits and periodic handlers, transactional reload, project scaffolding, data-driven rooms, asset discovery, sprites/entities/hitboxes, collision channels and specialized collision paths, triggers, interaction focus, projectiles, tweens, animation clips, screen-space canvas UI, lighting meshes, textured beams, deterministic seeded random streams, persistent-variable recovery, a separate CLI, and a localhost Room Studio tool.

The random API is already implemented and documented. Scripts can seed independent named streams and call `random_float`, inclusive unbiased `random_int`, and `random_choice`. It is deterministic pseudo-randomness, not a security RNG; stream state is process-local and not yet serialized with saved games.

## Current user-facing download work

- The README now has a direct Download route next to Documentation and GitHub.
- `docs/downloads.html` selects the newest published release that contains all six runtime targets, suggests an OS/architecture using browser hints when available, and allows manual overrides.
- The primary page links directly to the runtime archive. SHA-256 sidecars stay accessible on the full release page but are not presented as download choices.
- The Getting Started guide now matches the actual package layout: the graphical executable, CLI and single starter project are all under `CX-Engine/`; there is no old `bin/` plus separate `project/` structure.

These changes still need their current CI and Pages runs to finish, plus browser-level testing of the live download page.

## Next milestone: beginner-first onboarding and first-project success

The immediate priority is to prove that a Skript-familiar beginner can download CX, extract and launch it, find and edit the starter script, make a small game behavior change, validate it with `cx-cli`, run it, and recover from a syntax error using the diagnostic. This should be demonstrated by one annotated, complete mini-game project and tested from a clean runtime package, not only from a developer build.

See the [roadmap](roadmap.md) for the ordered tasks and acceptance gates.

## Important unfinished areas

- General moving-body physics and broad collision response are not complete; current collision systems have narrower documented contracts.
- Named/cancellable task handles, module/import boundaries and stronger data/type/schema semantics remain future scripting work.
- Consistent logical UI coordinates and physical DPI/aspect-ratio behavior need broader testing.
- Room Studio is a useful local helper, not a full visual editor.
- SFML audio playback is disabled in current release builds; practical multi-voice mixing and automation are not complete.
- Performance profiling and repeatable benchmarks on modest physical hardware are still needed.
- Linux currently uses the SFML X11/XWayland window backend; Wayland desktops need XWayland. A native Wayland backend is not implemented.
- CI native build success does not replace physical-device graphics, input, focus, DPI or packaging QA.

CX is a substantial experimental foundation, not yet v1.0 feature-complete. The next priority is first-project success, followed by the scripting, gameplay, audio and platform gates in the roadmap.
