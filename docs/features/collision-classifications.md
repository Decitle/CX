# Entity classifications and collision channels — v0.10.2

This release adds reusable entity metadata and collision filtering. It does **not** add physics response, trigger enter/exit events, multi-target projectiles, or collision shapes other than the existing AABBs.

## Classifications

Every entity has a `kind` and a `team`, both plain lowercase identifiers. Every entity can also own up to 32 tags.

```cx
create entity "warden" with texture {$boss}
set entity "warden" kind to "boss"
set entity "warden" team to "hostile"
set entity "warden" tags to "boss, flying, armored, phase_two"

if entity "warden" kind is "boss" and entity "warden" team is "hostile":
    set {hostile_boss_present} to true

if entity "warden" has tag "flying":
    set {warden_can_fly} to true
```

`kind` describes what an object is; `team` describes affiliation; tags describe additional capabilities or traits. These labels do not automatically change gameplay. Scripts can query them, and future selectors/events can build on the same metadata. Use identifiers containing 1–40 letters, digits, underscores or hyphens. Assigning `tags` replaces the set; an empty string clears it. Duplicate tags are ignored.

## Collision channels

Each entity has a layer bitset and a mask bitset. A pair is collision-compatible only when **both sides opt in**:

`(A.mask & B.layer) != 0` and `(B.mask & A.layer) != 0`.

The built-in channels are:

- `actors`
- `projectiles`
- `props`
- `triggers`
- `effects`
- `pickups`
- `hazards`
- `sensors`

An entity may belong to multiple channels. Masks may include multiple channels or use `all`/`none`.

```cx
create entity "player" with texture {$player}
create entity "crate" with texture {$crate}
set entity "player" collision layer to "actors"
set entity "player" collision mask to "props, pickups, hazards, triggers"
set entity "crate" collision layer to "props"
set entity "crate" collision mask to "actors"
```

Both entities must be collision-enabled and their hitboxes must overlap for `entity "a" overlaps entity "b"` to return true. Visibility is still independent from collision. The collision-channel filter affects entity overlap queries and projectile-to-target hits. Existing solid-tile collision is a separate room-grid system and is not controlled by these entity channels.

### Projectile masks and teams

New projectiles use the `projectiles` collision layer, inherit their firing entity's `team`, and copy the firing entity's `projectile collision mask` setting. The target's own collision mask must also include `projectiles`.

```cx
set entity "warden" team to "hostile"
set entity "warden" collision projectile mask to "actors, props"
set entity "player" collision mask to "actors, projectiles, pickups, hazards"
```

The projectile mask is configured on the firing entity because generated projectile IDs are runtime-owned. Setting `collision projectile mask` to `none` creates projectiles that do not hit entities; their solid-tile behavior remains unchanged. This is a filter, not a team-damage rule: scripts should still compare `_hit` data or the target's `team` when deciding friendly fire, immunity or damage type.

## Defaults and compatibility

- `create entity` defaults to kind `entity`, team `neutral`, layer `actors`, mask `all`, and projectile mask `all`.
- `create sprite` defaults to kind `sprite`, team `neutral`, layer `props`, and mask `all`.
- Projectiles default to kind `projectile`, layer `projectiles`, inherit the owner's team and projectile mask, and preserve the existing named-target launch behavior.
- Since masks default to `all`, existing scripts retain their prior overlap and projectile-hit behavior unless they opt into filtering.

## Deliberate boundaries

- `kind`, `team` and tags are metadata, not a full component registry or automatic damage system.
- Collision channels do not create trigger enter/exit events yet.
- Projectile shots still target one named entity instance per launch; this release does not add tag/team target selection or multiple-target acquisition.
- The hitbox remains an axis-aligned rectangle. There is no rigid-body response, impulse solver, continuous collision solver or spatial broadphase in this change.
- Room tiles retain their separate solid-tile collision behavior.

Kind/team/tag strings are interned once per runtime. Instances hold 32-bit IDs and a compact tag-ID vector rather than repeating strings or tree nodes. `sizeof(SpriteObject)` is 440 bytes in this build, versus 520 bytes for the first string/set implementation; this is an object-size comparison, not a whole-engine performance benchmark.

The C++ regression target `cx-collision-tests` covers classification queries, multi-channel assignments, default compatibility, mutual masks, outgoing-projectile masks, and target-side filtering.
