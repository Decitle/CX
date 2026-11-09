#!/usr/bin/env python3
"""Package an installed CX Engine runtime with a minimal editable project."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tarfile
import tempfile
import zipfile


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def copy_tree(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise RuntimeError(f"Required project directory is missing: {source}")
    shutil.copytree(source, destination)



# These are Windows operating-system components, not application redistributables.
# CMake's dependency scanner can report them as resolved imports even though every
# supported Windows installation already provides them. Keep this list conservative:
# unknown DLLs (including a future shared SFML build) are retained for the application.
WINDOWS_SYSTEM_DLLS = frozenset({
    "advapi32.dll",
    "aepic.dll",
    "audioses.dll",
    "avrt.dll",
    "bcp47langs.dll",
    "bcrypt.dll",
    "bcryptprimitives.dll",
    "cabinet.dll",
    "cfgmgr32.dll",
    "cldapi.dll",
    "combase.dll",
    "comppkgsup.dll",
    "crypt32.dll",
    "cryptbase.dll",
    "cryptsp.dll",
    "dbghelp.dll",
    "devobj.dll",
    "devrtl.dll",
    "dhcpcsvc.dll",
    "dhcpcsvc6.dll",
    "dnsapi.dll",
    "dpapi.dll",
    "dsparse.dll",
    "faultrep.dll",
    "fltlib.dll",
    "gdi32.dll",
    "glu32.dll",
    "httpapi.dll",
    "icu.dll",
    "imagehlp.dll",
    "iphlpapi.dll",
    "kernel32.dll",
    "kernelbase.dll",
    "logoncli.dll",
    "mmdevapi.dll",
    "mpr.dll",
    "msasn1.dll",
    "msimg32.dll",
    "msvcp_win.dll",
    "msvcrt.dll",
    "ncrypt.dll",
    "netapi32.dll",
    "netutils.dll",
    "nrtapi.dll",
    "nsi.dll",
    "ntasn1.dll",
    "ntdll.dll",
    "oleaut32.dll",
    "opengl32.dll",
    "profapi.dll",
    "propsys.dll",
    "rmclient.dll",
    "rpcrt4.dll",
    "samlib.dll",
    "sechost.dll",
    "shcore.dll",
    "srvcli.dll",
    "sspicli.dll",
    "tbs.dll",
    "tokenbinding.dll",
    "umpdc.dll",
    "user32.dll",
    "userenv.dll",
    "webio.dll",
    "websocket.dll",
    "wer.dll",
    "win32u.dll",
    "winhttp.dll",
    "winmm.dll",
    "winmmbase.dll",
    "winnsi.dll",
    "winsta.dll",
    "wintrust.dll",
    "wkscli.dll",
    "wldap32.dll",
    "ws2_32.dll",
    "xmllite.dll",
})


def copy_runtime_stage(stage: Path, destination: Path, target: str) -> None:
    """Copy only runtime payloads from CMake's install prefix.

    SFML's install rules also populate headers, static libraries, CMake metadata,
    and docs. Those are SDK/development files, not required to run CX. Keep native
    executables and shared runtime libraries only; omit known Windows system DLLs.
    """
    if not stage.is_dir():
        raise RuntimeError(f"CMake install stage is missing: {stage}")

    windows = target.startswith("windows-")
    macos = target.startswith("macos-")
    copied = 0
    stage_root = stage.resolve()
    for source in sorted(stage.rglob("*")):
        if not source.is_file():
            continue
        if not source.resolve().is_relative_to(stage_root):
            raise RuntimeError(f"Runtime stage contains a symlink escaping its root: {source}")
        relative = source.relative_to(stage)
        parts = relative.parts
        top = parts[0] if parts else ""
        suffix = source.suffix.lower()
        name = source.name.lower()

        keep = False
        if top == "bin":
            if windows:
                keep = suffix == ".exe" or (
                    suffix == ".dll"
                    and name not in WINDOWS_SYSTEM_DLLS
                    and not name.startswith(("api-ms-", "ext-ms-"))
                )
            else:
                keep = suffix in {"", ".so", ".dylib"} or ".so." in name or ".dylib." in name
        elif top == "lib":
            if windows:
                keep = suffix == ".dll" and name not in WINDOWS_SYSTEM_DLLS and not name.startswith(("api-ms-", "ext-ms-"))
            elif macos:
                keep = ".dylib" in name
            else:
                keep = ".so" in name
        elif top == "Frameworks":
            keep = True

        if not keep:
            continue
        output = destination / relative
        output.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, output)
        copied += 1

    required = ("bin/cx-engine.exe", "bin/cx-cli.exe") if windows else ("bin/cx-engine", "bin/cx-cli")
    for relative in required:
        if not (destination / relative).is_file():
            raise RuntimeError(f"CMake install stage did not contain required runtime file: {relative}")
    if copied < len(required):
        raise RuntimeError("Runtime install stage did not contain the expected executable payload")


def collect_files(root: Path) -> list[Path]:
    files=[]
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"Runtime package may not contain symlinks: {path.relative_to(root)}")
        if path.is_file():
            rel=PurePosixPath(path.relative_to(root).as_posix())
            if any(part in {"build", "CMakeFiles", "__pycache__", ".git"} for part in rel.parts):
                raise RuntimeError(f"Unexpected generated directory in runtime: {rel}")
            files.append(path)
    return files


def find_sfml_license(build_dir: Path, explicit: Path | None = None) -> Path:
    """Find SFML's top-level license without scanning outside its SDK.

    FetchContent places the SFML checkout under ``_deps/sfml-src``. Installed
    SDKs expose ``SFML_DIR`` (normally ``<sdk>/lib/cmake/SFML``) or a prefix
    through ``CMAKE_PREFIX_PATH``. Only a small set of known SDK roots is
    inspected; walking every ancestor recursively could descend into /proc.
    """
    if explicit is not None:
        candidate = explicit.expanduser().resolve()
        if not candidate.is_file():
            raise RuntimeError(f"The supplied SFML license file does not exist: {candidate}")
        return candidate

    roots: list[Path] = [build_dir / "_deps" / "sfml-src"]
    cache = build_dir / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("SFML_DIR:") and "=" in line:
                sfml_dir = Path(line.split("=", 1)[1].strip()).expanduser()
                # SFML_DIR is typically <sdk>/lib/cmake/SFML. Find the first
                # ancestor that actually looks like an SFML SDK root.
                for ancestor in (sfml_dir, *sfml_dir.parents):
                    if (ancestor / "include" / "SFML").is_dir():
                        roots.append(ancestor)
                        break
            elif line.startswith("CMAKE_PREFIX_PATH:") and "=" in line:
                for entry in line.split("=", 1)[1].split(os.pathsep):
                    if entry:
                        prefix = Path(entry).expanduser()
                        if (prefix / "include" / "SFML").is_dir():
                            roots.append(prefix)

    license_names = ("license.md", "license", "license.txt", "license-mit", "copying")
    checked: set[Path] = set()
    for root in roots:
        root = root.expanduser().resolve()
        if root in checked or not root.is_dir():
            continue
        checked.add(root)
        for name in license_names:
            candidate = root / name
            if candidate.is_file():
                return candidate

    raise RuntimeError(
        "Could not locate SFML's top-level license. Use a FetchContent SFML build, "
        "set SFML_DIR to an installed SFML SDK, or pass --sfml-license."
    )


def make_manifest(root: Path, target: str, version: str) -> None:
    files=collect_files(root)
    manifest={
        "product":"CX Engine",
        "version":version,
        "target":target,
        "package_type":"minimal-starter-runtime",
        "files":[
            {"path":p.relative_to(root).as_posix(),"size_bytes":p.stat().st_size,"sha256":sha256(p)}
            for p in files
        ],
    }
    (root/"runtime-manifest.json").write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")


def verify_manifest(root: Path) -> int:
    path=root/"runtime-manifest.json"
    data=json.loads(path.read_text(encoding="utf-8"))
    listed={entry["path"]:entry for entry in data["files"]}
    actual={p.relative_to(root).as_posix():p for p in collect_files(root) if p.name!="runtime-manifest.json"}
    if set(actual)!=set(listed):
        raise RuntimeError(f"Runtime manifest path mismatch: missing={set(listed)-set(actual)}, extra={set(actual)-set(listed)}")
    for name,p in actual.items():
        item=listed[name]
        if p.stat().st_size!=item["size_bytes"] or sha256(p)!=item["sha256"]:
            raise RuntimeError(f"Runtime manifest hash/size mismatch: {name}")
    return len(actual)


def archive(root: Path, output: Path, fmt: str) -> None:
    output.parent.mkdir(parents=True,exist_ok=True)
    if fmt=="zip":
        with zipfile.ZipFile(output,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=9) as zf:
            for path in collect_files(root):
                rel=path.relative_to(root).as_posix()
                info=zipfile.ZipInfo(rel,date_time=(2026,11,8,0,0,0))
                info.compress_type=zipfile.ZIP_DEFLATED
                info.create_system=3
                info.external_attr=(stat.S_IFREG|stat.S_IMODE(path.stat().st_mode))<<16
                zf.writestr(info,path.read_bytes(),compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)
        with zipfile.ZipFile(output) as zf:
            bad=zf.testzip()
            if bad: raise RuntimeError(f"Runtime ZIP integrity failure: {bad}")
    else:
        with tarfile.open(output,"w:gz",compresslevel=9) as tf:
            for path in collect_files(root):
                tf.add(path,arcname=path.relative_to(root).as_posix(),recursive=False)
        with tarfile.open(output,"r:gz") as tf:
            if any(not m.isfile() for m in tf.getmembers()):
                raise RuntimeError("Runtime TAR.GZ contains unexpected non-file entries")
    digest=sha256(output)
    output.with_suffix(output.suffix+".sha256").write_text(f"{digest}  {output.name}\n",encoding="utf-8")
    print(f"ARCHIVE: {output}\nSHA256: {digest}\nFILES: {verify_manifest(root)} + manifest")


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage",type=Path,required=True,help="CMake install prefix containing bin/")
    parser.add_argument("--project",type=Path,required=True,help="Minimal starter project folder")
    parser.add_argument("--build-dir",type=Path,required=True,help="CMake build directory used to configure SFML")
    parser.add_argument("--sfml-license",type=Path,help="Explicit SFML license file when using a custom installed SDK")
    parser.add_argument("--source-root",type=Path,default=Path.cwd())
    parser.add_argument("--target",required=True,help="OS/architecture label, e.g. linux-x86_64")
    parser.add_argument("--version",default=os.environ.get("CX_VERSION","0.10.14"))
    parser.add_argument("--output-dir",type=Path,required=True)
    args=parser.parse_args()
    try:
        exe="cx-engine.exe" if args.target.startswith("windows-") else "cx-engine"
        cli="cx-cli.exe" if args.target.startswith("windows-") else "cx-cli"
        if not (args.stage/"bin"/exe).is_file():
            raise RuntimeError(f"Graphical runtime executable is missing from install stage: bin/{exe}")
        if not (args.stage/"bin"/cli).is_file():
            raise RuntimeError(f"CLI executable is missing from install stage: bin/{cli}")
        if not (args.project/"config.json").is_file() or not (args.project/"scripts"/"main.cx").is_file():
            raise RuntimeError("Starter project must contain config.json and scripts/main.cx")
        with tempfile.TemporaryDirectory(prefix="cx-runtime-package-") as tmp:
            root=Path(tmp)/"CXEngine"
            root.mkdir()
            copy_runtime_stage(args.stage, root, args.target)
            copy_tree(args.project,root/"project")
            shutil.copy2(args.source_root/"LICENSE",root/"LICENSE")
            shutil.copy2(args.source_root/"docs"/"runtime-package.md",root/"README.md")
            shutil.copy2(args.source_root/"THIRD_PARTY_NOTICES.md",root/"THIRD_PARTY_NOTICES.md")
            third=root/"third-party-licenses"
            third.mkdir()
            shutil.copy2(find_sfml_license(args.build_dir, args.sfml_license),third/"SFML-LICENSE.txt")
            make_manifest(root,args.target,args.version)
            verify_manifest(root)
            suffix=".zip" if args.target.startswith(("windows-","macos-")) else ".tar.gz"
            output=args.output_dir/f"CXEngine-v{args.version}-{args.target}{suffix}"
            archive(root,output,"zip" if suffix==".zip" else "tar.gz")
    except (OSError,RuntimeError,ValueError,json.JSONDecodeError,tarfile.TarError,zipfile.BadZipFile) as exc:
        parser.exit(1,f"ERROR: {exc}\n")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
