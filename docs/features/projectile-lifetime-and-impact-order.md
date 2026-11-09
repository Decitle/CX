# Projectile lifetime and impact ordering — v0.9.2

## What changed

The v0.9.1 runtime safely deferred hit callbacks until after it finished traversing the sprite map. It resolved those queued hits one by one, but the order was the order in which projectile IDs appeared in the map. Since names such as `projectile_10` sort lexicographically before `projectile_2`, and because shot creation order is not necessarily contact order, a bullet that arrived later could run its callback first.

v0.9.2 changes the queue entry to include the sampled elapsed time from the beginning of the current tick to the first sample that overlaps the target. After collision detection and safe projectile removal, hits are stable-sorted by this sampled time. The earliest sampled impact is processed first. Equal-time hits retain the original deterministic map traversal order.

Each accepted hit still applies damage immediately before invoking `on projectile hit entity "name":`. A callback therefore sees the health after its own hit. If a previous callback removes the target or its health reaches zero, subsequent queued hits against that target are skipped.

## Lifetime boundary fix

Previously, the update incremented projectile age first and removed a projectile as soon as its age reached the lifetime. That branch skipped movement and collision checks for the current tick. As a result, a projectile could lose its final tick of travel and miss an object that it should reach before its expiry time.

The current update computes the remaining lifetime at the start of the tick, limits movement to the smaller of the tick duration and remaining lifetime, checks that segment for wall/target collision, and then removes the projectile if its lifetime elapsed. This also handles a partial final tick without allowing the projectile to travel past its lifetime.

## How to reason about simultaneous shots

For projectiles that hit during the same logic tick:

1. Each projectile follows its own bounded swept path.
2. The runtime records the first sampled target contact, not a callback, while iterating the sprite map.
3. The runtime removes consumed projectiles and updates the active-projectile count.
4. Candidate impacts are sorted by sampled time within the tick. Ties preserve deterministic traversal order.
5. Each impact is applied and dispatched only if its target still exists as a live entity with positive health.

The recorded time is sampled and therefore approximate. With the current step size, two physically close impacts can be assigned the same sample time. The engine does **not** claim analytical continuous-collision precision.

## Regression tests

`tests/projectile_runtime_tests.cpp` includes two focused cases:

- `testSameTickImpactsResolveBySampledImpactTime` fires three shots from different start distances so all collide in the same tick. `projectile_3` is closest and was spawned last; its callback must be first.
- `testProjectileGetsItsFinalLifetimeMovementStep` places a target at the path end reached after the projectile's five-second lifetime. The target must receive the hit during the final tick, and the projectile must then be removed.

The full four-target CTest suite passed in GCC 14.2 Debug with zlib, GCC 14.2 Release with zlib, and GCC 14.2 Debug with zlib explicitly disabled.

## Remaining projectile limitations

- Collision boxes are axis-aligned rectangles.
- Swept collision uses bounded sampling. This is more robust than checking only the final position, but it is not an exact segment/AABB or tile-grid traversal solver.
- Projectiles target the named entity captured at launch; there are no collision masks, teams, arbitrary multi-target filtering, wall bypass syntax, or independent wall-hit and target-hit persistence settings.
- Radial/fan patterns define travel directions. They do not spawn bullets at polygon vertices/perimeters, or coordinate orbiting/expanding trajectories over time.
- A true real-time performance benchmark and SFML graphical build were not run for this patch.
