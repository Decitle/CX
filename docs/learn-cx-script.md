# Learn CX Script: a friendly first path

This guide is for someone who wants to make a game before learning C++. You can learn CX Script first; the engine turns your script into instructions for the C++ runtime.

## 1. Start with one tiny file

Open `examples/first_steps.cx`. It is intentionally small and commented. Copy it into any `.cx` file under a game project's `scripts/` folder, run the project, and press Space. `main.cx` is a common filename but is not required; all scripts under `scripts/` are discovered. To check syntax without opening the game window, run:

```sh
./build/cx-cli --check /path/to/your-game-project
```

The CLI checks script structure; it does not prove that a graphical behavior looks correct. The graphical executable is `cx-engine` when the graphical runtime is built (the default build).

## 2. The shape of a script

```skript
set {player_name} to "Mira"
set {score} to 0

on load:
    broadcast "Welcome, {player_name}!"

on press key "SPACE":
    add 1 to {score}
    broadcast "Score: {score}"
```

- A command such as `set` or `add` does work.
- An event header such as `on press key "SPACE":` says *when* the indented commands should run.
- The colon `:` opens a block. Commands belonging to the block are indented beneath it.
- A line beginning with `#` is a comment. Comments help people; they do not run.
- Text must be inside double quotes. Variable references use braces, for example `{score}`.
- CX accepts common two-space and four-space indentation styles and tabs. Keep siblings aligned and avoid mixing indentation widths within one file even though the parser normalizes common styles.

## 3. Pick the lifetime you mean

| Example | Lifetime | Good use |
|---|---|---|
| `{score}` | Persistent global; saved between launches | Progress, settings a game owns, inventory |
| `{_amount}` | Local to one event/function execution | A calculation or temporary result |
| `{^frame_count}` | Shared in memory for the current process; not saved | Debug counters and high-frequency state |
| `engine::tick_rate` | Built-in engine setting; assigned without braces | Configuring runtime behavior |

Persistent variables can be edited through `variables.csv` while the engine is closed. Do not hand-edit the compressed `variables.cxv` cache. A persistent assignment may cause disk I/O, so avoid repeatedly saving rapidly changing counters; prefer `{^...}` when the data only matters until the app exits.

## 4. React to input versus update continuously

Use a press event for a one-time action:

```skript
on press key "SPACE":
    add 1 to {score}
```

Use `on tick:` for state that genuinely needs updating each logic step, such as movement while a key is held. Keep tick handlers short because they run frequently. Use `every 1 second:` for slower periodic tasks. Do not create an endless loop without an exit or a deliberate `wait`.

A `wait` resumes only while its captured room context is still current. If another event changes or reloads the active room before that wait resumes, the old continuation is quietly canceled. This is useful for scene-specific sequences like a door animation or enemy attack. A handler can enter a new room and then reach a new `wait`; that wait belongs to the new room. Failed script reloads keep pending waits from the last working program, while successful reloads replace old-program waits. Periodic `every` handlers remain process-wide.

## 5. Conditions, loops, and functions

```skript
if {score} >= 5:
    broadcast "Milestone reached!"
else:
    broadcast "Keep playing!"

loop 3 times:
    broadcast "A small repeated action"

function double(value):
    return {_value} * 2
```

Conditions control which block runs. A counted loop repeats its block. Functions let you name a calculation so you can reuse it. Function parameters are available as event-like local variables such as `{_value}`. Functions are synchronous and cannot use `wait` in the current language.

## 6. Make errors useful

Start with the exact file and line in the diagnostic. Read the error and suggested correction, then check the colon and indentation at that line. For hard-to-find problems, temporarily reduce the script to a small working example and add commands back in short steps. `/reload` uses a candidate runtime; if the new script fails to parse or initialize, the previous working version remains active.

## 7. Keep the language easy to learn

Prefer the plain-English commands already supported by `syntax.md`. Do not assume that every syntax feature from Minecraft Skript is available just because the syntax looks familiar. The engine's own syntax reference and feature audit are the contracts. A new language feature should include: a short example, a scope/type/lifetime explanation, a helpful error for misuse, and a regression test.

## 8. Where to go next

1. `examples/first_steps.cx` — variables, events, conditions.
2. `examples/list_functions.cx` — lists, functions, and arithmetic.
3. `examples/loops_and_timing.cx` — loops and non-blocking waits.
4. `examples/ui_grid_layout.cx` — canvas layout.
5. `examples/procedural_audio.cx` and `examples/ui_audio_import.cx` — audio.
6. `syntax.md` — complete reference; `engine-settings.md` — engine settings; `world-and-entities.md` — rooms/entities/paths/projectiles.
