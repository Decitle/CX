# Projectile queued-impact room-context safety (v0.9.5)

**Release:** CX Engine v0.9.5  
**Scope:** deferred projectile-hit dispatch when the active room/runtime context changes.

## Why this exists

Projectile collision candidates are gathered before callbacks execute so callbacks can safely mutate runtime state without invalidating the sprite traversal. That also means several candidate impacts can be waiting when the first callback changes scenes. Without a separate context token, a later candidate from the old room could still apply damage or invoke handlers after the active room had changed.

## Contract

The runtime owns a monotonic `roomContextRevision_` independent of the `RoomManager`'s content revision. A queued impact batch records the revision at the start of its projectile update. The runtime advances this token after:

- a successful `enter room`, including entering the same room at a different entrance;
- a successful reload of the currently active room;
- a successful unload of the currently active room (which leaves no active room); and
- a successfully committed script/runtime reload, after the staged replacement runtime has been accepted.

Loading, reloading, or unloading a room that is not active does not by itself invalidate the active scene context. Failed room operations do not advance the context token.

When a hit handler changes the context, that handler runs to completion and its normal post-handler persistence work completes. After it returns, the runtime does not dispatch additional matching handlers for that same hit. The projectile update then discards the remaining already-queued old-context impacts before applying their damage or invoking their callbacks.

## What this does not promise

This is a guard for deferred callbacks and hit candidates already queued within the same projectile update. It does **not** automatically clear every projectile/entity on room change, establish scene ownership for all runtime objects, or prevent an in-flight projectile from being considered on a later tick. Those are separate lifecycle decisions and should be specified before implementing a global cleanup policy.

Collision remains AABB-based with bounded sampled sweeps. This change does not add analytical continuous collision detection, multi-target projectiles, collision masks, or a resweep after a callback moves a target. It introduces no new CX syntax or project/save format.

## Regression coverage

`tests/projectile_runtime_tests.cpp` adds the following focused scenarios:

- `testQueuedImpactsAreCanceledOnRoomEntry`
- `testQueuedImpactsAreCanceledOnActiveRoomReload`
- `testQueuedImpactsAreCanceledOnActiveRoomUnload`
- `testQueuedImpactsRemainValidWhenNonActiveRoomReloads`

The three cancellation scenarios queue two impacts for the same target and register two matching handlers for the triggering hit. They assert that the first handler runs to completion, the second handler does not, the stale second impact causes no additional damage, the expected active-room state remains, and the script produces no runtime error. The complementary non-active-room reload scenario verifies that both queued hits continue to resolve when only a different room is reloaded.
