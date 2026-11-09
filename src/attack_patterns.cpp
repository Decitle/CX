#include "cx/attack_patterns.hpp"

#include <cmath>
#include <numbers>

namespace cx {

bool buildProjectilePatternAngles(
    ProjectilePatternKind kind,
    std::size_t count,
    float aimRadians,
    float spreadDegrees,
    std::vector<float>& anglesRadians,
    std::string& error) {
    anglesRadians.clear();
    error.clear();

    if (count == 0 || count > 64) {
        error = "Projectile pattern count must be between 1 and 64.";
        return false;
    }
    if (!std::isfinite(aimRadians)) {
        error = "Projectile pattern aim angle must be finite.";
        return false;
    }

    constexpr float fullTurn = 2.0F * std::numbers::pi_v<float>;
    constexpr float degreesToRadians = std::numbers::pi_v<float> / 180.0F;

    anglesRadians.reserve(count);
    if (kind == ProjectilePatternKind::Radial) {
        // Start at the aimed direction, then distribute bullets evenly around it.
        // This makes a radial burst useful even when the count is not divisible by 4.
        const float step = fullTurn / static_cast<float>(count);
        for (std::size_t i = 0; i < count; ++i) {
            anglesRadians.push_back(aimRadians + step * static_cast<float>(i));
        }
        return true;
    }

    if (kind != ProjectilePatternKind::Fan) {
        error = "Unknown projectile pattern kind.";
        anglesRadians.clear();
        return false;
    }
    if (!std::isfinite(spreadDegrees) || spreadDegrees < 0.0F || spreadDegrees > 180.0F) {
        error = "Fan spread must be between 0 and 180 degrees.";
        return false;
    }

    if (count == 1) {
        // A one-shot fan remains aimed at the target instead of leaning to one edge.
        anglesRadians.push_back(aimRadians);
        return true;
    }

    const float halfSpread = spreadDegrees * degreesToRadians * 0.5F;
    const float start = aimRadians - halfSpread;
    const float step = (halfSpread * 2.0F) / static_cast<float>(count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        anglesRadians.push_back(start + step * static_cast<float>(i));
    }
    return true;
}

} // namespace cx
