#!/usr/bin/env python3
"""Validate the complete set of CX Engine assets before publishing a GitHub release."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import tarfile
import zipfile


TARGETS = {
    "windows-x86_64": ".zip",
    "windows-arm64": ".zip",
    "linux-x86_64": ".tar.gz",
    "linux-arm64": ".tar.gz",
    "macos-x86_64": ".zip",
    "macos-arm64": ".zip",
}
MANIFEST_NAME = "runtime-manifest.json"
REQUIRED_COMMON = {
    "LICENSE",
    "README.md",
    "THIRD_PARTY_NOTICES.md",
    "third-party-licenses/SFML-LICENSE.txt",
    "project/config.json",
    "project/scripts/main.cx",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def safe_archive_path(name: str) -> None:
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts or not name:
        raise RuntimeError(f"Unsafe archive path: {name!r}")


def read_archive(path: Path) -> dict[str, bytes]:
    files: dict[str, bytes] = {}
    if path.name.endswith(".zip"):
        with zipfile.ZipFile(path) as archive:
            bad = archive.testzip()
            if bad is not None:
                raise RuntimeError(f"{path.name}: corrupt ZIP member {bad}")
            members = archive.infolist()
            if any(member.is_dir() for member in members):
                raise RuntimeError(f"{path.name}: directory entries are not expected")
            if len({member.filename for member in members}) != len(members):
                raise RuntimeError(f"{path.name}: duplicate archive paths")
            for member in members:
                safe_archive_path(member.filename)
                files[member.filename] = archive.read(member)
    else:
        with tarfile.open(path, "r:gz") as archive:
            members = archive.getmembers()
            if any(not member.isfile() for member in members):
                raise RuntimeError(f"{path.name}: non-regular archive member")
            if len({member.name for member in members}) != len(members):
                raise RuntimeError(f"{path.name}: duplicate archive paths")
            for member in members:
                safe_archive_path(member.name)
                stream = archive.extractfile(member)
                if stream is None:
                    raise RuntimeError(f"{path.name}: could not read {member.name}")
                files[member.name] = stream.read()
    return files


def verify_runtime_archive(path: Path, target: str, version: str) -> None:
    files = read_archive(path)
    if MANIFEST_NAME not in files:
        raise RuntimeError(f"{path.name}: missing {MANIFEST_NAME}")
    try:
        manifest = json.loads(files[MANIFEST_NAME].decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError(f"{path.name}: invalid runtime manifest: {error}") from error

    expected_metadata = {
        "product": "CX Engine",
        "version": version,
        "target": target,
        "package_type": "minimal-starter-runtime",
    }
    for key, expected in expected_metadata.items():
        if manifest.get(key) != expected:
            raise RuntimeError(
                f"{path.name}: manifest {key}={manifest.get(key)!r}, expected {expected!r}"
            )

    entries = manifest.get("files")
    if not isinstance(entries, list):
        raise RuntimeError(f"{path.name}: manifest files must be an array")
    listed: dict[str, dict] = {}
    for entry in entries:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str):
            raise RuntimeError(f"{path.name}: malformed manifest entry")
        name = entry["path"]
        safe_archive_path(name)
        if name == MANIFEST_NAME or name in listed:
            raise RuntimeError(f"{path.name}: duplicate or self-referencing manifest path {name}")
        listed[name] = entry

    actual = {name: data for name, data in files.items() if name != MANIFEST_NAME}
    if set(actual) != set(listed):
        missing = sorted(set(listed) - set(actual))
        extra = sorted(set(actual) - set(listed))
        raise RuntimeError(f"{path.name}: manifest mismatch; missing={missing}, unlisted={extra}")

    for name, data in actual.items():
        entry = listed[name]
        if entry.get("size_bytes") != len(data):
            raise RuntimeError(f"{path.name}: size mismatch for {name}")
        if entry.get("sha256") != sha256(data):
            raise RuntimeError(f"{path.name}: SHA-256 mismatch for {name}")

    executable_suffix = ".exe" if target.startswith("windows-") else ""
    required = set(REQUIRED_COMMON)
    required.update({f"bin/cx-engine{executable_suffix}", f"bin/cx-cli{executable_suffix}"})
    missing = sorted(required - set(actual))
    if missing:
        raise RuntimeError(f"{path.name}: missing required runtime files: {missing}")


def expected_asset_names(version: str) -> set[str]:
    names = {f"CXEngine-v{version}-reference.html"}
    for target, suffix in TARGETS.items():
        archive_name = f"CXEngine-v{version}-{target}{suffix}"
        names.add(archive_name)
        names.add(archive_name + ".sha256")
    return names


def verify_release_directory(directory: Path, version: str, repository: str) -> int:
    if not directory.is_dir():
        raise RuntimeError(f"Release asset directory does not exist: {directory}")
    expected = expected_asset_names(version)
    actual = {path.name for path in directory.iterdir() if path.is_file()}
    if actual != expected:
        missing = sorted(expected - actual)
        extra = sorted(actual - expected)
        raise RuntimeError(f"Release asset set is incomplete; missing={missing}, extra={extra}")

    for target, suffix in TARGETS.items():
        archive_name = f"CXEngine-v{version}-{target}{suffix}"
        archive_path = directory / archive_name
        digest = sha256(archive_path.read_bytes())
        sidecar_path = directory / (archive_name + ".sha256")
        expected_sidecar = f"{digest}  {archive_name}"
        if sidecar_path.read_text(encoding="utf-8").strip() != expected_sidecar:
            raise RuntimeError(f"{archive_name}: checksum sidecar does not match the archive")
        verify_runtime_archive(archive_path, target, version)

    tag = f"v{version}"
    reference_name = f"CXEngine-v{version}-reference.html"
    reference = (directory / reference_name).read_text(encoding="utf-8")
    if "Version-matched reference" not in reference or tag not in reference:
        raise RuntimeError(f"{reference_name}: missing the matching release version banner")
    for guide in ("syntax.md", "engine-settings.md", "world-and-entities.md"):
        pinned_url = f"https://github.com/{repository}/blob/{tag}/docs/{guide}"
        if pinned_url not in reference:
            raise RuntimeError(f"{reference_name}: guide link is not pinned to {tag}: {guide}")
    return len(expected)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--repository", required=True)
    args = parser.parse_args()
    try:
        count = verify_release_directory(args.directory, args.version, args.repository)
    except (OSError, RuntimeError, ValueError, tarfile.TarError, zipfile.BadZipFile) as error:
        parser.exit(1, f"ERROR: {error}\n")
    print(f"Verified {count} release assets: six runtime archives, six SHA-256 sidecars, and one version-matched HTML reference.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
