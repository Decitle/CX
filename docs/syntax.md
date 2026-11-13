# CX Script syntax guide (reviewed against v0.10.14)

> This page describes syntax accepted by the current v0.10.14 source line. The v0.11.0 language overhaul has a separate [design charter](language-design.md) and [source-backed syntax audit](SKRIPT_SYNTAX_AUDIT.md). Target examples in those documents are not necessarily accepted by this release yet.
>
> Feature headings retain the version in which each capability was introduced. They are historical introduction labels, not the version coverage of this whole guide. For detailed lifecycle guarantees, also read the focused feature notes.

CX is designed to read like plain English. Indentation is forgiving: CX auto-detects conventional four-space or two-space style per file, treats a tab as one level in the detected style, and normalizes both styles to the same internal block levels. In a two-space file, four spaces count as two levels. Align sibling statements; if an indentation level is ambiguous, the parser points it out. When a line is malformed, the runtime diagnostic output reports its file and line; the graphical runtime also retains reload diagnostics in the Ctrl+Tab drawer, shows the source and a caret, explains the problem, and suggests a correction.

## Events

```skript
on load:
    broadcast "The game started."

on tick:
    add 1 to {^ticks}

on press key "SPACE":
    broadcast "Space was pressed."

on click button "resume_btn":
    broadcast "The resume button was clicked."

on entity "player" enter trigger "door_area":
    set {near_door} to true

on entity "player" exit trigger "door_area":
    set {near_door} to false

on tween complete for entity "boulder":
    set {last_boulder_x} to {_tween_x}
```

An event header ends with `:`. Its commands are indented under it. `on tick:` uses the fixed logic rate, so avoid file I/O, expensive searches, and unbounded loops there. Use `{^...}` for high-frequency counters that do not need persistence, so they do not trigger unnecessary variable-file writes. Named trigger handlers run once when an actor starts overlapping a trigger area and once when that contact ends. Tween completion handlers run once when a deterministic entity movement finishes; see the trigger and tween sections below for event-local values and lifecycle limits.

## Global and event-local variables

```skript
set {player_name} to "Player One"
set {score} to 0

on press key "SPACE":
    add 1 to {score}
    set {_score_after_press} to {score}
    broadcast "{player_name} has {_score_after_press} points."
```

`{score}` persists across event calls and is written to `variables.csv` plus a compressed `variables.cxv` cache. `variables.meta` records the last complete save generation and fingerprints for those exact file bytes. `variables.csv` is authoritative if it exists, so deleting a row while the engine is closed deletes that variable on next launch. If the CSV is missing, a manifest-backed project only reconstructs it from cache bytes that match the last committed generation; older projects without a manifest retain legacy recovery and are upgraded when they next save or reconstruct the CSV. The cache uses zlib when available and a built-in portable fallback otherwise; the fallback cache can be read by either build. A zlib-compressed cache needs zlib to recover if the CSV is missing. Before replacing existing regular save files, the engine keeps one-generation `variables.csv.bak` and `variables.cxv.bak` copies for manual recovery; it does not automatically load those backups. Malformed CSV rows generate line-specific warnings, valid rows remain available, and a warning-only load does not immediately overwrite the source file. `{_score_after_press}` belongs to one event firing and is discarded at the end. `{^session_value}` is shared across event calls during the current process but never saved to disk; it starts empty on the next launch. Each event firing gets a fresh local map. Names are case-insensitive; curly braces are used for user-variable references. Variables with the `engine::` prefix are built-in settings and are assigned without braces.

Quoted names in commands such as `create entity "player"` are stable engine object IDs, not ordinary variable values. Use an ID consistently when a command acts on that object. Use `{player_health}` for a variable holding data, `{$hero}` for a read-only texture asset reference, and `{@impact}` for a read-only audio asset reference. This deliberately keeps object identity distinct from values without requiring a new syntax migration.

## Values and changing them

```skript
set {lives} to 3
set {game_started} to true
set {player_name} to "Player One"
set {missing_value} to none
add 1 to {lives}
give 2 to {lives}
subtract 2 from {lives}
reset {player_name}
toggle {game_started}
```

Basic values are numbers, booleans, `none`, quoted text, and variable references. Values inside quoted strings are interpolated. Initialize number variables before using arithmetic for clear behavior. `give amount to {variable}` is an alias for `add amount to {variable}`; `reset {variable}` clears one variable or an entire list namespace, like `delete`/`clear`.

## Conditions

```skript
if {score} >= 5:
    broadcast "Milestone reached!"
else:
    broadcast "Keep going!"
```

Use `>`, `<`, `>=`, `<=`, `is`, `is not`, `equals`, `contains`, `and`, `or`, `not`, and parentheses. Natural numeric comparisons accept forms such as `is greater than`, `higher than`, `bigger than`, `larger than`, `more than`, `is less than`, `lower than`, and `smaller than`.


## Loops, wait, and scheduled events

```skript
on press key "SPACE":
    set {_num} to 5
    loop 5 times:
        send "%{_num}%"
        add 1 to {_num}
        wait 1 second
    send "Countdown over!"
```

`loop 5 times:` runs its body five times. `loop {_count} times:` accepts a variable containing a non-negative whole number. Inside a counted loop, `loop-number` and `loop-value` both refer to the current iteration number (starting at 1). In strings, use Skript-style interpolation such as `"%{_num}%"` or `"%loop-number%"`.

`while condition:` re-checks its condition after each iteration. Use `exit loop` (also `stop loop`) to leave the nearest active loop, or `continue loop` (also `continue`) to skip the remainder of that iteration and move to the next one. Every active loop must have a reachable exit condition unless it intentionally runs over time with a wait.

`wait 1 second` (also `wait 250 milliseconds`, `wait 2 ticks`, or `halt for 1 minute`) yields the current script execution to the scheduler. The engine continues drawing and processing other events, then resumes the suspended statements later. Pending waits are in RAM only and are canceled by `/reload` or process exit; they are not durable timers. One-line terminal commands cannot suspend because their temporary execution tree would not remain valid.

Since v0.9.6, a suspended wait is also tied to the active room context at the moment the `wait` statement is reached. A successful room entry (including entering a different entrance in the same room), active-room reload, or active-room unload cancels continuations suspended in the previous context. If two waits become due on one tick and the first changes rooms, later ready continuations from the old room are skipped. A handler may change rooms and continue normally; if it reaches a new `wait` after that statement, the new suspension belongs to the new room. Failed `/reload` attempts preserve waits in the running program. A successful `/reload` replaces old-program waits but keeps waits created by the new program's successful startup. Cancellation is silent and has no script command or named task handle yet.

Use `every 1 second:` for periodic work, or `on tick:` for lightweight code that belongs to every fixed logic update:

```skript
every 1 second:
    add 1 to {seconds_elapsed}

on tick:
    add 1 to {logic_steps}
```

Periodic events are checked by the scheduler at logic-tick resolution and do not burst-run for every missed interval after a stall. Use `on tick:` only when the work needs that frequency. Large work loops should be reduced, split across frames, or deliberately yield with `wait`.

## List variables and iteration

List variables reuse the normal variable syntax. Entries can have integer indexes or named keys; list wildcard references end in `::*`.

```skript
set {_items::*} to "potion", "key", "map"
set {_items::favorite} to "compass"
add "torch" to {_items::*}
give "coin" to {_items::*}

loop {_items::*}:
    broadcast "%loop-index%: %loop-value%"

remove "key" from {_items::*}
remove every "potion" from {_items::*}
delete {_items::favorite}
clear {_items::*}
reset {_items::*}
```

`loop-value` returns the value and `loop-index` returns the original entry key (such as `1` or `favorite`). List loops iterate over a snapshot, so adding/removing entries inside a loop does not alter the current iteration. `set {copy::*} to {source::*}` copies all entries and keeps their keys. `set {_items::*} to "a", "b"` creates sequential numeric entries. `clear {items::*}`, `delete {items::*}`, or `reset {items::*}` removes every entry; `delete {items::key}` removes one entry. `add value to {items::*}` and `give value to {items::*}` append at the next numeric index. `remove value from {items::*}` removes the first matching value; `remove all value from {items::*}` and `remove every value from {items::*}` remove all matches. List entries use the same variable persistence format as other global variables.

## Reusable functions and expressions

```skript
function clamp(value, minimum, maximum):
    if {_value} < {_minimum}:
        return {_minimum}
    if {_value} > {_maximum}:
        return {_maximum}
    return {_value}

on load:
    set {volume} to clamp(1.4, 0, 1)
    set {double_score} to ({score} + 3) * 2
```

Function parameters become event-like locals named `{_value}`, `{_minimum}`, and `{_maximum}`. A function returns a value with `return`; calls can be used where a value is expected. Functions are synchronous and cannot use `wait`. Recursion is limited to 64 nested calls. Arithmetic uses parentheses and conventional precedence: `*`, `/`, `%` before `+` and `-`. `+` concatenates values when either operand is text. Numeric functions include `sin`, `cos`, `tan`, `sine`, `noise2d`, `fbm2d`, `clamp`, and `lerp`. Seeded random functions are documented in the next section.

### Random expressions and streams

```skript
on load:
    seed random stream "encounters" with 4815
    seed random stream "loot" with 90210

on tick:
    if random_float("encounters") < 0.002:
        set {next_encounter_kind} to random_choice("encounters", "slime", "bat", "guard")
    set {ambient_roll} to a random integer between 1 and 100

on press key "L":
    set {loot_roll} to a random integer from 1 to 100 using random stream "loot"
    set {random_number} to a random number between 0 and 1
    set {random_item} to a random item out of {items::*} using random stream "loot"
```

Natural expressions such as `a random integer between 1 and 10`, `random number from 0 to 1`, and `a random item out of {items::*}` use the runtime-local `"default"` stream unless they include `using random stream "name"`. The default stream is initialized automatically for each runtime with best-effort runtime entropy, so ordinary random expressions do not need setup but are not reproducible between sessions by default. To make the default stream reproducible, seed it explicitly with `seed random stream "default" with integer`.

`seed random stream "name" with integer` creates or resets a case-insensitive stream. Names are trimmed and limited to 64 characters; separate names do not consume one another's sequences. Seeds must be whole numbers in the exact JavaScript-safe integer range `-9007199254740991` through `9007199254740991`. Seed named streams once during startup (typically in `on load:`), not on every tick unless repeatedly restarting a sequence is intentional. Any named stream other than the pre-created `"default"` stream must be seeded before use.

- `random_float("stream")` returns a number in `[0, 1)` using the high 53 bits of SplitMix64.
- `random_int("stream", minimum, maximum)` returns an unbiased integer in the inclusive range. Both bounds must be finite safe integers and `minimum` must not exceed `maximum`. Even an equal-endpoint range consumes one draw.
- `random_choice("stream", option1, option2, ...)` accepts 1–256 value expressions and returns the selected value without converting its type. With multiple options, one bounded draw selects the option; only that option expression is evaluated. A single-option choice does not advance the stream.
- Random natural expressions also accept the optional `using random stream "name"` clause. `random item` and `random element` are equivalent list-choice forms.

Each runtime instance owns its stream state. A successful `/reload` builds a new default stream and replays the new program's explicit seed statements; a failed reload preserves the last-good runtime and its exact stream positions. Stream states are not written to `variables.csv`. Explicitly seeded streams reproduce their sequences; the automatically initialized default stream is regenerated on runtime reset. SplitMix64 is for game logic, not cryptography or security-sensitive tokens.



## Separate rooms and named entrances

Rooms are stored in `rooms/<name>.room.json`. Use Room Studio to paint a room grid and define named entrances without hand-counting tiles.

```skript
load room "start"
load room "cave"
set {entrance1::start} to an entrance point at (6, 9)
set {entrance1::cave} to an entrance point at (3, 8)

on load:
    enter room "start" at {entrance1::start}

on press key "C":
    enter room "cave" at {entrance1::cave}

on press key "V":
    enter room "start" at {entrance1::start}

on press key "R":
    reload room "start"
```

Room Studio entrance `spawn` in `start.room.json` automatically becomes `{entrance::start::spawn}` when that room is loaded. The variable is a tile coordinate pair such as `(6, 9)`. Room coordinates start at `(0, 0)` in the top-left. `enter room` loads the target if necessary and unloads the previous active room by default. `load room`, `unload room`, and `reload room` can also be used directly.

## Automatic texture IDs and per-tile zoom

Put files such as `grass.png`, `stone_up.png`, or `water.png` in `assets/textures/`. The filename without extension becomes a lowercase texture ID: `stone_up.png` becomes `{$stone_up}`. These texture identifiers are read-only. In room files, a tile stores only a texture ID (or `null` for empty); the image is loaded once by the renderer and reused.

Room Studio's **Texture zoom** control applies a zoom value when painting. Select **Scale** to change the zoom of existing cells. Each room has a `tileScales` grid parallel to `tiles`; older rooms without it default to `1.0`.

- `1.0×`: normal texture mapping.
- Below `1.0×`: repeat the texture more tightly inside the cell.
- Above `1.0×`: zoom in on the texture's center and crop the edges.

The editor disables canvas smoothing; the game defaults to `engine::texture_filter = "nearest"` so scaled pixel art remains crisp.

## Canvas UI: resolution-independent grid and anchors

A canvas keeps the existing panel/text/button declaration system. Grid coordinates are normalized on a 0–128 reference grid, then resolved against the actual viewport. `(0, 0)` is the top-left corner and `(128, 128)` is the bottom-right reference point. Anchors position the element relative to the selected point so, for example, a minimap can remain at the upper-right regardless of window size.

```skript
canvas "HUD":
    create panel "map_panel" at grid (127, 1) anchor top_right size (220, 150) color (18, 26, 40, 220)
    create button "map_btn" at grid (127, 1) anchor top_right size (200, 42) label "Map"
    create button "hotbar" at grid (64, 127) anchor bottom_center size (520, 54) label "Hotbar"
    create panel "tooltip_bg" to the left of button "map_btn" gap 10 size (180, 42) color (32, 42, 60, 235)

on press key "H":
    open canvas "HUD"

on press key "ESCAPE":
    open canvas "PauseMenu"

on click button "resume_btn":
    close canvas "PauseMenu"
```

Absolute `at (x, y)` declarations remain useful for pixel layouts and are scaled from the configured reference resolution. `at grid (x, y)` uses coordinates 0–128 and the optional anchors `top_left`, `top_center`, `top_right`, `center_left`, `center`, `center_right`, `bottom_left`, `bottom_center`, and `bottom_right`. `anchor auto` chooses the nearest edge based on the grid coordinate. Relative elements support `to the left/right/above/below of button|panel "id" gap N`; the target must be declared earlier in the same canvas. Use `engine::ui_scale` to scale control sizes globally without changing their edge anchor. `engine::resolution_mode` accepts `"responsive"` (adapt to the resizable window) or `"fixed"` (keep the configured window dimensions). `engine::ui_reference_width` and `engine::ui_reference_height` provide the design reference for legacy absolute-position UI coordinates. World rendering has its own tile-based view: `engine::world_view_height_tiles` defines the view height and view width follows window aspect ratio. The world is not stretched to different aspect ratios; the camera shows more or less world horizontally. Screen UI is resolved separately against the actual viewport dimensions. Panel colors are RGBA channels, each clamped to `0–255`. IDs are unique within a canvas. The canvas/button syntax remains unchanged; GUI left clicks hit-test resolved button bounds and run `on click button "id":`.

## Oblique room projection and transparent overlays

Room JSON supports `projection: "orthogonal"` (default) or `projection: "oblique"`. The oblique view draws each tile as a 2:1 diamond, but does not change the logical `(x, y)` tile coordinates used by entrances, paths, or the collision grid. Room Studio lets you switch the projection without reauthoring the room.

A transparent image needs an overlay when you want to see the ground beneath it. If you place a cut-out PNG into a base tile, that cell replaces the grass and its transparent pixels show the empty background. Use:

```skript
place overlay tile {$rock_cutout} at (8, 6) layer "foreground"
place overlay tile {$grass_fringe} at (8, 6) layer "above_entities"
clear overlay tile at (8, 6) layer "foreground"
```

Layer semantics are important: `background` is drawn above base tiles but behind entities (terrain border/autotile seams); `foreground` is depth-sorted with entities; `above_entities` is always drawn over entities. This naming is about render ordering, not behind/in front of base tile pixels. Different transparent overlays may share a cell/layer. They remain visual-only and do not modify the collision grid.

`assets/autotiles.json` can bind a terrain group to transparent edge/corner texture IDs. On room batch rebuild, exposed sides and corners generate cached `background` overlays. See `../assets/autotiles.json` for a working example and `engine-settings.md` for the detailed graphics controls.

## Sprites, entities, hitboxes, animation, paths, and projectiles (v0.8.0)

CX includes sprites and gameplay entities. `create sprite` is suitable for decorations and simple objects; `create entity` enables gameplay metadata such as hitboxes, health, path following and animation sheets. Entities use world pixel coordinates while room and NPC path points use tile coordinates. `engine::world_view_height_tiles` chooses the vertical number of visible tiles and the camera view derives its width from the window aspect ratio. `engine::camera_follow` can follow a named sprite/entity. This is a lightweight first collision/AI layer, not a complete physics engine.

```skript
set engine::show_debug_overlay to false
set engine::console_enabled to true
set engine::texture_filter to "nearest"
set engine::lighting_enabled to false

on load:
    create sprite "player" with texture {$player}
    set sprite "player" size to (48, 48)
    set sprite "player" position to (616, 336)

on tick:
    if key "W" is down:
        move sprite "player" by (0, -3)
    if key "A" is down:
        move sprite "player" by (-3, 0)
    if key "S" is down:
        move sprite "player" by (0, 3)
    if key "D" is down:
        move sprite "player" by (3, 0)
```

`player.png` in `assets/textures/` automatically becomes `{$player}`. Supported commands are:

- `create sprite "name" with texture {$texture}`
- `set sprite "name" position to (x, y)` and `set sprite "name" size to (width, height)`
- `set sprite "name" texture to {$other_texture}`
- `set sprite "name" visible to false` (or `true`)
- `move sprite "name" by (dx, dy)`
- `delete sprite "name"`

The graphical runtime updates keyboard state from key-press and key-release events, enabling conditions such as `if key "W" is down:`. Since `on tick:` runs at 60 Hz by default, a delta of 3 logical units per tick moves at approximately 180 logical units per second. Change the delta or `engine::tick_rate` to adjust speed. `set engine::show_debug_overlay to false` leaves a clean dark void with no engine header. When `engine::console_enabled` is true, `Ctrl+Tab` opens/closes the compact black terminal drawer sliding from the top; no terminal bar is shown while closed. The bundled `scripts/menus.cx` provides an in-game display/settings menu on F2.

### Entity classification and collision filtering (v0.10.2)

```cx
create entity "warden" with texture {$boss}
set entity "warden" kind to "boss"
set entity "warden" team to "hostile"
set entity "warden" tags to "boss, flying, armored"
set entity "warden" collision layer to "actors, hazards"
set entity "warden" collision mask to "actors, projectiles, props, triggers"
set entity "warden" collision projectile mask to "actors, props"

if entity "warden" kind is "boss" and entity "warden" team is "hostile":
    set {hostile_boss} to true
if entity "warden" has tag "flying":
    set {boss_can_fly} to true
```

`kind` says what the entity is, `team` says who it belongs to, and `tags` records additional author-defined traits. These are separate metadata fields; the engine does not automatically infer damage, behavior or allegiance from their values. Tag assignment replaces the set, and an empty string clears it. Identifiers are limited to 1–40 letters, digits, underscores or hyphens; an entity can have at most 32 tags.

Collision channels are `actors`, `projectiles`, `props`, `triggers`, `effects`, `pickups`, `hazards` and `sensors`. A layer may contain a comma-separated combination. A mask lists channels an object accepts; `all` and `none` are shortcuts. The relationship is mutual: each object's mask must include at least one channel assigned to the other object. Collision-enabled state and AABB overlap must also pass. `entity "a" overlaps entity "b"` uses this same filter. Room solid-tile collision remains a separate grid system.

Projectiles use the `projectiles` layer, inherit the firing entity's team, and copy that entity's `collision projectile mask`. The target's collision mask must also include `projectiles`. Both default masks are `all`, preserving existing scripts. This does not add team-based damage rules or multi-target selection. See `features/collision-classifications.md` for the v0.10.2 contract.

### Static room tile collision (v0.10.6)

Static tile solidity can be authored with `assets/collision.json` and per-cell `tileCollision` room data. These are room/asset data contracts rather than scripting commands. See `features/room-collision-metadata.md` for the JSON schema and Room Studio workflow. Entity collision layers/masks remain separate.

### Named entity animation clips (v0.10.7)

Configure an entity's spritesheet, define named clips, then explicitly play them. Frame and row indices are zero-based; frame ranges are inclusive.

```cx
set entity "player" animation sheet to (32, 32) columns 6 rows 8
define animation "wake" for entity "player" frames 0 to 3 row 0 at 8 fps once
define animation "walk" for entity "player" frames 0 to 5 row facing at 10 fps looping

on load:
    play animation "wake" for entity "player"

on animation complete "wake" for entity "player":
    play animation "walk" for entity "player"

on press key "S":
    stop animation for entity "player"
```

`row facing` follows the existing eight-way facing-row order and requires at least eight rows. A fixed row selects the same row regardless of movement direction. `once` holds the last frame for a full frame interval and emits a one-shot completion event; `looping` wraps and never completes. Clip playback advances on the logic tick, not the render frame. The completion handler receives `{_animation_entity}`, `{_animation_name}`, `{_animation_frame}`, and `{_animation_row}`. A selected custom clip takes priority over the legacy walk cycle; after a one-shot, it holds its final frame until another clip is played or animation is stopped. Changing an entity's animation sheet clears its clip definitions. See `features/animation-clips.md` for the lifecycle and timing contract.

### Textured beams and endpoint lights (v0.10.11)

A beam is a straight, constant-width textured segment between two world-space pixel coordinates. Put its tileable texture in `assets/textures/` and refer to its discovered asset ID:

```cx
create beam "sun shaft" from (320, -80) to (320, 240) using {$beam_strip}
set beam "sun shaft" width to 48
set beam "sun shaft" opacity to 0.72
set beam "sun shaft" tint to (255, 230, 190)
set beam "sun shaft" texture_repeat to 64
set beam "sun shaft" layer to "background" # or "world" / "foreground"
set beam "sun shaft" endpoint_light to true
set beam "sun shaft" endpoint_light_radius to 180
set beam "sun shaft" endpoint_light_intensity to 0.72
set beam "sun shaft" endpoint_light_color to (255, 196, 132)
set beam "sun shaft" end to (360, 260)
set beam "sun shaft" lifetime to 2.5 # zero is persistent
remove beam "sun shaft"
```

`width` is 1–2048 pixels; `opacity` and endpoint-light intensity are 0–1; texture repeat length and endpoint-light radius are 1–4096 pixels; lifetime is 0–3600 seconds. Tint and endpoint-light color take integer RGB triples from 0–255. Endpoints must be distinct finite coordinates. A beam created in an active room is associated with that room; one created with no active room is global. The three layers are: `background` after room lighting but before entities; `world` in the stable Y-depth draw order using the segment midpoint; and `foreground` after entities but before explicit `above_entities` overlays. Endpoint light is an independent 360-degree radial light at the projected end point and is rendered only while lighting is enabled. It does not inherit the beam's tint or opacity. See `features/textured-beams.md` for geometry, cache invalidation and limitations, and `examples/textured_beam.cx` for a complete example.

### Deterministic entity tweens (v0.10.5)

```cx
on press key "SPACE":
    tween entity "boulder" to (700, 360) over 0.9 seconds easing ease_in_out with arc height 120

on press key "C":
    cancel tween for entity "boulder"

on tween complete for entity "boulder":
    set {boulder_arrived_x} to {_tween_x}
    set {boulder_arrived_y} to {_tween_y}
    set {boulder_name} to {_tween_entity}
```

The tween moves an ordinary entity's top-left position in world pixels. Supported easing values are `linear`, `ease_in`, `ease_out`, and `ease_in_out`. The optional non-negative `arc height` adds an upward parabola above the straight path, peaking halfway through. Duration is evaluated when the command runs and rounded up to a fixed tick count using the tick rate at that moment; changing the tick rate later does not change the active tween's tick count.

`on tween complete for entity "name":` runs once after the exact destination is reached. Event locals are `{_tween_entity}`, `{_tween_x}`, and `{_tween_y}`. New tweens replace old ones without completing them; cancellation is silent. Explicit position changes, nonzero manual movement, pressed directional input, assigning a tile path, deletion, or same-name replacement cancel the tween. A room-context change discards stale tweens without completion. Successful startup tweens created during a transactional script load are preserved.

Since v0.10.10, tween collision response is explicit and defaults to `ignore` for backward compatibility:

```cx
set entity "crate" blocks movement to true
set entity "player" tween collision response to "stop" # or "slide" / "ignore"
```

Only entities marked `blocks movement true` can obstruct a tween; they must be collision-enabled ordinary entities and pass mutual collision-layer/mask filtering. Resolved solid room tiles and the active room boundary also obstruct `stop` and `slide`. `stop` cancels silently at the last safe tick position without a completion event. `slide` checks X then Y independently, continues its time curve, and completion locals report the actual final coordinates. `ignore` retains the original pass-through behavior. Trigger/focus targets and decorative entities do not block by default. This does not add general physics or swept collision to ordinary input/path movement. See `features/tween-collision-responses.md` for exact timing and limitations.

This is position interpolation, not general physics. The default collision response is `ignore`, preserving the pre-v0.10.10 behavior. Since v0.10.10, a tween can instead use swept `stop` or axis-separated `slide` against resolved solid room tiles, the active room boundary and explicitly marked entity blockers. Projectiles and plain sprites are not supported tween targets. See `features/tween-collision-responses.md` for exact syntax, timing, lifecycle and limitations.

### Entity proximity queries (v0.10.4)

Use a radius check when a game action depends on how close two gameplay entities are:

```cx
set {interaction_range} to 72
if entity "player" is within {interaction_range} pixels of entity "ancient_altar":
    set {can_interact_with_altar} to true
```

The distance is measured center-to-center using each entity's active hitbox center (or its sprite rectangle center when no hitbox is configured). The boundary is inclusive, so an entity exactly at the requested radius is within range. The distance can be a numeric expression or variable and must evaluate to a finite, non-negative number. Missing names and names that refer to sprites rather than gameplay entities evaluate false. `not` can negate the condition.

This is a geometric query, not a collision test: visibility, collision-enabled state, collision layers and masks do not affect the result. It does not automatically select the nearest entity, dispatch events, or move anything. See `features/entity-proximity.md`.

### Interaction focus (v0.10.8)

Interaction focus is a separate gameplay policy that picks the nearest opted-in entity for each configured actor. It does not pick an activation key or display player-facing text:

```cx
set entity "player" interaction range to 96
set entity "ancient_chest" interactable to true

on entity "player" focus enter entity "ancient_chest":
    set {interaction_hint} to "Press E to open the chest"

on entity "player" focus exit entity "ancient_chest":
    set {interaction_hint} to ""

on press key "E":
    if entity "player" is focused on entity "ancient_chest":
        broadcast "You open the chest."
```

A target defaults to non-interactable. A positive actor interaction range is required; zero disables selection. The range is finite and from 0 to 1,000,000 world pixels. Selection uses inclusive center-to-center distance from the effective hitbox centers and the same mutual collision-layer/mask filter used by entity interactions. The nearest target wins; exact distance ties use ascending entity name. Visibility and physical collision enablement do not disable focus. Focus events run after entity and trigger updates inside each tick; a change dispatches the old target's exit before the new target's enter. Room-context changes clear focus without cross-room exits. The public `is focused on` condition verifies the live target identity and eligibility immediately, even before the next exit event is reconciled. Physical blocking is separate: focused targets remain non-blocking unless explicitly opted into `blocks movement` for collision-aware tweens. See `features/interaction-focus.md` and `examples/interaction_focus.cx` for lifecycle limits and examples.

### Trigger enter/exit events (v0.10.3)

A trigger is an ordinary entity assigned to the `triggers` collision layer. The actor must use the `actors` layer and both sides' masks must accept the other side. For example:

```cx
create entity "player" with texture {$player}
create entity "door_area" with texture {$zone}
set entity "door_area" collision layer to "triggers"
set entity "door_area" collision mask to "actors"
set entity "player" collision mask to "triggers, props, hazards"

on entity "player" enter trigger "door_area":
    set {near_door} to true
    set {last_actor} to {_trigger_actor}
    set {last_area} to {_trigger_entity}

on entity "player" exit trigger "door_area":
    set {near_door} to false
```

`{_trigger_actor}` and `{_trigger_entity}` are local to the event firing. Trigger overlaps are sampled after the tick handler and entity/projectile update; they do not repeat while an actor remains inside. Separation, deletion, collision disablement, a newly incompatible mask, or loss of AABB overlap produces one exit on the next update. The pair is keyed by runtime instance IDs, so replacing an entity with another object under the same name closes the old contact and opens a new one. A room-context transition or successful script reload clears contacts without emitting exits across the scene boundary. Invisible entities can be triggers because visibility does not disable collision.

This first version only handles explicitly named non-projectile entity pairs. It is not swept trigger collision: an actor can cross a thin area between samples. It does not add general collision events or tile-authored triggers. Trigger overlap is independent from the v0.10.10 tween movement-blocker flag: a trigger remains non-blocking unless its author explicitly sets `blocks movement true`. Radius checks are a separate condition; see `features/entity-proximity.md` and `features/trigger-events.md`.

### Entity setup and animation

```skript
on load:
    create entity "player" with texture {$hero_8way}
    set entity "player" size to (32, 32)
    set entity "player" position to (608, 608)
    set entity "player" animation sheet to (32, 32) columns 4 rows 8
    set entity "player" hitbox to (15, 12) offset (8, 17)
    set entity "player" avoids solid tiles to true
    set entity "player" health to 6
    set entity "player" max_health to 6

on tick:
    move entity "player" using WASD at speed 170
```

The movement speed is in world pixels per second and diagonal movement is normalized. A spritesheet declaration gives frame width/height in pixels and the frame grid dimensions. This build advances the frame while moving and uses eight directional rows. Hitboxes are AABBs; hitbox dimensions are followed by an offset from the entity origin. Health values are exposed through `{entity::name::health}` and `{entity::name::max_health}` for UI bars or conditions.

### NPC paths and obstacle avoidance

```skript
create path "patrol" through tiles (10, 8), (16, 8), (16, 13), (10, 13)
set path of "npc" to path "patrol" with speed "1.25"
set entity "npc" avoids solid tiles to true
```

Waypoints are room tile coordinates, and path speed is in tiles per second. Avoidance uses four-neighbor breadth-first search around cells resolved as solid by the shared collision metadata rules (or the legacy filename defaults for old projects without a valid manifest). It does not yet route around moving entities or expose an in-editor waypoint painter.

### Projectiles and hit callbacks

```skript
on press key "SPACE":
    fire projectile from "player" toward "warden" with texture {$player_shot} speed 8 damage 1

on projectile hit entity "warden":
    broadcast "The Warden was hit for %{_hit_damage}% damage"
    if {entity::warden::health} <= 0:
        delete entity "warden"
```

A projectile chooses a straight trajectory toward the target's position at launch. Its speed is in tiles per second. It is removed on target hit, contact with a solid tile, or expiry, and its damage decreases target health before the `on projectile hit entity` handler runs. Runtime tile edits are available with `place tile {$cover_stone} at (x, y)` and `clear tile at (x, y)`. Both tile-path routing and projectile obstruction use the shared collision resolver. Asset defaults live in the optional `assets/collision.json`; room cells can override them through `tileCollision` (`inherit`, `solid`, or `none`). Room Studio paints these modes. See `features/room-collision-metadata.md`. Advanced rigid-body physics remains future work.

### Radial and fan attack patterns (introduced in v0.9.0; lifecycle fixes in v0.9.1)

```cx
on press key "1":
    fire projectile pattern radial from "warden" toward "player" with texture {$ember_orb} count 8 speed 4 damage 1

on press key "2":
    fire projectile pattern fan from "warden" toward "player" with texture {$ember_orb} count 5 spread 60 speed 4 damage 1
```

`radial` makes 1–64 shots evenly spaced around 360 degrees, starting toward the target and continuing around the circle. `fan` distributes shots evenly across an arc centered on the target direction; `spread` is the full arc width, from 0 to 180 degrees. Both commands resolve the source and target when fired; individual shots travel straight and do not home. Each shot uses the existing target-specific hit behavior.

A pattern is rejected as a whole if it would exceed the global cap of 4096 active projectiles. High-speed projectile movement is swept in small increments so target and solid-tile contacts between tick endpoints can be detected. The limits are protective guards, not a replacement for a reasonable firing cooldown.

This first version arranges travel directions only. Projectile rings/triangles/polygons spawned on their perimeters, rotating/orbiting/expanding patterns, arbitrary target filters, wall-hit bypass settings, and per-hit persistence choices remain future work. See [`features/projectile-patterns.md`](features/projectile-patterns.md) for examples, implementation notes, and the roadmap.

### World viewport sizing

```skript
set engine::world_view_height_tiles to 22.5
set engine::camera_follow to "player"
# To turn off camera following:
set engine::camera_follow to "none"
```

The view's height is a count of room tiles, independent of physical display resolution. With 32-pixel tiles, `22.5` corresponds to a 720-world-unit-tall view. The width is derived from the actual window aspect ratio, so an ultrawide monitor shows more of the sides rather than stretching the scene. Camera following is clamped to the room bounds. A moving camera smoothing option and editor-authored NPC paths are not yet implemented.

## Project creation, folders and file lookup

`main.cx` is a familiar starting filename, not a required entry point. The runtime recursively loads every `.cx` file under `scripts/`, in stable path order. Top-level statements in those files are bootstrap statements; `on load:` is the startup event after bootstrap. There is no special `init.cx` loader. `config.json` is optional and provides baseline settings; script bootstrap assignments override it.

The interactive runtime scaffolds an empty project folder and fills missing standard folders when it recognizes an existing `.cx` project. It never overwrites existing user files. A single root-level `.cx` script is copied into `scripts/` while preserving the original. `cx-cli --check` is deliberately read-only and does not scaffold.

```text
MyGame/
├── cx-engine                    # graphical executable after GUI build
├── cx-cli                       # optional CLI runtime for script tests
├── config.json                  # optional engine defaults
├── scripts/                     # recursively scanned for .cx files, sorted by path
│   ├── main.cx                  # optional conventional starting file
│   ├── menus.cx                 # optional UI definitions and handlers
│   └── systems/                 # nested .cx scripts are found automatically
├── rooms/                       # <name>.room.json, loaded by room commands
├── assets/
│   ├── textures/                # image filenames become {$name} IDs
│   ├── audio/                   # audio filenames become {@name} IDs
│   ├── fonts/                   # optional TTF/OTF/TTC fonts
│   ├── autotiles.json           # optional transparent edge/corner overlay rules
│   └── spritesheet.png          # legacy/sample image; not auto-sliced yet
├── variables.csv                # editable persistent variable source of truth
├── variables.cxv                # compressed fallback cache
├── variables.meta               # committed generation and integrity fingerprints
├── variables.csv.bak            # previous CSV generation (may exist)
├── variables.cxv.bak            # previous cache generation (may exist)
├── tools/room-editor.html       # browser room editor
├── tools/room_editor_server.py # localhost-only editor API
├── plugins/                    # reserved for future native extensions; not loaded yet
├── audio_plugins/              # reserved for future audio extensions; not loaded yet
├── docs.html                    # searchable static-site-ready language reference
├── docs/engine-settings.md      # complete engine setting reference
├── docs/IMPLEMENTATION_ROADMAP.md # current prioritized roadmap
└── docs/WEB_PUBLISHING.md       # static website publishing guide
```

Launch with the project root as argument (or current working directory): `./cx-engine .`. Configuration is read first, then script top-level statements execute in path-sorted file order, and `on load:` handlers fire after scripts parse. Every `.cx` file under `scripts/` is recursively discovered; a syntax error prevents script execution so startup does not run a half-parsed project. Images in `assets/textures/` and audio files in `assets/audio/` are discovered at runtime and exposed as read-only variable-like references. `rooms/` files are validated when loaded. The `variables.csv` is authoritative if present; edit it only with the engine closed. The web room editor uses the project’s `tools/` server and binds to localhost. `assets/autotiles.json` defines terrain members and transparent edge/corner overlays; it is optional, and missing/malformed rules should be corrected before expecting automatic path smoothing. ``plugins/` and `audio_plugins/` are reserved for future extension/host integration and are not dynamically loaded in the current v0.10.6 release.

## Graphics and performance settings

Use optional `config.json` for baseline settings, or put a `set engine::...` bootstrap statement in whichever `.cx` file owns project initialization. Script settings run after config and override it. You do not need to repeat the built-in window size, tick rate or UI reference dimensions in each game; use the defaults unless the game actually needs a change.

```skript
# Optional overrides: only set values this game actually wants to change.
set engine::window_title to "Ancient Arena"
set engine::console_enabled to false
set engine::ambient_light to 0.35
```

A new project already has a responsive window, 60 Hz logic tick, VSync, nearest texture filtering, pixel-perfect light placement, and the standard UI/world viewport defaults. Repeating those values at the top of `main.cx` is unnecessary. Use the complete table in [`engine-settings.md`](engine-settings.md) when a project needs a deliberate override.

- `target_fps`: `0` means no explicit FPS limit.
- `tick_rate`: fixed logic rate, separate from render FPS.
- `texture_filter`: `"nearest"` (crisp pixel art) or `"linear"` (smoothed textures).
- `pixel_perfect`: snaps light positions to whole pixels; grid tile positions already align to the grid.
- `ambient_light`: `0.0` dark to `1.0` fully bright.
- `lighting_quality`: `1` low, `2` medium, `3` high mesh resolution.
- `console_enabled`: controls the in-game terminal only. Parent-terminal startup diagnostics still work.
- `show_debug_overlay`: true shows the engine title/status and debug backdrop; false gives the game a clean blank-void backdrop.

The lighting pass dims the world by the ambient setting and adds colored, softly faded radial/cone light meshes. It is a light vertex-mesh effect rather than a programmable GLSL shader.

## Light sources

```skript
on load:
    enter room "start" at {entrance1::start}
    create light "torch" at (6, 9)
    set light "torch" radius to 220
    set light "torch" intensity to 0.9
    set light "torch" color to (255, 190, 120)
    set light "torch" direction to 0
    set light "torch" beam to 70

on press key "L":
    remove light "torch"
```

Light positions are tile coordinates. Radius is in screen pixels; intensity is `0–1`; color is three RGB integer channels `0–255`; direction is in degrees (`0` points right and positive values turn clockwise); beam angle is `1–360` (`360` is all-around light). Names are case-insensitive. Lights created while a room is active are associated with that room and appear when it is active.

## Mouse input and file attachment

Drag an audio/image/file from the desktop onto a button in an open canvas. On Linux X11/XWayland builds with X11 development support, the native file-drop bridge raises `on file attach:`. The event has `{_attached_file}` (absolute path), `{_mouse_x}`, `{_mouse_y}`, and `{_button_under_cursor}` locals. Use the existing canvas button bounds for targeting:

```skript
on file attach:
    if mouse cursor is over button "sound_import_btn":
        set {buttonsound1::%{project1}%} to {_attached_file}
        broadcast "<#59ccff>Sound attached for profile {project1}."
```

Use `/attach "/path/to/file.wav" 320 245` in the CLI to simulate a drop and test the handler. Native desktop drag-and-drop is currently implemented only on Linux X11/XWayland; the GUI frontend needs an SFML build on the target system.

## Audio file IDs and procedural waves

Drop `.wav`, `.ogg`, `.flac`, `.aiff`, or `.aif` files into `assets/audio/`; their base filename becomes an audio virtual variable with the `@` prefix. For example, `assets/audio/button_click.wav` becomes `{@button_click}`. Texture IDs still use the `$` prefix (such as `{$stone_up}`), so images and sounds do not share a namespace. Duplicate audio basenames are kept addressable by deterministic suffix IDs such as `{@click2}`; renaming duplicates is recommended for clarity. The virtual variables are read-only, but their resolved paths can be assigned to ordinary globals.

```skript
on load:
    create wave "chime" duration 0.35 sample_rate 44100
    add sine wave to "chime" frequency 440 amplitude 0.55 phase 0
    add sine wave to "chime" frequency 660 amplitude 0.22 phase 0.2
    set wave "chime" envelope attack 0.01 decay 0.08 sustain 0.55 release 0.18
    normalize wave "chime"
    save wave "chime" to "assets/audio/chime.wav"
    visualize wave "chime" to "assets/audio/chime.svg"
    play sound "chime"
```

A wave is a named reusable sample buffer. Add sine, square, triangle, saw, or seeded noise layers. Parameters include frequency in Hz, amplitude, phase in radians, and optional noise seed. `sine(hz, seconds[, phase])`, `noise2d(x, y[, seed])`, and `fbm2d(x, y[, seed[, octaves]])` are numeric expressions for procedural synthesis/terrain parameters. `visualize wave` exports an SVG graph; it is not a live audio oscilloscope. Audio generation/export works in the core/CLI, while sound playback requires the optional graphical SFML Audio backend. There is not yet a DAW-grade mixer or VST host.

## Color formatting and fonts

Both terminal logs and canvas text support Minecraft/TAB-style formatting:

```skript
broadcast "<#0099ff>Blue text</#0099ff> and default text"
broadcast "<gradient:#ff6600:#8b5cf6>Orange to purple</gradient>"
broadcast "&#66ff99Green &lbold &oitalic &nunderline &mstrike &rreset"
```

Also supported: `&0`–`&f`, `&l` bold, `&o` italic, `&n` underline, `&m` strikethrough, `&r` reset, `&#RRGGBB`, `<#RRGGBB>... </#RRGGBB>`, and `<gradient:#RRGGBB:#RRGGBB>...</gradient>`. Add a licensed Nerd Font Propo `.ttf`/`.otf` file under `assets/fonts/` to enable Nerd Font glyphs, or set `engine::font_path` explicitly. No font is bundled.

## Debugging and editor workflow

```sh
./build/cx-cli --check .
python3 tools/room_editor_server.py
```

The syntax checker does not run events. Room Studio opens at `http://127.0.0.1:8765/tools/room-editor.html`. It is a project-local web editor served by a localhost-only Python server, not an external website. Press `Ctrl+S` to save and `Ctrl+Z` to undo. Put textures in `assets/textures/`, then use Refresh assets in the editor.

## Optimization notes and known limits

Textures are discovered once per runtime reset, cached when used, and room tile geometry is grouped by texture. Geometry batches rebuild only when the active room's revision changes. The logic accumulator is fixed-rate and caps catch-up work after stalls.

This release remains an early foundation. The basic entity, 8-way spritesheet animation, camera-follow, solid-tile collision/avoidance, scripted NPC paths, directed projectiles, radial/fan direction patterns, and sampled projectile sweeps exist. Robust physics, general collision events, dynamic obstacle avoidance, editor-authored paths, chunk culling, advanced audio routing, programmable shader effects, VST hosting, and broader cross-platform file-drop support remain future stages.
