# Projects, configuration, assets, and rooms

## Basic project layout

```text
my-game/
├── config.json
├── scripts/
│   └── main.cx
├── rooms/
│   └── start.room.json
├── assets/
│   ├── textures/
│   ├── fonts/
│   └── audio/
└── tools/
```

CX discovers `.cx` files recursively under `scripts/` in stable path order. `main.cx` is a convention, not a mandatory special filename. Room files live under `rooms/`. Asset IDs are derived from asset filenames and loaded by the runtime. Keep filenames simple and consistent in case, especially when projects are moved between case-sensitive and case-insensitive filesystems.

## Configuration precedence

`config.json` supplies defaults for the next runtime launch. Script bootstrap statements can override settings after the project is loaded. The exact settings, accepted values, and application timing are listed in [Engine settings](engine-settings.md).

## First-run initialization

Launching the runtime against an empty project folder can create missing standard project files. Existing user files are not overwritten. Project initialization is conservative: unrelated non-empty directories should not be treated as an empty project. For CI or validation, use `cx-cli --check` because check-only mode does not create or rewrite project files.

## Room data

Rooms are JSON data files that describe dimensions, tiles, collision metadata, and optional visual layers. The engine supports transitions, named entrances, and room-local content. Use the included room examples to see the schema in context. Room Studio can create and edit room files through a local server.

## Persistent variables

Persistent global variables are stored in project files such as `variables.csv` and a compact cache. Local variables and transient variables have different lifetimes. See [variable-storage recovery](variable-storage-recovery.md) for how CX treats corrupt or interrupted writes.
