# Entity interaction focus — v0.10.8

## Purpose

Interaction focus selects one nearby interactable entity for each configured actor. It separates **what the player is currently pointing toward for interaction** from physical collision, proximity tests, and the action bound to a key. The engine does not invent prompts, choose an interaction key, or execute a generic "use" action; the game script owns those decisions and all player-facing text.

## Script contract

```cx
create entity "player" with texture {$hero}
create entity "ancient_chest" with texture {$chest}

set entity "player" interaction range to 96
set entity "ancient_chest" interactable to true

on entity "player" focus enter entity "ancient_chest":
    set {near_chest} to true

on entity "player" focus exit entity "ancient_chest":
    set {near_chest} to false

on press key "E":
    if entity "player" is focused on entity "ancient_chest":
        broadcast "The chest opens."
        set entity "ancient_chest" interactable to false
```

`set entity "actor" interaction range to N` configures an actor's radius in world pixels. The value is evaluated at assignment time and must be finite and between 0 and 1,000,000 inclusive. A value of zero disables focus selection for that actor.

`set entity "target" interactable to true|false` opts a target into or out of selection. Targets default to non-interactable. These properties apply to ordinary gameplay entities, not plain sprites or projectiles.

The event headers are:

```cx
on entity "player" focus enter entity "ancient_chest":
    set {focus_started} to true

on entity "player" focus exit entity "ancient_chest":
    set {focus_started} to false
```

Event-local values are `{_focus_actor}`, `{_focus_entity}`, and `{_focused_entity}`. They contain the actor and target names for that specific transition; `{_focused_entity}` is a compatibility alias for `{_focus_entity}`. See the [event context reference](../event-context.md).

The condition `entity "player" is focused on entity "ancient_chest"` is true only while that exact target instance is currently the actor's selected eligible target. Use it in a keypress handler to decide whether to perform an action. The condition returns false if either entity is missing/replaced or if the current range, center distance, interactable flag, or channel masks no longer permit the relationship—even if the next tick has not yet dispatched its exit event.

## Selection rules

- Each actor can have at most one focused target. Any ordinary entity with a positive interaction range can act as an actor; no special actor layer is required.
- A target must be an ordinary entity with `interactable` set to true. An entity cannot focus itself.
- The range is center-to-center in world pixels, measured using the same effective hitbox-center helper as entity proximity conditions: configured hitbox bounds and offset when present, otherwise the sprite rectangle.
- The radius is inclusive. A target exactly on the range boundary is eligible.
- If several targets are eligible, the smallest squared distance wins. Exactly equal distances are resolved by ascending normalized entity name, making ties deterministic.
- Both entities' collision-channel layer/mask filters must accept the other entity. This reuses the v0.10.2 channel policy.
- Visibility and `collision` enabled/disabled state do **not** affect focus. Focus is an authored interaction relationship, not physical collision or visibility.
- There is no line-of-sight, wall occlusion, pathfinding, priority/tag weighting, or automatic interaction prompt in this release.

## Update and event timing

Focus is reconciled once per `Runtime::tick`, after the normal tick handlers, entity movement/tweens, and trigger-contact updates. Therefore `on tick:` observes the last reconciled focus state before the current tick's entity update. `focus enter` and `focus exit` events are dispatched during the reconciliation phase after entity updates. Prefer the named focus events for immediate enter/exit reactions and use the focused-on condition inside direct input handlers to gate actions.

When focus changes from one target to another, the old target's exit event is dispatched before the new target's enter event. Stable focus does not repeat events. Room-context changes clear focus without emitting cross-room exit callbacks, matching trigger-contact teardown.

## Callback and identity safety

Relationships are keyed to monotonic runtime instance IDs, not script-visible names. Before each focus-enter handler after the first, the event dispatcher revalidates the actor/target identities, room revision, and the actor's currently selected eligible candidate. If an earlier callback moves an object, changes range or masks, disables the target, or introduces a nearer candidate, later handlers for that stale enter are discarded.

Focus-exit is a historical transition. If the original target was deleted before dispatch, the exit may still be reported by name while that target remains absent. If a same-name replacement already exists, the stale exit is suppressed rather than handing the replacement to old-event handlers. If the original target is still live when exit dispatch begins, callbacks may run, but deletion/replacement during one handler stops later handlers. Actor replacement and room-context changes also invalidate stale handlers.

The public focused-on condition re-runs current eligibility against live runtime state. Thus a script that changes range, position, hitbox, interactable state or channel masks can immediately cause the condition to return false; the corresponding exit callback is reconciled at the next focus-update phase.

## Boundaries and future work

- No automatic key binding, prompt text, generic activation callback, or inventory/dialogue policy.
- No line-of-sight, obstruction ray, target priority, tags/team preference, focus cone, input-device hint or hysteresis/dwell time.
- No group query or spatial index; the current implementation checks the live entity set per configured actor once per simulation tick. Avoid configuring large numbers of actors in very large crowds without profiling.
- No general collision response or moving-prop physics is added.
- A future richer policy should be designed only after profiling real scenes and should keep deterministic ordering, lifecycle identity and a simple beginner-facing API.

## Acceptance coverage

`cx-interaction-focus-tests` covers exact-range inclusion, nearest selection, lexical tie resolution, mask filtering in both directions, shared hitbox-center geometry, independence from visibility/collision-enabled flags, independent actors, exit-before-enter order, target and actor same-name replacement, replacement before/during exit dispatch, deleted-target exit reporting, enter-eligibility mutation, immediate query revalidation, invalid-range rejection, and reacquisition after a target becomes eligible again.
