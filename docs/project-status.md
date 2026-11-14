# Project status

**Published release:** [v0.10.14-preview.8](https://github.com/Decitle/CX/releases/tag/v0.10.14-preview.8)  
**Development milestone:** v0.11.0 Skript-like language overhaul  
**Integration PR:** [#2 — v0.11.0-language-overhaul](https://github.com/Decitle/CX/pull/2) (open, draft, targets `main`)  
**Latest completed six-target development CI before the current subsystem syntax increment:** [run #100](https://github.com/Decitle/CX/actions/runs/43505981740) — all six native targets passed build, CTest, all-example syntax checks, and native package creation; Linux and Windows also passed graphical startup smoke tests. The new natural UI/graphics/audio parser forms and dedicated corpus are awaiting their own CI run.

## Repository state

The repository has two branches:

- `main` remains the published v0.10.14 line and has not been modified by the v0.11.0 work.
- `v0.11.0-language-overhaul` contains the active development work and is the head of draft PR #2. It is ahead of `main` with no commits behind at the time of this status snapshot.

The branches are **not merged**. That is intentional. PR #2 must remain a draft until the language acceptance work, documentation, examples, compatibility coverage, release verification and owner review are complete. Do not tag or publish v0.11.0 based on one isolated syntax improvement or a green CI run.

## What is implemented on the development branch

The branch has moved well beyond the original v0.10.14 baseline. Its implemented language work includes:

- Natural single-value random integer and number expressions, plus random item/element selection from CX lists. An automatically initialized default random stream is available, with explicitly seeded named streams as an optional deterministic facility.
- English-first numeric expressions for common operations, including absolute value, square root, floor/ceiling, sine/cosine/tangent, rounding, average/mean/median, sum/product, minimum/maximum and natural clamp syntax. Technical helpers and compact function forms remain available.
- Worded comparison aliases alongside symbolic operators, plus list membership and type/presence conditions.
- Typed sprite/entity object references with live-instance identity, property getters, the shared `<property> of <object>` setter grammar, and its equivalent possessive setter form for supported transform/render/health/classification properties.
- Entity classification and tag collection operations, including whole-set tags, add, remove, clear and has-tag checks.
- Expanded tests for expressions, loops, events, object references, variable effects, room transitions, screen effects, entity tags, and packaging.
- Cross-platform runtime packaging fixes and successful six-target validation.
- CI now syntax-checks each standalone `.cx` example and every multi-file example project through `tools/check_examples.py`; graphical smoke tests and runtime behavior tests remain separate.

The latest validated code commit before this subsystem syntax increment passed Linux x86_64/ARM64, Windows x86_64/ARM64 and macOS x86_64/ARM64 in run #100. An earlier CI attempt, run #90, failed to compile an intermediate commit; the reserved-token issue was corrected in the next commit and #91 passed. This is retained here for an honest validation trail, not as a current blocker.

## Remaining v0.11.0 acceptance work

The principal P0 random-expression gap, **count-prefix/plural random list assignment**, has now been implemented and covered by runtime tests. Its current-head six-target CI passed in run #100, so the random-list milestone is validated for that exact head:

```skript
set {rolls::*} to 5 random integers between 5 and 96
```

The implementation accepts a numeric count expression from 0 through 4096 and expands the results into sequential indexed list entries through the existing scalar `Value` evaluator. It covers integer/number output, `between`/`from`, named streams and zero-count clearing. Current-head CI and example validation are still required.

Other remaining acceptance work includes:

- Complete the subsystem-by-subsystem grammar audit. Some APIs intentionally retain technical arguments or specialized commands; their language surface still needs a consistency review.
- The natural possessive property setter alias now normalizes to the shared object-property operation and has object-handle regression coverage. Event grammar/context values and remaining subsystem-specific properties still need review.
- Expand the initial positive/negative syntax corpus across all advertised language families. The current subsystem corpus checks canonical/legacy forms, normalized parser meaning, invalid values, ambiguous anchors, and missing event selectors. Event payload population and lifetime still need runtime-level coverage.
- Update the beginner learning path, complete-first-game example, subsystem pages and versioned generated reference to match the accepted v0.11.0 grammar.
- Pass CI and release/package verification on the final accepted branch head.

## Documentation and public reference

`docs/reference.html` is byte-for-byte unchanged from `main`. It is the searchable **v0.10.14 release reference**, and is intentionally not being rewritten as part of an unaccepted development branch. The branch syntax guide, language charter, compatibility notes and audit describe the in-progress v0.11.0 language separately.

The development documentation changes are still confined to the unmerged branch. They are not part of the published main-branch documentation source yet. The versioned HTML reference shipped with v0.11.0 must be regenerated only after the final syntax surface is accepted.

## Next priorities

1. Finish the event-header/context audit and verify event-local population and lifetime behavior at runtime.
2. Expand the positive/negative syntax corpus to every advertised syntax family and compatibility alias.
3. Bring examples, beginner materials, feature pages and versioned reference generation into alignment with accepted canonical grammar.
4. Run current-head six-target CI and final clean extraction/launch checks after the syntax increment.
5. Present the completed draft for owner review; merge and publish only after acceptance.

CX remains an experimental pre-release engine. The v0.11.0 milestone is not complete and v1.0 readiness is a later milestone.
