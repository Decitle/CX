# Textured beams and endpoint lighting (v0.10.11)

CX beams are straight, constant-width textured segments authored in world-space pixels. The runtime owns beam identity, room context, properties and fixed-tick lifetime. The renderer receives a renderer-neutral BeamEffect and turns it into a two-triangle quad. The visible beam and its optional endpoint light are separate effects with independent appearance settings.

## Create and remove

Place the beam texture in assets/textures/. Its asset ID is the filename without the extension, following the normal texture-discovery rules.

    create beam "sun shaft" from (320, -80) to (320, 240) using {$beam_strip}
    remove beam "sun shaft"

The beam name is normalized like other named runtime objects. The start and end values are finite world-space pixel coordinates and must not be identical. A beam created while a room is active is associated with that room; it is drawn only while that room is active. A beam created without an active room has global room visibility. The texture must exist in the project's texture registry; the graphical frontend loads it through the shared texture cache.

## Properties

    create beam "sun shaft" from (320, -80) to (320, 240) using {$beam_strip}
    set beam "sun shaft" width to 48
    set beam "sun shaft" opacity to 0.72
    set beam "sun shaft" tint to (255, 230, 190)
    set beam "sun shaft" texture_repeat to 64
    set beam "sun shaft" layer to "background"
    set beam "sun shaft" start to (320, -80)
    set beam "sun shaft" end to (360, 260)
    set beam "sun shaft" lifetime to 2.5

| Property | Range / values | Meaning |
|---|---|---|
| start, end | finite (x, y) pairs; endpoints must differ | Beam centerline endpoints in world pixels |
| width | 1–2048 | Constant full width in world pixels |
| opacity | 0–1 | Vertex alpha multiplier for the textured quad |
| tint | three integer RGB channels, 0–255 | Color multiplied with the sampled texture |
| texture_repeat | 1–4096 | World-pixel distance represented by one horizontal texture repeat |
| layer | background, world, foreground | Draw ordering relative to rooms and entities |
| lifetime | 0–3600 seconds | Fixed-tick lifetime; zero means persistent |

The quad builder normalizes the direction vector, offsets both endpoints by a perpendicular half-width, and emits six vertices. Texture U is beam_length / texture_repeat; V spans 0–1 across the beam width. The frontend uses repeated texture sampling so long beams do not stretch one copy of the image along their entire length. Width is constant from start to end. A property update is validated on a candidate copy before the live beam is replaced, so invalid endpoints or values do not partially mutate the current effect.

## Draw layers

- background: after the room's ambient/light pass and before depth-sorted world entities.
- world: inserted into the same stable Y-depth draw sequence as sprites and foreground-cover tiles, using the projected centerline midpoint as its sort key.
- foreground: after the world entity/cover pass and before explicit above_entities tile overlays.

For oblique rooms, the frontend projects each world endpoint to the room's 2:1 diamond plane before building the screen-space quad. The core geometry itself does not include SFML or other renderer types.

## Endpoint light

    set beam "sun shaft" endpoint_light to true
    set beam "sun shaft" endpoint_light_radius to 180
    set beam "sun shaft" endpoint_light_intensity to 0.72
    set beam "sun shaft" endpoint_light_color to (255, 196, 132)

endpoint_light accepts a boolean-like value. Radius is 1–4096 pixels, intensity is 0–1, and color is three integer RGB channels from 0–255. The light is radial (360 degrees) and centered at the projected end point. It is appended to the existing additive light mesh and participates in the normal light geometry cache; beam create/edit/remove/expiry advances the beam revision so the cache is rebuilt when needed. Endpoint light only appears while engine lighting is enabled. It is independent of beam tint and opacity: changing the visible beam does not implicitly change the light.

## Lifetime and runtime behavior

A positive lifetime advances in fixed simulation time and removes the beam when it expires. Zero is persistent until remove beam or the runtime is reloaded. A successful script/runtime reload replaces staged beam state with the staged script's result; a failed transactional load leaves the live runtime state unchanged. The beam is runtime state, not a room JSON or saved-game entity, and is not serialized to persistent variables.

## Example

See examples/textured_beam.cx. It expects assets/textures/beam_strip.png in the game project. The sample demonstrates tint/opacity, texture repetition, draw order, endpoint lighting, endpoint editing and lifetime syntax.

## Deliberate limits

This is a lightweight 2D textured quad plus an optional radial endpoint light. It is not volumetric lighting, a shader effect, bloom, occlusion shadows, or physically based scattering. Beams are straight and constant-width; there are no curved paths, taper, procedural noise, animated UV speed, independent start/end lights, tile occlusion, collision or hit detection, or renderer-neutral falloff profile. The world layer uses one midpoint depth key rather than splitting a beam around intersecting entities. Texture and light performance should be benchmarked with the actual target scene before making throughput claims.
