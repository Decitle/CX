# Seeded random streams

## Purpose

CX Script supports ordinary English random expressions through an automatically initialized runtime-local `"default"` stream, plus explicitly seeded streams for repeatable game logic, scene staging, procedural decoration and tests. The default stream is initialized with best-effort runtime entropy; projects that need reproducibility should explicitly seed the default stream or use their own named streams.

## Script contract

```skript
on load:
    seed random stream "loot" with 4815
    seed random stream "encounters" with 90210
    set {ordinary_roll} to a random integer between 1 and 100
    set {repeatable_roll} to a random integer between 1 and 100 using random stream "loot"
    set {drop_count} to random_int("loot", 1, 4)
    set {drop_variant} to random_choice("loot", "coin", "gem", "key")
```

`seed random stream "name" with integer` seeds or resets one named stream (including `"default"`). Names are trimmed, case-insensitive and 1–64 characters after trimming. Seeds must be whole numbers in the inclusive safe-integer range ±9,007,199,254,740,991. Values outside that range, text seeds and fractional seeds are rejected with a diagnostic. Negative seeds have a defined two's-complement bit pattern after conversion to the generator state.

### The built-in default stream

Every runtime creates a `"default"` stream automatically. Its initial state mixes the steady clock and runtime instance address with `std::random_device` when that source is available; the clock/address mix is the fallback. This gives ordinary random expressions a usable stream without a setup command, but does not promise cryptographic unpredictability or reproducibility between runs. Calling `seed random stream "default" with integer` replaces that initial state with a reproducible seed.

Other named streams do not exist until explicitly seeded; using one too early produces a diagnostic. Natural expressions can select one with `using random stream "name"`; the function forms below name their stream as their first argument.

### Count-prefix random list assignments

```skript
on load:
    seed random stream "loot" with 4815
    set {rolls::*} to 5 random integers between 5 and 96 using random stream "loot"
    set {fractions::*} to 3 random numbers from 0 to 1
```

A count-prefix expression is accepted when the assignment target is a list wildcard such as `{rolls::*}`. Its count is evaluated once and must be a numeric whole number from 0 through 4096. Each generated item then uses the ordinary single-value random evaluator, preserving typed integer/number results and consuming the selected stream once per item (integer rejection sampling can use more than one underlying generator output). The generated values occupy sequential numeric list indices. A count of zero clears the destination list and consumes no draws. Single-value expressions such as `a random integer between 1 and 10` remain scalar and may still be assigned to an ordinary variable or used as one list item.

### Built-ins

| Expression | Result | Stream consumption |
|---|---|---|
| `random_float("name")` | Deterministic value in `[0, 1)` | One SplitMix64 output |
| `random_int("name", min, max)` | Unbiased integer in the inclusive range | One or more outputs (rejection sampling); equal bounds still consume one |
| `random_choice("name", a, b, ...)` | Selected expression's original `Value` type | One bounded draw for 2–256 options; zero draws for one option |

`random_choice` evaluates only the chosen option. Unchosen expressions do not produce runtime errors or consume random streams as a side effect. The chosen expression is evaluated after the selection draw. Empty option arguments are rejected. A maximum of 256 choices prevents accidental oversized per-call work.

## Determinism and lifecycle

- Random draws use the specified SplitMix64 64-bit state transition and mixing constants; the generator output does not rely on `std::uniform_int_distribution` or a platform distribution. The automatically generated default seed may use `std::random_device`, with a portable clock/address fallback.
- Each `Runtime` owns its own map of named stream states. Names are normalized to lowercase, and separate names advance independently.
- A successful script reload constructs a candidate runtime with a fresh default stream, runs bootstrap and startup handlers, and adopts its stream map only after startup succeeds. Explicitly seeded named streams therefore restart from their script seeds (including any startup draws), while an unseeded default stream receives a fresh entropy-derived starting state. A failed reload preserves the last-good runtime and its exact stream positions.
- Stream states are transient process state. They are not saved in persistent variables. The default stream is regenerated on runtime reset; explicitly seeded named streams are initialized by the project's seed statements. To reproduce a particular gameplay segment after loading a save, a future save-game contract will need to persist stream states or derive subsystem seeds from a saved world seed and stable identifiers.
- Seed streams once in startup for normal play. Seeding inside `on tick:` resets that stream every tick and is usually a logic error.
- Use separate named streams for unrelated systems. This limits the cascading effect of inserting a new random choice into one subsystem.

## Boundaries

This API is deterministic pseudo-randomness, not cryptographic randomness. Do not use it for passwords, authentication, anti-cheat secrets, or anything requiring unpredictability. It does not promise that a game's entire simulation is deterministic if unrelated systems use wall-clock time, unordered iteration, external input, or nondeterministic assets. The engine's fixed-tick simulation and stable map iteration support repeatability, but the game author must control all other sources of variation.
