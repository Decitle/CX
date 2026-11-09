# Projectile attack patterns (introduced in v0.9.0; impact timing refined in v0.9.2)

The v0.9.0 release introduced a small, bounded first version of projectile attack patterns. The v0.9.1 release clarifies collision/visibility separation, resolves queued hits one at a time, and prevents generated projectile IDs from colliding with user-authored sprite names. v0.9.2 orders same-tick impacts by their sampled contact time and checks the final movement segment before lifetime expiry. The goal is to make the common boss-fight shapes easy to author in `.cx` without writing C++, while keeping the geometry math independent and unit-tested.

## What the first version supports

- **Radial burst:** create a set of projectiles whose directions are evenly spaced around a full circle. The first projectile points toward the named target; subsequent directions rotate around the circle.
- **Fan burst:** create a set of projectiles spread evenly across an arc centered on the direction toward the named target.
- **Normal projectile rules:** each projectile uses the existing texture, speed, damage, lifetime, solid-tile obstruction, and target-specific hit callback behavior. Generated internal names skip IDs already used by project sprites, so the active-projectile safety count remains aligned with live shots.
- **Swept movement collision:** the runtime samples the path between tick endpoints so a fast projectile is less likely to skip over a small target or thin solid tile. This is sampled collision, not a continuous solver.
- **Collision is independent of visibility:** hiding a target controls drawing but does not turn off hitboxes or projectile hits.
- **Ordered hit resolution:** collision candidates are collected before callbacks, then damage is applied one hit at a time in sampled time-of-impact order. Each callback sees health after its own hit; later queued hits are skipped if an earlier callback removes the target or its health reaches zero. Equal sampled impact times retain deterministic traversal order.
- **Lifetime boundary:** a projectile advances only through the time remaining before its lifetime limit, but the final partial tick is still collision-tested before removal. A target reached during that segment can still be hit.
- **Safety bounds:** a single pattern can request 1–64 projectiles; at most 4096 projectiles may be active simultaneously. A volley that would cross the global cap is rejected before any of its projectiles are created.

The current system describes **travel directions**, not a visible arrangement of projectiles on a shape perimeter. A radial burst with `count 3` makes three trajectories evenly spaced by 120 degrees (a triangular direction pattern); `count 4` makes four directions at 90-degree intervals. These are not orbiting bullets or projectiles spawned at the vertices of a triangle.

## Syntax

### Radial burst

```cx
on press key "1":
    fire projectile pattern radial from "warden" toward "player" with texture {$ember_orb} count 8 speed 4 damage 1
```

`count 8` produces eight shots with 45-degree spacing. One direction is aligned toward the player's position at the time the command runs. If the target moves afterward, the projectiles do not turn to follow it.

### Fan burst

```cx
on press key "2":
    fire projectile pattern fan from "warden" toward "player" with texture {$ember_orb} count 5 spread 60 speed 4 damage 1
```

Five shots across a 60-degree fan have angles of -30, -15, 0, +15 and +30 degrees relative to the aim direction. The engine uses screen-style world coordinates where positive Y points down, so positive angles are visually clockwise.

A one-shot fan (`count 1`) points directly toward the target. `spread` must be from 0 through 180 degrees. Radial patterns don't take a `spread` field because they always use a full turn.

### Keep the API readable

A pattern command names its source and intended target, texture, count and speed/damage explicitly. The source entity and target entity must exist before the statement runs, and the texture must be present in `assets/textures/`.

- `speed` is measured in **tiles per second**, matching the original single-projectile command.
- The target's position is read once when the pattern is spawned to determine the burst's orientation.
- Projectiles still only damage the named target. This version does not yet support collision against arbitrary enemies, allies, destructible props or multiple target categories.
- Projectiles still disappear on target hit, contact with a solid tile, or lifetime expiry. Separate wall-hit and target-hit persistence settings are not yet exposed by the script language.

## Suggested first boss pattern

```cx
# Repeated presses make a slow, readable pattern while you test hitboxes.
on press key "1":
    fire projectile pattern radial from "warden" toward "player" with texture {$ember_orb} count 8 speed 3 damage 1

# A warning-like fan uses fewer shots and a narrower spread.
on press key "2":
    fire projectile pattern fan from "warden" toward "player" with texture {$ember_orb} count 5 spread 50 speed 4 damage 1
```

Avoid placing a large `fire projectile pattern` command directly in `on tick:` without a cooldown. The 4096-projectile safety cap prevents unbounded growth, but it is a last-resort guard—not a firing-rate controller. For now, use a periodic handler, a key/button event, or a tick-time cooldown variable.

## Implementation model

The pattern angle builder lives in `include/cx/attack_patterns.hpp` and `src/attack_patterns.cpp`. It accepts a kind, count, aim angle and spread, then returns a bounded vector of angles in radians. Pure geometry tests are kept in `tests/attack_pattern_tests.cpp`. The parser/runtime boundary and projectile integration tests are in `src/parser.cpp`, `src/runtime_projectiles.cpp`, and `tests/projectile_runtime_tests.cpp`.

The runtime converts each angle into a velocity vector and makes regular projectile objects. If a requested volley does not fit under the global live-projectile cap, the whole volley is rejected without partial spawning. A cap warning is de-duplicated until the rejected volley could fit, so a repeated `on tick:` command will not append an identical warning every frame while capacity remains insufficient. The update loop uses short path samples for solid-tile and named-target checks, defers deletion and hit-handler dispatch until after iterating the sprite map, and avoids a string/map lookup for each sweep sample. Damage and hit callbacks then resolve sequentially in sampled time-of-impact order; equal-time hits retain deterministic traversal order. Visibility controls drawing only; collision overlap is governed by collision enablement and geometry. The global live-projectile counter is updated when shots are spawned, expire, hit, or are explicitly deleted.

## Not implemented yet

The following ideas remain on the roadmap and should not be implied by the v0.9.2 API:

- perimeter spawning for triangles, polygons, lines, spirals and rings;
- trajectory objects that rotate, orbit, expand, contract or move in formations over time;
- patterns attached to a moving boss and released later;
- texture rotation/edge effects controlled by pattern definitions;
- wall-hitbox bypass settings exposed in `.cx` syntax;
- independent persistence choices for target hits and wall hits;
- collision masks and general projectile-vs-entity event dispatch;
- a visual attack-pattern editor and timeline preview.

Add these only after collision categories, entity lifecycle, hit events, and cancellation semantics have stable contracts. Future patterns should extend the pure pattern module with deterministic geometry tests rather than growing one oversized runtime function.


## Lifecycle note added in v0.9.3

Each sprite/entity gets an internal runtime instance ID. Queued projectile impacts use that ID to ensure a hit detected against an old entity cannot damage a newly created replacement that reused the old name from inside an earlier hit callback. A projectile also captures its target instance when fired; if that target disappears or is replaced under the same name before the projectile arrives, the shot is removed on the next update and does not damage the replacement. A future API may expose configurable target-loss behavior, but current behavior is deliberately deterministic and simple.


## Deferred impact lifecycle (v0.9.5)

If a projectile-hit handler changes the active room context, the current handler completes and remaining queued impacts from the old context are discarded. This avoids running old-scene callbacks after the scene has changed. It does not clear all in-flight projectiles across later ticks. See `projectile-room-context.md`.

## Wall-versus-target collision precedence (v0.9.4)

The sampled path is checked in travel order. At each sample, solid tiles are checked before the named target; thus the earliest sampled contact wins, and the wall wins if both overlap at the same sample. Two dedicated runtime regressions pin down target-before-wall and same-sample wall occlusion. See `projectile-collision-precedence.md`. The underlying sweep remains approximate sampled collision, not exact continuous collision detection.
