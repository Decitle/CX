# Project status

**Published release:** [v0.10.14-preview.8](https://github.com/Decitle/CX/releases/tag/v0.10.14-preview.8)  
**Development milestone:** v0.11.0 Skript-like language overhaul  
**Integration PR:** [#2 — v0.11.0-language-overhaul](https://github.com/Decitle/CX/pull/2) (open, draft, targets `main`)  
**Latest completed six-target runtime/source CI:** [run #129](https://github.com/Decitle/CX/actions/runs/43687589060) — all six native targets passed build, CTest, all-example syntax checks, runtime package creation, and configured graphical startup smoke tests for runtime/source commit `3b3c9cc`. This includes the typed-function/default-argument regression suite and boolean comparison defaults.

## Repository state

The repository has two branches:

- `main` remains the published v0.10.14 line and has not been modified by the v0.11.0 work.
- `v0.11.0-language-overhaul` contains the active development work and is the head of draft PR #2. It is 107 commits ahead of `main` with no commits behind at the time of this status snapshot.

The branches are **not merged**. That is intentional. PR #2 must remain a draft until the language acceptance work, documentation, examples, compatibility coverage, release verification and owner review are complete. Do not tag or publish v0.11.0 based on one isolated syntax improvement or a green CI run.

## What is implemented on the development branch

The branch has moved well beyond the original v0.10.14 baseline. Its implemented language work includes:

- Natural single-value random integer and number expressions, plus random item/element selection from CX lists. An automatically initialized default random stream is available, with explicitly seeded named streams as an optional deterministic facility.
- English-first numeric expressions for common operations, including absolute value, square root, floor/ceiling, sine/cosine/tangent, rounding, average/mean/median, sum/product, minimum/maximum and natural clamp syntax. Technical helpers and compact function forms remain available.
- Worded comparison aliases alongside symbolic operators, plus list membership and type/presence conditions; the full implemented comparison vocabulary and sign/parity/type/emptiness predicates now have dedicated runtime regression coverage.
- Skript-style `else if` chains are parsed into nested conditional branches and have runtime tests for first-match behavior, fall-through, final `else`, and nested chains.
- Function signatures accept typed parameters, optional trailing defaults, and declared return types in both `returns type` and Skript `:: type` forms. Runtime checks validate argument counts, value types, and return types while preserving legacy untyped functions. Defaults run at call time from left to right; typed boolean defaults can evaluate comparison expressions such as `{_value} == 5`. This feature set passed six-target run #129.
- `send` and `broadcast` are currently identical developer-log output effects, not player-directed messaging; the docs state this explicitly and the core runtime suite verifies the equivalence.
- Engine setting names remain stable underscore identifiers shared with `config.json`; their technical spelling is intentional rather than an incomplete prose alias.
- Typed sprite/entity object references with live-instance identity, property getters, the shared `<property> of <object>` setter grammar, and its equivalent possessive setter form. Supported getter/setter aliases, texture/visibility writes, maximum-health spellings, type restrictions and stale-handle behavior have expanded runtime test coverage.
- Entity classification and tag collection operations, including whole-set tags, add, remove, clear and has-tag checks.
- Expanded tests for expressions, loops, event syntax and runtime payloads, object-reference property aliases, variable effects, room transitions, screen effects, entity tags, and packaging.
- Cross-platform runtime packaging fixes and successful six-target validation.
- CI now syntax-checks each standalone `.cx` example and every multi-file example project through `tools/check_examples.py`; graphical smoke tests and runtime behavior tests remain separate.

Run #132 passed all six native targets for source/docs commit `ce59fec`, including converted wait/periodic limits, configured tick-rate validation, key-header quote balancing, and the suspended trigger-handler lifecycle regression. Run #133 exposed a fixture issue in the new animation/room integration test: it referenced an entrance symbol after that room had been unloaded. The return transition now uses explicit room coordinates; fresh validation is pending in run #134.

## Remaining v0.11.0 acceptance work

The principal P0 random-expression gap, **count-prefix/plural random list assignment**, has been implemented and covered by runtime tests. Its initial feature milestone passed six-target CI in run #100; the current runtime/source commit was later validated in run #129:

```skript
set {rolls::*} to 5 random integers between 5 and 96
```

The implementation accepts a numeric count expression from 0 through 4096 and expands the results into sequential indexed list entries through the existing scalar `Value` evaluator. It covers integer/number output, `between`/`from`, named streams and zero-count clearing. The current runtime/source commit was included in the successful six-target run #129.

Other remaining acceptance work includes:

- Complete the subsystem-by-subsystem grammar audit. Some APIs intentionally retain technical arguments or specialized commands; their language surface still needs a consistency review.
- The natural possessive property setter alias now normalizes to the shared object-property operation and has object-handle regression coverage, including getter/setter aliases and live-instance validation. Event headers are enumerated in the syntax guide and covered by positive alias and negative selector tests. Run #132 validates the converted seven-day wait and periodic limits, suspended trigger cancellation, effective `engine::tick_rate` checks, and balanced key-header grammar on all six native targets.
- The first-steps example and quickstart now demonstrate a live canvas score HUD, and the beginner/getting-started guides explain that on-screen text is distinct from developer-log output.
- Added an asset-free first complete game, Number Hunt, with a multi-file example project, step-by-step tutorial, and runtime coverage of a full round lifecycle.
- Expand the initial positive/negative syntax corpus across all advertised language families and compatibility aliases. The current subsystem corpus checks canonical/legacy forms, normalized parser meaning, invalid values, ambiguous anchors, missing selectors, and whitespace-only quoted event selectors. Runtime CTest coverage now checks every documented event payload field and the full implemented English comparison operator vocabulary; broader condition lifecycle cases remain part of the audit.
- Validate Number Hunt through the actual download/extract/launch path using an installed package, then consider a larger follow-up game; the complete tutorial and runtime-tested project now provide the first end-to-end game.
- Pass CI and release/package verification on the final accepted branch head.

## Documentation and public reference

`docs/reference.html` is byte-for-byte unchanged from `main`. It is the searchable **v0.10.14 release reference**, and is intentionally not being rewritten as part of an unaccepted development branch. The branch syntax guide, language charter, compatibility notes and audit describe the in-progress v0.11.0 language separately.

The development documentation changes are still confined to the unmerged branch. They are not part of the published main-branch documentation source yet. The versioned HTML reference shipped with v0.11.0 must be regenerated only after the final syntax surface is accepted.

## Next priorities

1. Complete six-target CI for the animation/room-transition integration test; the event-header and suspended-handler audit is now closed on run #132.
2. Continue cross-system end-to-end scenarios from `docs/ENGINE_INTEROPERABILITY_AUDIT.md`; the animation → room transition → completion callback path now has a combined fixture.
3. Expand the positive/negative syntax corpus to every advertised syntax family and compatibility alias.
4. Bring examples, beginner materials, feature pages, and versioned reference generation into alignment with accepted canonical grammar.
5. Verify Number Hunt through a clean download, extraction, and launch using an installed package.
6. Run fresh six-target CI and final clean extraction/launch checks on the final accepted branch head.
7. Present the completed draft for owner review; merge and publish only after acceptance.

CX remains an experimental pre-release engine. The v0.11.0 milestone is not complete and v1.0 readiness is a later milestone.
