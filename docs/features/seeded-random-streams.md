# Seeded random streams

## Purpose

CX Script offers deterministic random values for repeatable game logic, scene staging, procedural decoration and tests. It deliberately does not seed itself from the wall clock. A project chooses its seeds explicitly so re-running the same startup script recreates the same sequences.

## Script contract

```skript
on load:
    seed random stream "loot" with 4815
    seed random stream "encounters" with 90210

set {drop_count} to random_int("loot", 1, 4)
set {drop_variant} to random_choice("loot", "coin", "gem", "key")
set {decoration_roll} to random_float("decoration") # error until decoration is seeded
```

`seed random stream "name" with integer` seeds or resets one named stream. Names are trimmed, case-insensitive and 1–64 characters after trimming. Seeds must be whole numbers in the inclusive safe-integer range ±9,007,199,254,740,991. Values outside that range, text seeds and fractional seeds are rejected with a diagnostic. Negative seeds have a defined two's-complement bit pattern after conversion to the generator state.

### Built-ins

| Expression | Result | Stream consumption |
|---|---|---|
| `random_float("name")` | Deterministic value in `[0, 1)` | One SplitMix64 output |
| `random_int("name", min, max)` | Unbiased integer in the inclusive range | One or more outputs (rejection sampling); equal bounds still consume one |
| `random_choice("name", a, b, ...)` | Selected expression's original `Value` type | One bounded draw for 2–256 options; zero draws for one option |

`random_choice` evaluates only the chosen option. Unchosen expressions do not produce runtime errors or consume random streams as a side effect. The chosen expression is evaluated after the selection draw. Empty option arguments are rejected. A maximum of 256 choices prevents accidental oversized per-call work.

## Determinism and lifecycle

- The implementation uses the specified SplitMix64 64-bit state transition and mixing constants; it does not rely on `std::uniform_int_distribution`, `std::random_device`, or a platform RNG.
- Each `Runtime` owns its own map of named stream states. Names are normalized to lowercase, and separate names advance independently.
- A successful script reload constructs a candidate runtime and commits the newly seeded stream states only after startup succeeds. Thus successful reload restarts streams from the script seed; failed reload preserves the last-good sequence exactly.
- Stream states are transient process state. They are not saved in persistent variables and are initialized by running the project's seed statements at startup. To reproduce a particular gameplay segment after loading a save, a future save-game contract will need to persist stream states or derive subsystem seeds from a saved world seed and stable identifiers.
- Seed streams once in startup for normal play. Seeding inside `on tick:` resets that stream every tick and is usually a logic error.
- Use separate named streams for unrelated systems. This limits the cascading effect of inserting a new random choice into one subsystem.

## Boundaries

This API is deterministic pseudo-randomness, not cryptographic randomness. Do not use it for passwords, authentication, anti-cheat secrets, or anything requiring unpredictability. It does not promise that a game's entire simulation is deterministic if unrelated systems use wall-clock time, unordered iteration, external input, or nondeterministic assets. The engine's fixed-tick simulation and stable map iteration support repeatability, but the game author must control all other sources of variation.
