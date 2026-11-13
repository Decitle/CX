# CX Script language design and Skript compatibility

CX Script is a language for portable 2D games and tools. Its product goal is to follow the **grammar and readability of Minecraft Skript closely for common language constructs**—events, variables, expressions, conditions, effects, loops and functions—without copying Minecraft-only server APIs.

This is a stronger requirement than simply looking “Skript-inspired”. The v0.10.14 language has a useful Skript-like foundation, but some later features drifted toward function-call syntax and command-specific property forms. The next version, v0.11.0, is dedicated to correcting that drift across the complete language surface.

## Released language versus v0.11.0 branch

The tagged v0.10.14 release and the interactive `reference.html` catalogue describe the released language. This branch is the in-progress v0.11.0 implementation, so `syntax.md` documents the grammar currently supported on this branch rather than serving as a frozen v0.10.14 reference. Do not assume every target example in the [language design charter](language-design.md) is implemented simply because it appears there.

The natural single-value forms `a random integer between 1 and 10`, `a random number from 0 to 1`, and `a random item out of {items::*}` are now accepted on this branch. They use an automatically initialized `"default"` stream unless an optional `using random stream "name"` clause selects another stream. Explicitly seeded streams provide reproducible sequences. Count-prefix/plural forms such as `5 random integers between 1 and 10` and `3 random numbers from 0 to 1` are implemented for list assignments, with a numeric whole-number count limited to 4096. The older `random_int`, `random_float`, and `random_choice` function forms remain available.

Typed sprite/entity references on this branch now share a natural property grammar: `<property> of <object>` for reads and `set <property> of <object> to <value>` for writes. In addition to transform/render properties, entity-only `health`, `max health`, `kind`, and `team` are supported. The health setters preserve the existing integer limits, max-health clamp and `{entity::name::...}` compatibility variables. Kind/team use the existing interned classification IDs; values must be quoted text. Older `set entity "id" health/kind/team to ...` forms remain accepted. Tags are collection operations: replace with `set entity "id" tags to ...`, add with `add tag "name" to entity "id"`, remove with `remove tag "name" from entity "id"`, or clear with `clear tags of entity "id"`. Collision-layer/mask configuration still uses specialized commands.

Common math operations now include natural forms such as `the sine of 90`, `the square root of 81`, `the average of {scores::*}`, and `clamp 15 between 0 and 10`; compact function spellings remain available. Procedural `noise2d`, `fbm2d`, and `lerp` remain function-call forms. The current engine also has familiar variable scopes, list variables, event blocks, `if`/`else`, loops, `wait`, periodic handlers, functions and return values. Those should be preserved while their complete grammar and edge cases are audited.

## Compatibility boundary

CX is not trying to replicate Minecraft-specific concepts such as Bukkit players, blocks, inventories, server commands, permissions or world administration. Those are not useful abstractions for a portable 2D desktop game engine.

Instead, CX uses its own portable concepts—projects, rooms, sprites, entities, canvas UI, input, animation, audio and rendering effects—with Skript-like syntax. Common operations should have English-first forms and sensible compact aliases. Coordinates and vectors may use conventional tuples such as `(x, y)`.

## Governing documents

- [Language design charter](language-design.md) — binding syntax and product principles for v0.11.0.
- [Skript syntax audit](SKRIPT_SYNTAX_AUDIT.md) — current feature inventory, concrete mismatches and implementation order.
- [Branch syntax guide](syntax.md) — grammar currently supported by this v0.11.0 development branch.
- [Interactive reference](reference.html) — searchable current release catalogue.

Do not describe CX as a drop-in Skript replacement. Do commit to making its portable language grammar recognizably close to Skript, with more capabilities in the 2D-game domain.
