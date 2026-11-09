#include "cx/runtime.hpp"

#include "cx/attack_patterns.hpp"
#include "cx/collision.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace cx {
namespace {

// Collision checks inside projectile sweeps already have references to both objects.
// Keeping this math on references avoids repeated string normalization and map lookups
// for every sample along a fast projectile's path.
bool spriteObjectsOverlap(const SpriteObject& left, const SpriteObject& right) {
    // Visibility controls drawing, not physics. Hidden actors may still be valid targets,
    // trigger volumes, or temporarily invisible during a gameplay animation.
    return left.collisionEnabled && right.collisionEnabled &&
           collisionFiltersAllow(left.collisionLayer, left.collisionMask,
                                 right.collisionLayer, right.collisionMask) &&
           collisionAabbOverlaps(spriteCollisionBounds(left), spriteCollisionBounds(right));
}

std::vector<std::string> splitPatternFields(std::string_view encoded) {
    std::vector<std::string> fields;
    std::size_t fieldStart = 0;
    for (std::size_t i = 0; i <= encoded.size(); ++i) {
        if (i == encoded.size() || encoded[i] == '|') {
            fields.emplace_back(encoded.substr(fieldStart, i - fieldStart));
            fieldStart = i + 1;
        }
    }
    return fields;
}

} // namespace

bool Runtime::spritesOverlap(std::string_view leftName, std::string_view rightName) const {
    const auto left = sprites_.find(lower(trim(leftName)));
    const auto right = sprites_.find(lower(trim(rightName)));
    if (left == sprites_.end() || right == sprites_.end()) return false;
    return spriteObjectsOverlap(left->second, right->second);
}

// A pattern left inside `on tick:` could otherwise trigger the same cap error
// dozens of times per second and grow diagnostics without adding useful information.
// Report once until enough slots are free for the rejected command. A pattern can
// be rejected below the hard cap (for example, 4050 live shots plus a 64-shot volley),
// so merely being below 4096 is not enough to re-arm the warning.
void Runtime::reportProjectileLimitOnce(const Statement& statement, std::size_t requiredCapacity, bool wholePattern) {
    if (projectileLimitDiagnosticIssued_) return;
    projectileLimitDiagnosticIssued_ = true;
    projectileLimitRequiredCapacity_ = std::clamp<std::size_t>(requiredCapacity, 1, maximumActiveProjectiles_);
    if (wholePattern) {
        runtimeError(statement, "This projectile pattern would exceed the active-projectile safety limit of 4096.",
                     "Reduce count or wait for existing projectiles to expire before firing another pattern.");
    } else {
        runtimeError(statement, "The engine's active-projectile safety limit was reached.",
                     "Wait for existing projectiles to expire or lower the attack's firing rate.");
    }
}

// Shared construction keeps single shots and pattern shots consistent. Callers
// validate the named entities, texture, speed and damage before reaching this method.
void Runtime::spawnProjectileAtAngle(const SpriteObject& owner, std::string_view targetEntity,
                                     std::uint64_t targetInstanceId, std::string_view textureId, float speedTilesPerSecond,
                                     int damage, float angleRadians) {
    SpriteObject projectile;
    // Script authors can legally name their own sprites "projectile_1", etc.
    // Skip any such names instead of letting map::emplace silently discard a shot.
    // The active count must describe actual live projectile objects, not attempts.
    do {
        projectile.name = "projectile_" + std::to_string(++projectileCounter_);
    } while (sprites_.contains(projectile.name));
    projectile.runtimeInstanceId = ++spriteInstanceCounter_;
    projectile.textureId = std::string(textureId);
    projectile.entity = true;
    projectile.projectile = true;
    projectile.kindId = internEntityLabel("projectile");
    projectile.teamId = owner.teamId;
    projectile.collisionLayer = CollisionProjectiles;
    projectile.collisionMask = owner.projectileCollisionMask;
    projectile.width = projectile.height = 16.0F;
    projectile.hitboxWidth = projectile.hitboxHeight = 12.0F;
    projectile.projectileOwner = owner.name;
    projectile.projectileTarget = lower(trim(targetEntity));
    projectile.projectileTargetInstanceId = targetInstanceId;
    projectile.projectileSpeedTilesPerSecond = speedTilesPerSecond;
    projectile.projectileDamage = damage;

    const float sourceX = owner.x + owner.width * 0.5F;
    const float sourceY = owner.y + owner.height * 0.5F;
    const float tilePixels = rooms_.activeRoom()
        ? static_cast<float>(rooms_.activeRoom()->tileSize)
        : 32.0F;
    const float pixelsPerSecond = speedTilesPerSecond * tilePixels;
    projectile.projectileVx = std::cos(angleRadians) * pixelsPerSecond;
    projectile.projectileVy = std::sin(angleRadians) * pixelsPerSecond;
    projectile.x = sourceX - projectile.width * 0.5F;
    projectile.y = sourceY - projectile.height * 0.5F;
    const auto [insertedAt, inserted] = sprites_.emplace(projectile.name, std::move(projectile));
    (void)insertedAt;
    if (inserted) ++activeProjectileCount_;
}

void Runtime::executeProjectileStatement(const Statement& statement) {
    const std::string ownerId = lower(trim(statement.target));
    const std::string targetId = lower(trim(statement.property));
    const auto owner = sprites_.find(ownerId);
    const auto target = sprites_.find(targetId);
    if (owner == sprites_.end() || !owner->second.entity ||
        target == sprites_.end() || !target->second.entity) {
        runtimeError(statement, "A projectile needs existing source and target entities.",
                     "Create both entities before firing; for example, fire from \"boss\" toward \"player\".");
        return;
    }

    if (statement.type == StatementType::SpawnProjectile) {
        const auto first = statement.expression.find('|');
        const auto second = first == std::string::npos ? std::string::npos : statement.expression.find('|', first + 1);
        if (first == std::string::npos || second == std::string::npos) {
            runtimeError(statement, "Projectile parameters are incomplete.", "Specify a texture, speed, and damage.");
            return;
        }
        const std::string textureId = lower(trim(statement.expression.substr(0, first)));
        float speed = 0.0F;
        int damage = 1;
        try {
            speed = std::stof(statement.expression.substr(first + 1, second - first - 1));
            damage = std::stoi(statement.expression.substr(second + 1));
        } catch (...) { speed = 0.0F; }
        if (!rooms_.textures().find(textureId) || !std::isfinite(speed) || speed <= 0.0F || speed > 100.0F ||
            damage < 1 || damage > 10000) {
            runtimeError(statement, "Projectile texture, speed, or damage is invalid.",
                         "Use a known texture, speed greater than 0 and at most 100, and damage from 1 to 10000.");
            return;
        }
        if (activeProjectileCount_ >= maximumActiveProjectiles_) {
            reportProjectileLimitOnce(statement, 1, false);
            return;
        }
        const float sourceX = owner->second.x + owner->second.width * 0.5F;
        const float sourceY = owner->second.y + owner->second.height * 0.5F;
        const float targetX = target->second.x + target->second.width * 0.5F;
        const float targetY = target->second.y + target->second.height * 0.5F;
        spawnProjectileAtAngle(owner->second, targetId, target->second.runtimeInstanceId, textureId, speed, damage,
                               std::atan2(targetY - sourceY, targetX - sourceX));
        return;
    }

    const auto fields = splitPatternFields(statement.expression);
    if (fields.size() != 6) {
        runtimeError(statement, "Projectile pattern data is incomplete.",
                     "Use a radial or fan pattern with texture, count, speed, and damage.");
        return;
    }
    const std::string textureId = lower(trim(fields[0]));
    const std::string kindText = lower(trim(fields[1]));
    unsigned int count = 0;
    float spread = 0.0F, speed = 0.0F;
    int damage = 0;
    try {
        count = static_cast<unsigned int>(std::stoul(fields[2]));
        spread = std::stof(fields[3]);
        speed = std::stof(fields[4]);
        damage = std::stoi(fields[5]);
    } catch (...) {
        runtimeError(statement, "Projectile pattern values are invalid.", "Review count, spread, speed, and damage.");
        return;
    }
    if (!rooms_.textures().find(textureId) || !std::isfinite(speed) || speed <= 0.0F || speed > 100.0F ||
        damage < 1 || damage > 10000) {
        runtimeError(statement, "Projectile pattern texture, speed, or damage is invalid.",
                     "Use a known texture, speed greater than 0 and at most 100, and damage from 1 to 10000.");
        return;
    }

    ProjectilePatternKind kind;
    if (kindText == "radial") kind = ProjectilePatternKind::Radial;
    else if (kindText == "fan") kind = ProjectilePatternKind::Fan;
    else {
        runtimeError(statement, "Unknown projectile pattern kind.", "Supported v1 shapes are radial and fan.");
        return;
    }

    const float sourceX = owner->second.x + owner->second.width * 0.5F;
    const float sourceY = owner->second.y + owner->second.height * 0.5F;
    const float targetX = target->second.x + target->second.width * 0.5F;
    const float targetY = target->second.y + target->second.height * 0.5F;
    const float aimRadians = std::atan2(targetY - sourceY, targetX - sourceX);
    std::vector<float> angles;
    std::string patternError;
    if (!buildProjectilePatternAngles(kind, count, aimRadians, spread, angles, patternError)) {
        runtimeError(statement, patternError, "Use count 1–64; fan spread must be 0–180 degrees.");
        return;
    }

    // Validate every field and the global count limit before creating any shots, so
    // a rejected command never leaves a partial volley in the live game state.
    if (activeProjectileCount_ > maximumActiveProjectiles_ ||
        angles.size() > maximumActiveProjectiles_ - activeProjectileCount_) {
        reportProjectileLimitOnce(statement, angles.size(), true);
        return;
    }
    for (const float angle : angles) {
        spawnProjectileAtAngle(owner->second, targetId, target->second.runtimeInstanceId, textureId, speed, damage, angle);
    }
}

void Runtime::updateProjectiles(float deltaSeconds, float tileSizePixels) {
    // All candidates gathered in this update belong to the active room context at
    // the start of projectile movement. A callback may change rooms while queued
    // impacts are being resolved; the remaining candidates must then be discarded.
    const std::uint64_t updateRoomContextRevision = roomContextRevision_;
    std::vector<std::string> toRemove;
    struct HitCandidate {
        std::string target;
        std::string projectile;
        std::uint64_t targetInstanceId{0};
        int damage{0};
        // Elapsed simulation seconds from this tick's start to the sampled impact.
        // This lets nearer impacts resolve before later impacts in the same tick.
        float impactSeconds{0.0F};
        // Capture the projectile's sampled contact rectangle. A prior callback may move
        // or disable the target before a later queued hit is dispatched; stale geometry
        // must not damage an entity that no longer overlaps this sampled contact.
        CollisionAabb impactBounds{};
        std::uint32_t projectileLayer{CollisionProjectiles};
        std::uint32_t projectileMask{AllCollisionChannels};
    };
    std::vector<HitCandidate> hits;
    // Reserve once using the hard-bounded live count. This avoids repeated growth
    // allocations in the hot update path when a dense volley expires together.
    // We defer deletion and event dispatch until after iteration so map traversal stays valid.
    toRemove.reserve(activeProjectileCount_);
    hits.reserve(activeProjectileCount_);
    for (auto& [id, projectile] : sprites_) {
        if (!projectile.projectile) continue;

        const float ageAtTickStart = projectile.projectileAgeSeconds;
        const float remainingLifetime = std::max(0.0F,
            projectile.projectileLifetimeSeconds - ageAtTickStart);
        if (remainingLifetime <= 0.0F) {
            toRemove.push_back(id);
            continue;
        }

        // A projectile must still travel through its final partial tick. Expiring
        // before movement used to make every projectile lose up to one tick of range
        // and could miss a target directly at the end of its five-second lifetime.
        const float movementSeconds = std::min(deltaSeconds, remainingLifetime);
        projectile.projectileAgeSeconds = ageAtTickStart + deltaSeconds;
        const bool expiresAfterMovement =
            projectile.projectileAgeSeconds >= projectile.projectileLifetimeSeconds;
        const float startX = projectile.x;
        const float startY = projectile.y;
        const float deltaX = projectile.projectileVx * movementSeconds;
        const float deltaY = projectile.projectileVy * movementSeconds;
        const float endX = startX + deltaX;
        const float endY = startY + deltaY;

        // Sample at a fraction of both tile size and projectile hitbox size. Typical
        // bullets need only a few samples; a fast bullet still checks thin obstacles.
        const float hitboxWidth = projectile.hitboxWidth > 0.0F ? projectile.hitboxWidth : projectile.width;
        const float hitboxHeight = projectile.hitboxHeight > 0.0F ? projectile.hitboxHeight : projectile.height;
        const float maxStep = std::max(1.0F, std::min(tileSizePixels * 0.25F,
                                                      std::min(hitboxWidth, hitboxHeight) * 0.5F));
        const float travel = std::max(std::abs(deltaX), std::abs(deltaY));
        const std::size_t steps = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(travel / maxStep)));
        const auto target = sprites_.find(projectile.projectileTarget);
        const bool targetInstanceIsAlive = target != sprites_.end() && target->second.entity &&
            !target->second.projectile &&
            target->second.runtimeInstanceId == projectile.projectileTargetInstanceId;
        // Single-target projectiles bind to the object instance selected at launch.
        // If it despawns or is recreated under the same name, discard the shot now;
        // otherwise an old projectile could unexpectedly damage a respawned entity.
        if (!targetInstanceIsAlive) {
            toRemove.push_back(id);
            continue;
        }

        bool remove = false;
        for (std::size_t step = 1; step <= steps; ++step) {
            const float amount = static_cast<float>(step) / static_cast<float>(steps);
            projectile.x = startX + deltaX * amount;
            projectile.y = startY + deltaY * amount;

            // Collision precedence is defined sample-by-sample: the first sampled
            // contact along the path wins. Test solid tiles before the target at each
            // sample so a wall occludes a target if both overlap at that same sample;
            // a target reached on an earlier sample still wins over a later wall.
            if (!projectile.projectileIgnoresWalls && positionHitsSolidTile(projectile, projectile.x, projectile.y)) {
                remove = true;
                break;
            }
            if (projectile.projectileCanHit && spriteObjectsOverlap(projectile, target->second)) {
                // Detection is collected during map traversal, but damage and scripts are
                // deliberately deferred until traversal is over. This keeps callbacks free
                // to delete entities safely and lets simultaneous hits resolve one at a time.
                hits.push_back({target->first, id, target->second.runtimeInstanceId,
                                projectile.projectileDamage, movementSeconds * amount,
                                spriteCollisionBounds(projectile), projectile.collisionLayer,
                                projectile.collisionMask});
                remove = true;
                break;
            }
        }
        if (remove || expiresAfterMovement) toRemove.push_back(id);
        else { projectile.x = endX; projectile.y = endY; }
    }

    for (const auto& id : toRemove) {
        const auto found = sprites_.find(id);
        if (found != sprites_.end()) {
            if (found->second.projectile) --activeProjectileCount_;
            sprites_.erase(found);
        }
    }
    // Do not clear the warning just because the count is technically below 4096:
    // the failed command may need more free slots than are available. Re-arm it only
    // when the exact rejected volley could fit, which prevents per-tick log flooding.
    if (projectileLimitDiagnosticIssued_ &&
        activeProjectileCount_ <= maximumActiveProjectiles_ - projectileLimitRequiredCapacity_) {
        projectileLimitDiagnosticIssued_ = false;
        projectileLimitRequiredCapacity_ = 1;
    }
    // Each projectile contributes at most one hit. Resolve the sampled impact times
    // rather than map names: projectile_10 sorts before projectile_2 lexically, and
    // spawn order is not the same thing as which bullet physically reached the target.
    // stable_sort preserves deterministic map order for impacts sampled at the same time.
    std::stable_sort(hits.begin(), hits.end(), [](const HitCandidate& left, const HitCandidate& right) {
        return left.impactSeconds < right.impactSeconds;
    });
    for (const auto& hit : hits) {
        if (roomContextRevision_ != updateRoomContextRevision) break;
        const auto targetIt = sprites_.find(hit.target);
        // A previous callback may have defeated or deleted the target. The instance
        // check matters even if a new entity is created immediately with the same name:
        // queued collisions describe the object that was hit, not any future replacement.
        if (targetIt == sprites_.end() || !targetIt->second.entity || targetIt->second.health <= 0 ||
            targetIt->second.runtimeInstanceId != hit.targetInstanceId) continue;
        // Candidate detection was intentionally deferred until after sprite-map traversal.
        // Callbacks for earlier impacts can move the same target or disable its collision,
        // so validate this sampled contact against the target's current hitbox before damage.
        // This prevents stale false positives; it does not resweep paths when a target moves
        // into a trajectory that was not a candidate during the original detection pass.
        if (!targetIt->second.collisionEnabled ||
            !collisionFiltersAllow(hit.projectileLayer, hit.projectileMask,
                                   targetIt->second.collisionLayer, targetIt->second.collisionMask) ||
            !collisionAabbOverlaps(hit.impactBounds, spriteCollisionBounds(targetIt->second))) continue;

        // Resolve queued impacts in sampled time-of-impact order. Each handler observes
        // health immediately after its own hit; stable tie ordering keeps simultaneous
        // impacts deterministic without making lexical projectile names decide priority.
        targetIt->second.health = std::max(0, targetIt->second.health - hit.damage);
        globals_["entity::" + targetIt->first + "::health"] = Value(targetIt->second.health);
        globals_["entity::" + targetIt->first + "::max_health"] = Value(targetIt->second.maxHealth);
        fireProjectileHit(targetIt->first, hit.projectile, hit.damage);
        if (roomContextRevision_ != updateRoomContextRevision) break;
    }
}

} // namespace cx
