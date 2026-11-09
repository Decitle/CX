# v0.9.6 — suspended wait lifecycle across room-context changes

## Contract

A script continuation suspended by `wait` is bound to the active room-context revision captured when that `wait` executes. A successful active-room entry (including re-entering the same room), active-room reload/unload, or committed runtime reload changes that context. Pending continuations from the prior room context are discarded before resuming. This prevents delayed work from unexpectedly mutating a different scene.

The rule is deliberately tied to **the moment of suspension**, rather than the event that originally started the handler. A handler may change rooms, continue executing after its transition statement, and then reach `wait`; that new suspension belongs to the new room context and remains valid until another context transition. This also applies to a later `wait` in a resumed continuation.

If multiple continuations become ready on the same tick, each is rechecked immediately before it runs. A continuation that changes rooms therefore invalidates other ready continuations from the prior context, regardless of queue order. Not-yet-due stale continuations are removed rather than held until their old deadline.

A successful transactional script reload replaces the old runtime's waits as before. Waits created during the candidate runtime's successful startup are retained and rebased to the committed runtime context; otherwise they would be incorrectly mistaken for stale candidate work. Failed reloads preserve the previously running runtime and its pending waits; this is regression-tested.

## Tests

- `testWaitContinuationIsCancelledAfterRoomContextChanges`: a not-yet-due wait cannot resume in a different room.
- `testWaitsDueTogetherRespectRoomContextAndCanRewaitAfterTransition`: an earlier ready continuation can invalidate a later same-tick continuation, while its own subsequent wait is allowed in the new room.
- `testFailedReloadPreservesExistingWaitContinuation`: a rejected candidate leaves the last-good runtime's pending wait active.
- `testSuccessfulReloadReplacesOldWaitAndKeepsOnLoadWait`: successful reload discards old-program waits but retains waits created during staged startup.

## Non-goals and limits

This does not add named task handles, explicit cancellation commands, timers, room-local task ownership, or new script syntax. The periodic schedule itself remains process-wide and is not canceled by room transitions; however, if an individual periodic-handler execution suspends at `wait`, that continuation follows the same room-context cancellation rule. Cancellation is silent control-flow disposal, so no runtime error is emitted. The rule should be revisited before adding asynchronous functions, user-visible task handles, or room-local scripts.
