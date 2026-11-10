# CX Engine world/entity reference (reviewed against v0.10.14)

> Feature headings retain their original introduction versions. This guide also includes later additions; use the linked feature notes for current lifecycle rules and limitations.

This document describes the working first implementation and the planned evolution. It deliberately separates the API that exists now from features that still need engineering.

## Current mental model

- **Sprite**: a textured draw object, suitable for decoration, signs, pickups, or visual effects.
- **Entity**: the same underlying draw object with gameplay state such as health, an optional axis-aligned hitbox, solid-tile avoidance, an animation sheet with optional named clips, and an optional path.
- **Room**: a tile grid with a tile size and named entrance coordinates. Runtime tile writes alter the currently loaded room data.
- **Path**: a named list of tile-coordinate waypoints. An entity follows them at a speed specified in tiles per second.
- **Projectile**: a short-lived sprite/entity fired by one entity toward a target entity. Its trajectory is chosen at spawn time and then remains straight.
- **Canvas UI**: separate screen-space panels, text, buttons and progress bars. It does not move with the world camera.

World positions and entity sizes use world pixels. Room tiles and path waypoints use integer tile coordinates. With `tileSize: 32`, tile `(x, y)` occupies the rectangle from `(x*32, y*32)` to `((x+1)*32, (y+1)*32)`. Tile coordinates start at zero.

## Working script API

### Make an entity

```skript
on load:
    create entity "guard" with texture {$guard_8way}
    set entity "guard" size to (32, 32)
    set entity "guard" position to (320, 224)
    set entity "guard" animation sheet to (32, 32) columns 4 rows 8
    set entity "guard" hitbox to (18, 12) offset (7, 16)
    set entity "guard" avoids solid tiles to true
    set entity "guard" health to 8
    set entity "guard" max_health to 8
```

`create entity` uses the existing texture registry, so `assets/textures/guard_8way.png` becomes `{$guard_8way}`. The object remains lightweight: no 3D transform, rigid-body solver, or heavyweight component graph is allocated.

Hitbox syntax is `(width, height) offset (x, y)`. The offset is relative to the sprite's top-left. The hitbox is an axis-aligned rectangle (AABB). If no explicit hitbox is provided, the sprite rectangle is used for overlap checks. Entity health is reflected in `{entity::guard::health}` and `{entity::guard::max_health}`. Visibility only affects drawing. A hidden entity remains collidable and can still receive projectile damage; use collision enablement as the collision policy rather than hiding a sprite.

### Named animation clips

The legacy eight-way sheet loop remains active whenever an entity has no custom clip selected. Named clips take priority and can be one-shot or looping.

```cx
set entity "guard" animation sheet to (32, 32) columns 6 rows 8
define animation "wake" for entity "guard" frames 0 to 3 row 0 at 8 fps once
define animation "walk" for entity "guard" frames 0 to 5 row facing at 10 fps looping
play animation "wake" for entity "guard"

on animation complete "wake" for entity "guard":
    play animation "walk" for entity "guard"
```

The completion event exposes `{_animation_entity}`, `{_animation_name}`, `{_animation_frame}`, and `{_animation_row}`. `once` holds its final frame for a full frame interval and dispatches once; `looping` wraps without dispatch. A selected one-shot holds its last frame until another clip is selected or animation is stopped. The animation is independent of entity movement/tweens. Changing the animation sheet clears clip definitions. See `features/animation-clips.md` for timing quantization, validation and callback identity guarantees.

### Continuous input movement

```skript
on tick:
    move entity "player" using WASD at speed 170
```

Speed is world pixels per second. Diagonal movement is normalized. When `avoids solid tiles` is enabled, movement checks the configured hitbox against room cells registered as solid.

### Scripted NPC path

```skript
create path "patrol" through tiles (10, 8), (16, 8), (16, 13), (10, 13)
set path of "guard" to path "patrol" with speed "1.25"
set entity "guard" avoids solid tiles to true
```

The speed is tiles per second. When obstacle avoidance is enabled, the runtime uses four-neighbor breadth-first search through static non-solid cells to route each waypoint segment. This is useful for small rooms and simple patrols. It is not yet A*, a navmesh, or a dynamic steering system. Moving entities are not considered as obstacles, paths are not automatically recalculated for every world mutation, and waypoints currently live in `.cx` scripts rather than being painted in Room Studio.

### Deterministic scripted movement

```cx
on press key "SPACE":
    tween entity "boulder" to (700, 360) over 0.9 seconds easing ease_in_out with arc height 120

on tween complete for entity "boulder":
    set {boulder_arrived_x} to {_tween_x}
    set {boulder_arrived_y} to {_tween_y}
```

The tween interpolates the entity's world-pixel top-left position over a fixed number of logic ticks. `linear`, `ease_in`, `ease_out`, and `ease_in_out` are supported. Optional `with arc height N` adds an upward parabolic offset whose apex is halfway through the duration. The completion handler is one-shot and receives `{_tween_entity}`, `{_tween_x}`, and `{_tween_y}`.

`cancel tween for entity "boulder"` silently stops the active tween. A replacement tween, explicit position assignment, manual move, pressed input movement, path assignment, deletion, or object replacement cancels the previous tween. Room-context changes discard old tweens without completion. A tween clears the entity's current tile path while it owns movement.

This is deterministic visual/scripted position movement, not collision-aware physics: it can pass through walls and other entities, and does not use `avoids solid tiles`. Use tile paths or input movement for the existing solid-tile avoidance behavior. Projectiles and plain sprites are not tween targets. More details are in `features/entity-tweens.md`.

### Fire a projectile and handle damage

```skript
on press key "SPACE":
    fire projectile from "player" toward "guard" with texture {$player_shot} speed 8 damage 1

on projectile hit entity "guard":
    if {entity::guard::health} <= 0:
        delete entity "guard"
```

The projectile begins at the source's center and chooses a direction toward the target's center at that moment. Its speed is in tiles per second; it is removed after hitting its selected target, touching a solid tile, or reaching its lifetime limit. Damage is applied before the `on projectile hit entity` handler executes. Projectiles do not home or retarget, and there is not yet a general collision event for every arbitrary pair of entities.

### Radial and fan patterns

```cx
fire projectile pattern radial from "warden" toward "player" with texture {$ember_orb} count 8 speed 4 damage 1
fire projectile pattern fan from "warden" toward "player" with texture {$ember_orb} count 5 spread 60 speed 4 damage 1
```

The pattern geometry is implemented as a small pure module and tested separately. `radial` evenly spaces directions over a full turn, with one direction aligned to the target at fire time. `fan` spaces directions over a target-centered arc. A pattern command creates all shots only after its values, texture, entities, and active-count budget have passed validation. A command may create at most 64 shots; the runtime has a global cap of 4096 live projectiles and rejects an over-cap volley without partially creating it.

Projectile movement checks intermediate positions between simulation ticks. This is a sampled sweep (step size is based on the projectile hitbox and tile size), not a continuous rigid-body solver. Projectiles still check only their named target and solid tiles; the pattern API does not yet support arbitrary target filters, projectile perimeter arrangements, time-varying/orbiting groups, exposed wall bypass, or separate hit-persistence rules. See `features/projectile-patterns.md` for limitations and the next planned extensions.

Starting in v0.9.3, each runtime sprite/entity receives an internal monotonically assigned instance ID in addition to its script-visible name. That identity is not part of the CX syntax; it protects deferred work. If two projectiles collide with an entity during one simulation tick and the first hit handler deletes and recreates that entity under the same name, the second queued impact is discarded because it struck a different (now gone) instance. Projectiles also capture the targeted instance at launch; if that exact target is deleted or replaced during flight, the projectile is removed on its next update rather than accidentally hitting a replacement that reused the same name.

### Make and remove cover

```skript
place tile {$cover_stone} at (12, 10)
clear tile at (12, 10)
```

These commands modify the active room in memory. They do not currently rewrite the room JSON on disk; save/load editing remains an editor workflow. Static solidity is resolved from optional `assets/collision.json` defaults plus per-cell `tileCollision` overrides. When the manifest is absent or invalid, legacy texture-name defaults (`stone`, `wall`, `trunk`, `crate`) are retained for compatibility. See `features/room-collision-metadata.md` for the full resolution contract and Room Studio workflow.

## Camera and display scaling

```skript
set engine::world_view_height_tiles to 22.5
set engine::camera_follow to "player"
```

`world_view_height_tiles` controls how tall the visible world is, in active room tile units. With 32-pixel tiles, `22.5` corresponds to 720 world units vertically. The world-view width derives from the window aspect ratio; ultra-wide windows show more horizontally instead of stretching tiles/sprites. The camera follow position is clamped to the active room bounds. The canvas UI is rendered in a separate screen-space view. Camera smoothing, manual camera bounds, split-screen, and editor tools for testing extreme aspect ratios remain to do.

## Entity classification and collision filtering (v0.10.2)

Entities now carry independent `kind`, `team`, `tags`, collision `layer`, and collision `mask` metadata. Use these to describe a boss, player, pickup or hazard without relying on texture filenames or hardcoded name conventions. The built-in channels are `actors`, `projectiles`, `props`, `triggers`, `effects`, `pickups`, `hazards`, and `sensors`.

```cx
set entity "warden" kind to "boss"
set entity "warden" team to "hostile"
set entity "warden" tags to "boss, armored, phase_two"
set entity "warden" collision layer to "actors, hazards"
set entity "warden" collision mask to "actors, projectiles, props"
set entity "warden" collision projectile mask to "actors, props"
```

Collision is mutually filtered: each side's mask must include one of the other side's layers. `entity "a" overlaps entity "b"` and projectile target checks use the same filter. Projectiles have the `projectiles` layer, inherit their owner's team and projectile mask, and still target the one entity instance named at launch. All default masks are `all` for compatibility. Kind/team/tags are script-queryable metadata; they do not implement team damage or behavior automatically. Room solid-tile collision remains separate. See `features/collision-classifications.md`.

## Entity proximity queries (v0.10.4)

The condition `entity "player" is within 72 pixels of entity "altar"` compares the center of each entity's effective collision rectangle (configured hitbox where present, otherwise sprite rectangle). The radius is inclusive and may be a numeric expression or variable. It requires finite, non-negative pixel units. Missing names or sprite-only objects return false. `not` works as expected.

Proximity checks are deliberately independent of visibility, collision-enabled state, and collision masks: they report geometric distance, not physical contact eligibility. The condition does not choose a nearest object or fire an event. See `features/entity-proximity.md`.

## Named trigger enter/exit events (v0.10.3)

A non-projectile entity on the `actors` layer can enter a non-projectile entity on the `triggers` layer. Both sides must accept each other through their collision masks. The runtime samples their current AABBs after entity/projectile updates and fires named event handlers:

```cx
on entity "player" enter trigger "interaction_area":
    set {inside_interaction_area} to true
    set {actor_name} to {_trigger_actor}
    set {area_name} to {_trigger_entity}

on entity "player" exit trigger "interaction_area":
    set {inside_interaction_area} to false
```

Contacts are tracked by runtime instance identity, so deletion/recreation under the same name closes the old pair and opens the new one. Mask/layer changes, collision disablement, deletion and geometric separation close a contact on the next update. Room-context changes clear the contact set without firing exits across rooms; if the pair still overlaps, its next sample is a fresh enter in the new context. Invisible trigger entities still work. This is sampled overlap, not swept collision; thin areas can be crossed between samples. See `features/trigger-events.md` for the full contract.

## Current limitations (do not assume these exist)

- No rigid-body physics, gravity, acceleration, impulses, knockback or continuous collision detection.
- Named actor/trigger enter/exit events are implemented, but there is no general collision response, projectile-trigger dispatch, radius/region query, or tag/team-based projectile acquisition. Current shape tests are AABB queries; projectile launch still binds to one named target instance.
- No moving-obstacle or entity-avoidance navigation; paths are tile waypoint lists with simple BFS around static solid cells.
- Named animation clips with fixed-tick timing and one-shot completion callbacks are implemented in v0.10.7. Blending, animation graphs, importer metadata, per-frame events/hitboxes, and an animation editor remain future work. When no custom clip is selected, the original eight-direction movement sheet cycle remains the fallback.
- No first-class camera smoothing or camera-to-world mouse conversion API.
- No object pooling, spatial hash, broadphase tree or chunk streaming yet. For large worlds, first profile the existing tile batches and update loops, then add a measured spatial index.
- Static tile solidity is now data-driven through `assets/collision.json` and room-cell overrides, with legacy filename defaults retained when no valid manifest exists. Hurt/opaque material properties and dynamic collision authoring remain future work.

## Recommended next steps

1. Define a reusable interaction-focus policy for multiple in-range objects and script-visible focus changes.
2. Add a seeded random-choice API for reproducible scene staging.
3. Define collision behavior for tweens and moving props before adding physics response.
4. Extend Room Studio with waypoint/path and trigger authoring; collision painting is now available, but the broader editor still needs parity and round-trip tests.

5. Extend the sampled projectile contract to fast entity movement only after designing and testing a continuous or analytical collision alternative.
6. Separate simulation updates from render state, profile before choosing spatial data structures, and benchmark on the target device.

Named clips and one-shot completion events were added in v0.10.7; see `features/animation-clips.md` for exact supported behavior and boundaries.

The core/CLI automated tests cover the initial entity/path/projectile/tile-placement workflow. A release is not complete until the same demo has also been launched through the SFML graphical runtime and movement, collision, camera, projectile cover and HUD have been visually verified on the target platform.


## v0.8.0 room graphics additions

Rooms may use orthogonal or oblique (2:1 diamond) projection. Transparent overlays can occupy `background`, `foreground`, or `above_entities` render layers. `assets/autotiles.json` defines cached edge and corner overlays for grouped terrain. Overlays are visual only; entity collision and tile solidity remain tied to the base room grid.


### Deferred projectile-hit lifecycle (v0.9.5)

Queued impacts from the prior active-room context are discarded if a hit handler successfully enters a room, reloads or unloads the active room. The handler that causes the change finishes, but later handlers for that event and later impacts from the old context do not run. This only cancels impacts already queued in that same projectile update; it does not clear projectiles globally or define their ownership across later ticks. See `features/projectile-room-context.md`.

### Projectile collision precedence (v0.9.4)

The current sampled projectile sweep tests solid tiles before target overlap at each sampled position. The first sampled contact consumes the projectile; wall wins a same-sample tie, while a target reached earlier wins over a wall farther down the path. This is documented and regression-tested in `features/projectile-collision-precedence.md`; it is not an exact continuous-collision solver.


## Suspended script work and room contexts (v0.9.6)

A `wait` continuation captures the active room-context revision when it suspends. Successful entry into a room, reload/unload of the active room, or successful script/runtime reload invalidates continuations from the earlier context. Ready continuations are rechecked before each executes, so one wait that changes rooms invalidates other old-room continuations due on the same tick. If the same handler then reaches another `wait`, that new suspension belongs to the new room. Failed reloads preserve last-good waits; successful reloads replace old-program waits and preserve waits created during candidate startup. These continuations are anonymous RAM-only work; named task IDs and explicit cancellation remain future work. See `features/task-lifecycle.md`.


## Queued projectile events and callback mutation

Projectile hit callbacks are deferred until after the collision traversal. Since v0.9.8, each queued target impact is checked again before damage: the same runtime target instance must still exist, collision must remain enabled, and the target's current hitbox must still overlap the projectile's sampled contact rectangle. A callback that moves the target or its hitbox away, or disables its collision, therefore invalidates later stale impacts already queued for that same update. A small movement that preserves overlap does not. The engine does not retroactively resweep a path or create a new impact when a callback moves another entity into a previously clear trajectory. See `features/projectile-callback-geometry.md`.


## Static room tile collision (v0.10.6)

Room tile collision is resolved separately from entity/entity collision layers and masks. Optional `assets/collision.json` establishes texture defaults; optional per-cell room `tileCollision` values override them. `inherit` uses the texture default, `solid` forces a blocker (even in an empty cell), and `none` forces passability. Older rooms without this matrix continue to use the established texture-name defaults. Transparent overlays are visual-only. The shared resolver is used by solid-tile movement checks and waypoint pathfinding. See `features/room-collision-metadata.md` for the complete format and boundaries.
