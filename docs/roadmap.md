# Roadmap

**Product goal:** a lightweight, reusable 2D game engine whose scripting workflow is comfortable for Minecraft Skript users and beginners who want to make games. The public language should feel like Skript for common portable constructs, while adding game-engine capabilities without inheriting Minecraft-only concepts.

The roadmap is dependency-ordered, not a delivery-date promise. A feature is not complete merely because its parser accepts one example.

## Current published checkpoint — v0.10.14-preview.8

- Six native runtime packages are published for Windows, Linux and macOS on x86_64 and ARM64.
- The six-target build/test workflow passed in [run 42959350359](https://github.com/Decitle/CX/actions/runs/42959350359).
- The tagged package assembly and publication workflow passed in [run 42961733306](https://github.com/Decitle/CX/actions/runs/42961733306).
- The README/download route, direct platform-aware download page, release-reference filename automation and dark Pages documentation work are in place.
- The published Pages baseline remains [run 42969227621](https://github.com/Decitle/CX/actions/runs/42969227621). The active v0.11.0 development branch most recently passed its six-target native CI matrix in [run #91](https://github.com/Decitle/CX/actions/runs/43491377925). These represent different trees: main/Pages remains on v0.10.14 until the draft overhaul is accepted.

## Active next milestone — Skript-like language overhaul (v0.11.0)

**This is the highest-priority engine milestone.** The original product goal drifted as features accumulated. The next version must make the entire public language consistently English-first and much closer to Skript's grammar.

### Phase 1 — syntax audit and design contract

- Inventory every event, variable form, expression, condition, effect/statement, function, loop and project-facing API.
- Choose a canonical English form and useful aliases for each supported family.
- Mark current syntax, accepted aliases, planned syntax and unsupported concepts distinctly.
- Settle the typed object-reference and event-context contracts before designing property access.
- Keep the design charter and audit visible from the public documentation and Library.

**Status:** the design charter and source-backed audit govern the active branch. Natural single-value random expressions, common natural math forms, worded comparisons, typed sprite/entity references, shared property getters/setters, and collection-oriented entity tags are implemented. Count-prefix/plural random list assignment has been implemented in the latest development commit; its current-head CI validation is pending.

### Phase 2 — expressions and common language

- [x] Add natural single-value random number expressions and random item/element selection from lists.
- [x] Retain deterministic named random streams as an optional reproducibility clause and initialize the default stream automatically.
- [x] Add English-readable forms for common math operations while retaining compact function aliases.
- [x] Add natural comparison wording alongside symbolic operators.
- [ ] Validate count-prefix/plural random list assignment (implemented on the current branch) for its 0–4096 count range, range semantics, typed outputs and deterministic streams.
- [x] Preserve list syntax, interpolation, function semantics and the current loop/wait foundation; continue the compatibility audit.

### Phase 3 — object properties and events

- [x] Define typed sprite/entity references so local and temporary variables can hold live object handles with instance identity.
- [x] Standardize `set <property> of <object> to <value>` and matching property getters for the currently supported object properties.
- [x] Add a natural possessive property setter alias that normalizes to the shared object-property operation.
- [ ] Introduce short, consistent event headers with standard event-context values. Preserve old header spellings as compatibility aliases where possible.
- [x] Keep coordinates as conventional tuples; use English around them rather than removing parentheses.

### Phase 4 — all subsystem surfaces

- Normalize room, movement, animation, path, UI, graphics, audio and settings syntax.
- Use word-spaced canonical grammar instead of implementation labels where appropriate (`font size`, `sample rate`, `ease in out`, etc.).
- Document proposed screen overlay/fade and audio track/bus systems as future work; do not imply that these systems exist today.
- Weighted selection is an optional follow-up after plain random integer/number/item expressions are complete.

### Phase 5 — examples, reference and release verification

- Rewrite examples so the canonical English grammar is what new users encounter first.
- Update the searchable reference, syntax guide, learning guide, feature pages, quickstart, standalone versioned HTML docs and package instructions together.
- Build a parser syntax corpus covering every advertised form and alias, with invalid-form and ambiguous-parse tests.
- Test old v0.10.14 examples through compatibility aliases wherever practical.
- Pass all six native CI targets and the normal release package verifier.

**Exit:** only after all phases pass may the project tag `v0.11.0`. Do not create a release merely because the random expression has been repaired.

## Following milestone — beginner-first first-game success

Once the language surface is stable, finish the complete beginner journey: download, extract, launch, find the starter script, make a meaningful change, validate with `cx-cli`, run the game and fix a deliberately introduced syntax error using the source diagnostic. Use one small complete annotated game as the acceptance project.

## Later engineering stages

### Physics and gameplay reliability

- Audit collision and motion contracts, including tunneling, overlap ordering, object deletion/replacement, room boundaries and the difference between tween collision and general physics.
- Define deterministic fixed-tick movement/physics behavior and tests before broadening the solver.
- Improve Room Studio editing and round-trip reliability; test logical UI coordinates and DPI/aspect-ratio behavior on physical displays.

### Audio, performance and platform quality

- Investigate practical audio playback and multi-track/bus controls as a separate engine design, with real-time callback safety.
- Add frame/tick, allocation/memory, rendering and handler-cost profiling.
- Benchmark on modest physical hardware; CI runner results are not Chromebook performance measurements.
- Continue physical-device testing across supported platforms. Linux currently uses X11/XWayland and requires XWayland on Wayland desktops.

### v1.0 readiness

A stable 1.0 should wait until the language is consistent, the first-game workflow is straightforward, common 2D game workflows are dependable, physics/input/UI behavior has appropriate physical-device coverage, audio/performance claims have evidence, and all release archives pass clean extraction and launch tests.
