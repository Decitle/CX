# Projectile hit ordering and visibility semantics — v0.9.1

## Why this change exists

Two related behaviors in the first projectile implementation were surprising and could make game logic unreliable:

1. The shared AABB overlap helper treated `visible = false` as if collision were disabled. That mixed a render setting with gameplay collision. A hidden actor may still be intended to receive attacks, act as a trigger, or remain present during a fade/animation.
2. The runtime detected every projectile impact in a tick and applied all damage before it ran any hit callbacks. With multiple projectiles crossing one target during the same tick, the first callback could already see the final health after every shot. If it deleted the target, later queued callbacks could still run against the now-missing entity and repeat a death effect or produce a stale-delete diagnostic.

## v0.9.1 behavior

- `visible` affects drawing; it does not by itself disable overlap or projectile hit detection. Collision checks still require collision to be enabled on the two objects.
- During projectile map traversal, the runtime records the target, projectile ID, and damage, and marks the projectile for removal. It does not run script callbacks while traversing the sprite map.
- After traversal, removed projectiles are erased and live-projectile accounting is updated.
- The queued hit events then resolve in stable projectile-name/map order. Before each hit is applied, the target must still exist as an entity and have health above zero.
- Each accepted hit subtracts damage, updates `entity::name::health` and `entity::name::max_health`, and immediately runs `on projectile hit entity "name":`. The callback sees the health after that specific hit. Its event-local values are {_hit_entity} (target name), {_hit_projectile} (projectile ID), and {_hit_damage} (damage applied).
- If a callback deletes the target or an earlier hit reduces its health to zero, later queued impacts are skipped. The projectiles that had already collided are still consumed; they do not later fly through the scene.

## Projectile-hit event context

    on projectile hit entity "warden":
        set {last_hit_target} to {_hit_entity}
        set {last_hit_projectile} to {_hit_projectile}
        set {last_hit_damage} to {_hit_damage}
        set {warden_health_after_hit} to {entity::warden::health}

These locals belong to that single hit-handler invocation. The health read reflects the current hit after its damage has been applied. The same names and meanings are listed in the [event context reference](../event-context.md).

## Generated projectile IDs and live-count invariant

The engine uses names such as `projectile_1` for generated shots, but sprite names are user-controlled. If a user sprite already occupied the next generated name, the old insertion attempt could be silently ignored while the active count still increased. The generator now advances past names already present in the sprite map and increments the count only if insertion succeeds. This keeps the safety cap aligned with actual live projectile objects. `testGeneratedProjectileNamesAvoidUserSpritesAndKeepCountAccurate` covers the occupied first ID, deletion, and the next spawn.

## Example consequence

For a target with 2 health and three simultaneous one-damage shots:

1. The first callback sees health 1.
2. The second callback sees health 0 and can run the defeat logic.
3. The third impact is discarded from the event queue because the target is now dead or has been removed; it cannot repeat the defeat callback.

This avoids death-event duplication while preserving one callback for each accepted nonlethal hit. It also makes the callback's health value correspond to the individual hit rather than the final sum of all hits in the tick.

## Validation

`tests/projectile_runtime_tests.cpp` covers an invisible target that still receives a hit and the three-shot/two-health ordering case. The latter checks the first callback's health, the number of executed callbacks, successful target deletion, and absence of stale-target diagnostics.

## Limits

Collision is still AABB-based, uses bounded path sampling rather than analytical continuous collision detection, and projectiles only target the entity named when fired. This is not a full collision-mask, arbitrary target-filter, or physics system. Collision candidates are detected before callbacks; callbacks affect later queued-hit resolution, but do not retroactively change the collision geometry already sampled during that tick.
