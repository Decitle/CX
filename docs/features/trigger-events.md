# Trigger enter/exit events — v0.10.3

CX v0.10.3 adds named, pair-specific trigger lifecycle events on top of the v0.10.2 collision channels. This is an overlap/event layer, not a general physics engine.

## Script syntax

```cx
create entity "player" with texture {$player}
create entity "door_area" with texture {$invisible_area}

set entity "door_area" collision layer to "triggers"
set entity "door_area" collision mask to "actors"
set entity "player" collision mask to "triggers, props, hazards"

on entity "player" enter trigger "door_area":
    set {near_door} to true
    set {last_actor} to {_trigger_actor}
    set {last_trigger} to {_trigger_entity}

on entity "player" exit trigger "door_area":
    set {near_door} to false
```

Both event headers name the actor and trigger entity. Names are normalized case-insensitively, just like the existing entity APIs. The runtime dispatches only these authored pairs rather than enumerating every possible actor/trigger combination each tick. Event-local values available to either handler are:

- `{_trigger_actor}` — the actor's script-visible entity name.
- `{_trigger_entity}` — the trigger area's script-visible entity name.

Each handler invocation receives a fresh local-variable scope. `wait` may suspend a handler under the normal scheduler rules; a later room-context change invalidates that suspended continuation in the same way as other scheduled script work.

## Which overlaps qualify

A qualifying pair must satisfy all of these conditions:

1. The actor is a non-projectile `create entity` object with the `actors` collision layer and without the `triggers` layer.
2. The trigger is a different non-projectile `create entity` object with the `triggers` collision layer.
3. Both objects have collision enabled.
4. Their masks mutually accept at least one of the other object's layers.
5. Their current AABBs overlap. The configured hitbox size/offset is used when present; otherwise the sprite dimensions are used.

Visibility is independent of collision, so an invisible trigger entity can be used as a sensor volume. The trigger entity can also carry additional channels, but any entity carrying `triggers` is treated as a trigger endpoint rather than an actor endpoint for this event API.

## Lifecycle and ordering

- The runtime samples trigger overlaps once per logic tick, after the tick handler and entity/projectile updates.
- The first qualifying overlap produces one enter event. Continuous overlap does not repeat it.
- Separation, deletion, collision disablement, a channel/mask change that blocks the pair, or a hitbox/position change that removes overlap produces one exit event on the next trigger update.
- Re-entry after an exit produces a new enter.
- Contacts are keyed by monotonic runtime instance IDs. Deleting and recreating an entity under the same name therefore closes the old contact and opens a new one; old exits are processed before new enters in that update.
- Contacts are runtime-only and are not saved. A successful active-room transition/reload/unload or successful script reload clears the prior contact set without emitting exits across the scene boundary. If the same named pair still overlaps on the next sample, it is a fresh enter in the new context.
- If a trigger handler changes the room context, remaining transitions gathered from the old context are discarded.

## Deliberate boundaries

- This is endpoint overlap sampling, not swept trigger collision. An actor that completely crosses a very thin trigger between two samples can be missed; use a sufficiently wide trigger volume or smaller fixed-step movement until a swept-entity query exists.
- The first version dispatches only explicitly named actor/trigger pairs. There are no wildcard selectors, general `on collision` events, trigger enter/exit for projectiles, or automatic team/faction rules. This is also a bounded-work choice: the runtime walks the authored pairs rather than scanning every actor against every trigger on each tick. A future wildcard form needs an indexed or otherwise bounded dispatch design before it can be added safely.
- Trigger contacts do not move or block actors. Solid room tiles remain a separate grid system.
- There is no radius-based proximity query yet. A trigger volume is currently the reusable proximity mechanism.
- This does not add rigid-body response, swept actor movement, spatial broadphase, per-tile collision metadata, or Room Studio collision authoring.

The event locals are also listed in the [event context reference](../event-context.md). The implementation lives in `Runtime::updateTriggerEvents` and uses the shared `spritesOverlap` filter. Regression coverage is included in `tests/collision_runtime_tests.cpp` under the existing `cx-collision-tests` target. It covers stable overlap, separation/re-entry, same-name replacement, mutual-mask changes, callback-driven deletion, and room-context reset.
