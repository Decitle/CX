# Projectile wall-versus-target precedence (v0.9.4)

**Current source:** v0.9.4.  
**Scope:** contract documentation and regression tests for the existing sampled projectile sweep. No scripting syntax or public API changed.

## Rule

For each moving projectile, the runtime checks the sampled positions along its path in order. The first sampled collision ends that projectile's update:

1. At each sample, test solid-tile collision first unless the projectile is configured internally to ignore walls.
2. If a solid tile overlaps the projectile at that sample, remove the projectile and do not dispatch a target hit.
3. Otherwise, if target collision is enabled and the projectile overlaps its bound target instance, record the target hit and consume the projectile.
4. If the target is first encountered at an earlier sample than a wall, the hit wins; the later wall is never reached by that projectile.
5. If target and solid tile both overlap at the same sample, the wall wins because it is checked first. This provides conservative wall occlusion and avoids hitting an actor through a wall boundary at a sampled tie.

The tie policy is deliberate and now pinned down by two regressions. It follows the existing code order; v0.9.4 adds an explanatory comment and tests rather than changing successful runtime behavior.

## What “sampled” means

The projectile is swept by moving its hitbox through a series of positions between tick endpoints. Samples are spaced using projectile hitbox size and active tile size. The engine tests each sampled position, not every possible continuous position. Therefore a thin overlap can still fall between samples under edge conditions; impact timing remains approximate. Do not describe this as analytical continuous collision detection or a general physics engine.

## Regression fixtures

- `testTargetHitBeforeWallUsesEarlierSampledContact` places a solid tile farther down the path than the target. The target loses one health and the projectile is consumed by that earlier contact.
- `testSolidWallWinsWhenTargetOverlapsSameWallSample` places the target over a solid tile boundary. On the first sampled position that overlaps both, the wall consumes the projectile and the target's health stays unchanged.
- The existing `testFastProjectileCannotTunnelThroughSolidTile` continues to check a fast shot is stopped by an intervening wall even if the tick endpoint lies beyond it.

## Compatibility and future work

This behavior applies to current single-target radial/fan and direct projectiles. Target identity is still captured at launch; if that instance disappears, the shot is removed. The rule does not settle collision masks, multi-target projectiles, exact contact order between multiple tiles, target movement caused by callbacks after hit candidates were gathered, room transition/reload, or independent wall-hit versus target-hit persistence. Those are separate roadmap issues.

