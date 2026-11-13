# CX Script / Skript syntax audit

**Audit baseline:** CX Engine `main` commit `b00b8b6ff851462f58eea3a15ff37e0de419e40f` (v0.10.14-preview.8 source line)  
**Reference baseline:** official Skript core documentation 2.17.1  
**Purpose:** establish the current CX language surface, identify syntax drift, and set the work required for the v0.11.0 overhaul.

This compares language design and grammar, not Minecraft server APIs. CX should reuse Skript's approachable patterns for portable concepts while using CX-specific room, sprite, entity, input, UI, audio and graphics concepts where Minecraft has no equivalent.

## 1. Verified source inventory

The current public searchable reference contains **128 catalogue entries** across the categories below. The parser source separately defines **13 event kinds**, **55 statement kinds**, and **64 named regular-expression patterns**. Some catalogue entries intentionally group several forms under one heading; the counts are not a count of individual tokens or aliases.

| Catalogue category | Entries | Audit scope |
|---|---:|---|
| Events | 13 | Event header patterns and event-context data |
| Variables | 7 | Lifetimes, lists and discovered asset namespaces |
| Expressions | 11 | Literals, arithmetic, interpolation, numeric helpers and random values |
| Statements | 1 | Named random-stream seeding statement |
| Conditions | 10 | Presence, input, geometry, metadata and comparisons |
| Effects | 15 | Assignment, mutation, output, control and room/tile actions |
| Functions | 2 | Function declaration and return |
| Loops | 7 | Counted/list/while iteration, wait and loop control |
| World/entity | 23 | Rooms, sprites/entities, properties, movement, animation and spatial systems |
| Project | 6 | Project layout, config, assets and persistence |
| UI | 7 | Canvases, elements, layout and open/close |
| Graphics | 7 | Lighting, beams, projection and visual layers |
| Audio | 5 | Asset IDs, generated waves and playback controls |
| Settings | 10 | Window, simulation, console, display, lighting and UI configuration |
| Debug | 4 | CLI checking, reload, diagnostics and Room Studio |
| **Total** | **128** | |

The current parser's `EventType` enumeration is: `Load`, `Tick`, `Periodic`, `KeyPress`, `ButtonClick`, `FileAttach`, `ProjectileHit`, `TriggerEnter`, `TriggerExit`, `FocusEnter`, `FocusExit`, `TweenComplete`, and `AnimationComplete`.

The 55 `StatementType` values cover variable assignment/deletion/mutation, random seeding, room lifecycle and entrances, tile/overlay edits, light/beam operations, sprite/entity creation and properties, movement/path/animation operations, timed motion, interaction/collision metadata, loops, waits, function returns, canvas operations, audio generation/playback and stop. The parser enum is the implementation inventory; the searchable reference is the author-facing overview.

## 2. Current language catalogue

### Events — 13

- `on load:`
- `on tick:` / `every tick:`
- `every <duration>:`
- `on press key "KEY":` and the accepted alias `on key press "KEY":`
- `on click button "id":`
- `on file attach:`
- Named target-impact callback event
- `on tween complete for entity "name":`
- `on animation complete "clip" for entity "name":`
- `on entity "actor" enter trigger "area":`
- `on entity "actor" exit trigger "area":`
- `on entity "actor" focus enter entity "target":`
- `on entity "actor" focus exit entity "target":`

**Audit result:** lifecycle and periodic events are already close to Skript. Key press has two word orders, but the event family is narrow: a consistent key/mouse press/release vocabulary, window focus, room lifecycle and standard event-context expressions still need design. Several newer event headers embed concrete entity names and multiple clauses directly in the header. That is more verbose than necessary; event participants should generally be exposed through documented context expressions/locals and filterable inside the body.

### Variables — 7

- `{name}` — persistent global
- `{_name}` — event/function-local
- `{^name}` — process-only temporary (CX extension)
- `{$texture_id}` — read-only discovered texture reference
- `{@audio_id}` — read-only discovered audio reference
- `{list::*}` — list wildcard / namespace
- `engine::setting` — reserved setting namespace without braces

**Audit result:** this is one of the strongest parts of CX's Skript lineage. Global/local/list forms are recognizable. The process-only `^` namespace and asset namespaces are legitimate CX extensions, but their lifetime, mutability and interpolation behavior need to stay consistent. `engine::setting` is deliberately special; its names and friendly aliases need a stable rule.

### Expressions — 11 groups

- Primitive numbers, booleans, `none` and quoted text
- Arithmetic operators and parentheses
- Variable interpolation inside text (including `%{name}%` in supported paths)
- `sin(x)`, `cos(x)`, `tan(x)`
- `sine(hz, seconds[, phase])`
- `noise2d(x, y[, seed])`
- `fbm2d(x, y[, seed[, octaves]])`
- `random_float("stream")`, `random_int("stream", minimum, maximum)` and `random_choice("stream", option1, option2, ...)`
- Natural single-value forms: `a random integer between 1 and 10` and `a random number from 0 to 1`
- Random selection from list variables: `a random element/item out of {items::*}`
- Optional `using random stream "name"` clause for natural forms
- The runtime-local `"default"` stream is initialized automatically; other named streams must be seeded explicitly
- `clamp(value, min, max)` and `lerp(a, b, t)`

<badge color="warning">P0 syntax gap</badge>

The v0.11.0 branch has implemented natural **single-value** random integer/number expressions, list item/element selection, and the automatically initialized default stream. The remaining Skript compatibility gap is the count-prefix/plural form (for example, `5 random integers between 5 and 96`) and its assignment into a list variable. CX currently evaluates scalar `Value` expressions and stores lists through indexed variable namespaces, so this needs a deliberate collection-return/assignment contract rather than a regex-only special case.

Numeric helpers should offer natural aliases such as “the sine of …”, “noise at (x, y) using seed …” and “clamp … between …” while compact function aliases remain available. Parentheses remain appropriate for coordinate/vector tuples, grouping and advanced function calls.

### Conditions — 10 groups

- `if condition:` / `else:` blocks
- `value is set` / `value is not set`
- Held-key state: `key "W" is down` / `is not down`
- `mouse cursor is over button "id"`
- Entity AABB overlap query
- Entity distance/proximity query in pixels
- Interaction-focus query
- Entity kind/team query
- Entity tag query
- Comparisons and boolean operators

**Audit result:** the overall structure is familiar. Numeric operators such as `>=` and `<=` are compact, but common English comparison alternatives should be recognized and documented. Presence, proximity, overlap and focus conditions should keep clear distinctions; no syntax alias should change their lifecycle or filtering semantics.

### Effects — 15 groups

- Set an entity's interaction eligibility or interaction range
- `set {variable} to value`
- `add value to {variable}`
- `subtract value from {variable}`
- `toggle {boolean}`
- `delete {variable}` / `clear {list::*}`
- Rich-text color/gradient formatting
- `broadcast text` / `send text`
- `stop`
- Open/close canvas
- Load/unload/reload room
- Enter room at an entrance
- Place/clear tile
- Place/clear overlay tile

**Audit result:** variable mutation and basic control effects are reasonably Skript-like. The typed object-property grammar now covers entity health as well as common visual/geometry properties, using forms such as `set health of entity "id" to 5`; the older `set entity "id" health to 5` form remains as a compatibility alias. Specialized entity metadata and collision settings still use command-specific forms. `send` also needs clearer audience semantics rather than being treated as a generic synonym for log output. Retain good `play sound {@impact}`-style forms.

### Functions and loops

- `function name(parameters):`
- `return value`
- Counted loops, list loops, `while`, `wait`/`halt`, loop exit, loop continue, and loop-number/counter/iteration/value/index context values

**Audit result:** these are already close to Skript's readable block model. Preserve them, test scope and scheduler lifecycle, and improve error messages/aliases rather than rewriting them without a reason. Functions are synchronous today and cannot suspend with `wait`; this limitation must stay explicit until task semantics change.

### World, UI, graphics, audio, settings and debug

The reference adds 23 world/entity entries, 6 project-structure entries, 7 UI entries, 7 graphics entries, 5 audio entries, 10 settings entries and 4 debug entries. They include named rooms/entrances, sprite and entity operations, properties, collision metadata, movement, paths, animation, timed motion, canvas elements and layout, lights/beams/layers, wave creation/export, sound playback, engine configuration, diagnostics and Room Studio.

**Audit result:** the API breadth is real, but the wording is uneven. Some operations read like plain English; others expose implementation labels (`font_size`, `sample_rate`, `ease_in_out`, `engine::ui_reference_width`) or use different object/property ordering. Coordinate tuples such as `(x, y)` are appropriate and should remain. The system-wide task is to unify the surrounding grammar, not to remove useful technical parameters.

## 3. Main syntax gaps, ranked

| Priority | Gap | Required direction |
|---|---|---|
| P0 | Multi-count random expressions are not implemented | Support Skript-style count prefixes and plural random number forms in list assignments without weakening CX's scalar/list value contracts. |
| P0 | Built-in math helpers are function-only | Add English expression forms and keep compact aliases. |
| P0 | Property setter grammar varies by subsystem | Standardize `set <property> of <object> to <value>` and a natural possessive alias. |
| P0 | No first-class object references in `Value` | Define typed sprite/entity/room references before accepting `{_object}` as an object target. Current `Value` holds only none, bool, number and string. |
| P1 | Event headers are inconsistent and sometimes over-specific | Define a short event vocabulary plus standard event-context expressions/locals and filtering conditions. |
| P1 | English comparison aliases are incomplete | Support common worded comparisons alongside symbols with identical semantics. |
| P1 | UI/graphics/audio phrases are mixed with underscore-style implementation terms | Offer word-spaced canonical syntax; preserve existing forms as aliases. |
| P1 | Event payload and object naming are inconsistent | Publish one event-value naming contract and one object-reference grammar. |
| P2 | Screen opacity/fade and audio-track/bus controls are absent | Treat as future engine capabilities; design their script surface in the same English-first style. They are not implemented in v0.10.14. |
| P2 | Weighted random selection is absent | Consider after the natural equal-weight/random-expression syntax is established. |
| P2 | Named cancellable tasks and module/import semantics are incomplete | Keep on roadmap; expose through readable constructs if/when implemented. |

## 4. Skript comparison baseline

The review uses official Skript core documentation 2.17.1. Its expression reference explicitly documents the random-number pattern `[a|%integer%] random (integer|number)[s] (from|between) %number% (to|and) %number%`, with examples using `a random integer between 1 and 10`. It also supports multiple random values in a list assignment. The effects reference uses `(wait|halt) [for] %time span%`; the event reference uses `on load:`/`on unload:` patterns and a broad event catalogue. CX should adopt those portable grammar patterns where the domain matches, not Minecraft-only objects or server behavior.

Official references:

- [Expressions (Skript 2.17.1)](https://docs.skriptlang.org/expressions.html)
- [Effects (Skript 2.17.1)](https://docs.skriptlang.org/effects.html)
- [Events (Skript 2.17.1)](https://docs.skriptlang.org/events.html)
- [Conditions (Skript 2.17.1)](https://docs.skriptlang.org/conditions.html)

## 5. Implementation sequence for v0.11.0

1. Freeze this inventory as the source audit; resolve every item into canonical syntax, aliases, implementation status and tests.
2. Design grammar/AST support for English expressions and property access. Avoid implementing the entire overhaul as one-off regex additions.
3. Complete the random-expression grammar: the single-value natural forms and implicit default stream now exist; design and implement count-prefix/plural results as a well-defined list assignment while retaining the existing function-call API.
4. Add natural math/helper expressions and worded comparisons.
5. Introduce explicit object references/handles, then standard property getter/setter syntax.
6. Normalize event grammar and event-context values; preserve existing handlers through aliases.
7. Normalize UI/graphics/audio/settings command wording and document the limits of currently unavailable screen/audio capabilities.
8. Run all examples and a dedicated syntax corpus through `cx-cli --check`; add positive tests for every alias and negative tests for ambiguity, invalid types and missing context.
9. Update the searchable reference, syntax guide, beginner guide, examples, generated release reference and complete-first-project tutorial together.
10. Pass the native six-target CI matrix and all packaging checks before tagging `v0.11.0`.

The v0.11.0 tag is blocked until the whole language surface has been audited and the acceptance criteria in [language-design.md](language-design.md) pass. No preview/stable release should imply that one random-expression patch alone completes this overhaul.
