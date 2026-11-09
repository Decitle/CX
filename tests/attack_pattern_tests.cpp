#include "cx/attack_patterns.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float left, float right, float epsilon = 0.0001F) {
    return std::abs(left - right) <= epsilon;
}

void testRadialDirectionsAreEvenlySpaced() {
    std::vector<float> angles;
    std::string error;
    constexpr float aim = 0.35F;
    expect(cx::buildProjectilePatternAngles(cx::ProjectilePatternKind::Radial, 4, aim, 0.0F, angles, error),
           "four-shot radial pattern should be valid: " + error);
    expect(angles.size() == 4, "radial pattern should make exactly the requested number of angles");
    expect(near(angles[0], aim), "first radial shot should begin at the target-aim direction");
    expect(near(angles[1] - angles[0], std::numbers::pi_v<float> * 0.5F),
           "radial shots should be separated by one quarter turn");
    expect(near(angles[2] - angles[1], std::numbers::pi_v<float> * 0.5F),
           "radial spacing should remain uniform");
}

void testFanHasRequestedEdgesAndCenter() {
    std::vector<float> angles;
    std::string error;
    constexpr float aim = 1.0F;
    expect(cx::buildProjectilePatternAngles(cx::ProjectilePatternKind::Fan, 5, aim, 80.0F, angles, error),
           "five-shot fan should be valid: " + error);
    const float halfSpread = 40.0F * std::numbers::pi_v<float> / 180.0F;
    expect(angles.size() == 5, "fan should emit the requested number of directions");
    expect(near(angles.front(), aim - halfSpread), "fan's first angle should sit on the left edge");
    expect(near(angles[2], aim), "odd-count fan should include the exact aim direction");
    expect(near(angles.back(), aim + halfSpread), "fan's last angle should sit on the right edge");
}

void testSingleShotFanStaysAimed() {
    std::vector<float> angles;
    std::string error;
    expect(cx::buildProjectilePatternAngles(cx::ProjectilePatternKind::Fan, 1, -0.7F, 160.0F, angles, error),
           "one-projectile fan should be valid: " + error);
    expect(angles.size() == 1 && near(angles.front(), -0.7F),
           "one-projectile fan should point directly at the target");
}

void testPatternInputsAreBounded() {
    std::vector<float> angles;
    std::string error;
    expect(!cx::buildProjectilePatternAngles(cx::ProjectilePatternKind::Radial, 0, 0.0F, 0.0F, angles, error),
           "zero shots must be rejected");
    expect(angles.empty() && !error.empty(), "failed pattern construction should clear output and explain why");
    expect(!cx::buildProjectilePatternAngles(cx::ProjectilePatternKind::Radial, 65, 0.0F, 0.0F, angles, error),
           "pattern count above safety cap must be rejected");
    expect(!cx::buildProjectilePatternAngles(cx::ProjectilePatternKind::Fan, 3, 0.0F, 181.0F, angles, error),
           "fan spread above 180 degrees must be rejected");
    expect(!cx::buildProjectilePatternAngles(cx::ProjectilePatternKind::Fan, 3, 0.0F, -1.0F, angles, error),
           "negative fan spread must be rejected");
    expect(!cx::buildProjectilePatternAngles(cx::ProjectilePatternKind::Radial, 3,
                                              std::numeric_limits<float>::infinity(), 0.0F, angles, error),
           "non-finite aim angles must be rejected");
}

} // namespace

int main() {
    try {
        testRadialDirectionsAreEvenlySpaced();
        testFanHasRequestedEdgesAndCenter();
        testSingleShotFanStaysAimed();
        testPatternInputsAreBounded();
    } catch (const std::exception& exception) {
        std::cerr << "ATTACK PATTERN TEST FAILED: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All projectile attack-pattern tests passed.\n";
    return 0;
}
