# Event context and event-local values

This page is the source-of-truth reference for the extra local values supplied by CX event handlers on the v0.11.0 development branch. The values are ordinary CX locals: read them with {_name}, use them in expressions or interpolation, and copy them into persistent variables only when the information needs to survive the handler.

Every handler invocation starts with a fresh local-variable scope. Event-context locals are populated by the runtime for the event families listed below; they are not global variables and are not saved automatically.

## Event payload reference

### Trigger enter and exit

    on entity "player" enter trigger "door_area":
        set {last_actor} to {_trigger_actor}
        set {last_trigger} to {_trigger_entity}

    on entity "player" exit trigger "door_area":
        set {last_actor} to {_trigger_actor}
        set {last_trigger} to {_trigger_entity}

| Local | Meaning |
|---|---|
| {_trigger_actor} | Script-visible name of the actor entity entering or leaving |
| {_trigger_entity} | Script-visible name of the trigger entity |

The event header names both endpoints. Trigger handlers are pair-specific; they do not currently provide a wildcard “on trigger enter” event. The runtime evaluates only the actor/trigger pairs actually referenced by handlers, rather than scanning every actor against every trigger on every tick. See the trigger event lifecycle page.

### Interaction focus enter and exit

    on entity "player" focus enter entity "ancient_chest":
        set {focused_actor} to {_focus_actor}
        set {focused_target} to {_focus_entity}

| Local | Meaning |
|---|---|
| {_focus_actor} | Script-visible name of the actor whose selected target changed |
| {_focus_entity} | Script-visible name of the selected target |
| {_focused_entity} | Compatibility alias for the same target name as {_focus_entity} |

The actor and target are both named in the header. The two target locals are equivalent; new scripts should prefer {_focus_entity}. See the interaction focus page.

### Projectile hit

    on projectile hit entity "warden":
        set {last_hit_target} to {_hit_entity}
        set {last_hit_projectile} to {_hit_projectile}
        set {last_hit_damage} to {_hit_damage}
        set {warden_health_after_hit} to {entity::warden::health}

| Local | Meaning |
|---|---|
| {_hit_entity} | Script-visible name of the entity that received the accepted hit |
| {_hit_projectile} | Generated projectile's script-visible ID |
| {_hit_damage} | Damage amount applied by this impact |

The hit handler runs after that impact's damage has been applied, so the target's health variable reflects the result of this individual hit. If an earlier same-tick hit removes the target or reduces its health to zero, later queued hits are skipped. See projectile hit ordering.

### Tween completion

    on tween complete for entity "boulder":
        set {arrival_entity} to {_tween_entity}
        set {arrival_x} to {_tween_x}
        set {arrival_y} to {_tween_y}

| Local | Meaning |
|---|---|
| {_tween_entity} | Script-visible name of the entity whose tween completed |
| {_tween_x} | Captured final X coordinate in world pixels |
| {_tween_y} | Captured final Y coordinate in world pixels |

The coordinates are the captured arrival coordinates for that completed tween. See deterministic entity tweens.

### Animation completion

    on animation complete "wake" for entity "player":
        set {finished_animation} to {_animation_name}
        set {finished_frame} to {_animation_frame}
        set {finished_row} to {_animation_row}

| Local | Meaning |
|---|---|
| {_animation_entity} | Script-visible name of the entity whose clip completed |
| {_animation_name} | Name of the completed animation clip |
| {_animation_frame} | Final frame index (zero-based) |
| {_animation_row} | Resolved final row index (zero-based) |

Looping clips do not emit completion events. See named animation clips.

### File attachment

    on file attach:
        set {last_attached_path} to {_attached_file}
        set {drop_x} to {_mouse_x}
        set {drop_y} to {_mouse_y}
        set {drop_button} to {_button_under_cursor}

| Local | Meaning |
|---|---|
| {_attached_file} | Absolute path of the attached file |
| {_mouse_x} | Cursor X coordinate supplied by the desktop file-drop event |
| {_mouse_y} | Cursor Y coordinate supplied by the desktop file-drop event |
| {_button_under_cursor} | ID of the open canvas button under the cursor, or empty text when no button is under it |

The accepted header aliases are “on file attach” and “on file attached”. The file-drop bridge is platform/build dependent; see the mouse-input and file-attachment section of the syntax guide.

## Events without an extra payload

The current on-load, on-tick, periodic, key-press, and canvas-button click handlers receive the normal fresh local scope, but the runtime does not currently populate a separate event-specific payload for them. Use the named event selector in the header (such as the key or button ID) and query current runtime state inside the handler.

There is no universal event object, dictionary of payload values, or generic collision/wildcard trigger event in this branch. Do not assume a local exists unless it is listed on this page or in the corresponding feature contract.

## Design and performance boundary

Event-local names describe what the runtime observed; they do not extend object lifetimes. Sprite/entity object handles remain typed, local/temporary-only values with live-instance identity. Event payload names are text/number locals, not persistent object handles.

## Runtime regression coverage

The runtime payload contract is checked in CTest alongside the subsystem behavior that produces each event:

- Trigger actor/trigger names: `tests/collision_runtime_tests.cpp`.
- Focus actor/target names and the `_focused_entity` compatibility alias: `tests/interaction_focus_tests.cpp`.
- Hit target, projectile ID and applied damage: `tests/projectile_runtime_tests.cpp`.
- Tween entity name and captured final coordinates: `tests/motion_runtime_tests.cpp`.
- Animation entity name, clip name, final frame and resolved row: `tests/animation_runtime_tests.cpp`.
- Attached absolute path, cursor coordinates and the empty button-ID case: `tests/event_context_runtime_tests.cpp`.

These value assertions complement the existing lifecycle checks for contact separation, callback-driven deletion/replacement, playback/tween identity, and room-context changes. The file-attachment test uses the public host API with a temporary regular file; it does not depend on a native desktop file-drop bridge.

Trigger and focus transitions have intentionally specific actor/target selectors. Trigger dispatch iterates the authored pairs and checks their live endpoints, avoiding a full actor-by-trigger Cartesian scan every tick. Focus selection likewise has explicit configured actors and a known per-tick candidate-search cost. A more general event vocabulary may be considered later, but must preserve bounded work, clear filtering semantics, and the existing room/object lifetime guarantees.
