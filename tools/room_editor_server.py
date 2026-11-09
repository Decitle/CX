#!/usr/bin/env python3
"""Local-only standard-library server for CX Engine's room editor."""
from __future__ import annotations

import json
import mimetypes
import os
import re
import sys
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

EDITOR_ROOT = Path(__file__).resolve().parent.parent
# ROOT is the selected game-project root; --project-root lets the editor tool live
# in the engine source tree without editing that source tree as if it were a game.
ROOT = EDITOR_ROOT
ROOMS = ROOT / "rooms"
TEXTURES = ROOT / "assets" / "textures"
IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif"}
ROOM_NAME = re.compile(r"^[A-Za-z0-9_-]{1,64}$")


def room_id(value: str) -> str:
    if value.endswith(".room.json"):
        value = value[:-len(".room.json")]
    return value


def room_path(name: str) -> Path | None:
    name = room_id(name)
    if not ROOM_NAME.fullmatch(name):
        return None
    return ROOMS / f"{name}.room.json"


def _legacy_texture_is_solid(texture_id: str) -> bool:
    """Match the pre-metadata C++ defaults for older projects."""
    return any(token in texture_id for token in ("stone", "wall", "trunk", "crate"))


def load_texture_collision_defaults(texture_ids: set[str]) -> tuple[dict[str, str], list[str]]:
    """Load the project's explicit defaults, or retain legacy inference on failure.

    The manifest is transactional: a malformed entry rejects the whole file so the
    editor never previews a partially applied collision configuration.
    """
    defaults = {texture_id: ("solid" if _legacy_texture_is_solid(texture_id) else "none")
                for texture_id in texture_ids}
    warnings: list[str] = []
    manifest = ROOT / "assets" / "collision.json"
    if not manifest.is_file():
        return defaults, warnings
    try:
        payload = json.loads(manifest.read_text(encoding="utf-8"))
        default_mode = payload.get("default") if isinstance(payload, dict) else None
        if (not isinstance(payload, dict) or payload.get("schema") != "cx-collision" or
                type(payload.get("version")) is not int or payload["version"] != 1 or
                not isinstance(default_mode, str) or default_mode not in {"solid", "none"} or
                not isinstance(payload.get("textures"), dict)):
            raise ValueError("Expected schema cx-collision, version 1, default solid/none, and a textures object.")
        candidate = {texture_id: default_mode for texture_id in texture_ids}
        normalized_ids: set[str] = set()
        for raw_id, mode in payload["textures"].items():
            texture_id = str(raw_id).strip().lower()
            if not texture_id or not isinstance(mode, str) or mode not in {"solid", "none"}:
                raise ValueError("Every texture entry needs a valid texture ID and mode solid/none.")
            if texture_id in normalized_ids:
                raise ValueError(f"Collision metadata repeats texture ID '{texture_id}' after normalization.")
            normalized_ids.add(texture_id)
            if texture_id not in texture_ids:
                raise ValueError(f"Collision metadata refers to missing texture '{texture_id}'.")
            candidate[texture_id] = mode
        return candidate, warnings
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        warnings.append(f"assets/collision.json was ignored; legacy texture-name defaults remain active. {exc}")
        return defaults, warnings


class Handler(BaseHTTPRequestHandler):
    server_version = "CXRoomEditor/1.0"

    def log_message(self, fmt: str, *args: object) -> None:
        # Quiet out high-frequency browser asset requests; show API mutations only.
        if self.path.startswith("/api/") and self.command != "GET":
            super().log_message(fmt, *args)

    def _send(self, status: int, data: bytes, content_type: str = "application/json; charset=utf-8") -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, status: int, payload: object) -> None:
        self._send(status, json.dumps(payload, ensure_ascii=False).encode("utf-8"))

    def do_GET(self) -> None:  # noqa: N802
        url = urlparse(self.path)
        if url.path == "/api/textures":
            TEXTURES.mkdir(parents=True, exist_ok=True)
            image_paths = [path for path in sorted(TEXTURES.iterdir(), key=lambda p: p.name.lower())
                           if path.is_file() and path.suffix.lower() in IMAGE_EXTENSIONS]
            texture_ids = {path.stem.lower() for path in image_paths}
            collision_defaults, warnings = load_texture_collision_defaults(texture_ids)
            items = []
            for path in image_paths:
                texture_id = path.stem.lower()
                items.append({"id": texture_id, "file": path.name,
                              "url": "/assets/textures/" + path.name,
                              "collision": collision_defaults[texture_id]})
            self._json(200, {"textures": items, "warnings": warnings})
            return
        if url.path == "/api/rooms":
            ROOMS.mkdir(parents=True, exist_ok=True)
            names = sorted(p.name[:-len(".room.json")] for p in ROOMS.glob("*.room.json") if p.is_file())
            self._json(200, {"rooms": names})
            return
        if url.path.startswith("/api/room/"):
            name = url.path[len("/api/room/"):]
            path = room_path(name)
            if path is None or not path.is_file():
                self._json(404, {"error": f"Room '{name}' was not found."})
                return
            try:
                with path.open("r", encoding="utf-8") as stream:
                    self._json(200, json.load(stream))
            except (OSError, json.JSONDecodeError) as exc:
                self._json(500, {"error": f"Could not read room file: {exc}"})
            return
        relative = url.path.lstrip("/") or "tools/room-editor.html"
        # The editor UI belongs to the engine tools; project textures belong to the
        # selected game folder. Keep those roots distinct when --project-root is used.
        if relative == "tools/room-editor.html":
            target = (EDITOR_ROOT / relative).resolve()
            allowed_root = EDITOR_ROOT.resolve()
        else:
            target = (ROOT / relative).resolve()
            allowed_root = ROOT.resolve()
        try:
            target.relative_to(allowed_root)
        except ValueError:
            self._json(403, {"error": "Path is outside the allowed editor/project folder."})
            return
        if not target.is_file():
            self._json(404, {"error": "File not found."})
            return
        content_type = mimetypes.guess_type(str(target))[0] or "application/octet-stream"
        if target.suffix.lower() in {".html", ".css", ".js"}:
            content_type += "; charset=utf-8"
        try:
            self._send(200, target.read_bytes(), content_type)
        except OSError as exc:
            self._json(500, {"error": f"Could not read file: {exc}"})

    def do_POST(self) -> None:  # noqa: N802
        if urlparse(self.path).path != "/api/room":
            self._json(404, {"error": "Unknown API endpoint."})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > 12_000_000:
                raise ValueError("Request size must be between 1 byte and 12 MB.")
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
        except (ValueError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            self._json(400, {"error": f"Invalid JSON request: {exc}"})
            return
        if not isinstance(payload, dict):
            self._json(400, {"error": "Room data must be a JSON object."})
            return
        name_value = payload.get("name", "")
        if not isinstance(name_value, str):
            self._json(400, {"error": "Room name must be a string."})
            return
        name = name_value
        path = room_path(name)
        if path is None:
            self._json(400, {"error": "Room names can use only letters, numbers, underscores and hyphens (up to 64 characters)."})
            return
        width_value, height_value = payload.get("width", 0), payload.get("height", 0)
        tile_size_value = payload.get("tileSize", 16)
        # JSON booleans are Python ints by inheritance, so use exact type checks.
        # Do not silently truncate 2.5 to 2: the saved grid must match the UI.
        if any(type(value) is not int for value in (width_value, height_value, tile_size_value)):
            self._json(400, {"error": "Width, height and tileSize must be whole-number JSON integers."})
            return
        width, height, tile_size = width_value, height_value, tile_size_value
        tiles = payload.get("tiles")
        entrances = payload.get("entrances", {})
        projection = str(payload.get("projection", "orthogonal")).strip().lower()
        if projection == "isometric":
            projection = "oblique"
        if projection not in {"orthogonal", "oblique"}:
            self._json(400, {"error": "Projection must be orthogonal or oblique (isometric is accepted as an alias)."})
            return
        overlays = payload.get("overlays", [])
        if not isinstance(overlays, list):
            self._json(400, {"error": "overlays must be an array of {x, y, texture, layer} objects."})
            return
        if not (1 <= width <= 512 and 1 <= height <= 512 and 1 <= tile_size <= 256):
            self._json(400, {"error": "Room dimensions must be 1–512 tiles and tileSize must be 1–256."})
            return
        if not isinstance(tiles, list) or len(tiles) != height or any(not isinstance(row, list) or len(row) != width for row in tiles):
            self._json(400, {"error": "Tile grid rows and columns must exactly match width and height."})
            return
        texture_ids = {p.stem.lower() for p in TEXTURES.iterdir() if p.is_file() and p.suffix.lower() in IMAGE_EXTENSIONS} if TEXTURES.exists() else set()
        tile_scales = payload.get("tileScales")
        if tile_scales is None:
            tile_scales = [[1.0 for _ in range(width)] for _ in range(height)]
        if (not isinstance(tile_scales, list) or len(tile_scales) != height or
                any(not isinstance(row, list) or len(row) != width for row in tile_scales)):
            self._json(400, {"error": "tileScales must have the same rows and columns as tiles. Remove it for default 1× scale or reopen/save in Room Studio."})
            return
        tile_collision = payload.get("tileCollision")
        if tile_collision is None:
            tile_collision = [["inherit" for _ in range(width)] for _ in range(height)]
        if (not isinstance(tile_collision, list) or len(tile_collision) != height or
                any(not isinstance(row, list) or len(row) != width for row in tile_collision)):
            self._json(400, {"error": "tileCollision must have the same rows and columns as tiles."})
            return
        for y, row in enumerate(tile_collision):
            for x, mode in enumerate(row):
                if not isinstance(mode, str) or mode not in {"inherit", "solid", "none"}:
                    self._json(400, {"error": f"Collision mode at ({x}, {y}) must be inherit, solid, or none."})
                    return
        for y, row in enumerate(tile_scales):
            for x, scale in enumerate(row):
                if isinstance(scale, bool) or not isinstance(scale, (int, float)) or not (0.25 <= scale <= 4.0):
                    self._json(400, {"error": f"Texture scale at ({x}, {y}) must be a number between 0.25 and 4.0."})
                    return
        for y, row in enumerate(tiles):
            for x, tile in enumerate(row):
                if tile is not None and (not isinstance(tile, str) or tile.lower() not in texture_ids):
                    self._json(400, {"error": f"Tile ({x}, {y}) refers to unknown texture {tile!r}. Reload the palette and choose an available texture."})
                    return
        normalized_overlays = []
        seen_overlays = set()
        for index, overlay in enumerate(overlays):
            if not isinstance(overlay, dict):
                self._json(400, {"error": f"Overlay {index} must be an object with x, y, texture, and layer."})
                return
            x, y = overlay.get("x"), overlay.get("y")
            texture = str(overlay.get("texture", "")).lower()
            layer = str(overlay.get("layer", "foreground")).lower()
            if isinstance(x, bool) or not isinstance(x, int) or isinstance(y, bool) or not isinstance(y, int):
                self._json(400, {"error": f"Overlay {index} coordinates must be whole numbers."})
                return
            if not (0 <= x < width and 0 <= y < height):
                self._json(400, {"error": f"Overlay {index} at ({x}, {y}) is outside this room."})
                return
            if texture not in texture_ids:
                self._json(400, {"error": f"Overlay {index} refers to unknown texture {texture!r}. Reload the palette and choose an available texture."})
                return
            if layer not in {"background", "foreground", "above_entities"}:
                self._json(400, {"error": f"Overlay {index} layer must be background, foreground, or above_entities."})
                return
            identity = (x, y, texture, layer)
            if identity in seen_overlays:
                self._json(400, {"error": f"Overlay {index} duplicates the same texture at ({x}, {y}) in layer {layer!r}. Different textures may share a tile and layer."})
                return
            seen_overlays.add(identity)
            normalized_overlays.append({"x": x, "y": y, "texture": texture, "layer": layer})
        if not isinstance(entrances, dict):
            self._json(400, {"error": "Entrances must be an object of named {x, y} points."})
            return
        for entry_name, point in entrances.items():
            if not isinstance(entry_name, str) or not ROOM_NAME.fullmatch(entry_name) or not isinstance(point, dict):
                self._json(400, {"error": "Entrance names can use letters, numbers, underscores and hyphens; each needs x and y."})
                return
            x, y = point.get("x"), point.get("y")
            if type(x) is not int or type(y) is not int:
                self._json(400, {"error": f"Entrance '{entry_name}' needs whole-number JSON integer x and y coordinates."})
                return
            if not (0 <= x < width and 0 <= y < height):
                self._json(400, {"error": f"Entrance '{entry_name}' at ({x}, {y}) is outside this room."})
                return
        payload["schema"] = "cx-room"
        payload["version"] = 1
        payload["name"] = path.name[:-len(".room.json")]
        payload["tileScales"] = tile_scales
        payload["tileCollision"] = tile_collision
        payload["projection"] = projection
        payload["overlays"] = normalized_overlays
        temp: Path | None = None
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            # Use a unique same-directory temporary file so simultaneous saves
            # cannot collide, and replace the destination only after a complete write.
            fd, temp_name = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
            temp = Path(temp_name)
            with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
                stream.write(json.dumps(payload, indent=2, ensure_ascii=False) + "\n")
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temp, path)
            temp = None
        except OSError as exc:
            if temp is not None:
                try:
                    temp.unlink(missing_ok=True)
                except OSError:
                    pass
            self._json(500, {"error": f"Could not save room: {exc}"})
            return
        self._json(200, {"ok": True, "name": payload["name"], "path": str(path.relative_to(ROOT))})


def main() -> int:
    global ROOT, ROOMS, TEXTURES
    project_root = ROOT
    port = 8765
    args = iter(sys.argv[1:])
    for arg in args:
        if arg in {"-h", "--help"}:
            print("Usage: python3 tools/room_editor_server.py [--project-root PROJECT_DIR] [port]")
            print("Without --project-root, the parent directory of tools/ is used as the project.")
            return 0
        if arg == "--project-root":
            try:
                project_root = Path(next(args)).expanduser().resolve()
            except StopIteration:
                print("Usage: python3 tools/room_editor_server.py [--project-root PROJECT_DIR] [port]", file=sys.stderr)
                return 2
        else:
            try:
                port = int(arg)
            except ValueError:
                print("Usage: python3 tools/room_editor_server.py [--project-root PROJECT_DIR] [port]", file=sys.stderr)
                return 2
    if not project_root.is_dir():
        print(f"Project directory does not exist: {project_root}", file=sys.stderr)
        return 2
    ROOT = project_root
    ROOMS = ROOT / "rooms"
    TEXTURES = ROOT / "assets" / "textures"
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    print(f"CX Engine Room Studio: http://127.0.0.1:{port}/tools/room-editor.html")
    print(f"Game project: {ROOT}")
    print("This server is bound to localhost only. Press Ctrl+C to stop it.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nRoom editor server stopped.")
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
