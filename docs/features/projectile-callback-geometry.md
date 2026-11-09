# Queued projectile contacts after callback-driven geometry changes — v0.9.8

## Problem

Projectile movement and collision candidates are gathered while `sprites_` is traversed. Hit callbacks intentionally run afterward so callbacks can safely mutate the sprite map. This means a first callback can change the geometry or collision policy of a target before another same-update impact, which was already queued from the old geometry, is dispatched.

## v0.9.8 contract

Each queued target hit now stores the projectile's collision AABB at the exact sampled contact position. Immediately before resolving the queued impact, the runtime still validates the target name, entity type, health, and runtime instance ID. It additionally requires that collision is currently enabled and that the target's current collision AABB still overlaps the stored projectile contact AABB.

The consequences are deliberate:

- If an earlier callback moves the target fully away from the sampled contact, a later queued impact against that target is discarded.
- If the callback moves the target's hitbox offset away from the sampled contact, the later impact is discarded even if the sprite itself did not move.
- If the callback disables target collision, later queued impacts are discarded.
- If a callback makes a small movement but the current hitbox still overlaps the stored contact rectangle, the queued impact remains valid.
- The callback that has already caused the geometry/policy change completes normally. Its mutation remains committed.
- The check uses the exact sampled projectile contact rectangle, not the target's original full position and not a recomputed path.

## What this does not do

Collision detection is still gathered before callbacks run. If a callback moves an entity into a projectile trajectory that did not overlap during the detection pass, the engine does not retroactively create a new hit. It also does not rewind/resweep projectiles, convert sampled collision to analytical continuous collision, alter wall-versus-target precedence, or change target-instance/room-context cancellation. These are separate contracts.

The impact AABB is captured while the projectile is positioned at the sampled step that first overlapped its then-current target. The candidate is only kept if that sample still overlaps the target's current collision AABB at dispatch time. This limits the new work to one inexpensive rectangle-overlap check per queued target hit; the inner sweep remains unchanged.

## Regression tests

`tests/projectile_runtime_tests.cpp` now covers:

- a callback moving the target far away, suppressing a later stale queued impact;
- a callback moving the target slightly while it still overlaps the sampled contact, preserving the later impact;
- a callback disabling collision, suppressing subsequent queued impacts;
- a callback moving the target hitbox offset away while leaving the sprite position unchanged, suppressing the later impact.

The existing tests for defeated/deleted targets, runtime target-instance identity, room transitions/reload/unload, sampled impact ordering, wall precedence, projectile lifetime, and hidden-but-collidable entities continue to define their own boundaries.
