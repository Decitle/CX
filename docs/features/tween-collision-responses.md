# Tween collision responses · v0.10.10

CX v0.10.10 adds an explicit collision policy to deterministic entity position tweens. This is a deliberately small movement contract built on the existing static room-tile collision metadata and entity collision channels. It is not a general rigid-body or dynamic-physics system.

## Script properties

```cx
set entity "crate" blocks movement to true
set entity "player" tween collision response to "stop"

# Alternative responses: "ignore" and "slide".
set entity "player" tween collision response to "slide"
```

`blocks movement` is an opt-in property on an ordinary entity. Its default is `false`, so existing entities, triggers, interaction-focus targets and decorative objects remain non-blocking. A blocker must also have collision enabled. `blocks movement false` turns off its role as a movement obstacle without changing trigger or focus behavior.

The mover's collision-enabled state must be true for any response to be applied. Entity blockers must be ordinary non-projectile entities, must have `blocks movement true`, and must pass the existing **mutual** collision-channel filter: the mover's mask accepts the blocker's layer and the blocker's mask accepts the mover's layer. An entity's visibility does not affect movement blocking.

Static obstacles come from the active room's resolved solid-tile grid, including `assets/collision.json` defaults and per-cell room overrides. The active room's outside edge is also treated as solid. These checks use the same effective sprite/hitbox AABB geometry as the runtime's entity collision helpers.

## Responses

| Response | Behavior |
| --- | --- |
| `ignore` | Default. Preserves the pre-v0.10.10 behavior: the tween moves through solid tiles and entities. |
| `stop` | Sweeps the full segment from the previous tick position to the next tween position. If it encounters a solid tile, room boundary or eligible entity blocker, the tween is canceled silently and the entity remains at its previous safe tick position. No tween-complete event is emitted. |
| `slide` | Tests horizontal travel first, then vertical travel. A blocked axis stays at its prior coordinate while a free axis advances. The tween's time curve continues; when its duration ends it emits the normal completion event, with `_tween_x` and `_tween_y` set to the actual final position rather than the requested destination. |

AABBs that only touch at an edge are not considered penetrating. Swept checks detect a thin blocker crossed between tick endpoints. If an object starts already overlapping a blocker, it may make incremental progress along the shortest deterministic separation direction over multiple ticks; movement into the blocker or through it to escape on the opposite side is blocked. The slide order is intentionally deterministic (X, then Y); it is not a contact-normal solver and may feel different from a physics engine on diagonal corners.

## Update timing and lifecycle

Movement-blocking entity bounds are captured once at the start of each tween-update tick. All tweens in that update observe the same snapshot, avoiding lexical entity-name update-order bias. A blocker that is itself tweening therefore obstructs against its start-of-tick position for that update; continuous moving-vs-moving time-of-impact resolution is not implemented.

A `stop` response cancels at the last safe tick position, not at a mathematically exact point of contact. It is silent and does not run `on tween complete`. A `slide` response keeps advancing its timer even when one or both axes are blocked; its completion locals report the actual position. The existing completion callback identity/room-context checks remain in effect.

Triggers and interaction focus remain separate systems. A trigger or focused target does not block movement merely because it has a collision layer, an AABB or an event handler. Authors may deliberately set `blocks movement true` on such an entity; its trigger/focus events continue to work alongside its physical blocking role.

## Scope and limits

- This policy applies to deterministic entity tweens only. It does not change `move entity`, directional input, waypoint paths, projectile response, trigger sampling or plain sprite movement.
- Existing `avoids solid tiles` path/movement behavior remains unchanged and is not upgraded into entity-aware navigation.
- There are no impulses, mass, friction, one-way platforms, slope normals, moving-obstacle trajectory prediction, push chains, automatic separation or physics timestep integration.
- The default is deliberately `ignore` so older scripts keep their existing motion behavior until they opt in.

See `../syntax.md`, `examples/tween_collision.cx`, and the `cx-motion-tests` / `cx-collision-tests` regression targets for concrete usage and tested boundaries.
