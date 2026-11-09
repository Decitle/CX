# Named sprite animation clips — v0.10.7

## Purpose and scope

The earlier entity animation support was a fixed eight-direction walk cycle: while an entity moved, its frame advanced every seven simulation ticks; while idle, it returned to the first frame of its facing row. That remains the fallback for existing projects.

v0.10.7 adds named clips for authored actions such as waking, stumbling, kneeling, standing, or playing a looping walk cycle. Clip playback is simulation-tick driven, does not depend on render rate, and can emit a one-shot completion event. It is not a blending system, animation graph, timeline editor, or physics/movement controller.

## Script contract

Configure the sheet before defining clips. Frame indices and rows are zero-based. The frame range is inclusive.

```cx
create entity "player" with texture {$player}
set entity "player" animation sheet to (32, 32) columns 6 rows 8

define animation "wake" for entity "player" frames 0 to 3 row 0 at 8 fps once
define animation "walk" for entity "player" frames 0 to 5 row facing at 10 fps looping

on load:
    play animation "wake" for entity "player"

on animation complete "wake" for entity "player":
    play animation "walk" for entity "player"

on press key "K":
    play animation "wake" for entity "player"

on press key "S":
    stop animation for entity "player"
```

The event header is `on animation complete "clip_name" for entity "entity_name":`. During that handler these local values are available:

- `{_animation_entity}` — entity name.
- `{_animation_name}` — completed clip name.
- `{_animation_frame}` — final frame index.
- `{_animation_row}` — resolved final row index.

## Definition rules

`define animation "name" for entity "id" frames FIRST to LAST row ROW at FPS fps once|looping`

- The entity must already exist and have a configured animation sheet.
- `FIRST` and `LAST` are zero-based and inclusive; `LAST` must be greater than `FIRST` and inside the configured column count.
- `ROW` is a zero-based row index inside the configured sheet, or `facing`.
- `facing` follows the entity's current eight-way facing row and therefore requires at least eight sheet rows. Direction order remains down, down-left, left, up-left, up, up-right, right, down-right.
- `FPS` is finite and from 0.1 through the current simulation tick rate. Higher frame rates cannot be represented by a lower-frequency logic tick and are rejected rather than silently skipping frames.
- `once` plays through the inclusive range, holds the final frame for its complete frame interval, then stops and queues one completion event.
- `looping` wraps from the last frame to the first and never emits a completion event.

A definition is validated completely before it is installed. A malformed definition leaves any previously installed clip unchanged. Redefining the currently selected clip stops that playback and clears its selection so stale completion cannot fire.

## Playback rules

`play animation "name" for entity "id"` restarts the selected clip from its first frame. Playing a missing clip reports a runtime error and does not replace the current animation. `stop animation for entity "id"` cancels the selected clip, clears its selection, and returns frame selection to the legacy movement animation.

A selected one-shot holds its final frame after completion. It is not automatically replaced by the walk cycle when the entity moves; the script should explicitly choose the next clip. A selected looping clip keeps running while the entity moves or stands still. The entity's movement, pathing, tweens and facing are independent; animation playback does not move the entity.

If a new animation sheet is assigned, previous named clips are cleared because their frame coordinates may no longer be valid. Configure the sheet first, then define its clips.

## Timing model

The update runs once per `Runtime::tick`. Clip advancement uses the current logic tick rate and the defined FPS. The graphics renderer only reads the current frame/row; it does not advance animation. The cadence is therefore stable against render-rate changes and render stalls. It is quantized to simulation ticks; a fractional ticks-per-frame duration is accumulated deterministically.

## Completion and lifecycle safety

Finished clips are gathered while the sprite map is being iterated and dispatched afterward. Each pending completion captures:

- the script-visible entity name;
- the monotonic runtime instance ID;
- a playback-generation number;
- the active room-context revision;
- the final frame and row.

Before each matching handler, runtime verifies that the same entity instance still exists, the room context is unchanged, and the same playback generation remains selected and stopped. If a callback deletes/recreates the entity, starts or stops another clip, redefines the selected clip, or changes rooms, later callbacks from the old completion are discarded. Completion handlers are delivered once and in script declaration order.

## Boundaries

- No cross-fade, blending, layered animation tracks, animation graph, queued transition, or automatic state selection.
- No animation JSON importer or Room Studio animation editor.
- No sprite-origin/pivot editor, per-frame hitbox, event markers embedded in frames, or animation retargeting.
- The existing eight-way movement animation remains the fallback when no named clip is selected.
- The graphical runtime must still be visually checked on the target Chromebook; automated tests exercise simulation state, not pixel-perfect atlas presentation.

## Acceptance coverage

`cx-animation-tests` verifies clip start, frame interval timing, inclusive final-frame hold, one-shot completion payload, looping wrap, facing-row resolution, callback entity replacement safety, invalid range rejection, and unknown-clip diagnostics. Existing movement, collision, projectile, trigger, tween, task, persistence and room-editor tests remain separate regression targets.
