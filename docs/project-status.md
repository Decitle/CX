# Project status

**Current published release:** [v0.10.14-preview.8](https://github.com/Decitle/CX/releases/tag/v0.10.14-preview.8)  
**Last verified six-target CI baseline:** [run 42969227408](https://github.com/Decitle/CX/actions/runs/42969227408)  
**Last verified Pages baseline:** [run 42969227621](https://github.com/Decitle/CX/actions/runs/42969227621)

## Main conclusion: language drift requires a v0.11.0 overhaul

The defining CX goal is a readable, Skript-like language for portable 2D games. The v0.10.14 codebase has a broad and useful feature foundation, but later APIs drifted toward function-call syntax and command-specific property forms. The random-number example exposed that the language no longer consistently meets its original usability goal.

The current release does **not** accept `set {_roll} to a random integer between 1 and 10` as a random expression. It documents `random_int("stream", minimum, maximum)` instead. The natural English form is now a P0 acceptance requirement for v0.11.0, with existing helper syntax retained as an optional alias.

The new [language design charter](language-design.md) and [Skript syntax audit](SKRIPT_SYNTAX_AUDIT.md) record the governing philosophy and source-backed current inventory. The full overhaul—not a single alias patch—is the next priority. No v0.11.0 tag should be created until its acceptance gate passes.

## Verified implementation inventory

The searchable reference contains 128 catalogue entries. The parser currently defines 13 event kinds, 55 statement kinds and 64 named regular-expression patterns. The public catalogue categories include events, seven variable forms, expression groups, conditions, effects, functions, loops, world/project/UI/graphics/audio/settings/debug APIs.

Random streams are already implemented: named streams support deterministic float, inclusive integer and option selection. Their current expression surface is function-style and requires an explicit seed. The new grammar must make ordinary random values work naturally while retaining the named deterministic facility.

## Confirmed design gaps

- The current random expression does not accept the expected plain-English grammar.
- Common math helpers are also function-call based.
- Sprite setters currently accept only a small fixed property set (position, size, texture, visibility and speed); x-coordinate and rotation forms are not implemented.
- The `Value` type holds only none, bool, number and string. First-class object handles are not available, so variable-targeted object properties need a deliberate typed-reference contract.
- Newer event headers often embed named objects and multiple clauses rather than using a short event header plus context values.
- Screen fade/opacity and named audio track/bus systems are not currently implemented. They are future capabilities, not syntax aliases waiting to be enabled.

## Current user-facing downloads and docs

The README links to the dedicated [download page](https://decitle.github.io/CX/downloads.html), which selects a complete six-platform runtime release and provides manual OS/architecture selectors. The release reference generator uses the `0_CX-Docs-v{version}.html` convention for future releases. The new Jekyll stylesheet makes rendered Markdown documentation use the same dark theme as the custom HTML reference, download page and home page; verify the deployment after the current docs changes.

## Next priorities

1. Complete the v0.11.0 Skript-like syntax overhaul using the design charter and audit.
2. Rework examples and the guided first-game experience to use the canonical English grammar.
3. Audit collision/motion stability and physical-device UI behavior.
4. Design richer audio track/bus control and screen fades only after core language consistency is established.

CX remains an experimental pre-release, not a v1.0-complete engine. The next version number is acceptance-gated by the complete language overhaul, all-platform validation and consistent documentation.
