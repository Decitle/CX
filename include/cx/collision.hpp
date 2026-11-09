#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cx {

// Axis-aligned bounds are shared by projectile, trigger and movement collision.
// Edges that merely touch are not overlapping; this lets an object rest flush
// against a wall without being treated as penetrating it.
struct CollisionAabb {
    float left{0.0F};
    float top{0.0F};
    float right{0.0F};
    float bottom{0.0F};
};

[[nodiscard]] bool collisionAabbOverlaps(const CollisionAabb& left,
                                          const CollisionAabb& right) noexcept;
// Tests the full straight-line sweep of one AABB against a stationary AABB.
// Unlike checking only the destination, this cannot tunnel through a thin blocker.
[[nodiscard]] bool sweptCollisionAabbOverlaps(const CollisionAabb& movingStart,
                                               float deltaX, float deltaY,
                                               const CollisionAabb& obstacle) noexcept;

enum class TweenCollisionResponse { Ignore, Stop, Slide };
[[nodiscard]] std::optional<TweenCollisionResponse> parseTweenCollisionResponse(
    std::string_view text);
[[nodiscard]] std::string_view tweenCollisionResponseName(TweenCollisionResponse response) noexcept;

// Named collision channels are a compact bitset, not runtime objects. They let
// authors opt entities into broad interaction groups without adding per-frame
// string comparisons to collision checks.
inline constexpr std::uint32_t CollisionActors     = 1U << 0U;
inline constexpr std::uint32_t CollisionProjectiles = 1U << 1U;
inline constexpr std::uint32_t CollisionProps      = 1U << 2U;
inline constexpr std::uint32_t CollisionTriggers   = 1U << 3U;
inline constexpr std::uint32_t CollisionEffects    = 1U << 4U;
inline constexpr std::uint32_t CollisionPickups    = 1U << 5U;
inline constexpr std::uint32_t CollisionHazards    = 1U << 6U;
inline constexpr std::uint32_t CollisionSensors    = 1U << 7U;
inline constexpr std::uint32_t AllCollisionChannels =
    CollisionActors | CollisionProjectiles | CollisionProps | CollisionTriggers |
    CollisionEffects | CollisionPickups | CollisionHazards | CollisionSensors;

// Parse a comma-separated list such as "actors,projectiles". `all` and `none`
// are accepted for masks; `all` is also useful when restoring compatibility.
// The caller chooses whether a zero result is meaningful for a layer or mask.
[[nodiscard]] std::optional<std::uint32_t> parseCollisionChannels(
    std::string_view text, std::string& error);

// Filtering is intentionally symmetric: both objects must opt into the other's
// layer. A default all-channel mask preserves the pre-filter collision behavior.
[[nodiscard]] constexpr bool collisionFiltersAllow(
    std::uint32_t leftLayer, std::uint32_t leftMask,
    std::uint32_t rightLayer, std::uint32_t rightMask) noexcept {
    return (leftMask & rightLayer) != 0U && (rightMask & leftLayer) != 0U;
}

// Kind/team/tag identifiers are metadata, not collision masks. Keep their syntax
// deliberately small and portable so projects behave consistently across platforms.
[[nodiscard]] bool validEntityClassification(std::string_view value) noexcept;

} // namespace cx
