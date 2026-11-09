# Room-transition lifecycle audit — v0.10.11

**Audit date:** 2026-11-07 (America/Los_Angeles)  
**Engine version:** v0.10.11 (unchanged)  
**Status:** implementation inspected; compatibility behavior documented; focused regression added. No production runtime behavior changed in this audit.

## Purpose and terminology

A room file is currently a piece of loaded world geometry, not an owning container for every runtime object. `RoomManager` tracks loaded room definitions and one active room. `Runtime` separately owns entities, projectiles, paths, lighting/effects, canvases, event handlers and delayed script executions. The active-room context revision (`roomContextRevision_`) tells deferred work whether it still belongs to the scene in which it was created.

This distinction is important: a room transition does **not** currently mean “destroy every object from the old room.” The contracts below describe the code that exists in v0.10.11. They do not imply a per-room entity registry, automatic object cleanup, save-game scene graph, or a universal scene-transition API.

## What changes the active-room context?

A successful operation advances the runtime room-context revision when it:

- enters a room, including entering the same room again at a different or identical entrance;
- reloads the currently active room from its room file;
- unloads the currently active room, leaving no active room; or
- commits a successful transactional script/runtime reload.

Loading a room without entering it, or reloading/unloading a room that is not active, does not invalidate current-room deferred work. A failed operation does not advance the context revision. `load room` is therefore a preload/cache operation, while `enter room` changes the live scene.

Entering a different room unloads the previous active room definition. It does not automatically unload every room that happened to be preloaded; the active-room transition path specifically releases the previous active room.

## Lifecycle matrix

| Runtime state | `enter room` | Reload active room | Unload active room | Notes |
|---|---|---|---|---|
| Active room definition | Destination becomes active; previous active definition is unloaded | Re-read from disk and remain active | Removed; no room remains active | Room cache operations on inactive rooms do not change the scene revision |
| Ordinary entities and plain sprites | Retained | Retained | Retained | `sprites_` is runtime-owned and has no room-owner field; identities and coordinates remain unchanged |
| Named paths | Retained | Retained | Retained | Paths are runtime-global definitions; their movement queries use whichever room is active at update time |
| In-flight projectiles | Retained and continue moving/updating | Retained and continue moving/updating | Retained and continue moving/updating | They retain target instance identity. They are not assigned room ownership |
| Already-queued projectile impacts | Remaining old-context candidates are discarded after a callback changes context | Same | Same | The callback that caused the change finishes; later handlers for that hit and remaining old-context queued impacts are suppressed |
| Entity tweens | Old-context tweens are silently discarded on the next entity update | Same | Same | Entity stays at its last committed interpolated position; no completion event is emitted |
| Named animation playback | Playback state remains on the entity and continues advancing | Same | Same | A completion callback captured in the old context is suppressed. A one-shot clip remains stopped on its final frame |
| Trigger contacts | Old contacts are cleared without exit events | Same | Same | If the same pair still overlaps on the next sample, it creates a fresh enter in the new context |
| Interaction focus | Old focus is cleared without cross-room exit events | Same | Same | A fresh eligible target can be acquired on the next focus reconciliation |
| Lights | Registered light remains; room-name filter controls visibility | Retained | Retained | Created in a room: visible only in that active room. Created with no active room: global scope. Lights have no lifetime timer |
| Beams and endpoint lights | Beam remains registered; its room-name filter controls visibility | Retained | Retained | Room-tagged endpoint light inherits the beam's room scope. Positive beam lifetime continues advancing on every runtime tick, including while hidden |
| Open canvases | Remain open | Remain open | Remain open | Canvas visibility is runtime-global overlay state, not room-owned state |
| `on tick` and input handlers | Program handlers remain registered | Same | Same | These are program-wide events, not room-local scripts |
| `every N` periodic schedule | Deadline remains scheduled | Same | Same | Schedules use wall-clock deadlines and are not reset by room transitions |
| A continuation suspended by `wait` | Old-context continuation is silently cancelled | Same | Same | The wait captures the room revision at the moment the `wait` statement executes |

### Entities and projectiles

The runtime stores ordinary entities and plain sprites in the global sprite map. The same object instance remains after room entry, active-room reload and active-room unload. No automatic hiding, removal, coordinate translation, serialization or room ownership assignment occurs. This is a compatibility behavior, not a promise that global entities are the best final model for every game.

Projectiles use the same global object storage and continue to age, move, expire and test collisions on later ticks. The target is still protected by its captured runtime instance identity. The room-context guard is narrower: it prevents the rest of an **already-collected** old-room hit batch from executing after the first callback changes context. It does not clear the projectile or prevent it from being processed on a later tick in the new room.

### Tweens and animation

Tweens explicitly capture the room-context revision when started. On a context change, the tween is dropped silently, leaving the entity at the last position already committed by a simulation tick. It is not treated as a successful arrival and cannot call `on tween complete`.

Named animation playback is stored on the entity rather than in a room registry, so the clip continues to advance while the entity survives. A queued completion captures the room revision; if the revision changes before dispatch, the stale completion is discarded. The one-shot clip has already stopped at its final frame and remains there until script changes playback.

### Trigger contacts and focus

Trigger-pair and interaction-focus state is transient runtime bookkeeping, keyed by entity instance identity. Both systems clear stale state on room-context revision mismatch without generating exits across the room boundary. This is intentional: teardown of a scene is not represented as an actor walking out of every trigger/focus target. The next simulation reconciliation can emit a new enter for a pair/target that qualifies in the new context.

### Effects and canvases

Lights and beams are stored independently of the active room. The renderer filters a non-empty `roomName` against the active room name; an empty `roomName` bypasses that room-name filter. Effects are not deleted simply because they are currently hidden. Persistent lights and zero-lifetime beams remain registered until explicitly removed or the runtime is replaced. Positive-lifetime beams advance once per simulation tick regardless of which room is currently drawing them, so they can expire off-screen/in another room. Endpoint lights are generated from their beam and use the beam's same room scope.

Open canvases are a single runtime-level set. A room transition, active-room reload or unload does not close them. Script authors must explicitly close room-sensitive canvases when changing scenes, or deliberately treat them as global overlays.

### Periodic handlers and `wait`

`every N` schedules are indexed alongside the program's handlers and use `steady_clock` deadlines. A room transition does not replace or pause those deadlines. `on tick`, keypress and other generic program handlers are likewise not room-owned.

A periodic handler body that reaches `wait` creates a suspended execution context bound to the current room revision. That continuation is cancelled silently if the context changes. This is separate from the periodic schedule itself, which remains scheduled and can fire again. A handler may enter a new room, continue running, and then reach `wait`; that wait binds to the **new** room revision and remains valid until the next context change. If multiple waits become ready together, the runtime rechecks the revision immediately before each one resumes.

### Script reload is not a room reload

A successful `loadScripts()` stages a new runtime and then replaces the current program/runtime-owned state with that staged state. It is a much broader replacement than `reload room`: entities, projectiles, lights, beams, paths, canvas state, schedules and other staged runtime state are replaced by the candidate's startup result. Old-program waits are discarded. Waits created during the successful candidate startup are rebased to the committed context. A failed transactional script load keeps the existing live runtime and its pending work intact.

## Callback dispatch boundaries — clarified in the v0.10.11 maintenance update

There are two intentionally different callback categories:

1. **Program-level events** are registered by the loaded script program, not by a room. Examples include `on load:`, `on tick:`, `on press key`, and button-click handlers. `Runtime::fire()` walks matching handlers in script-file/source order. If one key handler enters a different room, later handlers for that same key press still run. They are responding to the original global key event; they are not stale room-contact notifications. Each handler still receives its own fresh event-local variable scope.
2. **Snapshot-based gameplay callbacks** describe something sampled from a particular room or object instance: trigger contacts, projectile impacts, interaction focus, tween completion and animation completion. These dispatchers capture the room-context revision and relevant object identities. A callback that changes rooms or invalidates an object causes remaining stale callbacks to be skipped.

This distinction is now explicit in the comment above `Runtime::fire()` and is covered by `tests/room_lifecycle_tests.cpp`: the first `B` handler enters the arena, and a second matching `B` handler must still increment its global marker. The test does not change runtime behavior; it protects the existing program-wide event contract.

This rule applies to the current global handler model. If room-local event subscriptions are introduced later, they will need a separate ownership/dispatch contract rather than silently changing the meaning of existing global handlers.

## Compatibility decisions for v0.10.11 and clarification in the v0.10.11 maintenance update

1. Preserve global entity/projectile/path storage. Do not add implicit cleanup or coordinate changes during this audit.
2. Preserve room-name visibility filtering for lights, beams and beam endpoint lights while retaining the underlying effect objects.
3. Preserve fixed-tick aging for timed beams even when their room is inactive.
4. Preserve silent invalidation of old-context tween/wait/completion work and silent trigger/focus teardown without cross-room exits.
5. Preserve global canvas and program-handler behavior.
6. Keep `cx_core` and the new lifecycle tests independent of SFML. The graphical frontend is an optional integration layer.
7. Defer a future room-owned entity/effect API, room-local event handler model and scene-state serialization until each has an explicit migration/compatibility decision and regression coverage. Program-level versus snapshot-based callback dispatch is now explicitly documented and regression-tested.

## Regression coverage

The new `tests/room_lifecycle_tests.cpp` / `cx-room-lifecycle-tests` target verifies:

- global entity identity survives room entry;
- in-flight projectile identity, age and movement survive room entry;
- lights and beams retain their creation-room tags, and global lights remain global;
- a beam's endpoint-light configuration remains attached to the room-tagged beam;
- active-room reload and unload retain global objects/effects/canvas state;
- open canvases remain open across entry, reload and unload;
- periodic and `on tick` handlers run after room entry and periodic schedules continue with no active room;
- all matching program-level key handlers run in source order even when an earlier handler changes rooms;
- a positive-lifetime room beam expires while its room is hidden/unloaded, while persistent room effects remain registered.

Existing tests provide the remaining lifecycle boundaries:

- `cx-motion-tests`: old-room tween cancellation without completion;
- `cx-projectile-tests`: queued impacts invalidated on entry/active reload/active unload, plus inactive-room reload does not invalidate them;
- `cx-collision-tests`: trigger contacts reset without cross-room exit;
- `cx-interaction-focus-tests`: focus is cleared without cross-room exit and can be reacquired;
- `cx-task-lifecycle-tests`: wait cancellation, simultaneous ready continuations, post-transition re-wait, and failed/successful script reload behavior;
- `cx-animation-tests`: animation timing and stale completion identity/revision safety.

**v0.10.11 verification:** GCC 14.2 Release, SFML disabled, zlib enabled: all 16 registered Linux CTest targets passed. The SFML 3.1.0 graphical target was also built and linked with audio disabled in the recorded Linux development environment. Platform and physical-display coverage remain separate requirements.

**v0.10.11 maintenance follow-up:** the room-lifecycle target adds one program-level event-dispatch assertion. The current regression suite retains these cases as automated CTest coverage.
