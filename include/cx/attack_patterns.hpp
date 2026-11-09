#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cx {

// v1 pattern shapes describe projectile travel directions, not animated groups.
// Radial emits evenly spaced directions around a full circle; Fan emits directions
// across an arc centered on the source-to-target aim direction.
enum class ProjectilePatternKind { Radial, Fan };

// Build angles in radians. Keeping this math independent from Runtime makes it easy
// to test and reuse without opening a window or constructing a whole game project.
// A maximum of 64 directions per command protects accidental scripts from spawning
// unbounded numbers of projectiles in a single tick.
[[nodiscard]] bool buildProjectilePatternAngles(
    ProjectilePatternKind kind,
    std::size_t count,
    float aimRadians,
    float spreadDegrees,
    std::vector<float>& anglesRadians,
    std::string& error);

} // namespace cx
