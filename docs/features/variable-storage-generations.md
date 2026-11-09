# Persistent save generations and verified cache recovery — v0.10.0

## Purpose

v0.10.0 adds a small sidecar record, `variables.meta`, to identify the last complete save generation. The existing save files and scripting API are unchanged:

- `variables.csv` remains the editable authoritative representation whenever it exists.
- `variables.cxv` remains the compressed recovery cache.
- `.variables.lock` still serializes cooperating engine writers.
- `variables.csv.bak` and `variables.cxv.bak` remain one-generation manual recovery copies.

The manifest reduces the chance that a stale, truncated, or mismatched cache will be mistaken for the most recently committed save after the CSV is missing.

## Manifest format

The file is UTF-8-compatible plain ASCII with a strict, versioned, fixed-field layout:

```text
CXVM1
generation=2
csv_size=123
csv_hash=0123456789abcdef
cache_size=89
cache_hash=fedcba9876543210
```

The values above are illustrative. `generation` starts at 1 and increments after each complete successful save. Size fields are byte counts. Hash fields are 16-character hexadecimal FNV-1a 64-bit fingerprints of the exact file bytes. The parser rejects missing, reordered, duplicated, unknown, malformed, or trailing fields and limits the metadata file to 1 KiB.

FNV-1a is used only as a compact accidental-corruption/mismatch detector. It is **not cryptographic authentication**, does not prevent malicious modification, and must not be described as a security boundary.

## Commit order

A save obtains `.variables.lock`, determines the next generation, preserves the previous regular CSV/cache files as manual backups, atomically replaces `variables.csv`, atomically replaces `variables.cxv`, and finally atomically replaces `variables.meta`. The metadata file is therefore the marker written last for a complete pair.

If the CSV replacement succeeds but the cache replacement fails, the save reports the partial commit and leaves the previous manifest in place. If both data files are replaced but the manifest replacement fails, the save also reports failure; the CSV remains usable, but a missing CSV will not be reconstructed from a cache that cannot be verified against the old manifest.

This protocol does not make three files one indivisible filesystem transaction. It makes the last fully manifested cache generation identifiable. The CSV remains authoritative when present.

## Load and recovery rules

1. **CSV exists:** read it and retain the existing CSV-authoritative behavior, including manual edits and row deletions. The manifest never overrides a present CSV. If the CSV bytes still match the manifest but the cache bytes do not, load the CSV normally and append a warning that cache recovery is disabled until a successful save refreshes it.
2. **CSV absent, manifest absent:** use the legacy cache-recovery path for projects created before v0.10.0. After the cache is validated and the CSV is reconstructed under the project lock, the engine attempts to create generation-1 metadata. Failure to create metadata does not discard successfully recovered in-memory values or the reconstructed CSV; it adds a warning.
3. **CSV absent, manifest valid:** recover only if the exact `variables.cxv` bytes match the manifest's cache size and fingerprint, then decompress and validate every row before writing a new CSV.
4. **CSV absent, manifest malformed or cache mismatch:** refuse recovery, leave the cache untouched, and return a clear warning. The engine does not invent an empty CSV or expose a partial map.
5. **Concurrent save during cache recovery:** after acquiring `.variables.lock`, re-check the CSV, cache, and manifest. A newer CSV wins; a cache changed while waiting for the lock must match the current manifest before it can be restored.

A successful ordinary save also establishes a manifest for a legacy project. This allows existing projects to migrate without a separate command or bulk rewrite.

## Manual backup restoration

The `.bak` files are not auto-selected. When restoring a known paired backup manually, close every engine using the project, copy the desired backup files over `variables.csv` and `variables.cxv`, and remove or move aside a `variables.meta` file that describes a different newer generation. The next successful save will create a new manifest. Do not delete `.variables.lock` while any cooperating process may have it open.

The current manifest validates the current CSV/cache pair; it does **not** certify that the two `.bak` files belong to one generation. Backup-pair certification and an in-engine restore UI remain future work.

## Compatibility and limits

Projects without `variables.meta` remain compatible. Old engine builds ignore the new sidecar and may still read/write the CSV/cache pair, but they do not participate in the v0.9.9+ lock contract or update the manifest. Do not let older and newer binaries write the same project concurrently.

The manifest cannot detect an intentional edit to a present CSV as an error, by design. It does not add a transaction across CSV/cache/manifest, does not guarantee hardware power-loss behavior beyond the documented flush/fsync contract, and does not protect against hostile tampering. Windows still requires native-host verification; SFML Audio and physical-display GUI testing are separate outstanding items.

## Source and tests

- `src/variable_storage.cpp`: `SaveManifest`, `hashBytes`, serialization/parsing, `readManifest`, load-time cache gate, and last-step manifest commit.
- `include/cx/variable_storage.hpp`: public storage contract.
- `tests/variable_storage_manifest_tests.cpp`: generation increment and overflow refusal, hand-edited CSV precedence, stale-cache warning, cache mismatch refusal, malformed-manifest refusal, missing-data refusal, and verified legacy recovery. This focused target keeps the general runtime test translation unit below the source-size limit.
