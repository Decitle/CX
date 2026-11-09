#!/usr/bin/env python3
"""Verify the SFML frontend gives a clear error when Wayland lacks XWayland."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: test_wayland_no_xwayland.py PATH_TO_CX_ENGINE", file=sys.stderr)
        return 2

    engine = Path(sys.argv[1]).resolve()
    if not engine.is_file():
        print(f"Graphical executable does not exist: {engine}", file=sys.stderr)
        return 2

    environment = os.environ.copy()
    # Simulate a Wayland session in which XWayland is unavailable. Keeping
    # WAYLAND_DISPLAY set exercises the actionable XWayland-specific message.
    environment.pop("DISPLAY", None)
    environment["WAYLAND_DISPLAY"] = "wayland-test"
    environment["XDG_SESSION_TYPE"] = "wayland"

    with tempfile.TemporaryDirectory(prefix="cx-no-xwayland-") as temporary:
        project = Path(temporary) / "empty-project"
        project.mkdir()
        result = subprocess.run(
            [str(engine), str(project)],
            env=environment,
            capture_output=True,
            text=True,
            timeout=8,
            check=False,
        )
        message = result.stderr + result.stdout
        expected = "This Wayland session does not expose an XWayland DISPLAY"
        if result.returncode != 2 or expected not in message:
            print("CX did not report the expected missing-XWayland error.", file=sys.stderr)
            print(f"exit={result.returncode}\n{message}", file=sys.stderr)
            return 1
        if any(project.iterdir()):
            print("CX modified/scaffolded the project despite the unavailable display.", file=sys.stderr)
            return 1

    print("PASS: missing XWayland is explained and project files are left untouched.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
