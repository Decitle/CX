# Roadmap

**Product goal:** a lightweight, reusable 2D game engine whose scripting workflow is comfortable for Minecraft Skript users and beginners who want to make games. The public language should feel like Skript for common portable constructs, while adding game-engine capabilities without inheriting Minecraft-only concepts.

The roadmap is dependency-ordered, not a delivery-date promise. A feature is not complete merely because its parser accepts one example.

## Current published checkpoint — v0.10.14-preview.8

- Six native runtime packages are published for Windows, Linux and macOS on x86_64 and ARM64.
- The six-target build/test workflow passed in [run 42959350359](https://github.com/Decitle/CX/actions/runs/42959350359).
- The tagged package assembly and publication workflow passed in [run 42961733306](https://github.com/Decitle/CX/actions/runs/42961733306).
- The README/download route, direct platform-aware download page, release-reference filename automation and dark Pages documentation work are in place.
- The last verified six-target CI and Pages baseline before this roadmap update passed in [run 42969227408](https://github.com/Decitle/CX/actions/runs/42969227408) and [run 42969227621](https://github.com/Decitle/CX/actions/runs/42969227621).

## Active next milestone — Skript-like language overhaul (v0.11.0)

**This is the highest-priority engine milestone.** The original product goal drifted as features accumulated. The next version must make the entire public language consistently English-first and much closer to Skript's grammar.

### Phase 1 — syntax audit and design contract

- Inventory every event, variable form, expression, condition, effect/statement, function, loop and project-facing API.
- Choose a canonical English form and useful aliases for each supported family.
- Mark current syntax, accepted aliases, planned syntax and unsupported concepts distinctly.
- Settle the typed object-reference and event-context contracts before designing property access.
- Keep the design charter and audit visible from the public documentation and Library.

**Status:** the design charter and source-backed audit are written. They are the acceptance contract for implementation, not a claim that the planned syntax already works.

### Phase 2 — expressions and common language

- Add natural random number expressions: `a random integer between 1 and 10`, `a random number between 0 and 1`, and a natural random-item/choice form.
- Retain deterministic named random streams as an optional reproducibility clause; specify and test the implicit default stream.
- Add English-readable math/helper expressions while retaining compact function aliases.
- Add natural comparison wording alongside symbolic operators.
- Preserve list syntax, interpolation, function semantics and the current reliable loop/wait foundation.

### Phase 3 — object properties and events

- Define typed sprite/entity/room references so variables can hold actual object handles rather than only text, numbers, booleans or none.
- Standardize `set <property> of <object> to <value>`, property getters and possessive aliases.
- Introduce short, consistent event headers with standard event-context values. Preserve old header spellings as compatibility aliases where possible.
- Keep coordinates as conventional tuples; use English around them rather than removing parentheses.

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
