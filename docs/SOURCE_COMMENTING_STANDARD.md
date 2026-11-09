# Source-commenting standard

This standard is part of the project contract because the source is also a future C++ learning resource for the project owner.

## What to comment

- Public types and functions: their responsibility and what callers can expect.
- Algorithms that are not obvious from the names: parser indentation normalization, nested-block parsing, path reconstruction, save replacement, reload staging, and coordinate/projection conversions.
- Lifetime/ownership boundaries: who owns runtime state, how local variables differ from persistent globals, and when scheduled executions are discarded.
- Safety assumptions: what happens when a file is missing or malformed, what is preserved on failure, and which platform branch is untested.
- Non-obvious reason for a workaround or ordering requirement.

## What not to do

- Do not add a comment that merely translates `x += 1` into English.
- Do not promise behavior that code does not enforce.
- Do not write “always”, “never”, “atomic”, “thread-safe”, “fast”, or “safe” without naming the relevant boundary and evidence.
- Do not let stale comments survive a behavior change. Inspect code and tests, then update nearby comments and the affected feature documentation.
- Do not confuse the current implementation with roadmap intentions.

## The comment review loop

For every source change: review comments on the changed path, update the relevant public feature guide if a behavior claim changed, and add a regression test for any new externally observable contract. Keep changes scoped so a beginner can follow a diff.

## Coverage plan

The v0.8.6 pass adds teaching comments to public data structures, parser stages, runtime lifecycle/execution, variable scopes, storage format/replacement, room loading, audio generation, diagnostics, and the graphical host loop. This is the first layer, not a claim that every C++ line in the 9,000-line project has an explanatory comment. Future passes should continue module by module, with the parser and runtime expression/statement paths as the main teaching targets.
