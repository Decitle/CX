# Project status

**Development checkpoint:** v0.10.14-preview.3 (pre-release candidate; not published)  
**Source repository:** complete curated C++20 source tree, build system, regression tests, examples, tools, and public documentation  
**Verified baseline:** Linux x86_64 SFML 3.1.0 graphical Release build, clean-source extraction build, and 19/19 CTest  
**Native Windows status:** not yet verified; native CI is configured but has not produced a result  
**Wayland status:** works through XWayland when available; no native Wayland backend in SFML 3.1.0

## Implemented foundation

The engine contains a C++20 core, parser and scripting runtime, local/event/transient/persistent variable lifetimes, function and loop support, waits and scheduled handlers, transactional script reload, project scaffolding, data-driven rooms, asset discovery, tile collision metadata, entities and hitboxes, collision channels, triggers, interaction focus, projectiles, tweens, animation clips, seeded random streams, textured-beam/effect geometry, canvas UI, lighting meshes, a graphical terminal, a separate CLI, persistent storage recovery, and a local Room Studio tool.

## Current verified checks

The curated source tree for v0.10.14-preview.3 has been configured and built locally. Its graphical Linux x86_64 Release build passes 19/19 CTest targets; the package tests pass 6/6, and the Room Studio HTTP tests pass 9/9. A separate headless build forced the portable floating-point conversion fallback and passed the runtime conversion suite. The Linux starter-runtime package has an embedded manifest that verifies file sizes and hashes; it also passed a clean extraction, `ldd` dependency check, CLI project check, and six-second Xvfb graphical startup smoke. No GitHub Release has been published. The complete source import is in draft PR #1 and has not been merged to `main`.

The first native CI run exposed two issues: Linux runners were missing the FreeType development dependency required by the SFML source build, and Apple libc++ did not provide the floating-point `std::from_chars` overload used in `src/value.cpp`. The review branch now includes the missing Linux package and a tested portable conversion fallback. These fixes require a fresh native CI run; no non-Linux platform is yet classified as verified.

- Clean source ZIP and TAR.GZ both pass archive manifest verification.
- The exact source ZIP configures and builds the graphical runtime and CLI against SFML 3.1.0.
- The clean-source graphical build passes 19/19 CTest targets.
- The minimal Linux x86_64 starter runtime package launches under Xvfb and its CLI validates the starter project.
- Windows, ARM64, and macOS native builds remain unverified.

## Important unfinished areas

- Successful native Windows compilation, graphical startup, and platform-specific storage validation.
- Linux/Windows ARM64 and macOS native builds.
- Native Wayland support (current SFML backend relies on XWayland).
- General moving-body physics and broader collision response.
- More complete Room Studio tools and undo/redo.
- UI widget/menu coverage and coordinate/DPI verification.
- Full real-time audio mixer and playback automation.
- Repeatable performance benchmarks and larger-world optimization.
- First public release, further first-run/editor polish, and native runtime verification for Windows, ARM64, and macOS.

## Completion estimate

The codebase has a substantial working foundation, but feature count is not the same as product readiness. The project is not yet v1.0-ready. Native Windows and other platform validation, editor/user-experience polish, physics, audio, and profiling remain significant work. Current estimates should be updated from actual CI and feature milestones rather than inferred from the version number.
## Windows package delivery

The `windows-x86_64` CI job now installs and packages the tested runtime, then uploads a 14-day GitHub Actions artifact. This makes a Windows test ZIP downloadable from a successful CI run after the complete source tree is imported. It does not replace native build/test results, and it does not publish a GitHub Release automatically.
