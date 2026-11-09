# CX Engine settings reference (v0.10.14)

This is the canonical reference for all **21** settings in `EngineSettings` for CX Engine v0.10.14. Settings are optional: the built-in defaults are already chosen to make a new project usable without a long configuration block.

A setting can be supplied in the project's `config.json` file or changed by a `.cx` statement in the reserved `engine::` namespace:

```json
{
  "window_title": "My Game",
  "window_width": 1280,
  "window_height": 720,
  "resolution_mode": "responsive",
  "vsync": true,
  "target_fps": 0,
  "tick_rate": 60,
  "texture_filter": "nearest",
  "lighting_enabled": true,
  "ambient_light": 0.72,
  "ui_scale": 1.0
}
```

The script equivalent is:

```cx
set engine::window_title to "My Game"
set engine::console_enabled to false
```

In `config.json`, a key may be written with or without the `engine::` prefix. Use one ordinary key/value pair per line, with a quoted string, `true`/`false`, or an unquoted number. The current reader is deliberately a small line-oriented settings reader, not a general-purpose JSON implementation: do not use comments, arrays, nested objects, or put multiple properties on one line. Duplicate keys are reported. A malformed or unsupported value is reported with its config file and line instead of silently changing the setting.

## When a setting is read

The order is:

1. `Runtime` constructs `EngineSettings` using the defaults in `include/cx/runtime.hpp`.
2. `Runtime::loadScripts(true)` reads `config.json`, if it exists.
3. The sorted `.cx` script files are parsed and their top-level bootstrap statements run. These can override the config values before the graphical window is created.
4. Event handlers may change settings while the game is running. The C++ runtime stores the new value during that statement. The SFML graphical host reads most settings again on the next host/render loop and applies the change there.

**Important distinction:** accepting a setting in the core does not mean every host can display its effect. `cx-cli` can validate and run the same scripts, but it has no window or renderer. Window, font, texture, UI, camera, and lighting settings therefore have no visible effect in the CLI. `tick_rate` still matters to the CLI because it controls how often the interactive CLI advances logic ticks.

## Window and pacing settings

| Setting | Default | Accepted value | Unit / meaning | When it takes effect |
|---|---:|---|---|---|
| `engine::window_title` | `"CX Engine \| .cx Runtime"` | Text string | Operating-system window title. No explicit length limit is enforced by the runtime. | At startup; later changes update the window title on the next graphical host loop. |
| `engine::window_width` | `1280` | Whole number `320–7680` | Requested window width in physical pixels. | At startup; later changes resize the window on the next graphical host loop. In `fixed` mode, user resizing is also corrected back to the configured dimensions. |
| `engine::window_height` | `720` | Whole number `240–4320` | Requested window height in physical pixels. | Same as `window_width`. |
| `engine::resolution_mode` | `"responsive"` | `"responsive"` or `"fixed"` | `responsive` accepts user resizing. `fixed` restores the configured width and height when the operating system reports a resize. | At startup; changes are observed by the next graphical loop and affect subsequent resize events. |
| `engine::vsync` | `true` | Boolean | Requests vertical synchronization with the display. A compositor or driver may affect the result. | Set at startup; changes are applied on the next graphical loop. |
| `engine::target_fps` | `0` | Whole number `0–1000` | Maximum rendered frames per second requested from SFML. `0` disables the explicit frame cap; VSync can still limit rendering. | Set at startup; changes are applied on the next graphical loop. Does not change the logic tick rate. |
| `engine::tick_rate` | `60` | Whole number `1–1000` | Logic updates per second. A tick is one call to `Runtime::tick()`. | Used by the next host scheduling step and by subsequent runtime tick-based timing. Changing it changes the real-time pace of tick-count-based systems; it is not a render-FPS setting. |

The dimensions are requests, not guarantees that the desktop can fit the window on every display. Window managers, display scaling, and screen bounds may constrain the result. `window_width`/`window_height` are not the same as the UI reference dimensions below.

## UI and world-view settings

| Setting | Default | Accepted value | Unit / meaning | When it takes effect |
|---|---:|---|---|---|
| `engine::ui_scale` | `1.0` | Finite number `0.25–4.0` | Multiplier for canvas element sizes and font sizes after viewport/reference scaling. It does not change world physics or tile coordinates. | On the next canvas layout resolution/draw. |
| `engine::ui_reference_width` | `1280` | Whole number `320–7680` | Width of the reference coordinate space used by canvas UI and generated background/layout geometry. | On the next graphical loop/layout resolution. It is not a request to resize the physical window. |
| `engine::ui_reference_height` | `720` | Whole number `240–4320` | Height of the reference coordinate space used by canvas UI and generated background/layout geometry. | On the next graphical loop/layout resolution. |
| `engine::world_view_height_tiles` | `22.5` | Finite number `6–100` | How many active-room tiles fit vertically in the world view. With a 32-pixel tile, `22.5` tiles corresponds to 720 world pixels vertically. | On the next world-view calculation/render frame. |
| `engine::camera_follow` | `"none"` | Text string; empty text becomes `"none"` | Name of a visible sprite/entity for the camera to follow, or `"none"` to center the view without a target. The name is normalized to lowercase. A name that does not currently identify a visible sprite is accepted but will not move the camera. | On the next world-view calculation/render frame. The target is checked again each frame. |

World projection and screen UI are separate. The world camera decides which part of the active room is visible. Canvas UI is resolved in screen space against the actual viewport. The renderer preserves the apparent shape of world units rather than stretching tiles to match every aspect ratio. Wide windows show more world horizontally; narrow windows show less.

## Pixel-art, lighting, and effect settings

| Setting | Default | Accepted value | Unit / meaning | When it takes effect |
|---|---:|---|---|---|
| `engine::texture_filter` | `"nearest"` | `"nearest"` or `"linear"` | `nearest` keeps pixel-art edges crisp. `linear` smooths resampling and can blur small pixel art. | At startup for newly loaded textures; changes update the existing texture cache on the next graphical loop. |
| `engine::pixel_perfect` | `true` | Boolean | Rounds selected render positions and light/beam coordinates to whole pixels. It reduces sub-pixel shimmer but does not force every scale factor to be an integer. | On the next render pass. Cached light geometry is rebuilt when this value changes. |
| `engine::lighting_enabled` | `true` | Boolean | Enables the ambient-darkening pass and the lightweight radial/cone light meshes. | On the next render pass. |
| `engine::ambient_light` | `0.72` | Finite number `0–1` | Overall ambient brightness: `0` is darkest and `1` removes ambient darkening. This is not a physically based exposure value. | On the next render pass. |
| `engine::lighting_quality` | `2` | Whole number `1–3` | Subdivision quality for radial/cone light meshes. Higher values create smoother geometry with more vertices and extra rendering work. | On the next graphical loop; cached light geometry is invalidated and rebuilt as needed. |
| `engine::screen_flash` | `0` | Finite number `0–1` | Strength of the pale screen-flash overlay: `0` is off and `1` is the strongest request. | Drawn on the next frame. The value is multiplied by `0.82` on every logic tick and set to zero once it drops below `0.01`, so it is a short-lived effect rather than a persistent brightness setting. |
| `engine::show_debug_overlay` | `false` | Boolean | Draws the optional debug/grid overlay. | On the next render pass. |
| `engine::console_enabled` | `true` | Boolean | Enables the in-window developer terminal and its `Ctrl+Tab` shortcut. It does not disable core logging or source diagnostics. | On the next graphical loop/input event. Disabling it closes and unfocuses the drawer. It does not disable the separate `cx-cli` terminal. |
| `engine::font_path` | `""` (automatic) | Text path; no explicit length limit is enforced | Optional `.ttf`, `.otf`, or `.ttc` font path. Relative paths are resolved from the project folder; absolute paths are also accepted. Empty text asks the frontend to search `assets/fonts/`, common project font locations, and platform fallbacks. | **Startup only.** The SFML font object is loaded once after scripts/bootstrap and before the main window loop. Restart the graphical executable to apply a later change; `/reload` alone does not reload the font. |

### What the lighting is (and is not)

The current renderer uses a lightweight 2D mesh-lighting effect, not a programmable GLSL shader or a volumetric renderer. It draws the room, applies a translucent ambient-darkening rectangle, then draws radial or directional/conical light meshes. Named lights have tile coordinates, a rendered-world-pixel radius, intensity, RGB color, direction in degrees, and beam angle. Direction `0°` points right and `90°` points down; a `360°` beam covers all directions. `lighting_quality` changes mesh subdivision.

Walls do not cast shadows or automatically block lights. There is no normal mapping or physically based material system. Texture transparency is preserved; lighting affects the rendered world pass.

Example:

```cx
on load:
    set engine::lighting_enabled to true
    set engine::ambient_light to 0.62
    set engine::lighting_quality to 2
    create light "shrine" at (19, 3)
    set light "shrine" radius to 280
    set light "shrine" intensity to 0.9
    set light "shrine" color to (255, 190, 105)
    set light "shrine" direction to 90
    set light "shrine" beam to 42

on press key "F":
    set engine::screen_flash to 1
```

## Reload and diagnostics

- `/reload` parses the discovered `.cx` scripts into a staged candidate before replacing the active program. On failure, the previous live runtime remains active and diagnostics explain the failed candidate. On success, a new runtime session is installed; see [`features/script-reload.md`](features/script-reload.md) for the precise preserve/reset matrix.
- `reload room "name"` validates the replacement room file before swapping it. If JSON or texture references are invalid, the currently loaded room, active-room selection, entrance and revision remain unchanged.
- The graphical terminal keeps up to 300 diagnostic display lines. The combined visible drawer history is capped at 500 entries/lines after merging runtime logs and formatted diagnostics. The core runtime independently retains at most 500 log entries. These are bounded presentation histories, not permanent log files.
- Errors open/focus the graphical drawer automatically. `PageUp` and `PageDown` scroll the drawer history. A terminal command starts by clearing the previous command's diagnostic list so stale errors are not presented as a new command's result.
- Room Studio validates room grids, texture IDs, overlays, scale values and entrance coordinates before saving. Its local server writes a unique temporary file beside the room file and replaces the destination only after the write completes.

## Inspecting and changing settings

- **Startup defaults:** use the optional project-root `config.json`.
- **Project overrides:** put `set engine::setting_name to value` at top level in any discovered `scripts/*.cx` file. Every `.cx` file under `scripts/` is loaded in stable sorted path order; `main.cx` is a convention, not a special entry point.
- **Runtime changes:** set supported options from event handlers. The settings are stored immediately by the core; the graphical host applies visible effects at the times described in the tables above.
- **Inspection:** open the graphical terminal with `Ctrl+Tab` and enter `/settings` to print current runtime values. Enter `/help` for all runtime-console commands. CLI-only `/quit`, `quit`, and `exit` are handled by `cx-cli`, not by the game runtime.
- **Validation:** values are checked in `Runtime::applySetting`. Whole-number settings reject fractions; numeric settings reject NaN/infinity and values outside their listed range; boolean settings require actual booleans; text settings require strings.

A setting being syntactically accepted does not prove its rendered effect on every system. For example, a valid `font_path` is not read again during play, and rendering options cannot be visually inspected in the dependency-free CLI. Use the graphical target for those checks.
