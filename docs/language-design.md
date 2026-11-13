# CX Script language design charter

**Status:** Governing design requirement for the v0.11.0 language overhaul  
**Applies to:** Every new language feature, syntax revision, example, built-in expression, effect, condition, event and project-facing API.

## Product purpose

CX Engine is a lightweight, reusable 2D game engine with a readable scripting language for people who have used Minecraft Skript and for people who simply want an approachable way to make 2D games. It should let a creator make a small game without learning C++ or installing a compiler. The engine's C++ core is an implementation detail for ordinary game authors, not a prerequisite for using the product.

The defining idea is:

> **Skript's approachable English-like scripting model, adapted for portable 2D games and extended where a game engine genuinely needs more.**

CX is not a Minecraft server, a Bukkit replacement, or a promise to implement every Minecraft-specific Skript expression or addon. It should, however, follow Skript's language patterns much more closely than the current v0.10.14 syntax does. Saying only that CX is “Skript-inspired” is not enough if everyday operations require function-call syntax that a Skript user would not expect.

## Non-negotiable language rules

### 1. English-first syntax is the default

A common action or value must read like an instruction. Basic operations should not require the user to think of a function name, parentheses, argument ordering, or a stringly-typed property key.

For example, the desired random expression is:

```skript
set {_roll} to a random integer between 1 and 10
```

The current `random_int("loot", 1, 10)` helper may remain as a compact/advanced alias, but it must not be the only way to express an everyday operation.

### 2. Offer multiple natural forms where they are useful

A primary English form and a compact or alternate form should resolve to the same behavior. Do not add dozens of synonyms without a reason; offer alternatives where they make the script clearer or more concise.

Examples of the intended direction (these are **design targets**, not claims about the current parser):

```skript
set {_roll} to a random integer between 1 and 10
set {_roll} to a random integer from 1 to 10
set {_roll} to random_int("loot", 1, 10) # compact compatibility alias

set x-coordinate of sprite "player" to 15
set sprite "player"'s x-coordinate to 15
set rotation of sprite "player" to 90

enter room "noteastereggroom1" at room position (5, 3)
wait 10 seconds
play sound {@impact}
```

Typed sprite/entity handles and the named random-stream grammar are implemented on the development branch. The standard event-context expression contract remains an open audit item; examples involving event context should not be advertised until that contract and its tests are complete.

### 3. Parentheses are appropriate when they communicate structure

Coordinate pairs, sizes, colors, vectors, grouped arithmetic and explicitly constructed values are good uses of parentheses:

```skript
move sprite "player" to (120, 80)
create light "torch" at (8, 5)
set {_distance} to ({_dx} * {_dx}) + ({_dy} * {_dy})
```

Do not force parentheses around simple English operations just because the C++ implementation is a function. An ordinary creator should be able to write a random integer, a value's coordinate, an object's rotation, or a length without calling a Python-like helper.

### 4. Use one property grammar throughout the engine

The preferred form is `set <property> of <object> to <value>`, with a possessive alternative where it reads naturally:

```skript
set x-coordinate of sprite "player" to 15
set rotation of sprite "player" to 90
set health of entity "guard" to 25
```

The `set entity "guard" health to 25` family remains accepted as a compatibility form; the shared property grammar also supports `set health of entity "guard" to 25`. The development branch stores sprite/entity references as typed values with live-instance identity in local and temporary variables. Ordinary strings never silently become object references, and stale handles are rejected after an object is removed or replaced.

### 5. Events describe what happened; context values describe who/what

Event headers should be short, recognizable and consistent. Event participants and event-specific data should be available through documented event-context expressions/locals. Avoid requiring every object name to be embedded in the event header when a handler can filter on an entity or condition inside its body.

The current key-press, button, trigger, focus, animation, timing and room lifecycle events should be reviewed for a coherent `on <event>:` grammar. Keep readable aliases when they improve clarity.

### 6. Conditions should read like conditions

Retain symbols for concise numeric logic, but support common English comparisons as aliases, including “is greater than”, “is less than”, “is at least”, “is at most”, “is equal to”, “is not equal to”, “contains”, “is set” and their natural negations. The same condition must behave identically regardless of spelling.

### 7. Variables should preserve Skript's familiar model

Keep and document these lifetimes and namespaces:

| Form | Meaning |
|---|---|
| `{name}` | Persistent project/global value |
| `{_name}` | Event/function-local value |
| `{^name}` | CX process-lifetime temporary value; a documented CX extension |
| `{list::*}` | List namespace / wildcard iteration |
| `{$texture}` | Read-only discovered texture asset reference |
| `{@sound}` | Read-only discovered audio asset reference |
| `engine::setting` | Reserved engine setting namespace, assigned without variable braces |

List access, iteration values, indices, named keys, deletion and empty-list behavior must remain consistent. Do not make a new feature invent its own unrelated variable syntax.

### 8. Keep the existing useful Skript-like constructs

The current `set`, `add`, `subtract`, `delete`, `clear`, `if`/`else`, counted/list/while loops, `loop-value`, `loop-index`, `wait`, `every`, functions and `return` are a useful foundation. Audit their edge cases and naming, but do not replace working forms merely to make them look different.

### 9. Preserve compact/technical forms as optional tools

Mathematical operators, explicit coordinate tuples, user-defined function calls and the current random/math helper calls can remain available. They are secondary forms, not the mandatory surface for common tasks. Advanced procedural helpers may reasonably use parameters, but a beginner must have an English-readable route when the operation is common enough to deserve one.

### 10. New subsystems must inherit the same language style

A feature is not complete when its C++ implementation works. Its public script interface must be reviewed against this charter before it is considered ready. This applies equally to entities, rooms, UI, animation, lighting, audio, files, settings, timing and future tools.

Possible future APIs such as screen overlays/fades and named audio tracks/buses should follow the same style. They are **not implemented by this document**. For example, a future audio-track system should read in terms of tracks and volume rather than expose raw mixer function calls; a future screen fade should read as a fade/opacity effect rather than require a creator to reverse-engineer color-channel tricks.

## Randomness contract

The v0.11.0 branch supports natural single-value expressions such as `a random integer between 1 and 10`, `a random number between 0 and 1`, and `a random item out of {_items::*}`. They use an automatically initialized runtime-local `"default"` stream unless the optional `using random stream "name"` clause selects another stream. The default stream can be explicitly seeded for reproducibility; other named streams must be seeded before use. The compact `random_float`, `random_int`, and `random_choice` forms remain available.

The count-prefix/plural form is implemented on the development branch for list assignments, such as `set {rolls::*} to 5 random integers between 5 and 96`. The count may be a numeric expression but must resolve to a whole number from 0 through 4096; zero clears the target list without drawing. Each item is evaluated through the existing scalar random expression path, so CX keeps its scalar `Value` model and uses its indexed-list assignment contract rather than introducing an untyped collection value. CI and full example validation remain required. A weighted selector is a later optional extension, not a prerequisite for the first syntax overhaul.

## Parser and runtime implementation guidance

- Normalize aliases into one semantic operation. Different spellings must not create separate or subtly different runtime behaviors.
- Prefer a tokenizer and typed expression/statement representation for the language surface as it grows; avoid adding an unbounded pile of isolated regular expressions for every new phrasing.
- Keep the core and CLI independent of SFML. Syntax checking and graphical execution must use the same grammar and expression semantics.
- Preserve source locations through parsing so diagnostics can identify the exact file, line and relevant span.
- Keep parsing/evaluation predictable and lightweight for low-power computers. Do not trade away bounded work or runtime stability for syntactic flexibility.
- Validate ranges, units, types and object lifetimes at a clear boundary. Do not silently coerce an invalid object reference or make an unknown property look like a valid string.
- Maintain backwards-compatible aliases wherever practical. New canonical forms should not unnecessarily invalidate existing user scripts.

## Documentation and teaching rules

- The quick reference shows the canonical English form first and then accepted alternatives.
- Full guides clearly distinguish **currently supported syntax** from **planned v0.11.0 target syntax**.
- Every language feature gets at least one runnable script example, a concise scope/lifetime explanation, an error example and a regression test.
- Examples should sound like useful game scripts, not a collection of artificial parser demonstrations.
- All player-facing dialogue, narration, tutorials and prompts remain authored by the game creator. The engine supplies capabilities, not a game's words.

## v0.11.0 acceptance gate

CX must not be tagged `v0.11.0` until:

1. Every current event, variable form, expression, condition, effect/statement, function, loop and project-facing API has an audit entry and a chosen canonical syntax.
2. The random-number example in this charter and the common mathematical/property expressions have natural-English syntax and regression tests.
3. Property access and object references have a clear, consistent contract; no syntax example relies on unsupported first-class values.
4. Common events and effects have short, coherent Skript-style forms while the engine remains portable and 2D-focused.
5. Existing v0.10.14 scripts continue to parse through compatibility aliases where feasible, or any unavoidable migration is documented and tested.
6. The searchable reference, syntax guide, learning guide, examples and generated release HTML all show the same canonical grammar.
7. All native CI targets, clean runtime packaging and relevant parser/runtime regression suites pass.

A version number is not an acceptance criterion. No `v0.11.0` tag should be made just because the first natural syntax expression lands.
