#include "cx/effects.hpp"

#include <cmath>

namespace cx {

bool buildBeamQuad(const BeamEffect& beam, BeamQuad& output) noexcept {
    const float values[] = {beam.start.x, beam.start.y, beam.end.x, beam.end.y,
                            beam.width, beam.textureRepeatLength};
    for (const float value : values) {
        if (!std::isfinite(value)) return false;
    }
    if (beam.width <= 0.0F || beam.textureRepeatLength <= 0.0F) return false;

    const float dx = beam.end.x - beam.start.x;
    const float dy = beam.end.y - beam.start.y;
    const float lengthSquared = dx * dx + dy * dy;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-8F) return false;
    const float length = std::sqrt(lengthSquared);
    if (!std::isfinite(length)) return false;

    const float halfWidth = beam.width * 0.5F;
    const float normalX = -dy / length * halfWidth;
    const float normalY = dx / length * halfWidth;
    if (!std::isfinite(normalX) || !std::isfinite(normalY)) return false;

    const EffectPoint startLeft{beam.start.x + normalX, beam.start.y + normalY};
    const EffectPoint endLeft{beam.end.x + normalX, beam.end.y + normalY};
    const EffectPoint endRight{beam.end.x - normalX, beam.end.y - normalY};
    const EffectPoint startRight{beam.start.x - normalX, beam.start.y - normalY};
    const float endU = length / beam.textureRepeatLength;
    if (!std::isfinite(endU)) return false;

    BeamQuad candidate;
    candidate.length = length;
    candidate.vertices = {{
        {startLeft, 0.0F, 0.0F},
        {endLeft, endU, 0.0F},
        {endRight, endU, 1.0F},
        {startLeft, 0.0F, 0.0F},
        {endRight, endU, 1.0F},
        {startRight, 0.0F, 1.0F}
    }};
    output = candidate;
    return true;
}

} // namespace cx
