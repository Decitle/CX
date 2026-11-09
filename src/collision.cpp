#include "cx/collision.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <utility>

namespace cx {

bool collisionAabbOverlaps(const CollisionAabb& left, const CollisionAabb& right) noexcept {
    return left.left < right.right && left.right > right.left &&
           left.top < right.bottom && left.bottom > right.top;
}

bool sweptCollisionAabbOverlaps(const CollisionAabb& movingStart,
                                float deltaX, float deltaY,
                                const CollisionAabb& obstacle) noexcept {
    const float width = movingStart.right - movingStart.left;
    const float height = movingStart.bottom - movingStart.top;
    if (!(width > 0.0F) || !(height > 0.0F) ||
        !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(deltaX) || !std::isfinite(deltaY)) {
        return false;
    }

    // If the objects already overlap, permit incremental movement only along
    // the shortest deterministic separation axis. Requiring the full escape in
    // one tick would trap an entity that is recovering from a small overlap.
    // The direction check still prevents it from crossing through the blocker
    // and emerging on the opposite side while the initial overlap is unresolved.
    if (collisionAabbOverlaps(movingStart, obstacle)) {
        const double separateLeft = static_cast<double>(movingStart.right) - obstacle.left;
        const double separateRight = static_cast<double>(obstacle.right) - movingStart.left;
        const double separateUp = static_cast<double>(movingStart.bottom) - obstacle.top;
        const double separateDown = static_cast<double>(obstacle.bottom) - movingStart.top;
        const double smallest = std::min({separateLeft, separateRight, separateUp, separateDown});
        if (smallest == separateLeft) return !(deltaX < 0.0F);
        if (smallest == separateRight) return !(deltaX > 0.0F);
        if (smallest == separateUp) return !(deltaY < 0.0F);
        return !(deltaY > 0.0F);
    }

    // Minkowski expansion converts a swept rectangle-vs-rectangle test into a
    // point segment-vs-open-rectangle test for the moving AABB's top-left corner.
    const double minX = static_cast<double>(obstacle.left) - width;
    const double maxX = static_cast<double>(obstacle.right);
    const double minY = static_cast<double>(obstacle.top) - height;
    const double maxY = static_cast<double>(obstacle.bottom);
    const double x = movingStart.left;
    const double y = movingStart.top;
    const double dx = deltaX;
    const double dy = deltaY;

    double enter = -std::numeric_limits<double>::infinity();
    double exit = std::numeric_limits<double>::infinity();
    const auto clipAxis = [&](double position, double delta, double minimum,
                              double maximum, double& axisEnter, double& axisExit) {
        if (delta == 0.0) {
            if (!(position > minimum && position < maximum)) return false;
            axisEnter = -std::numeric_limits<double>::infinity();
            axisExit = std::numeric_limits<double>::infinity();
            return true;
        }
        const double first = (minimum - position) / delta;
        const double second = (maximum - position) / delta;
        axisEnter = std::min(first, second);
        axisExit = std::max(first, second);
        return true;
    };

    double xEnter = 0.0;
    double xExit = 0.0;
    double yEnter = 0.0;
    double yExit = 0.0;
    if (!clipAxis(x, dx, minX, maxX, xEnter, xExit) ||
        !clipAxis(y, dy, minY, maxY, yEnter, yExit)) {
        return false;
    }
    enter = std::max(xEnter, yEnter);
    exit = std::min(xExit, yExit);

    // Strict intervals match the engine's overlap rule: edge/corner grazing is
    // allowed, while any positive-duration intersection blocks the movement.
    return enter < exit && exit > 0.0 && enter < 1.0;
}

namespace {

std::string normalize(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    std::string result(value.substr(first, last - first));
    for (char& character : result) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return result;
}

struct ChannelName {
    std::string_view name;
    std::uint32_t bit;
};

constexpr std::array<ChannelName, 8> channels{{
    {"actors", CollisionActors},
    {"projectiles", CollisionProjectiles},
    {"props", CollisionProps},
    {"triggers", CollisionTriggers},
    {"effects", CollisionEffects},
    {"pickups", CollisionPickups},
    {"hazards", CollisionHazards},
    {"sensors", CollisionSensors},
}};

} // namespace

std::optional<TweenCollisionResponse> parseTweenCollisionResponse(std::string_view text) {
    const std::string value = normalize(text);
    if (value == "ignore") return TweenCollisionResponse::Ignore;
    if (value == "stop") return TweenCollisionResponse::Stop;
    if (value == "slide") return TweenCollisionResponse::Slide;
    return std::nullopt;
}

std::string_view tweenCollisionResponseName(TweenCollisionResponse response) noexcept {
    switch (response) {
        case TweenCollisionResponse::Ignore: return "ignore";
        case TweenCollisionResponse::Stop: return "stop";
        case TweenCollisionResponse::Slide: return "slide";
    }
    return "ignore";
}

std::optional<std::uint32_t> parseCollisionChannels(std::string_view text, std::string& error) {
    error.clear();
    const std::string normalized = normalize(text);
    if (normalized == "all") return AllCollisionChannels;
    if (normalized == "none") return 0U;
    if (normalized.empty()) {
        error = "At least one collision channel is required, or use `none`.";
        return std::nullopt;
    }

    std::uint32_t result = 0U;
    std::size_t start = 0;
    while (start <= normalized.size()) {
        const std::size_t comma = normalized.find(',', start);
        const std::size_t end = comma == std::string::npos ? normalized.size() : comma;
        const std::string_view token(normalized.data() + start, end - start);
        std::size_t first = 0;
        while (first < token.size() && std::isspace(static_cast<unsigned char>(token[first]))) ++first;
        std::size_t last = token.size();
        while (last > first && std::isspace(static_cast<unsigned char>(token[last - 1]))) --last;
        const std::string_view name = token.substr(first, last - first);
        if (name.empty()) {
            error = "Collision channel lists cannot contain an empty item.";
            return std::nullopt;
        }
        bool found = false;
        for (const auto& channel : channels) {
            if (channel.name == name) {
                result |= channel.bit;
                found = true;
                break;
            }
        }
        if (!found) {
            error = "Unknown collision channel `" + std::string(name) + "`. Supported channels: actors, projectiles, props, triggers, effects, pickups, hazards, sensors, all, none.";
            return std::nullopt;
        }
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return result;
}

bool validEntityClassification(std::string_view value) noexcept {
    if (value.empty() || value.size() > 40) return false;
    for (const unsigned char character : value) {
        if (!(std::isalnum(character) || character == '_' || character == '-')) return false;
    }
    return true;
}

} // namespace cx
