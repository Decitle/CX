#include "cx/runtime.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace cx {

void Runtime::fireTweenComplete(std::string_view entityName, std::uint64_t instanceId,
                                std::uint64_t roomRevision, float finalX, float finalY) {
    const std::string name(entityName);
    const auto stillOwnsEntity = [&]() {
        const auto found = sprites_.find(name);
        return roomContextRevision_ == roomRevision && found != sprites_.end() &&
               found->second.entity && found->second.runtimeInstanceId == instanceId;
    };

    // Completion callbacks are one-shot and belong to the exact entity instance
    // and room context that finished the motion. A callback may delete or replace
    // the entity, or transition rooms, so revalidate before every handler.
    for (const auto& handler : program_.handlers) {
        if (handler.type != EventType::TweenComplete || handler.selector != name) continue;
        if (!stillOwnsEntity()) break;
        ExecutionContext context;
        context.locals["_tween_entity"] = Value(name);
        context.locals["_tween_x"] = Value(static_cast<double>(finalX));
        context.locals["_tween_y"] = Value(static_cast<double>(finalY));
        executeStatements(handler.body, context);
        savePersistentVariables();
        if (!stillOwnsEntity()) break;
    }
}

void Runtime::fireAnimationComplete(const AnimationCompletion& completion) {
    const auto stillOwnsPlayback = [&]() {
        const auto found = sprites_.find(completion.entityName);
        return roomContextRevision_ == completion.roomContextRevision &&
               found != sprites_.end() && found->second.entity && !found->second.projectile &&
               found->second.runtimeInstanceId == completion.entityInstanceId &&
               found->second.animationPlaybackGeneration == completion.playbackGeneration &&
               found->second.currentAnimation == completion.animationName &&
               !found->second.animationPlaying;
    };

    // Completion events are tied to the exact entity instance and playback
    // generation. A callback that restarts/stops the clip, replaces the entity,
    // or enters another room invalidates the remainder of this event batch.
    for (const auto& handler : program_.handlers) {
        if (handler.type != EventType::AnimationComplete ||
            handler.selector != completion.entityName ||
            handler.secondarySelector != completion.animationName) continue;
        if (!stillOwnsPlayback()) break;

        ExecutionContext context;
        context.locals["_animation_entity"] = Value(completion.entityName);
        context.locals["_animation_name"] = Value(completion.animationName);
        context.locals["_animation_frame"] = Value(completion.finalFrame);
        context.locals["_animation_row"] = Value(completion.finalRow);
        executeStatements(handler.body, context);
        savePersistentVariables();
        if (!stillOwnsPlayback()) break;
    }
}

bool Runtime::movementSegmentBlocked(
    const SpriteObject& mover, float fromX, float fromY, float toX, float toY,
    const std::vector<MovementBlockerSnapshot>& blockers) const {
    if (!mover.collisionEnabled) return false;
    if (!std::isfinite(fromX) || !std::isfinite(fromY) ||
        !std::isfinite(toX) || !std::isfinite(toY)) {
        return true;
    }

    const CollisionAabb start = spriteCollisionBoundsAt(mover, fromX, fromY);
    const float deltaX = toX - fromX;
    const float deltaY = toY - fromY;
    const CollisionAabb finish = spriteCollisionBoundsAt(mover, toX, toY);
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY) ||
        !std::isfinite(start.left) || !std::isfinite(start.top) ||
        !std::isfinite(start.right) || !std::isfinite(start.bottom) ||
        !std::isfinite(finish.left) || !std::isfinite(finish.top) ||
        !std::isfinite(finish.right) || !std::isfinite(finish.bottom)) {
        return true;
    }
    const float sweptLeft = std::min(start.left, finish.left);
    const float sweptTop = std::min(start.top, finish.top);
    const float sweptRight = std::max(start.right, finish.right);
    const float sweptBottom = std::max(start.bottom, finish.bottom);

    // The active room's outer edge is a solid boundary for collision-aware tweens.
    // A segment is linear, so checking both endpoint extents covers the full sweep.
    if (const auto* room = rooms_.activeRoom()) {
        const float tileSize = static_cast<float>(std::max(1, room->tileSize));
        const float worldWidth = static_cast<float>(room->width) * tileSize;
        const float worldHeight = static_cast<float>(room->height) * tileSize;
        if (sweptLeft < 0.0F || sweptTop < 0.0F ||
            sweptRight > worldWidth || sweptBottom > worldHeight) {
            return true;
        }

        // Only enumerate room cells touched by the swept rectangle's broad phase.
        // The exact segment-vs-tile test below removes diagonal-corner false positives.
        const int firstX = std::max(0, static_cast<int>(std::floor(sweptLeft / tileSize)));
        const int firstY = std::max(0, static_cast<int>(std::floor(sweptTop / tileSize)));
        const int lastX = std::min(room->width - 1,
            static_cast<int>(std::floor(std::max(sweptLeft, sweptRight - 0.001F) / tileSize)));
        const int lastY = std::min(room->height - 1,
            static_cast<int>(std::floor(std::max(sweptTop, sweptBottom - 0.001F) / tileSize)));
        for (int tileY = firstY; tileY <= lastY; ++tileY) {
            for (int tileX = firstX; tileX <= lastX; ++tileX) {
                if (!room->isSolidTile(tileX, tileY, rooms_.textures())) continue;
                const CollisionAabb tileBounds{
                    static_cast<float>(tileX) * tileSize,
                    static_cast<float>(tileY) * tileSize,
                    static_cast<float>(tileX + 1) * tileSize,
                    static_cast<float>(tileY + 1) * tileSize};
                if (sweptCollisionAabbOverlaps(start, deltaX, deltaY, tileBounds)) return true;
            }
        }
    }

    // Only entities explicitly marked as movement blockers take part. Triggers,
    // interaction-focus targets and decorative entities remain non-solid by default.
    for (const auto& blocker : blockers) {
        if (blocker.instanceId == mover.runtimeInstanceId) continue;
        if (!collisionFiltersAllow(mover.collisionLayer, mover.collisionMask,
                                   blocker.collisionLayer, blocker.collisionMask)) {
            continue;
        }
        if (sweptCollisionAabbOverlaps(start, deltaX, deltaY, blocker.bounds)) return true;
    }
    return false;
}

void Runtime::updateEntityTweens() {
    pendingTweenCompletions_.clear();
    if (pendingTweenCompletions_.capacity() < entityTweens_.size())
        pendingTweenCompletions_.reserve(entityTweens_.size());

    if (entityTweens_.empty()) return;

    // Do not scan or allocate a blocker snapshot for legacy ignore-only tweens.
    // This keeps their pre-v0.10.10 update cost effectively unchanged.
    bool needsCollisionSnapshot = false;
    for (const auto& [name, tween] : entityTweens_) {
        const auto entity = sprites_.find(name);
        if (entity != sprites_.end() && entity->second.entity &&
            entity->second.runtimeInstanceId == tween.entityInstanceId &&
            entity->second.collisionEnabled &&
            entity->second.tweenCollisionResponse != TweenCollisionResponse::Ignore) {
            needsCollisionSnapshot = true;
            break;
        }
    }

    // Snapshot blockers once per simulation tick. Every tween therefore sees the
    // same world state, rather than depending on which entity name sorts first.
    std::vector<MovementBlockerSnapshot> blockers;
    if (needsCollisionSnapshot) {
        const auto blockerCount = static_cast<std::size_t>(std::count_if(
            sprites_.begin(), sprites_.end(), [](const auto& entry) {
                const SpriteObject& object = entry.second;
                return object.entity && !object.projectile && object.collisionEnabled && object.blocksMovement;
            }));
        blockers.reserve(blockerCount);
        for (const auto& [name, object] : sprites_) {
            (void)name;
            if (!object.entity || object.projectile || !object.collisionEnabled || !object.blocksMovement)
                continue;
            blockers.push_back({object.runtimeInstanceId, object.collisionLayer,
                                object.collisionMask, spriteCollisionBounds(object)});
        }
    }

    for (auto it = entityTweens_.begin(); it != entityTweens_.end();) {
        auto entity = sprites_.find(it->first);
        const EntityTween& tween = it->second;
        if (tween.roomContextRevision != roomContextRevision_ || entity == sprites_.end() ||
            !entity->second.entity || entity->second.runtimeInstanceId != tween.entityInstanceId) {
            it = entityTweens_.erase(it);
            continue;
        }

        EntityTween& active = it->second;
        SpriteObject& sprite = entity->second;
        const float previousX = sprite.x;
        const float previousY = sprite.y;
        active.elapsedTicks = std::min(active.durationTicks, active.elapsedTicks + 1U);
        const float progress = active.durationTicks == 0U ? 1.0F :
            static_cast<float>(static_cast<double>(active.elapsedTicks) /
                               static_cast<double>(active.durationTicks));
        float eased = progress;
        if (active.easing == "ease_in") {
            eased = progress * progress;
        } else if (active.easing == "ease_out") {
            const float inverse = 1.0F - progress;
            eased = 1.0F - inverse * inverse;
        } else if (active.easing == "ease_in_out") {
            if (progress < 0.5F) eased = 2.0F * progress * progress;
            else {
                const float inverse = -2.0F * progress + 2.0F;
                eased = 1.0F - (inverse * inverse) * 0.5F;
            }
        }

        const float desiredX = active.startX + (active.targetX - active.startX) * eased;
        // Arc height is measured upward from the straight interpolated line. Using
        // raw time keeps the apex at the temporal midpoint for every easing curve.
        const float arc = 4.0F * progress * (1.0F - progress) * active.arcHeightPixels;
        const float desiredY = active.startY + (active.targetY - active.startY) * eased - arc;

        if (sprite.tweenCollisionResponse == TweenCollisionResponse::Stop &&
            movementSegmentBlocked(sprite, previousX, previousY, desiredX, desiredY, blockers)) {
            // A blocked stop-response tween is canceled silently. It never reports
            // a successful arrival at a destination it could not reach.
            it = entityTweens_.erase(it);
            continue;
        }

        if (sprite.tweenCollisionResponse == TweenCollisionResponse::Slide) {
            // Resolve horizontal and vertical travel separately. If one axis is
            // blocked, the free axis still advances along the obstacle's edge.
            float resolvedX = previousX;
            float resolvedY = previousY;
            if (!movementSegmentBlocked(sprite, previousX, previousY,
                                        desiredX, previousY, blockers)) {
                resolvedX = desiredX;
            }
            if (!movementSegmentBlocked(sprite, resolvedX, previousY,
                                        resolvedX, desiredY, blockers)) {
                resolvedY = desiredY;
            }
            sprite.x = resolvedX;
            sprite.y = resolvedY;
        } else {
            // Ignore preserves the pre-v0.10.10 pass-through behavior. Stop reaches
            // this branch only when its complete swept segment is collision-free.
            sprite.x = desiredX;
            sprite.y = desiredY;
        }

        const float dx = sprite.x - previousX;
        const float dy = sprite.y - previousY;
        if (std::abs(dx) > 0.001F || std::abs(dy) > 0.001F) {
            sprite.moving = true;
            if (std::abs(dx) > 0.001F && std::abs(dy) > 0.001F)
                sprite.facingRow = dx < 0.0F ? (dy < 0.0F ? 3 : 1) : (dy < 0.0F ? 5 : 7);
            else if (std::abs(dx) > 0.001F) sprite.facingRow = dx < 0.0F ? 2 : 6;
            else sprite.facingRow = dy < 0.0F ? 4 : 0;
        }

        if (active.elapsedTicks >= active.durationTicks) {
            // A sliding tween completes its time curve even when an obstacle held
            // one axis. Completion locals report the actual final position.
            if (sprite.tweenCollisionResponse != TweenCollisionResponse::Slide) {
                sprite.x = active.targetX;
                sprite.y = active.targetY;
            }
            pendingTweenCompletions_.push_back({it->first, active.entityInstanceId,
                                                active.roomContextRevision, sprite.x, sprite.y});
            it = entityTweens_.erase(it);
        } else {
            ++it;
        }
    }

    // Dispatch after the map iteration. Handlers are allowed to start/cancel
    // tweens and delete entities without invalidating the active iteration.
    for (const auto& completion : pendingTweenCompletions_) {
        if (completion.roomRevision != roomContextRevision_) break;
        fireTweenComplete(completion.name, completion.instanceId, completion.roomRevision,
                          completion.finalX, completion.finalY);
    }
    pendingTweenCompletions_.clear();
}

} // namespace cx
