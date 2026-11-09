# Persistent storage writer coordination and durable replacement — v0.9.9

## Scope

v0.9.9 hardens the existing two-file save design without changing its data format or loading precedence. `variables.csv` remains authoritative whenever present; `variables.cxv` remains a recovery cache used only when the CSV is absent. The work addresses two previously documented gaps: concurrent engine processes writing the same project, and replacement returning before file/directory metadata has been explicitly synchronized.

## Per-project writer lock

Every `VariableStorage::save` acquires an exclusive lock on `<project>/.variables.lock` before rotating either backup or changing either save file. Linux/POSIX uses `flock(LOCK_EX)`; Windows uses an exclusive `LockFileEx` byte-range lock. Acquisition retries for up to 10 seconds, then fails with a diagnostic rather than waiting indefinitely.

The lock file is intentionally persistent and must not be deleted while the project is in use. Deleting a lock pathname while a process still has the old file open can allow a second process to create and lock a different file at the same path, defeating coordination. The lock coordinates CX Engine binaries that implement this storage contract; older CX Engine builds that predate v0.9.9 do not participate. Do not open the same project for saving in an older and newer engine process at the same time. It also cannot coordinate external editors or third-party tools that ignore the lock.

Cache recovery that reconstructs a missing CSV also acquires the same lock and rechecks `variables.csv` after locking. If another writer committed a newer CSV while the loader was decoding the cache, the authoritative CSV wins and is loaded instead of being overwritten by stale cache contents.

## Durable atomic replacement

Temporary files remain beside their destinations so the replacement stays on the same filesystem.

- POSIX: write the complete temporary file, retry interrupted writes, call `fsync()` on the file, close it, rename it over the destination, then `fsync()` the containing directory. A directory-sync failure is reported distinctly because the replacement has already happened but its directory entry could not be confirmed durable.
- Windows: write through a native file handle, call `FlushFileBuffers()`, then replace with `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`. The Windows branch is implemented but was not compiled or exercised in this Linux validation environment.

The save lock remains held across backup installation, CSV replacement, cache replacement, and all durability barriers. This prevents two cooperating writers from interleaving `.tmp` files or rotating backup generations over one another.

## Tests

`cx-storage-process-tests` now covers three related behaviors:

1. Existing process-kill boundary: terminate a child after the authoritative CSV has committed but while the cache temporary path is blocked; verify live files, manual backups, fresh-process CSV precedence, and deliberate rollback.
2. Cache-recovery race: let a child reader begin restoring a missing CSV from an old cache while the project lock is held; commit a newer CSV before releasing the lock; verify the child rechecks and preserves the newer authoritative CSV.
3. Lock honoring: hold the project's lock from the test process, start a child save, verify it remains blocked and leaves the CSV untouched, then release the lock and verify the save completes.
4. Concurrent writers: start two child processes behind a common gate; each performs eight saves with distinct 32 KiB payloads. Both must complete, and the final CSV-loaded values must match the values recovered from the compressed cache after temporarily hiding the CSV.

The process test remains Linux-only because it uses `fork`, `exec`, `flock`, FIFO barriers, and `SIGKILL`. The underlying production lock and replacement code also has a Windows implementation, but it needs native Windows validation.

## Durability limits

This is stronger than v0.9.8 but is not a universal promise against every power-loss scenario. It relies on the operating system, filesystem, storage device, mount options, and hardware honoring `fsync`/flush semantics. The two representations are still replaced sequentially rather than as one indivisible transaction: the CSV-first authority rule and previous-generation manual backups remain the recovery policy if interruption occurs between the two commits. The project directory itself should exist before the game first saves; durability of creating a previously nonexistent ancestor directory hierarchy is not independently guaranteed here.

The one-generation `.bak` files are still manual recovery copies, not automatic load sources. A future pass should add a validated generation manifest or journal so backup pairs can be recognized as matching after a failure that occurs during backup rotation or after a prior partial save. No automatic backup selection has been introduced.

## Source-reading path

Read `SaveLock`, `syncDirectory`, and `writeAtomic` in `src/variable_storage.cpp`, then follow `VariableStorage::load`'s locked cache-recovery branch and `VariableStorage::save`'s lock lifetime. The Linux integration coverage is in `tests/storage_process_tests.cpp`.
