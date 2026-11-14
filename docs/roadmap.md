# Roadmap

**Product goal:** a lightweight, reusable 2D game engine whose scripting workflow is comfortable for Minecraft Skript users and beginners who want to make games. The public language should feel like Skript for common portable constructs, while adding game-engine capabilities without inheriting Minecraft-only concepts.

The roadmap is dependency-ordered, not a delivery-date promise. A feature is not complete merely because its parser accepts one example.

## Current published checkpoint — v0.10.14-preview.8

- Six native runtime packages are published for Windows, Linux and macOS on x86_64 and ARM64.
- The six-target build/test workflow passed in [run 42959350359](https://github.com/Decitle/CX/actions/runs/42959350359).
- The tagged package assembly and publication workflow passed in [run 42961733306](https://github.com/Decitle/CX/actions/runs/42961733306).
- The README/download route, direct platform-aware download page, release-reference filename automation and dark Pages documentation work are in place.
- The published Pages baseline remains [run 42969227621](https://github.com/Decitle/CX/actions/runs/42969227621). The active v0.11.0 branch passed its six-target native CI matrix at [run #100](https://github.com/Decitle/CX/actions/runs/43505981740). The next subsystem parser/corpus increment is awaiting its own run. These represent different trees: main/Pages remains on v0.10.14 until the draft overhaul is accepted.

## Active next milestone — Skript-like language overhaul (v0.11.0)

**This is the highest-priority engine milestone.** The original product goal drifted as features accumulated. The next version must make the entire public language consistently English-first and much closer to Skript's grammar.

### Phase 1 — syntax audit and design contract

- Inventory every event, variable form, expression, condition, effect/statement, function, loop and project-facing API.
- Choose a canonical English form and useful aliases for each supported family.
- Mark current syntax, accepted aliases, planned syntax and unsupported concepts distinctly.
- Typed sprite/entity references and shared property getter/setter rules are implemented for supported properties. The event-context contract is catalogued in one source-of-truth page and runtime-tested for every documented payload field; event-header vocabulary and suspended-handler edge cases remain open.
- Keep the design charter and audit linked from the repository's public documentation and learning resources.

**Status:** the design charter and source-backed audit govern the active branch. Natural single-value and count-prefix random expressions, common natural math forms, the implemented worded comparison vocabulary, typed sprite/entity references with shared/possessive property setters, and collection-oriented entity tags are implemented. Run #109 passed the comparison matrix and run #111 passed the expanded object-property tests on all six platforms. Run #114 is validating live canvas interpolation, the first-steps and quickstart HUD examples, beginner-guide alignment, and canonicalized feature examples.

### Phase 2 — expressions and common language

- [x] Add natural single-value random number expressions and random item/element selection from lists.
- [x] Retain deterministic named random streams as an optional reproducibility clause and initialize the default stream automatically.
- [x] Add English-readable forms for common math operations while retaining compact function aliases.
- [x] Add natural comparison wording alongside symbolic operators and validate every implemented operator spelling with runtime tests.
- [x] Validate natural sign, parity, zero, type, and emptiness predicates with positive and negative runtime cases.
- [x] Validate count-prefix/plural random list assignment for its 0–4096 count range, range semantics, typed outputs and deterministic streams on all six native targets.
- [x] Preserve list syntax, interpolation, function semantics and the current loop/wait foundation; continue the compatibility audit.

### Phase 3 — object properties and events

- [x] Define typed sprite/entity references so local and temporary variables can hold live object handles with instance identity.
- [x] Standardize `set <property> of <object> to <value>` and matching property getters for the currently supported object properties.
- [x] Add a natural possessive property setter alias that normalizes to the shared object-property operation.
- [x] Validate supported object-property aliases, texture/visibility setters, entity-only health/classification properties, maximum-health spellings, and stale-handle behavior.
- [x] Publish one event-context local contract for all currently implemented payload-bearing events; assert every documented payload field at runtime and cover local-scope continuity across `wait`.
- [ ] Design a broader, consistent event-header vocabulary while preserving existing spellings as compatibility aliases. Keep named trigger pairs explicit until any more general filtering mechanism has a bounded dispatch strategy; required selectors must remain non-empty.
- [x] Keep coordinates as conventional tuples; use English around them rather than removing parentheses.

### Phase 4 — all subsystem surfaces

- Normalize room, movement, animation, path, UI, graphics, audio and settings syntax.
- Use word-spaced canonical grammar instead of implementation labels where appropriate (`font size`, `sample rate`, `ease in out`, etc.).
- Document proposed screen overlay/fade and audio track/bus systems as future work; do not imply that these systems exist today.
- Weighted selection is an optional follow-up after plain random integer/number/item expressions are complete.

### Phase 5 — examples, reference and release verification

- Rewrite examples so the canonical English grammar is what new users encounter first. The first-steps example and quickstart now show a live score HUD; Number Hunt is the first complete asset-free tutorial game.
- [x] Add a CI syntax pass for every standalone `.cx` example and every multi-file example project using `tools/check_examples.py` (syntax only; runtime/GUI behavior is checked separately).
- Update the searchable reference, syntax guide, learning guide, feature pages, quickstart, standalone versioned HTML docs and package instructions together.
- [x] Add an initial positive/negative subsystem syntax corpus for natural/legacy UI, graphics, audio and event forms; expand it to every advertised language family, invalid type, and event-context boundary.
- [x] Add runtime assertions for every documented event payload field, including file-attachment path, cursor coordinates, and the empty/no-button case. Keep callback identity and room-context lifecycle checks in their subsystem integration tests.
- Test old v0.10.14 examples through compatibility aliases wherever practical.
- Pass all six native CI targets and the normal release package verifier.

**Exit:** only after all phases pass may the project tag `v0.11.0`. Do not create a release merely because the random expression has been repaired.

## Following milestone — beginner-first first-game success

The first complete acceptance project is [Number Hunt](first-game.md): an asset-free game with a multi-file runnable project, annotated tutorial, CLI syntax-check instructions, a deliberate syntax-error recovery exercise, and a runtime test for a complete round. The remaining beginner-journey gate is to validate this project through the actual download/extract/launch path using an installed package.

## v0.12.0 planning direction — engine interoperability and broad game scenarios

After the 0.11.0 language and compatibility gate passes, use [the engine interoperability audit](ENGINE_INTEROPERABILITY_AUDIT.md) as the source of truth for cross-system work. The next milestone should build several small end-to-end game scenarios and settle shared contracts for simulation time, task scheduling, event ordering, room/world ownership, persistence, UI/input, audio, and asset loading. Do not add large standalone feature lists before identifying how each feature interacts with existing systems.

The objective is broad, composable game-design coverage—not a literal promise that every conceivable game can be represented. Candidate acceptance slices should include exploration/adventure, platforming/precision movement, puzzle logic, arcade simulation, and presentation-heavy gameplay. Each slice needs an executable sample project, automated interaction/lifecycle tests, and clean-package launch verification.

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
