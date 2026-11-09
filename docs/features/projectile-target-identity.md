# Projectile impact target identity — v0.9.3

## Problem

Projectile movement is updated by collecting collisions first, removing consumed projectiles second, and dispatching damage/event callbacks afterward. Deferring callbacks keeps script code from mutating the `sprites_` map during its collision traversal.

That creates a subtle lifetime boundary. Suppose two projectiles collide with `player` during the same tick. The runtime records both impacts. The first callback deletes `player` and creates a new `player` entity under the same script-visible name. A check that only calls `sprites_.find("player")` sees the replacement and can mistakenly apply the second impact to it, even though collision sampling detected that second impact against the old object.

## Fix

`SpriteObject::runtimeInstanceId` distinguishes runtime object instances from their visible names. `Runtime::spriteInstanceCounter_` assigns a monotonically increasing ID when a script creates/replaces a sprite or the engine creates a projectile. A successfully staged script reload commits the new counter along with the new sprite set; a fresh runtime starts its own sequence.

Each deferred `HitCandidate` captures `targetInstanceId` at detection time. During dispatch, the runtime now requires all of the following:

1. An object with the expected target name still exists.
2. That object is still an entity.
3. Its health is still positive.
4. Its runtime instance ID matches the instance hit during collision detection.

If an earlier callback removed or replaced the original target, the queued impact is skipped. It does not mutate the replacement's health and does not fire a misleading hit callback.

## Compatibility

This is internal lifecycle metadata; it adds no new CX syntax and does not change how scripts address sprites/entities by name. The ID should not be saved in `variables.csv`, room JSON, or script APIs: it identifies an object in one runtime instance, not a persistent game-world identity.

## Regression test

`testQueuedHitsDoNotTransferToReplacementEntityWithSameName` fires two shots that reach a target during the same tick. The first hit handler deletes and recreates the target with the same name and restores its health. The expected result is one hit callback only, with the replacement retaining its full restored health. A second regression removes and recreates the target while a shot is still in flight; the shot must disappear without hitting the replacement.

The test was first executed against the v0.9.2 implementation and failed: both queued impacts were delivered by matching the target name alone. After the instance ID was recorded and checked, the same test passed.

## In-flight target policy

The same rule applies from launch onward. A projectile records `projectileTargetInstanceId` when it is created. If the target name no longer resolves to that exact live entity instance on a later update, the projectile is removed without hit damage or a runtime error. It will not transfer to a replacement that reuses the same name. This also releases its active-projectile capacity rather than allowing a now-useless single-target shot to linger until its lifetime expires.

This default is intentionally simple for the first target-specific projectile API. A future extension can make target-loss behavior configurable (for example, persist as a visual, reacquire by name, or retarget another enemy), but any such choice should be an explicit script-facing policy rather than accidental name reuse.
