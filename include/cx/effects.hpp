#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace cx {

// A world-space point in the same pixel coordinate system used by sprites.
// Rendering backends may project it into their own room/view coordinate system.
struct EffectPoint {
    float x{0.0F};
    float y{0.0F};
};

enum class BeamDrawLayer : std::uint8_t {
    Background, // After room lighting, before depth-sorted entities.
    World,      // Participates in world Y-depth ordering with entities.
    Foreground  // After entities, before explicit above-entity tile overlays.
};

// Renderer-neutral description of a textured beam segment. The core owns its
// lifetime and authoring data; a frontend turns it into a textured quad.
struct BeamEffect {
    std::string name;
    std::string roomName; // Empty means global; otherwise visible only in this room.
    std::string textureId;
    EffectPoint start{};
    EffectPoint end{};
    float width{32.0F};
    float opacity{1.0F};
    float textureRepeatLength{64.0F};
    BeamDrawLayer layer{BeamDrawLayer::Background};
    float lifetimeSeconds{0.0F}; // Zero is persistent; positive values expire automatically.
    float ageSeconds{0.0F};
    std::uint8_t red{255};
    std::uint8_t green{255};
    std::uint8_t blue{255};

    // The endpoint light is independent of the visible textured quad. A frontend
    // can render it through its normal light pass without changing the light map.
    bool endpointLightEnabled{false};
    float endpointLightRadius{144.0F};
    float endpointLightIntensity{0.65F};
    std::uint8_t endpointLightRed{255};
    std::uint8_t endpointLightGreen{205};
    std::uint8_t endpointLightBlue{145};
};

struct BeamVertex {
    EffectPoint position{};
    float u{0.0F}; // Normalized texture coordinates; frontend scales by texture dimensions.
    float v{0.0F};
};

struct BeamQuad {
    std::array<BeamVertex, 6> vertices{};
    float length{0.0F};
};

// Builds two triangles around the segment's perpendicular. Texture U repeats
// according to textureRepeatLength; V spans the full width. Degenerate or
// non-finite inputs are rejected without modifying the caller's output.
[[nodiscard]] bool buildBeamQuad(const BeamEffect& beam, BeamQuad& output) noexcept;

} // namespace cx
