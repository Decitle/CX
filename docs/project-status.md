# Project status

**Published release:** [v0.10.14-preview.8](https://github.com/Decitle/CX/releases/tag/v0.10.14-preview.8)  
**Development milestone:** v0.11.0 Skript-like language overhaul  
**Integration PR:** [#2 — v0.11.0-language-overhaul](https://github.com/Decitle/CX/pull/2) (open, draft, targets `main`)  
**Latest verified six-target development CI:** [run #91](https://github.com/Decitle/CX/actions/runs/43491377925) — all six native targets passed.

## Repository state

The repository has two branches:

- `main` remains the published v0.10.14 line and has not been modified by the v0.11.0 work.
- `v0.11.0-language-overhaul` contains the active development work and is the head of draft PR #2. It is 60 commits ahead of `main`, with no commits behind at the time of this status snapshot.

The branches are **not merged**. That is intentional. PR #2 must remain a draft until the language acceptance work, documentation, examples, compatibility coverage, release verification and owner review are complete. Do not tag or publish v0.11.0 based on one isolated syntax improvement or a green CI run.

## What is implemented on the development branch

The branch has moved well beyond the original v0.10.14 baseline. Its implemented language work includes:

- Natural single-value random integer and number expressions, plus random item/element selection from CX lists. An automatically initialized default random stream is available, with explicitly seeded named streams as an optional deterministic facility.
- English-first numeric expressions for common operations, including absolute value, square root, floor/ceiling, sine/cosine/tangent, rounding, average/mean/median, sum/product, minimum/maximum and natural clamp syntax. Technical helpers and compact function forms remain available.
- Worded comparison aliases alongside symbolic operators, plus list membership and type/presence conditions.
- Typed sprite/entity object references with live-instance identity, property getters and the shared `<property> of <object>` setter grammar for supported transform/render/health/classification properties.
- Entity classification and tag collection operations, including whole-set tags, add, remove, clear and has-tag checks.
- Expanded tests for expressions, loops, events, object references, variable effects, room transitions, screen effects, entity tags, and packaging.
- Cross-platform runtime packaging fixes and successful six-target validation.

The latest validated code commit before the current documentation corrections passed Linux x86_64/ARM64, Windows x86_64/ARM64 and macOS x86_64/ARM64 in run #91. An earlier CI attempt, run #90, failed to compile an intermediate commit; the reserved-token issue was corrected in the next commit and #91 passed. This is retained here for an honest validation trail, not as a current blocker.

## Remaining v0.11.0 acceptance work

The principal P0 language gap, **count-prefix/plural random expressions**, has now been implemented in the current development head. Its new six-target CI run is pending, so this is an implementation checkpoint rather than a validated completion:

```skript
set {rolls::*} to 5 random integers between 5 and 96
```

The implementation accepts a numeric count expression from 0 through 4096 and expands the results into sequential indexed list entries through the existing scalar `Value` evaluator. It covers integer/number output, `between`/`from`, named streams and zero-count clearing. Current-head CI and example validation are still required.

Other remaining acceptance work includes:

- Complete the subsystem-by-subsystem grammar audit. Some APIs intentionally retain technical arguments or specialized commands; their language surface still needs a consistency review.
- Review natural possessive property aliases and event grammar/context values. Typed sprite/entity handles and the main property grammar are already implemented; these are refinement/audit tasks, not a claim that object references are absent.
- Run the advertised syntax corpus and all repository examples through the CLI; add negative tests for ambiguous, invalid and out-of-context forms.
- Update the beginner learning path, complete-first-game example, subsystem pages and versioned generated reference to match the accepted v0.11.0 grammar.
- Pass CI and release/package verification on the final accepted branch head.

## Documentation and public reference

`docs/reference.html` is byte-for-byte unchanged from `main`. It is the searchable **v0.10.14 release reference**, and is intentionally not being rewritten as part of an unaccepted development branch. The branch syntax guide, language charter, compatibility notes and audit describe the in-progress v0.11.0 language separately.

The Pages site continues to represent the published/main documentation until the development PR is merged and the site workflow deploys the updated tree. The versioned HTML reference shipped with v0.11.0 must be regenerated only after the final syntax surface is accepted.

## Next priorities

1. Validate count-prefix/plural random list assignment on all six CI targets and through the full script/example corpus.
2. Complete the property/event/subsystem grammar review, preserving compatibility aliases where practical.
3. Bring examples, beginner materials, feature pages and reference generation into alignment with the accepted language.
4. Run the full syntax corpus, native six-target CI and clean extraction/launch checks.
5. Present the completed draft for owner review; merge and publish only after acceptance.

CX remains an experimental pre-release engine. The v0.11.0 milestone is not complete and v1.0 readiness is a later milestone.
