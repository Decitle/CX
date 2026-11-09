#!/usr/bin/env python3
"""Create and verify clean, deterministic CX Engine source distributions.

The source archives contain repository files only. Runtime binaries are built
and packaged by native CI jobs so that no host-specific executable leaks into
source ZIP/TAR.GZ distributions.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import os
from pathlib import Path, PurePosixPath
import stat
import sys
import tarfile
import zipfile

VERSION = __import__("os").environ.get("CX_VERSION", "0.10.14")
ROOT_FILES = (
    ".gitignore", "CHANGELOG.md", "CMakeLists.txt", "CONTRIBUTING.md",
    "LICENSE", "README.md", "THIRD_PARTY_NOTICES.md",
)
SOURCE_ROOTS = (".github", "docs", "examples", "include", "licenses", "src", "tests", "tools")
FORBIDDEN_PARTS = {
    ".git", ".venv", "venv", "node_modules", "__pycache__", ".pytest_cache",
    ".mypy_cache", ".ruff_cache", "CMakeFiles", "Testing", "dist", "target",
    "build", "out", "handoff", "handoffs", "logs", "private", "internal",
}
FORBIDDEN_SUFFIXES = {
    ".pyc", ".pyo", ".o", ".obj", ".a", ".lib", ".so", ".dll", ".dylib",
    ".exe", ".class", ".zip", ".7z", ".rar", ".tar", ".gz", ".xz", ".zst",
    ".sha256", ".core", ".dmp",
}
ALLOWED_SUFFIXES = {
    ".md", ".html", ".yml", ".yaml", ".cpp", ".hpp", ".cx", ".json", ".py",
    ".txt", ".cff", ".cfg", ".ini", ".css", ".js",
}
GENERATED_RUNTIME_FILES = {
    ".variables.lock", "variables.csv", "variables.csv.bak",
    "variables.cxv", "variables.cxv.bak", "variables.meta",
}
MANIFEST = f"PACKAGE-MANIFEST-v{VERSION}.tsv"
FIXED_TIME = (2026, 11, 8, 0, 0, 0)
FIXED_EPOCH = 1794096000  # 2026-11-08 00:00:00 UTC


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def forbidden_path(name: str) -> str | None:
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        return "unsafe archive path"
    if any(part in FORBIDDEN_PARTS or part.startswith("build-") for part in path.parts):
        return "generated, private, or internal directory"
    if path.suffix.lower() in FORBIDDEN_SUFFIXES:
        return "compiled artifact, cache, or nested archive"
    if path.name in GENERATED_RUNTIME_FILES or path.name.endswith(".tmp"):
        return "generated project save data or temporary file"
    lowered = name.lower()
    if any(token in lowered for token in ("prompt_log", "project_journal", "comment_audit", "continuation_protocol")):
        return "internal development record"
    if "validation-v" in lowered or "package-manifest-v" in lowered:
        return "generated validation/manifest file"
    return None


def collect_source(root: Path) -> list[tuple[str, bytes, int]]:
    files: list[tuple[str, bytes, int]] = []
    missing = [name for name in ROOT_FILES if not (root / name).is_file()]
    if missing:
        raise RuntimeError("Required repository files are missing: " + ", ".join(missing))

    paths = [root / name for name in ROOT_FILES]
    for directory in SOURCE_ROOTS:
        base = root / directory
        if not base.is_dir():
            raise RuntimeError(f"Required source directory is missing: {directory}")

        # Walk top-down so generated/private directories are pruned before their
        # contents are visited. A source checkout may legitimately have build
        # output or Python caches beside its source; those are excluded from the
        # archive rather than making an otherwise clean source package fail.
        for current, dirnames, filenames in os.walk(base, topdown=True, followlinks=False):
            current_path = Path(current)
            kept_directories: list[str] = []
            for dirname in dirnames:
                candidate = current_path / dirname
                if candidate.is_symlink():
                    raise RuntimeError(f"Refusing symlink directory in source package: {candidate.relative_to(root).as_posix()}")
                if dirname in FORBIDDEN_PARTS or dirname.startswith("build-"):
                    continue
                kept_directories.append(dirname)
            dirnames[:] = kept_directories

            for filename in filenames:
                candidate = current_path / filename
                if candidate.is_symlink():
                    raise RuntimeError(f"Refusing symlink in source package: {candidate.relative_to(root).as_posix()}")
                if candidate.is_file():
                    paths.append(candidate)

    seen: set[str] = set()
    for path in sorted(paths, key=lambda p: p.relative_to(root).as_posix()):
        rel = path.relative_to(root).as_posix()
        issue = forbidden_path(rel)
        if issue:
            raise RuntimeError(f"Refusing to package {rel}: {issue}")
        if path.is_symlink():
            raise RuntimeError(f"Refusing symlink in source package: {rel}")
        if rel not in ROOT_FILES:
            suffix = path.suffix.lower()
            if suffix and suffix not in ALLOWED_SUFFIXES:
                raise RuntimeError(f"Unexpected file type needs explicit review: {rel}")
            if not suffix and path.name not in {".gitignore", ".nojekyll"}:
                raise RuntimeError(f"Unexpected extensionless file needs explicit review: {rel}")
        if rel in seen:
            raise RuntimeError(f"Duplicate source path: {rel}")
        seen.add(rel)
        files.append((rel, path.read_bytes(), stat.S_IMODE(path.stat().st_mode)))

    if not any(name.startswith("src/") for name, _, _ in files):
        raise RuntimeError("Source tree has no src/ files")
    if not any(name.startswith("include/") for name, _, _ in files):
        raise RuntimeError("Source tree has no include/ files")
    if not any(name.startswith("tests/") for name, _, _ in files):
        raise RuntimeError("Source tree has no tests/")
    if not any(name == "docs/reference.html" for name, _, _ in files):
        raise RuntimeError("Source tree is missing the interactive language reference")
    return sorted(files, key=lambda entry: entry[0])


def manifest_bytes(entries: list[tuple[str, bytes, int]]) -> bytes:
    lines = [
        f"# CX Engine v{VERSION} source package manifest",
        "# Columns: SHA256<TAB>SIZE_BYTES<TAB>ARCHIVE_PATH",
        "# The manifest does not hash itself; every other file is listed.",
    ]
    lines.extend(f"{sha256(data)}\t{len(data)}\t{name}" for name, data, _ in entries)
    return ("\n".join(lines) + "\n").encode("utf-8")


def zip_info(name: str, mode: int) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, date_time=FIXED_TIME)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = (stat.S_IFREG | mode) << 16
    return info


def package_zip(root: Path, output: Path) -> str:
    entries = collect_source(root)
    manifest = manifest_bytes(entries)
    output.parent.mkdir(parents=True, exist_ok=True)
    tmp = output.with_suffix(output.suffix + ".tmp")
    with zipfile.ZipFile(tmp, "w", allowZip64=True) as zf:
        for name, data, mode in entries:
            zf.writestr(zip_info(name, mode), data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
        zf.writestr(zip_info(MANIFEST, 0o644), manifest, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
    tmp.replace(output)
    digest = sha256(output.read_bytes())
    output.with_suffix(output.suffix + ".sha256").write_text(f"{digest}  {output.name}\n", encoding="utf-8")
    verify_archive(output)
    print(f"ZIP: {output}\nFILES: {len(entries)} + manifest\nSHA256: {digest}")
    return digest


def package_tar_gz(root: Path, output: Path) -> str:
    entries = collect_source(root)
    manifest = manifest_bytes(entries)
    output.parent.mkdir(parents=True, exist_ok=True)
    tmp = output.with_suffix(output.suffix + ".tmp")
    with tmp.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0) as gz:
            with tarfile.open(fileobj=gz, mode="w", format=tarfile.PAX_FORMAT) as tf:
                for name, data, mode in entries:
                    item = tarfile.TarInfo(name)
                    item.size = len(data)
                    item.mode = mode
                    item.uid = 0
                    item.gid = 0
                    item.uname = ""
                    item.gname = ""
                    item.mtime = FIXED_EPOCH
                    tf.addfile(item, io.BytesIO(data))
                item = tarfile.TarInfo(MANIFEST)
                item.size = len(manifest)
                item.mode = 0o644
                item.uid = item.gid = 0
                item.uname = item.gname = ""
                item.mtime = FIXED_EPOCH
                tf.addfile(item, io.BytesIO(manifest))
    tmp.replace(output)
    digest = sha256(output.read_bytes())
    output.with_suffix(output.suffix + ".sha256").write_text(f"{digest}  {output.name}\n", encoding="utf-8")
    verify_archive(output)
    print(f"TAR.GZ: {output}\nFILES: {len(entries)} + manifest\nSHA256: {digest}")
    return digest


def verify_entries(items: list[tuple[str, bytes]]) -> tuple[int, int]:
    names = [name for name, _ in items]
    if len(names) != len(set(names)):
        raise RuntimeError("Archive contains duplicate file paths")
    # Verification is independent of the caller's CX_VERSION environment.
    # A preview archive should still verify on a machine that has no release
    # environment configured, so discover the single embedded manifest by name.
    manifests = [
        name for name in names
        if PurePosixPath(name).name.startswith("PACKAGE-MANIFEST-v")
        and PurePosixPath(name).name.endswith(".tsv")
    ]
    if len(manifests) != 1:
        raise RuntimeError(f"Expected exactly one versioned PACKAGE-MANIFEST-v*.tsv; found {manifests}")
    manifest_name = manifests[0]
    listed: dict[str, tuple[str, int]] = {}
    for line in dict(items)[manifest_name].decode("utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        digest, size, name = line.split("\t", 2)
        listed[name] = (digest, int(size))
    if set(names) != set(listed) | {manifest_name}:
        raise RuntimeError("Manifest path list does not exactly match archive contents")
    total = 0
    mapping = dict(items)
    for name, (digest, size) in listed.items():
        issue = forbidden_path(name)
        if issue:
            raise RuntimeError(f"Archive contains forbidden path {name}: {issue}")
        data = mapping[name]
        if len(data) != size or sha256(data) != digest:
            raise RuntimeError(f"Manifest mismatch for {name}")
        total += size
    if not {"CMakeLists.txt", "LICENSE", "README.md", "docs/reference.html"}.issubset(listed):
        raise RuntimeError("Source archive is missing required repository content")
    if any(name.startswith(("logs/", "build/", "handoff/", "internal/")) for name in listed):
        raise RuntimeError("Source archive includes development or generated material")
    return len(listed), total


def verify_archive(archive: Path) -> tuple[int, int]:
    if archive.name.endswith(".zip"):
        with zipfile.ZipFile(archive, "r") as zf:
            bad = zf.testzip()
            if bad:
                raise RuntimeError(f"ZIP CRC/integrity failure at {bad}")
            items = [(item.filename, zf.read(item.filename)) for item in zf.infolist() if not item.is_dir()]
    elif archive.name.endswith(".tar.gz") or archive.name.endswith(".tgz"):
        with tarfile.open(archive, "r:gz") as tf:
            members = [item for item in tf.getmembers() if item.isfile()]
            if any(not item.isfile() for item in tf.getmembers()):
                raise RuntimeError("TAR.GZ source archive may contain regular files only")
            items = []
            for member in members:
                stream = tf.extractfile(member)
                if stream is None:
                    raise RuntimeError(f"Could not read TAR.GZ member {member.name}")
                items.append((member.name, stream.read()))
    else:
        raise RuntimeError("Expected .zip, .tar.gz, or .tgz archive")
    return verify_entries(items)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    source_cmd = commands.add_parser("source", help="Build a clean source archive")
    source_cmd.add_argument("--format", choices=("zip", "tar.gz", "all"), default="zip")
    source_cmd.add_argument("--output", type=Path, help="Output archive path (required unless --format all)")
    source_cmd.add_argument("--output-dir", type=Path, help="Output directory when --format all is selected")
    source_cmd.add_argument("--root", type=Path, default=Path.cwd())
    verify_cmd = commands.add_parser("verify", help="Verify manifest, hashes, and archive contents")
    verify_cmd.add_argument("--archive", type=Path, required=True)

    args = parser.parse_args()
    try:
        if args.command == "source":
            if args.format == "all":
                if not args.output_dir:
                    parser.error("source --format all requires --output-dir")
                base = args.output_dir
                package_zip(args.root, base / f"CXEngine-v{VERSION}-source.zip")
                package_tar_gz(args.root, base / f"CXEngine-v{VERSION}-source.tar.gz")
            else:
                if not args.output:
                    parser.error("source requires --output")
                (package_zip if args.format == "zip" else package_tar_gz)(args.root, args.output)
        else:
            count, size = verify_archive(args.archive)
            print(f"VERIFIED: {args.archive}\nFILES: {count}\nUNCOMPRESSED: {size} bytes")
    except (OSError, RuntimeError, ValueError, zipfile.BadZipFile, tarfile.TarError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
