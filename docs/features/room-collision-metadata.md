# Room and asset tile-collision metadata — v0.10.6

CX Engine v0.10.6 adds explicit, backward-compatible tile solidity metadata. The feature is deliberately limited to static room tiles; it is not a rigid-body solver, one-way platform system, or generalized trigger-region authoring system.

## Resolution order

For a cell `(x, y)`, runtime solidity is resolved in this order:

1. A room cell override of `solid` always blocks, even when the base tile is empty. This creates an invisible static blocker.
2. A room cell override of `none` is passable, even when the assigned texture defaults to solid.
3. `inherit` (including an omitted legacy matrix) uses the texture's asset default.
4. When `assets/collision.json` is absent, the legacy texture-name heuristic is retained for compatibility: IDs containing `stone`, `wall`, `trunk`, or `crate` default to solid; other texture IDs default to non-solid.
5. Empty cells with `inherit` remain non-solid. Out-of-bounds queries are not solid; existing movement and navigation bounds checks retain responsibility for room limits.

Changing a cell's base tile through `RoomManager::setActiveTile` or `clearActiveTile` resets that cell's collision override to `inherit`. Explicitly paint the override again when a cell needs a one-off exception to its new texture default. Visual overlays are not included in the collision query and do not add or remove solidity.

## Asset defaults: `assets/collision.json`

The file is optional. When present, it must be valid as a whole before any default is applied. A parse error, unsupported version, invalid mode, empty ID, or unknown texture ID causes the entire file to be ignored and a diagnostic warning emitted; the previous filename-based defaults remain in effect. This prevents a partially valid file from producing a half-updated collision palette.

```json
{
  "schema": "cx-collision",
  "version": 1,
  "default": "none",
  "textures": {
    "stone_wall": "solid",
    "stone_floor": "none",
    "wood_crate": "solid",
    "moss": "none"
  }
}
```

`default` is either `solid` or `none` and applies to discovered textures not listed in `textures`. Each listed texture entry must refer to an existing discovered texture and use `solid` or `none`. IDs use the same normalized texture IDs exposed by the palette. Prefer `default: none` and list only known blockers for ordinary adventure rooms; choose a different default when the project's asset library is primarily solid terrain.

## Per-cell room overrides

Room JSON remains `schema: "cx-room"`, `version: 1`. The optional `tileCollision` property is a row-major matrix with the exact same dimensions as `tiles`. Each cell accepts `inherit`, `solid`, `none`, or JSON `null` (`null` is treated as `inherit`). If omitted, every cell inherits its texture default. A mismatched number of rows or columns or an unknown mode rejects the room load without replacing the currently loaded room.

```json
{
  "schema": "cx-room",
  "version": 1,
  "name": "small_ruin",
  "width": 3,
  "height": 2,
  "tileSize": 32,
  "projection": "orthogonal",
  "tiles": [
    ["stone_floor", "stone_floor", "stone_floor"],
    ["stone_wall", "stone_floor", "stone_wall"]
  ],
  "tileCollision": [
    ["inherit", "inherit", "inherit"],
    ["inherit", "none", "inherit"]
  ],
  "overlays": [],
  "entrances": {}
}
```

The center floor cell explicitly inherits `stone_floor`'s asset default; change it to `solid` to make an invisible blocker or to `none` to force an otherwise solid asset passable. If `tileCollision` is not in an older room file, no migration is required.

## Runtime coverage

The shared `RoomDefinition::isSolidTile` resolver is used by static solid-tile movement checks and tile waypoint pathfinding. Projectile swept-tile checks also route through the same runtime solid-tile check. Entity/entity AABB collision channels remain a separate system. Tweens still do not perform collision response and may pass through tiles; this update does not change their v0.10.5 contract.

## Room Studio

The Room Studio Collision tool paints `inherit`, `solid`, or `none` on a cell. Blue cells represent explicit solids; amber cells represent explicit non-solids; unmarked/default cells inherit the asset. Right-clicking a cell restores `inherit`. The tool supports empty-cell blockers, undo snapshots, room resizing, load/save and round-trip validation. Placing or clearing a base tile resets that cell to inherit, so a one-off override is not accidentally carried onto a different tile.

The `/api/textures` palette response exposes each texture's effective default. A malformed manifest is returned as a warning and falls back to legacy defaults. The save endpoint validates the collision matrix before writing and uses the existing same-directory temporary-file/replace path; invalid input does not overwrite the previous room.

## Boundaries and next work

This milestone does not add per-pixel shapes, rotated polygons, one-way platforms, dynamic moving obstacles, entity collision response, swept actor movement, object-attached collision, triggers encoded in room cells, or automatic runtime collision edits by script. Those require separate contracts. The next recommended work is named animation clips/states and a small interaction-focus policy, then deterministic seeded scene choices and a texture-backed beam effect for the cave vertical slice.
