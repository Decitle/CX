# CX Game Project

This folder is a CX game project.

## Scripts

Every `.cx` file under `scripts/` is loaded recursively in stable path order. `main.cx` is only a convenient starting file; it is not a required entry point. Use `on load:` for game startup behavior. Top-level settings run before load handlers.

## Configuration

`config.json` is optional. It sets baseline engine options; top-level `set engine::...` statements in scripts override those values. The built-in window defaults are responsive and do not need to be repeated in every game.

## Generated save files

Persistent variable files are created only when the game first saves persistent values. Do not create empty `variables.csv`, `variables.cxv`, or `variables.meta` files by hand.

## Folders

- `rooms/` — room maps
- `assets/textures/` — image assets
- `assets/audio/` — audio assets
- `assets/fonts/` — optional fonts
- `tools/` — project-local authoring tools
