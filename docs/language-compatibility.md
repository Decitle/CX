# CX Script and Skript-inspired design

CX Script takes inspiration from the approachable event-and-action style of Minecraft Skript. It is a language designed for CX games and tools, not a drop-in implementation of Skript and not a Minecraft server scripting environment.

The intent is to make common 2D game behavior readable without requiring users to write C++. CX has its own types, runtime, project model, events, and platform-neutral game concepts.

## Concepts currently supported

| Concept | CX form | Notes |
|---|---|---|
| Event handlers | `on load:`, `on tick:`, input and entity events | Event blocks use indented statements. |
| Variables | `{score}`, `{_temporary}`, `{^session}` | Persistent global, execution-local, and process-lifetime temporary storage. |
| Conditions | `if`, `else`, comparisons, boolean expressions | See the full syntax guide for supported operators. |
| Loops | `loop N times:`, `while`, list iteration | Counted and collection loops are bounded. |
| Delays | `wait 1 second` | Suspends the handler without sleeping the graphical thread. Pending continuations are volatile and follow room/reload lifecycle rules. |
| Periodic handlers | `every 1 second:` | Timing is resolved at logic-tick granularity, not as a high-resolution real-time timer. |
| Functions | `function name(value):` and `return` | Synchronous functions with parameters and bounded recursion; `wait` is not available inside a function. |
| Lists | `{_items::*}` and named keys | Supports indexed/named entries and iteration. |
| Entity/game concepts | sprites, hitboxes, collision channels, triggers, projectiles, tweens, animation clips | CX-specific rather than Minecraft player/block abstractions. |
| Diagnostics and reload | parser/runtime diagnostics and `/reload` | A failed candidate reload leaves the last working program active. |

The [complete syntax reference](syntax.md) is the contract for exact syntax and behavior. The [interactive reference](reference.html) is a searchable overview. Individual systems have more detailed [feature guides](features/).

## Deliberately out of scope

CX is not trying to replicate Minecraft-specific concepts such as Bukkit players, blocks, inventories, server commands, permissions, or world administration. Those would not form a useful portable foundation for a 2D desktop game engine.

## Remaining language work

The language still needs more complete task handles and cancellation, clearer module/import boundaries, stronger type/schema diagnostics, and additional authoring conveniences. These are tracked on the [roadmap](roadmap.md), rather than being implied by familiar-looking syntax.

## Example

```skript
set {score} to 0

on load:
    broadcast "Welcome! Press SPACE."

on press key "SPACE":
    add 1 to {score}
    broadcast "Score: {score}"
```

The syntax resembles Skript, but the events, supported commands, and runtime semantics are defined by CX itself.
