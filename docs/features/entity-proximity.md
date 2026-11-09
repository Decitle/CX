# Entity proximity queries — v0.10.4

CX v0.10.4 adds a radius-based geometric condition for gameplay entities.

## Syntax

```cx
set {interaction_range} to 72
if entity "player" is within {interaction_range} pixels of entity "ancient_altar":
    set {can_interact_with_altar} to true
```

The distance expression can be any numeric value expression supported by CX, including a variable and arithmetic. It must evaluate to a finite, non-negative number. The radius is inclusive: a center-to-center distance equal to the radius returns true. `not` can negate the condition.

## Distance definition

The query compares the center points of the effective entity rectangles. A configured hitbox (size and offset) is used when present; otherwise the sprite rectangle is used. The distance is measured in world pixels. This is a circular radius check, not an AABB overlap check.

The query deliberately ignores visibility, collision-enabled state, collision layers and masks. Those properties govern drawing or physical contact eligibility; a proximity condition only reports geometric distance. Two references must resolve to gameplay entities. Missing names and names that resolve only to sprites evaluate false.

## Boundaries

- This checks two named entities. It does not search for the nearest entity in a group or choose by tag/team.
- It does not dispatch enter/exit events, change focus automatically, show an interaction prompt, or run an interaction action. The game script owns those decisions and all player-facing text.
- It is not a collision response or a physics query. It does not block movement, sweep between positions, or account for walls/occlusion.
- It adds no per-frame search over every entity. Evaluation occurs only where the script evaluates the condition.

The implementation is in `Runtime::evaluateCondition` in `src/runtime_expressions.cpp`; focused coverage is in `tests/collision_runtime_tests.cpp` under `cx-collision-tests`.
