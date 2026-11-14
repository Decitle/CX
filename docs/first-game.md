# First game: Number Hunt

This tutorial turns the basics of CX Script into a small, complete game. Number Hunt chooses a secret integer from 1 to 5. The player clicks a number button, receives a higher/lower hint, and earns a point for finding the secret. A **New Round** button resets the attempt counter and picks a new number without erasing the score.

The project is asset-free. It uses the graphical runtime's built-in canvas, panel, text, and button support; it needs no room file, image, or audio asset.

## 1. Run the finished game

The complete project lives in `examples/first_game/`.

From the repository root, run:

```sh
./build/cx-engine examples/first_game
```

To check its syntax without opening a window:

```sh
./build/cx-cli --check examples/first_game
```

Open `examples/first_game/scripts/main.cx` while following the sections below. It is a working game, not pseudocode.

## 2. Give the game a small amount of state

The first lines create the variables that describe a round:

```skript
set {score} to 0
set {attempts} to 0
set {secret} to a random integer between 1 and 5
set {round_won} to false
set {status_message} to "Pick a number from 1 to 5."
```

- `{score}` is the number of rounds the player has won.
- `{attempts}` counts guesses in the current round.
- `{secret}` stores the random answer. The game never displays it.
- `{round_won}` prevents extra guesses from changing a finished round.
- `{status_message}` is text that the HUD can display.

The secret uses CX's natural random-integer expression. Its endpoints are inclusive, so the possible values are 1, 2, 3, 4, and 5.

## 3. Draw the interface with a canvas

The `NumberHunt` canvas contains one panel, three text elements, five numbered buttons, and the New Round button. The important distinction is that text elements contain global placeholders:

```skript
canvas "NumberHunt":
    create panel "game_panel" at (20, 20) size (480, 270) color (12, 20, 32, 230)
    create text "Number Hunt" at (40, 35) font size 28
    create text "Score: {score}   Attempts: {attempts}" at (40, 78) font size 18
    create text "{status_message}" at (40, 110) font size 18
```

The renderer resolves those global values as it draws the canvas, so the score, attempts, and status update when a handler changes a variable. The script does not need to close and reopen the canvas.

The numbered buttons each have a stable ID, such as `guess_1` and `guess_5`. Their IDs connect visible clicks to event handlers. The game opens the canvas once on startup:

```skript
on load:
    open canvas "NumberHunt"
```

## 4. Handle a guess

Here is the handler for the number 3. The other four guess buttons use the same steps but assign 1, 2, 4, or 5.

```skript
on click button "guess_3":
    if {round_won} is false:
        set {guess} to 3
        add 1 to {attempts}
        if {guess} is equal to {secret}:
            add 1 to {score}
            set {round_won} to true
            set {status_message} to "Correct! You found the secret number."
        else:
            if {guess} greater than {secret}:
                set {status_message} to "Too high. Try a smaller number."
            else:
                set {status_message} to "Too low. Try a larger number."
```

The outer condition ignores clicks after a win. Before comparing the guess, the handler records it and increments the attempt count. A correct guess increases the score and marks the round complete. A wrong guess selects one of two helpful status messages.

This is a good first example of event-driven code: a click happens, one matching handler runs, and the global variables change. The canvas displays those changes automatically.

## 5. Start another round

The New Round button starts the next round:

```skript
on click button "new_round":
    set {secret} to a random integer between 1 and 5
    set {attempts} to 0
    set {round_won} to false
    set {status_message} to "A new number has been picked."
```

Notice that it does not reset `{score}`. The player's total carries from one round to the next.

## 6. Make a change

Try changing the game in one place, then run `cx-cli --check examples/first_game` before launching it again.

- Change both random-range expressions from 1–5 to 1–10, then add buttons and matching handlers for the extra values.
- Change the scoring rule so a round awards two points.
- Add a global `{rounds_played}` counter and display it in the HUD.
- Add a second status line that shows the number of attempts remaining.

The example intentionally keeps each handler explicit so a beginner can trace a button click from its visible ID to the statements it runs. Once the pattern is familiar, the [functions and lists example](../examples/list_functions.cx) shows how to organize reusable logic.

## 7. What the tests cover

The core runtime test exercises a whole game flow: it picks an in-range secret, checks a wrong guess and its directional hint, completes the round with a correct guess, confirms further guesses are ignored, and starts a new round while preserving the score. The project is also included in CI's all-example syntax pass.
