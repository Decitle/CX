# Persistent-save process interruption and recovery — v0.9.7

## Scope

This release adds an integration test of a real interrupted writer on Linux. It does not change the production save format, save order, load precedence, or automatic recovery policy. It demonstrates one important interruption point, rather than claiming that two separate files form one atomic transaction.

## Interruption boundary tested

The test executable first uses `VariableStorage::save` to create a complete generation with `score=1`. It then places a FIFO at `variables.cxv.tmp` and launches itself as a child writer to save `score=2`. The production save path writes/replaces `variables.csv` before opening the cache temporary path. Opening the FIFO for output blocks until a reader is present, so the parent can observe the complete new CSV and then terminate the child with `SIGKILL` before cache replacement. This is a filesystem barrier, not a production failure hook.

The assertions confirm the expected state byte-for-byte:

| File | Expected after interruption | Reason |
|---|---|---|
| `variables.csv` | New complete generation (`score=2`) | The CSV replacement completed before the child was killed |
| `variables.cxv` | Previous complete generation | Cache replacement had not started/completed |
| `variables.csv.bak` | Previous CSV generation (`score=1`) | The previous file was copied before CSV replacement |
| `variables.cxv.bak` | Previous cache generation | The previous cache was copied before cache replacement |

The test then starts a new process and verifies that the present CSV wins over the stale cache. This is intentional: normal startup must not resurrect an older value from the cache when a CSV exists.

## Manual rollback that is tested

If the owner deliberately wants the prior generation, the test preserves the newer CSV under a different name, copies `variables.csv.bak` to `variables.csv`, copies `variables.cxv.bak` to `variables.cxv`, and starts another fresh process that must read `score=1`. This verifies the paired-backup instructions in `../variable-storage-recovery.md`.

For real projects, close all engine/game processes first and copy the entire project folder to a safe location before restoring anything. Keep a separate copy of the interrupted files. Restore only if the prior generation is the one wanted. Inspect startup diagnostics and verify the expected values before continuing; a later successful save rotates the one-generation backup files.

## Test execution and portability

CMake registers `cx-storage-process-tests` only on Linux because it depends on POSIX process creation, named pipes, `waitpid`, and `SIGKILL`. The target has a 15-second timeout. It uses a fresh temporary folder per process and attempts to kill and reap its blocked child on an assertion failure.

The test does **not** establish:

- power-loss or kernel-crash durability;
- that file data and directory entries were persisted to physical media (`fsync`/directory `fsync` is not added by this change);
- concurrent-writer correctness or locking;
- process-interruption behavior on Windows or other non-Linux platforms;
- automatic `.bak` selection or a restore UI.

These remain separate engineering work; platform tests should be run natively on each supported operating system.
