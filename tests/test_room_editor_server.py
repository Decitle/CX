#!/usr/bin/env python3
"""Regression tests for Room Studio's local save/load API."""
from __future__ import annotations

import importlib.util
import json
import tempfile
import threading
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

SOURCE_ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "cx_room_editor_server", SOURCE_ROOT / "tools" / "room_editor_server.py"
)
assert SPEC is not None and SPEC.loader is not None
server_module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(server_module)


class RoomEditorServerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.tempdir = tempfile.TemporaryDirectory(prefix="cx-room-editor-test-")
        self.root = Path(self.tempdir.name)
        self.rooms = self.root / "rooms"
        self.textures = self.root / "assets" / "textures"
        self.rooms.mkdir(parents=True)
        self.textures.mkdir(parents=True)
        (self.textures / "grass.png").write_bytes(b"test image placeholder")
        (self.textures / "rock.png").write_bytes(b"test image placeholder")

        # Isolate the handler from the real project tree while exercising its real HTTP code.
        server_module.ROOT = self.root
        server_module.ROOMS = self.rooms
        server_module.TEXTURES = self.textures
        self.server = server_module.ThreadingHTTPServer(("127.0.0.1", 0), server_module.Handler)
        self.server.daemon_threads = True
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base_url = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)
        self.tempdir.cleanup()

    def post_room(self, payload: dict) -> tuple[int, dict]:
        request = Request(
            self.base_url + "/api/room",
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        try:
            with urlopen(request, timeout=3) as response:
                return response.status, json.loads(response.read().decode("utf-8"))
        except HTTPError as exc:
            return exc.code, json.loads(exc.read().decode("utf-8"))

    def get_room(self, name: str) -> tuple[int, dict]:
        try:
            with urlopen(self.base_url + "/api/room/" + name, timeout=3) as response:
                return response.status, json.loads(response.read().decode("utf-8"))
        except HTTPError as exc:
            return exc.code, json.loads(exc.read().decode("utf-8"))

    @staticmethod
    def room_payload(name: str = "roundtrip") -> dict:
        return {
            "schema": "cx-room",
            "version": 1,
            "name": name,
            "width": 2,
            "height": 1,
            "tileSize": 16,
            "projection": "oblique",
            "tiles": [["grass", "grass"]],
            "tileScales": [[1, 1.5]],
            "tileCollision": [["inherit", "solid"]],
            "overlays": [
                {"x": 0, "y": 0, "texture": "rock", "layer": "foreground"},
                {"x": 0, "y": 0, "texture": "grass", "layer": "foreground"},
                {"x": 1, "y": 0, "texture": "rock", "layer": "above_entities"},
            ],
            "entrances": {"spawn": {"x": 1, "y": 0}},
        }

    def test_editor_html_and_project_textures_are_served_from_their_expected_roots(self) -> None:
        with urlopen(self.base_url + "/tools/room-editor.html", timeout=3) as response:
            self.assertEqual(response.status, 200)
            self.assertIn(b"Room Studio", response.read())
        with urlopen(self.base_url + "/assets/textures/grass.png", timeout=3) as response:
            self.assertEqual(response.status, 200)
            self.assertEqual(response.read(), b"test image placeholder")

    def test_projection_scales_overlay_layers_and_entrances_round_trip(self) -> None:
        status, result = self.post_room(self.room_payload())
        self.assertEqual(status, 200, result)
        self.assertTrue(result.get("ok"))

        status, saved = self.get_room("roundtrip")
        self.assertEqual(status, 200, saved)
        self.assertEqual(saved["projection"], "oblique")
        self.assertEqual(saved["tileScales"], [[1, 1.5]])
        self.assertEqual(saved["tileCollision"], [["inherit", "solid"]])
        self.assertEqual(saved["entrances"], {"spawn": {"x": 1, "y": 0}})
        self.assertEqual(len(saved["overlays"]), 3)
        self.assertEqual(
            {(item["x"], item["y"], item["texture"], item["layer"]) for item in saved["overlays"]},
            {
                (0, 0, "rock", "foreground"),
                (0, 0, "grass", "foreground"),
                (1, 0, "rock", "above_entities"),
            },
        )

    def test_legacy_room_without_collision_grid_gets_inherit_defaults(self) -> None:
        payload = self.room_payload("legacy")
        payload.pop("tileCollision")
        status, result = self.post_room(payload)
        self.assertEqual(status, 200, result)
        status, saved = self.get_room("legacy")
        self.assertEqual(status, 200, saved)
        self.assertEqual(saved["tileCollision"], [["inherit", "inherit"]])

    def test_invalid_collision_mode_is_rejected_without_overwriting_room(self) -> None:
        original = self.room_payload("collision_stable")
        status, result = self.post_room(original)
        self.assertEqual(status, 200, result)

        malformed = self.room_payload("collision_stable")
        malformed["tileCollision"] = [["solid", "sometimes"]]
        status, result = self.post_room(malformed)
        self.assertEqual(status, 400, result)

        status, saved = self.get_room("collision_stable")
        self.assertEqual(status, 200, saved)
        self.assertEqual(saved["tileCollision"], [["inherit", "solid"]])

    def test_texture_palette_exposes_collision_defaults(self) -> None:
        (self.root / "assets" / "collision.json").write_text(json.dumps({
            "schema": "cx-collision", "version": 1, "default": "none",
            "textures": {"rock": "solid"},
        }), encoding="utf-8")
        with urlopen(self.base_url + "/api/textures", timeout=3) as response:
            payload = json.loads(response.read().decode("utf-8"))
        defaults = {texture["id"]: texture["collision"] for texture in payload["textures"]}
        self.assertEqual(defaults["rock"], "solid")
        self.assertEqual(defaults["grass"], "none")
        self.assertEqual(payload["warnings"], [])

    def test_invalid_collision_manifest_reports_warning_and_uses_legacy_defaults(self) -> None:
        (self.root / "assets" / "collision.json").write_text(json.dumps({
            "schema": "cx-collision", "version": 1, "default": "none",
            "textures": {"missing": "solid"},
        }), encoding="utf-8")
        with urlopen(self.base_url + "/api/textures", timeout=3) as response:
            payload = json.loads(response.read().decode("utf-8"))
        self.assertTrue(payload["warnings"])
        defaults = {texture["id"]: texture["collision"] for texture in payload["textures"]}
        self.assertEqual(defaults["grass"], "none")
        self.assertEqual(defaults["rock"], "none")

    def test_duplicate_normalized_collision_texture_ids_report_warning_and_fall_back(self) -> None:
        (self.root / "assets" / "collision.json").write_text(json.dumps({
            "schema": "cx-collision", "version": 1, "default": "none",
            "textures": {"rock": "none", " ROCK ": "solid"},
        }), encoding="utf-8")
        with urlopen(self.base_url + "/api/textures", timeout=3) as response:
            payload = json.loads(response.read().decode("utf-8"))
        self.assertTrue(any("repeats texture ID" in warning for warning in payload["warnings"]))
        defaults = {texture["id"]: texture["collision"] for texture in payload["textures"]}
        self.assertEqual(defaults["rock"], "none")
        self.assertEqual(defaults["grass"], "none")

    def test_non_integer_dimensions_are_rejected_without_overwriting_room(self) -> None:
        original = self.room_payload("stable")
        status, result = self.post_room(original)
        self.assertEqual(status, 200, result)

        malformed = self.room_payload("stable")
        malformed["width"] = 2.5
        status, result = self.post_room(malformed)
        self.assertEqual(status, 400, result)

        status, saved = self.get_room("stable")
        self.assertEqual(status, 200, saved)
        self.assertEqual(saved["width"], 2)
        self.assertEqual(saved["tiles"], [["grass", "grass"]])

    def test_boolean_or_fractional_entrance_coordinates_are_rejected(self) -> None:
        for point in ({"x": True, "y": 0}, {"x": 1.25, "y": 0}, {"x": 0, "y": "0"}):
            with self.subTest(point=point):
                payload = self.room_payload("bad_entrance")
                payload["entrances"] = {"spawn": point}
                status, result = self.post_room(payload)
                self.assertEqual(status, 400, result)
                self.assertFalse((self.rooms / "bad_entrance.room.json").exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
