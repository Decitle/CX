# Persistent variable storage and recovery (current through v0.10.0)

This guide explains the files that preserve `{global}` variables, what the engine does when a file is imperfect, and how to inspect/recover the data without accidentally resurrecting deleted variables.

## The files

| File | Purpose | Automatically loaded? |
|---|---|---|
| `variables.csv` | Human-editable authoritative variable table whenever it exists | Yes, first |
| `variables.cxv` | Compressed fallback snapshot used only if `variables.csv` is absent | Yes, if CSV is absent and cache validates |
| `variables.csv.bak` | One-generation copy of the previous regular CSV before replacement | No; manual recovery copy |
| `variables.cxv.bak` | One-generation copy of the previous regular cache before replacement | No; manual recovery copy |

The first save creates the main files without creating backups because there is no older generation to preserve. Subsequent saves preserve each existing regular file byte-for-byte before replacing it. The next successful save rotates the `.bak` files to the versions that existed immediately before that save; this is intentionally a one-generation backup, not an archive of every save.

## The CSV row format

The file starts with comments, followed by a header and rows such as:

```csv
# CX Engine persistent variables. Edit or delete rows while the engine is closed.
# name,type,value
score,number,42
player_name,string,Alex
music_enabled,bool,true
last_checkpoint,none,
```

Variable names are case-insensitive. The currently persisted value types are `string`, `number`, `bool`, and `none`. Reserved engine variables and the local/session-only prefixes are not stored as ordinary persistent globals.

CSV quoting and string escaping are separate layers. Commas and quotes use CSV quoting; quotes inside a quoted field are doubled. Backslashes and line breaks use the engine's text escaping layer: `\\` means a literal backslash, `\n` means a newline, and `\r` means a carriage return. The CSV layer must not escape those backslashes a second time. That distinction is why the v0.8.8 regression tests include both real newlines and literal characters like backslash followed by `n`.

## Loading precedence and damage policy

1. **If `variables.csv` exists, it wins.** The engine does not merge the cache into the CSV. Otherwise a row deliberately deleted in the CSV could come back from an older cache.
2. **For a readable CSV with malformed rows**, valid rows remain available and the engine creates a warning naming each of up to eight bad lines, followed by a count if more rows were bad. Invalid rows are skipped. The stale cache is not consulted.
3. A warning-only startup does not immediately rewrite the malformed CSV merely to refresh the cache. If a script deliberately mutates persistent variables during startup or later, the next save can still replace the CSV; before doing so, it preserves the current CSV and cache as `.bak` copies.
4. **If the CSV is absent**, the engine validates and decodes `variables.cxv`. A malformed compressed stream or decoded table fails recovery; it does not fabricate a blank save or commit a partial reconstructed CSV.
5. Reads are bounded to 256 MiB per file before streaming it into memory. The decompressed cache size has its own bound and codec-specific validation.
6. Every engine save acquires the persistent project lock at `.variables.lock` before rotating backups or changing either save file. Competing engine writers wait up to 10 seconds; timeout is reported rather than allowing the `.tmp` files or backup rotations to race. Do not delete the lock file while the project may be open.
7. Save uses temporary files beside each destination, flushes the new file before replacement, and replaces each destination individually. POSIX additionally syncs the containing directory after each rename; Windows uses `FlushFileBuffers` and `MOVEFILE_WRITE_THROUGH`. If CSV replacement succeeds but cache replacement fails, the save returns failure while explicitly reporting that the authoritative CSV was already saved. The previous regular CSV/cache copies remain in `.bak` if their backup step succeeded.
8. Cache recovery uses the same writer lock before reconstructing a missing CSV, then rechecks whether another process has installed a newer authoritative CSV. If so, the CSV is loaded instead of being overwritten by decoded cache contents.

## Manual recovery procedure

**Always close the engine before editing or restoring save files.**

1. Copy the entire project folder somewhere safe before changing anything. Keep that copy even if the project appears to be recoverable.
2. Open `variables.csv` and `variables.csv.bak` as text. Compare the current values with the previous CSV. The `.bak` file may itself contain malformed rows; it is a snapshot, not a guarantee that the previous data was perfect.
3. Decide whether you want the current authoritative CSV or a deliberate rollback. A process interruption after CSV replacement can leave the current `variables.csv` at the newer complete generation while `variables.cxv` still contains the previous generation. Normal startup uses the CSV and does not silently fall back to the stale cache.
4. To roll back deliberately, preserve the current CSV under a new name (for example `variables.csv.after-interruption`), then copy `variables.csv.bak` to `variables.csv`. When both backup files are readable and you intend to restore that previous generation, also copy `variables.cxv.bak` to `variables.cxv`. Restore while the engine is closed. Keeping the current CSV present ensures it remains the authority even if the cache is older or incompatible.
5. Start the game and inspect diagnostics and the expected variables before continuing play. Keep the separate project copy until satisfied. The next successful save rotates the one-generation `.bak` files, so preserve the recovery copy before making further saves.
6. Keep `variables.csv` when moving a project between computers. A build without zlib can read the built-in codec-0 cache but cannot decode a codec-1 zlib cache on its own.
7. Do not edit `variables.cxv` in a text editor. It is binary data. The engine does not automatically load `.bak` files; this keeps it from silently choosing an older timeline and resurrecting deleted variables.

## What this does not guarantee yet

The current save operation is still not a transaction spanning both data files as a unit. v0.9.9 serializes cooperating engine writers with `.variables.lock`, flushes temporary files before replacement, and uses POSIX file plus parent-directory `fsync` or the Windows flush/write-through APIs. v0.10.0 adds `variables.meta`, which identifies the last complete CSV/cache generation and validates cache bytes before reconstruction. The Linux integration target checks lock honoring, two concurrent writers, process interruption at the CSV/cache boundary, fresh-process CSV precedence, and deliberate backup rollback. Ten repeated storage integration runs passed in the v0.9.9 validation session. These improvements are not a universal power-loss guarantee: actual sudden-power-loss behavior depends on filesystem, mount options, controller, and hardware; the Windows branch has not yet been built on a Windows host. Older CX Engine releases that predate v0.9.9, external editors, and third-party processes that ignore `.variables.lock` are not coordinated; do not have them save the same project simultaneously with a v0.9.9-or-newer engine. The manifest describes the current CSV/cache generation, not the `.bak` pair. Backup-generation certification, automatic selection, history rotation, and restore UI remain future work. See `features/variable-storage-durability.md` for the precise contract and limits.

## Source-reading path

Start in `include/cx/variable_storage.hpp`, then read these functions in order in `src/variable_storage.cpp`:

1. `escapeText` / `unescapeText` — engine-specific text escaping.
2. `csvField` / `splitCsv` — CSV-level quoting.
3. `serializeCsv` / `parseCsv` — typed variables to/from rows.
4. `compressCache` / `decompressCache` — binary cache codec and bounds checks.
5. `SaveLock`, `syncDirectory`, `writeAtomic`, and `backupExistingRegularFile` — cross-process serialization, file/directory durability barriers, temporary replacement, and prior-generation backups.
6. `VariableStorage::load` / `VariableStorage::save` — authority, locked cache recovery, writer-lock lifetime, and partial save behavior.
7. `Runtime::loadPersistentVariables` / `Runtime::savePersistentVariables` in `src/runtime.cpp` — how storage policy becomes runtime diagnostics and write decisions.

Useful regressions live in `tests/runtime_tests.cpp` and begin with `testVariableStorage` or `testRuntimeDoesNotImmediatelyOverwriteMalformedCsv`. For v0.8.9 in particular, inspect `testVariableStorageBackupFailuresDoNotCommitNewGeneration` and `testVariableStorageCacheCommitFailureKeepsRecoveryGeneration`.
