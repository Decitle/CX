from __future__ import annotations

import hashlib
import importlib.util
import json
import subprocess
import sys
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

            (project / "scripts").mkdir(parents=True)
            (project / "config.json").write_text("{}\n", encoding="utf-8")
            (project / "scripts" / "main.cx").write_text("broadcast \"ready\"\n", encoding="utf-8")

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
                self.assertEqual(zipped.read("bin/cx-engine.exe"), b"Windows executable fixture")
                self.assertEqual(zipped.read("bin/cx-cli.exe"), b"Windows CLI fixture")
                self.assertEqual(zipped.read("bin/sfml-graphics-3.dll"), b"SFML DLL fixture")
                self.assertEqual(zipped.read("project/scripts/main.cx").replace(b"\r\n", b"\n"), b"broadcast \"ready\"\n")
                self.assertEqual(zipped.read("third-party-licenses/SFML-LICENSE.txt").replace(b"\r\n", b"\n"), b"SFML license fixture\n")
                payloads = {
                    name: zipped.read(name)
                    for name in ("bin/cx-engine.exe", "bin/cx-cli.exe", "bin/sfml-graphics-3.dll")
                }
                manifest = json.loads(zipped.read("runtime-manifest.json"))

            self.assertEqual(manifest["target"], "windows-x86_64")
            listed = {entry["path"]: entry for entry in manifest["files"]}
            for name, data in payloads.items():
                self.assertEqual(listed[name]["size_bytes"], len(data))
                self.assertEqual(listed[name]["sha256"], hashlib.sha256(data).hexdigest())


if __name__ == "__main__":
    unittest.main()
