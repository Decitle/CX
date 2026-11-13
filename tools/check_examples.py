#!/usr/bin/env python3
"""Syntax-check every standalone CX example and each multi-file example project."""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run cx-cli --check against every .cx file under an examples directory."
    )
    parser.add_argument("--cli", required=True, help="Path to the built cx-cli executable.")
    parser.add_argument("--examples", default="examples", help="Examples directory to scan.")
    return parser.parse_args()


def run_check(cli: Path, project: Path, label: str, repo_root: Path) -> bool:
    try:
        result = subprocess.run(
            [str(cli), "--check", str(project)],
            cwd=repo_root,
            text=True,
            capture_output=True,
            timeout=45,
            check=False,
        )
    except subprocess.TimeoutExpired:
        print(f"FAIL: {label}: cx-cli --check exceeded 45 seconds.", file=sys.stderr)
        return False
    except OSError as exc:
        print(f"FAIL: {label}: could not run cx-cli: {exc}", file=sys.stderr)
        return False

    if result.returncode == 0:
        print(f"PASS: {label}")
        return True

    print(f"FAIL: {label} (exit code {result.returncode})", file=sys.stderr)
    if result.stdout:
        print(result.stdout, file=sys.stderr, end="" if result.stdout.endswith("\n") else "\n")
    if result.stderr:
        print(result.stderr, file=sys.stderr, end="" if result.stderr.endswith("\n") else "\n")
    return False


def main() -> int:
    args = parse_args()
    repo_root = Path(__file__).resolve().parents[1]
    cli_arg = Path(args.cli).expanduser()
    cli = (repo_root / cli_arg).resolve() if not cli_arg.is_absolute() else cli_arg.resolve()
    examples_arg = Path(args.examples).expanduser()
    examples = (repo_root / examples_arg).resolve() if not examples_arg.is_absolute() else examples_arg.resolve()

    if not cli.is_file():
        print(f"cx-cli executable not found: {cli}", file=sys.stderr)
        return 2
    if not examples.is_dir():
        print(f"Examples directory not found: {examples}", file=sys.stderr)
        return 2

    all_scripts = sorted(path.resolve() for path in examples.rglob("*.cx") if path.is_file())
    if not all_scripts:
        print(f"No .cx example scripts found under {examples}", file=sys.stderr)
        return 2

    project_roots: list[Path] = []
    scripts_in_projects: set[Path] = set()
    for scripts_dir in sorted(path for path in examples.rglob("scripts") if path.is_dir()):
        project_scripts = sorted(path.resolve() for path in scripts_dir.rglob("*.cx") if path.is_file())
        if not project_scripts:
            continue
        project_roots.append(scripts_dir.parent.resolve())
        scripts_in_projects.update(project_scripts)

    failures = 0
    for project_root in project_roots:
        relative = project_root.relative_to(examples)
        if not run_check(cli, project_root, f"project {relative.as_posix()}", repo_root):
            failures += 1

    standalone = [path for path in all_scripts if path not in scripts_in_projects]
    for source in standalone:
        relative = source.relative_to(examples)
        with tempfile.TemporaryDirectory(prefix="cx-example-check-") as temporary:
            project = Path(temporary)
            scripts = project / "scripts"
            scripts.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, scripts / source.name)
            if not run_check(cli, project, f"script {relative.as_posix()}", repo_root):
                failures += 1

    if failures:
        print(
            f"Example syntax checks failed: {failures} of "
            f"{len(project_roots) + len(standalone)} checks.",
            file=sys.stderr,
        )
        return 1

    print(
        f"All example syntax checks passed: {len(project_roots)} project(s), "
        f"{len(standalone)} standalone script(s), {len(all_scripts)} source file(s)."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
