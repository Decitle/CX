#!/usr/bin/env python3
"""Confirm that the graphical engine stays alive while opening a starter project."""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


def prepare_smoke_project(source: Path, destination: Path) -> Path:
    """Create a disposable copy so startup scaffolding never modifies examples."""
    if not (source / "config.json").is_file() or not (source / "scripts" / "main.cx").is_file():
        raise ValueError(f"Starter project is invalid: {source}")
    shutil.copytree(source, destination)
    return destination


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine",type=Path,required=True)
    parser.add_argument("--project",type=Path,required=True)
    parser.add_argument("--seconds",type=float,default=6.0)
    args=parser.parse_args()
    if not args.engine.is_file(): parser.error(f"Graphical executable does not exist: {args.engine}")
    if not (args.project/"config.json").is_file(): parser.error(f"Starter project is invalid: {args.project}")
    with tempfile.TemporaryDirectory(prefix="cx-graphical-smoke-") as temporary:
        smoke_project = prepare_smoke_project(args.project.resolve(), Path(temporary) / "project")
        process=subprocess.Popen([str(args.engine.resolve()),str(smoke_project.resolve())],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            time.sleep(max(1.0,args.seconds))
            code=process.poll()
            if code is not None:
                stdout,stderr=process.communicate()
                print("Graphical engine exited before the smoke interval.",file=sys.stderr)
                if stdout: print(stdout,file=sys.stderr)
                if stderr: print(stderr,file=sys.stderr)
                return code or 1
            print(f"Graphical engine remained running for {args.seconds:.1f} seconds with a disposable copy of {args.project}.")
            return 0
        finally:
            if process.poll() is None:
                process.terminate()
                try: process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait(timeout=3)

if __name__=="__main__":
    raise SystemExit(main())
