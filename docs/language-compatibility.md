# CX Script language design and Skript compatibility

CX Script is a language for portable 2D games and tools. Its product goal is to follow the **grammar and readability of Minecraft Skript closely for common language constructs**—events, variables, expressions, conditions, effects, loops and functions—without copying Minecraft-only server APIs.

This is a stronger requirement than simply looking “Skript-inspired”. The v0.10.14 language has a useful Skript-like foundation, but some later features drifted toward function-call syntax and command-specific property forms. The next version, v0.11.0, is dedicated to correcting that drift across the complete language surface.

## Current release versus target syntax

The v0.10.14 syntax guide and interactive reference describe what the current release accepts. Do not assume that a target example in the new [language design charter](language-design.md) is already implemented.

One concrete example: the current random API documents `random_int("stream", minimum, maximum)`; it does not currently accept `set {_roll} to a random integer between 1 and 10` as a random expression. The latter is the required English-first form for the next language release. The compact function form can remain as an alias.

The current engine already has familiar variable scopes, list variables, event blocks, `if`/`else`, loops, `wait`, periodic handlers, functions and return values. Those should be preserved while their complete grammar and edge cases are audited.

## Compatibility boundary

CX is not trying to replicate Minecraft-specific concepts such as Bukkit players, blocks, inventories, server commands, permissions or world administration. Those are not useful abstractions for a portable 2D desktop game engine.

Instead, CX uses its own portable concepts—projects, rooms, sprites, entities, canvas UI, input, animation, audio and rendering effects—with Skript-like syntax. Common operations should have English-first forms and sensible compact aliases. Coordinates and vectors may use conventional tuples such as `(x, y)`.

## Governing documents

- [Language design charter](language-design.md) — binding syntax and product principles for v0.11.0.
- [Skript syntax audit](SKRIPT_SYNTAX_AUDIT.md) — current feature inventory, concrete mismatches and implementation order.
- [Current syntax guide](syntax.md) — exact supported v0.10.14 grammar.
- [Interactive reference](reference.html) — searchable current release catalogue.

Do not describe CX as a drop-in Skript replacement. Do commit to making its portable language grammar recognizably close to Skript, with more capabilities in the 2D-game domain.
