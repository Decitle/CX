#include "cx/runtime.hpp"

#include "cx/collision.hpp"

#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cx {

std::optional<Runtime::InteractionFocus> Runtime::selectInteractionTarget(const SpriteObject& actor) const {
    if (!actor.entity || actor.projectile || !std::isfinite(actor.interactionRangePixels) ||
        actor.interactionRangePixels <= 0.0F) {
        return std::nullopt;
    }

    const auto actorCenter = spriteHitboxCenter(actor);
    const double range = static_cast<double>(actor.interactionRangePixels);
    const double rangeSquared = range * range;
    double bestDistanceSquared = rangeSquared;
    std::optional<InteractionFocus> best;

    // sprites_ is an ordered map, but the explicit name tie-break is part of the
    // contract rather than an accidental consequence of container traversal order.
    for (const auto& [targetName, target] : sprites_) {
        if (!target.entity || target.projectile || !target.interactable ||
            target.runtimeInstanceId == actor.runtimeInstanceId) {
            continue;
        }
        if (!collisionFiltersAllow(actor.collisionLayer, actor.collisionMask,
                                   target.collisionLayer, target.collisionMask)) {
            continue;
        }

        const auto targetCenter = spriteHitboxCenter(target);
        const double dx = actorCenter[0] - targetCenter[0];
        const double dy = actorCenter[1] - targetCenter[1];
        const double distanceSquared = dx * dx + dy * dy;
        if (!std::isfinite(distanceSquared) || distanceSquared > rangeSquared) continue;

        const bool isCloser = !best || distanceSquared < bestDistanceSquared;
        const bool isNameTieWinner = best && distanceSquared == bestDistanceSquared &&
            targetName < best->targetName;
        if (isCloser || isNameTieWinner) {
            bestDistanceSquared = distanceSquared;
            best = InteractionFocus{actor.name, targetName,
                                    actor.runtimeInstanceId, target.runtimeInstanceId};
        }
    }
    return best;
}

void Runtime::fireInteractionFocusEvent(EventType type, const InteractionFocus& focus) {
    if (type != EventType::FocusEnter && type != EventType::FocusExit) return;
    const std::uint64_t eventRoomRevision = roomContextRevision_;

    const auto actorAtStart = sprites_.find(focus.actorName);
    if (actorAtStart == sprites_.end() || !actorAtStart->second.entity || actorAtStart->second.projectile ||
        actorAtStart->second.runtimeInstanceId != focus.actorInstanceId) {
        return;
    }

    // For an exit, the previous target may already be absent (deleted before this
    // tick) or replaced under the same name. Capture the current name->instance
    // state so a callback that mutates it invalidates later handlers in this event.
    // For an enter, the target must be the exact instance selected by the query.
    const auto targetAtStart = sprites_.find(focus.targetName);
    const bool targetExistedAtStart = targetAtStart != sprites_.end();
    const std::uint64_t targetInstanceAtStart = targetExistedAtStart
        ? targetAtStart->second.runtimeInstanceId : 0U;
    if (type == EventType::FocusEnter &&
        (!targetExistedAtStart || targetInstanceAtStart != focus.targetInstanceId ||
         !targetAtStart->second.entity || targetAtStart->second.projectile || !targetAtStart->second.interactable)) {
        return;
    }
    // An exit may still be useful after the original target was deleted, but if a
    // replacement already occupies the same name before dispatch, do not hand that
    // replacement to handlers as though it were the object that just lost focus.
    if (type == EventType::FocusExit && targetExistedAtStart &&
        targetInstanceAtStart != focus.targetInstanceId) {
        return;
    }

    const auto eventStateStillValid = [&]() {
        if (roomContextRevision_ != eventRoomRevision) return false;
        const auto actor = sprites_.find(focus.actorName);
        if (actor == sprites_.end() || !actor->second.entity || actor->second.projectile ||
            actor->second.runtimeInstanceId != focus.actorInstanceId) {
            return false;
        }
        const auto target = sprites_.find(focus.targetName);
        if (targetExistedAtStart) {
            if (target == sprites_.end() || target->second.runtimeInstanceId != targetInstanceAtStart) return false;
        } else if (target != sprites_.end()) {
            return false;
        }

        // Focus-enter is a statement about the actor's current selected target, not
        // merely a historical notification. If an earlier handler moves either
        // object, changes range/masks, or disables the target, suppress the rest of
        // this now-stale enter event. Focus-exit remains a historical transition and
        // is allowed to finish while both original object identities remain stable.
        if (type == EventType::FocusEnter) {
            const auto current = selectInteractionTarget(actor->second);
            return current && current->targetName == focus.targetName &&
                current->targetInstanceId == focus.targetInstanceId;
        }
        return true;
    };

    for (const auto& handler : program_.handlers) {
        if (handler.type != type || handler.selector != focus.actorName ||
            handler.secondarySelector != focus.targetName) {
            continue;
        }
        // Event callbacks are allowed to delete/recreate objects or change rooms.
        // Never let later handlers run with an identity-stale event payload.
        if (!eventStateStillValid()) break;

        ExecutionContext context;
        context.locals["_focus_actor"] = Value(focus.actorName);
        context.locals["_focus_entity"] = Value(focus.targetName);
        context.locals["_focused_entity"] = Value(focus.targetName);
        executeStatements(handler.body, context);
        savePersistentVariables();
        if (!eventStateStillValid()) break;
    }
}

void Runtime::updateInteractionFocus() {
    // Like trigger contacts, focus never crosses room-context boundaries. A room
    // replacement is teardown, not an ordinary focus-exit interaction.
    if (interactionFocusRoomContextRevision_ != roomContextRevision_) {
        activeInteractionFocus_.clear();
        interactionFocusRoomContextRevision_ = roomContextRevision_;
    }

    const auto previousFocus = activeInteractionFocus_;
    for (const auto& [actorInstanceId, previous] : previousFocus) {
        const auto actor = sprites_.find(previous.actorName);
        if (actor == sprites_.end() || !actor->second.entity || actor->second.projectile ||
            actor->second.runtimeInstanceId != actorInstanceId) {
            // Deleting/replacing the actor invalidates its own focus silently. An
            // exit handler must not be run against the replacement actor instance.
            activeInteractionFocus_.erase(actorInstanceId);
            continue;
        }

        const auto desired = selectInteractionTarget(actor->second);
        if (desired && desired->targetInstanceId == previous.targetInstanceId &&
            desired->targetName == previous.targetName) {
            continue;
        }

        const auto liveFocus = activeInteractionFocus_.find(actorInstanceId);
        if (liveFocus == activeInteractionFocus_.end() ||
            liveFocus->second.targetInstanceId != previous.targetInstanceId ||
            liveFocus->second.targetName != previous.targetName) {
            continue;
        }
        activeInteractionFocus_.erase(liveFocus);
        fireInteractionFocusEvent(EventType::FocusExit, previous);
        if (roomContextRevision_ != interactionFocusRoomContextRevision_) {
            activeInteractionFocus_.clear();
            interactionFocusRoomContextRevision_ = roomContextRevision_;
            return;
        }
    }

    // Copy identities before dispatch: callbacks can insert/remove map entries.
    // For each actor, recompute its best target after all earlier callbacks so a
    // mutation cannot cause a stale target from the initial scan to be entered.
    std::vector<std::pair<std::string, std::uint64_t>> actorInstances;
    actorInstances.reserve(sprites_.size());
    for (const auto& [name, object] : sprites_) {
        if (object.entity && !object.projectile && object.interactionRangePixels > 0.0F)
            actorInstances.emplace_back(name, object.runtimeInstanceId);
    }

    for (const auto& [actorName, actorInstanceId] : actorInstances) {
        if (roomContextRevision_ != interactionFocusRoomContextRevision_) {
            activeInteractionFocus_.clear();
            interactionFocusRoomContextRevision_ = roomContextRevision_;
            return;
        }

        const auto actor = sprites_.find(actorName);
        if (actor == sprites_.end() || actor->second.runtimeInstanceId != actorInstanceId ||
            !actor->second.entity || actor->second.projectile) {
            continue;
        }
        const auto desired = selectInteractionTarget(actor->second);
        if (!desired) continue;

        const auto current = activeInteractionFocus_.find(actorInstanceId);
        if (current != activeInteractionFocus_.end()) {
            if (current->second.targetInstanceId == desired->targetInstanceId &&
                current->second.targetName == desired->targetName) {
                continue;
            }
            // An exit callback may have altered this actor's focus after the exit
            // phase. Defer the next transition to the next tick rather than nesting
            // focus callbacks or generating ambiguous enter/exit ordering.
            continue;
        }

        // Recheck immediately before publication in case a prior callback changed
        // this target or its channel mask since the selection was first computed.
        const auto actorNow = sprites_.find(actorName);
        if (actorNow == sprites_.end() || actorNow->second.runtimeInstanceId != actorInstanceId) continue;
        const auto finalTarget = selectInteractionTarget(actorNow->second);
        if (!finalTarget || finalTarget->targetInstanceId != desired->targetInstanceId ||
            finalTarget->targetName != desired->targetName) {
            continue;
        }

        activeInteractionFocus_.emplace(actorInstanceId, *finalTarget);
        fireInteractionFocusEvent(EventType::FocusEnter, *finalTarget);
        if (roomContextRevision_ != interactionFocusRoomContextRevision_) {
            activeInteractionFocus_.clear();
            interactionFocusRoomContextRevision_ = roomContextRevision_;
            return;
        }
    }
}

} // namespace cx
