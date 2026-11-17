# Number Hunt — first complete CX game

A small, asset-free button game for learning CX Script. The engine chooses a secret number from 1 to 5. Click a number button to guess it; the HUD gives a higher/lower hint, counts attempts, and awards a point when you find the number. Click **New Round** to play again without losing your score.

## Run

From the CX repository root:

```sh
./build/cx-engine examples/first_game
```

To check the script without opening a window:

```sh
./build/cx-cli --check examples/first_game
```

The game uses only canvas panels, text, buttons, variables, random integer expressions, conditions, and button-click events. It needs no image or audio assets.

See [the first-game tutorial](../../docs/first-game.md) for a guided walkthrough.
