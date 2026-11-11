from __future__ import annotations

import hashlib
import importlib.util
import json
import re
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "package_release.py"
SPEC = importlib.util.spec_from_file_location("cx_package_release", MODULE_PATH)
assert SPEC and SPEC.loader
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)

RUNTIME_MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "package_runtime.py"
RUNTIME_SPEC = importlib.util.spec_from_file_location("cx_package_runtime", RUNTIME_MODULE_PATH)
assert RUNTIME_SPEC and RUNTIME_SPEC.loader
runtime_package = importlib.util.module_from_spec(RUNTIME_SPEC)
RUNTIME_SPEC.loader.exec_module(runtime_package)

SMOKE_MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "smoke_graphical.py"
SMOKE_SPEC = importlib.util.spec_from_file_location("cx_smoke_graphical", SMOKE_MODULE_PATH)
assert SMOKE_SPEC and SMOKE_SPEC.loader
smoke_graphical = importlib.util.module_from_spec(SMOKE_SPEC)
SMOKE_SPEC.loader.exec_module(smoke_graphical)


class PackageReleaseTests(unittest.TestCase):
    def make_repo(self, root: Path) -> None:
        for name in package.ROOT_FILES:
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"required {name}\n", encoding="utf-8")
        for directory in package.SOURCE_ROOTS:
            (root / directory).mkdir(parents=True, exist_ok=True)
        (root / "src" / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        (root / "include" / "main.hpp").write_text("#pragma once\n", encoding="utf-8")
        (root / "tests" / "main_test.cpp").write_text("int test() { return 1; }\n", encoding="utf-8")
        (root / "docs" / "reference.html").write_text("<!doctype html><title>Reference</title>", encoding="utf-8")
        (root / "examples" / "hello.cx").write_text("broadcast \\\"hello\\\"\n", encoding="utf-8")

    def test_source_zip_and_tar_gz_verify(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "repo"
            root.mkdir()
            self.make_repo(root)
            output = Path(temporary) / "release"
            zip_path = output / "source.zip"
            tar_path = output / "source.tar.gz"
            package.package_zip(root, zip_path)
            package.package_tar_gz(root, tar_path)
            self.assertGreater(package.verify_archive(zip_path)[0], 0)
            self.assertGreater(package.verify_archive(tar_path)[0], 0)

    def test_internal_and_build_files_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_repo(root)
            (root / "logs").mkdir()
            (root / "logs" / "test.txt").write_text("internal", encoding="utf-8")
            collected = package.collect_source(root)
            self.assertFalse(any(name.startswith("logs/") for name, _, _ in collected))

    def test_generated_project_save_files_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_repo(root)
            save = root / "examples" / "quickstart" / "variables.csv"
            save.parent.mkdir(parents=True, exist_ok=True)
            save.write_text("score,42\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "generated project save data"):
                package.collect_source(root)

    def test_installed_sfml_sdk_license_is_found_without_scanning_parent_filesystems(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / "SFML-3.1.0"
            (sdk / "include" / "SFML").mkdir(parents=True)
            (sdk / "lib" / "cmake" / "SFML").mkdir(parents=True)
            expected = sdk / "license.md"
            expected.write_text("SFML license fixture\n", encoding="utf-8")
            build = root / "build"
            build.mkdir()
            cache = build / "CMakeCache.txt"
            cache.write_text(
                f"SFML_DIR:PATH={sdk / 'lib' / 'cmake' / 'SFML'}\n",
                encoding="utf-8",
            )
            self.assertEqual(runtime_package.find_sfml_license(build), expected.resolve())

    def test_unexpected_binary_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_repo(root)
            (root / "src" / "bad.exe").write_bytes(b"not source")
            with self.assertRaisesRegex(RuntimeError, "compiled artifact, cache, or nested archive"):
                package.collect_source(root)

    def test_runtime_packager_rejects_generated_starter_save_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            project = Path(temporary) / "quickstart"
            (project / "scripts").mkdir(parents=True)
            (project / "config.json").write_text("{}\n", encoding="utf-8")
            (project / "scripts" / "main.cx").write_text("broadcast \"hello\"\n", encoding="utf-8")
            (project / "variables.csv").write_text("score,0\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "generated persistent state"):
                runtime_package.validate_starter_project(project)

    def test_graphical_smoke_copies_project_before_startup(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            (source / "scripts").mkdir(parents=True)
            (source / "config.json").write_text("{}\n", encoding="utf-8")
            (source / "scripts" / "main.cx").write_text("broadcast \"hello\"\n", encoding="utf-8")
            copied = smoke_graphical.prepare_smoke_project(source, root / "smoke-copy")
            (copied / "variables.csv").write_text("score,1\n", encoding="utf-8")
            self.assertFalse((source / "variables.csv").exists())
            self.assertTrue((copied / "variables.csv").is_file())


    def test_windows_system_dependency_filter_matches_cmake_lowercase_names(self) -> None:
        cmake_text = (MODULE_PATH.parents[1] / "CMakeLists.txt").read_text(encoding="utf-8")
        block = re.search(
            r"PRE_EXCLUDE_REGEXES\s+(.*?)\s+POST_EXCLUDE_REGEXES",
            cmake_text,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(block, "CMake runtime dependency exclusion block must exist")
        patterns = re.findall(r'"([^"]+)"', block.group(1))
        dll_pattern = next((pattern for pattern in patterns if "azureattest" in pattern), None)
        self.assertIsNotNone(dll_pattern, "CMake must explicitly filter the Windows runner-only system components")
        assert dll_pattern is not None
        # CMake quoted arguments unescape each doubled source backslash before regex matching.
        dll_pattern = dll_pattern.replace("\\\\", "\\")
        self.assertEqual(dll_pattern, dll_pattern.lower(), "CMake matches lowercased Windows DLL names")

        system_names = (
            "AzureAttestManager.dll",
            "AzureAttestNormal.dll",
            "HvsiFileTrust.dll",
            "PdmUtilities.dll",
            "wpaxholder.dll",
            "shell32.dll",
            "shlwapi.dll",
            "dbgeng.dll",
            "d2d1.dll",
            "uiautomationcore.dll",
            "esent.dll",
            "coreuicomponents.dll",
            "certenroll.dll",
            "setupapi.dll",
            "windows.storage.dll",
        )
        for name in system_names:
            with self.subTest(name=name):
                self.assertRegex(name.lower(), dll_pattern)

    def test_windows_runtime_zip_contains_executables_dependencies_and_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            stage = root / "stage"
            project = root / "quickstart"
            build = root / "build"
            output = root / "dist"
            sdk = root / "SFML-3.1.0"

            (source / "LICENSE").parent.mkdir(parents=True)
            (source / "LICENSE").write_text("CX license fixture\n", encoding="utf-8")
            (source / "THIRD_PARTY_NOTICES.md").write_text("Third-party notices\n", encoding="utf-8")
            (source / "docs").mkdir()
            (source / "docs" / "runtime-package.md").write_text("Runtime instructions\n", encoding="utf-8")

            (stage / "bin").mkdir(parents=True)
            (stage / "bin" / "cx-engine.exe").write_bytes(b"Windows executable fixture")
            (stage / "bin" / "cx-cli.exe").write_bytes(b"Windows CLI fixture")
            (stage / "bin" / "sfml-graphics-3.dll").write_bytes(b"SFML DLL fixture")
            (stage / "bin" / "kernel32.dll").write_bytes(b"Windows system DLL must not be redistributed")
            for name in (
                "AzureAttestManager.dll", "AzureAttestNormal.dll", "HvsiFileTrust.dll",
                "PdmUtilities.dll", "wpaxholder.dll", "shell32.dll", "shlwapi.dll",
                "dbgeng.dll", "d2d1.dll", "uiautomationcore.dll", "esent.dll",
                "coreuicomponents.dll", "certenroll.dll", "setupapi.dll",
                "windows.storage.dll",
            ):
                (stage / "bin" / name).write_bytes(b"Windows system component must not be bundled")
            (stage / "include" / "SFML").mkdir(parents=True)
            (stage / "include" / "SFML" / "Graphics.hpp").write_text("development header\\n", encoding="utf-8")
            (stage / "lib").mkdir(parents=True)
            (stage / "lib" / "sfml-graphics-s.lib").write_bytes(b"static development library")
            (stage / "share" / "doc" / "SFML").mkdir(parents=True)
            (stage / "share" / "doc" / "SFML" / "readme.md").write_text("development documentation\\n", encoding="utf-8")

            (project / "scripts").mkdir(parents=True)
            (project / "config.json").write_text("{}\n", encoding="utf-8")
            (project / "README.md").write_text("Quickstart project\n", encoding="utf-8")
            (project / "scripts" / "main.cx").write_text("broadcast \"ready\"\n", encoding="utf-8")

            (sdk / "include" / "SFML").mkdir(parents=True)
            (sdk / "lib" / "cmake" / "SFML").mkdir(parents=True)
            (sdk / "license.md").write_text("SFML license fixture\n", encoding="utf-8")
            build.mkdir()
            (build / "CMakeCache.txt").write_text(
                f"SFML_DIR:PATH={sdk / 'lib' / 'cmake' / 'SFML'}\n",
                encoding="utf-8",
            )
            sfml_tree = build / "_deps" / "sfml-src"
            (sfml_tree / "license.md").parent.mkdir(parents=True, exist_ok=True)
            (sfml_tree / "license.md").write_text("SFML license fixture\n", encoding="utf-8")
            (sfml_tree / "extlibs" / "headers" / "qoi").mkdir(parents=True)
            (sfml_tree / "extlibs" / "headers" / "cpp-unicodelib").mkdir(parents=True)
            (sfml_tree / "extlibs" / "headers" / "glad" / "include" / "glad").mkdir(parents=True)
            (sfml_tree / "extlibs" / "headers" / "stb_image").mkdir(parents=True)
            (sfml_tree / "extlibs" / "headers" / "qoi" / "qoi.h").write_text(
                "SPDX-License-Identifier: MIT\nCopyright (c) 2021, Dominic Szablewski",
                encoding="utf-8",
            )
            (sfml_tree / "extlibs" / "headers" / "cpp-unicodelib" / "unicodelib.h").write_text(
                "Copyright (c) 2025 Yuji Hirose. All rights reserved.\nMIT License",
                encoding="utf-8",
            )
            (sfml_tree / "extlibs" / "headers" / "glad" / "include" / "glad" / "gl.h").write_text(
                "SPDX-License-Identifier: (WTFPL OR CC0-1.0) AND Apache-2.0",
                encoding="utf-8",
            )
            stb_fixture = (
                "ALTERNATIVE A - MIT License\nCopyright (c) 2017 Sean Barrett\n"
                "Permission is hereby granted.\nTHE SOFTWARE IS PROVIDED AS IS.\n"
                "ALTERNATIVE B - Public Domain\n"
            )
            for name in ("stb_image.h", "stb_image_write.h"):
                (sfml_tree / "extlibs" / "headers" / "stb_image" / name).write_text(
                    stb_fixture, encoding="utf-8"
                )
            dependency_licenses = {
                "_deps/freetype-src/docs/FTL.TXT": "FreeType fixture license",
                "_deps/freetype-src/src/bdf/README": "BDF fixture license",
                "_deps/freetype-src/src/pcf/README": "PCF fixture license",
                "_deps/harfbuzz-src/COPYING": "HarfBuzz fixture license",
                "_deps/harfbuzz-src/src/ms-use/COPYING": "HarfBuzz Microsoft fixture license",
                "_deps/sheenbidi-src/LICENSE": "SheenBidi fixture license",
            }
            for relative, body in dependency_licenses.items():
                license_path = build / relative
                license_path.parent.mkdir(parents=True, exist_ok=True)
                license_path.write_text(body, encoding="utf-8")
            hash_license = (
                "/*\\n * Copyright 2000 Computing Research Labs, New Mexico State University\\n"
                " * Copyright 2001-2015 Francesco Zappa Nardelli\\n"
                " * Permission is granted, free of charge, to any person obtaining a copy\\n"
                " * of this software and associated documentation files (the Software)\\n"
                " */"
            )
            for relative in (
                "_deps/freetype-src/src/base/fthash.c",
                "_deps/freetype-src/include/freetype/internal/fthash.h",
            ):
                license_path = build / relative
                license_path.parent.mkdir(parents=True, exist_ok=True)
                license_path.write_text(hash_license, encoding="utf-8")

            command = [
                sys.executable, str(RUNTIME_MODULE_PATH),
                "--stage", str(stage),
                "--project", str(project),
                "--build-dir", str(build),
                "--source-root", str(source),
                "--target", "windows-x86_64",
                "--version", "0.10.14-test",
                "--output-dir", str(output),
            ]
            subprocess.run(command, check=True, capture_output=True, text=True)

            archive = output / "CXEngine-v0.10.14-test-windows-x86_64.zip"
            sidecar = archive.with_suffix(archive.suffix + ".sha256")
            self.assertTrue(archive.is_file())
            self.assertTrue(sidecar.is_file())
            expected_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
            self.assertTrue(sidecar.read_text(encoding="utf-8").startswith(expected_hash))

            with zipfile.ZipFile(archive) as zipped:
                self.assertIsNone(zipped.testzip())
                self.assertEqual(zipped.read("CX-Engine/cx-engine.exe"), b"Windows executable fixture")
                self.assertEqual(zipped.read("CX-Engine/cx-cli.exe"), b"Windows CLI fixture")
                self.assertEqual(zipped.read("CX-Engine/sfml-graphics-3.dll"), b"SFML DLL fixture")
                names = set(zipped.namelist())
                self.assertNotIn("CX-Engine/kernel32.dll", names, "Windows system DLLs must not be bundled")
                for name in ("AzureAttestManager.dll", "AzureAttestNormal.dll", "HvsiFileTrust.dll", "PdmUtilities.dll", "wpaxholder.dll"):
                    self.assertNotIn(f"CX-Engine/{name}", names, f"{name} is a Windows system component and must not be bundled")
                self.assertFalse(any(name.startswith("include/") for name in names), "development headers must not be bundled")
                self.assertFalse(any(name.startswith("bin/") or name.startswith("lib/") for name in names), "runtime files must be flattened inside CX-Engine/")
                self.assertFalse(any(name.startswith("lib/") for name in names), "static libraries and SDK metadata must not be bundled")
                self.assertFalse(any(name.startswith("share/") for name in names), "SDK documentation must not be bundled")
                self.assertFalse(any(name.startswith("project/") for name in names))
                self.assertIn("README.md", names)
                self.assertIn("LICENSES.txt", names)
                license_text = zipped.read("LICENSES.txt").decode("utf-8")
                self.assertIn("CX license fixture", license_text)
                self.assertIn("SFML license fixture", license_text)
                self.assertIn("FreeType fixture license", license_text)
                self.assertIn("BDF fixture license", license_text)
                self.assertIn("PCF fixture license", license_text)
                self.assertIn("FreeType BDF hash helper — MIT LICENSE", license_text)
                self.assertIn("HarfBuzz fixture license", license_text)
                self.assertIn("HarfBuzz Microsoft fixture license", license_text)
                self.assertIn("SheenBidi fixture license", license_text)
                self.assertIn("QOI — MIT LICENSE", license_text)
                self.assertIn("cpp-unicodelib — MIT LICENSE", license_text)
                self.assertIn("glad generated loader — CC0-1.0", license_text)
                self.assertIn("glad generated loader — APACHE LICENSE 2.0", license_text)
                self.assertIn("stb_image and stb_image_write — MIT LICENSE", license_text)
                self.assertEqual(sum(name == "LICENSES.txt" for name in names), 1)
                self.assertNotIn("THIRD_PARTY_NOTICES.md", names)
                self.assertFalse(any(name.startswith("third-party-licenses/") for name in names))
                self.assertEqual(zipped.read("CX-Engine/scripts/main.cx").replace(b"\r\n", b"\n"), b"broadcast \"ready\"\n")
                self.assertEqual(zipped.read("CX-Engine/config.json").replace(b"\r\n", b"\n"), b"{}\n")
                payloads = {
                    name: zipped.read(name)
                    for name in ("CX-Engine/cx-engine.exe", "CX-Engine/cx-cli.exe", "CX-Engine/sfml-graphics-3.dll")
                }
                manifest = json.loads(zipped.read("CX-Engine/runtime-manifest.json"))

            self.assertEqual(manifest["target"], "windows-x86_64")
            listed = {entry["path"]: entry for entry in manifest["files"]}
            for name, data in payloads.items():
                self.assertEqual(listed[name]["size_bytes"], len(data))
                self.assertEqual(listed[name]["sha256"], hashlib.sha256(data).hexdigest())


    def test_linux_runtime_archive_flattens_shared_libraries_into_engine_folder(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            stage = root / "stage"
            project = root / "quickstart"
            build = root / "build"
            output = root / "dist"
            sdk = root / "SFML-3.1.0"

            (source / "LICENSE").parent.mkdir(parents=True)
            (source / "LICENSE").write_text("CX license fixture\n", encoding="utf-8")
            (source / "docs").mkdir()
            (source / "docs" / "runtime-package.md").write_text("Runtime instructions\n", encoding="utf-8")
            (stage / "bin").mkdir(parents=True)
            (stage / "bin" / "cx-engine").write_bytes(b"Linux executable fixture")
            (stage / "bin" / "cx-cli").write_bytes(b"Linux CLI fixture")
            (stage / "lib").mkdir(parents=True)
            (stage / "lib" / "libsfml-graphics.so.3.1.0").write_bytes(b"SFML shared library fixture")

            (project / "scripts").mkdir(parents=True)
            (project / "config.json").write_text("{}\n", encoding="utf-8")
            (project / "README.md").write_text("Quickstart project\n", encoding="utf-8")
            (project / "scripts" / "main.cx").write_text('broadcast "ready"\n', encoding="utf-8")

            (sdk / "include" / "SFML").mkdir(parents=True)
            (sdk / "lib" / "cmake" / "SFML").mkdir(parents=True)
            (sdk / "license.md").write_text("SFML license fixture\n", encoding="utf-8")
            build.mkdir()
            (build / "CMakeCache.txt").write_text(
                f"SFML_DIR:PATH={sdk / 'lib' / 'cmake' / 'SFML'}\n",
                encoding="utf-8",
            )

            command = [
                sys.executable, str(RUNTIME_MODULE_PATH),
                "--stage", str(stage),
                "--project", str(project),
                "--build-dir", str(build),
                "--source-root", str(source),
                "--target", "linux-x86_64",
                "--version", "0.10.14-test",
                "--output-dir", str(output),
            ]
            subprocess.run(command, check=True, capture_output=True, text=True)

            archive = output / "CXEngine-v0.10.14-test-linux-x86_64.tar.gz"
            with tarfile.open(archive, "r:gz") as packed:
                names = {member.name for member in packed.getmembers()}
                self.assertIn("CX-Engine/cx-engine", names)
                self.assertIn("CX-Engine/cx-cli", names)
                self.assertIn("CX-Engine/libsfml-graphics.so.3.1.0", names)
                self.assertIn("CX-Engine/runtime-manifest.json", names)
                self.assertFalse(any(name.startswith("project/") for name in names))
                self.assertIn("README.md", names)
                self.assertIn("LICENSES.txt", names)
                self.assertIn("CX-Engine/scripts/main.cx", names)
                self.assertFalse(any(name.startswith("bin/") or name.startswith("lib/") for name in names))
                self.assertNotIn("LICENSE", names)
                self.assertNotIn("THIRD_PARTY_NOTICES.md", names)
            cmake_text = (MODULE_PATH.parents[1] / "CMakeLists.txt").read_text(encoding="utf-8")
            self.assertIn('set(CX_INSTALL_RPATH "$ORIGIN")', cmake_text)


if __name__ == "__main__":
    unittest.main()
