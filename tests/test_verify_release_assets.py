from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
import zipfile

MODULE_PATH = Path(__file__).resolve().parents[1] / "tools" / "verify_release_assets.py"
SPEC = importlib.util.spec_from_file_location("cx_verify_release_assets", MODULE_PATH)
assert SPEC and SPEC.loader
verifier = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verifier)


class VerifyReleaseAssetsTests(unittest.TestCase):
    version = "0.10.14-preview.7"
    repository = "Decitle/CX"

    def create_valid_release(self, root: Path) -> None:
        root.mkdir(parents=True, exist_ok=True)
        for target, suffix in verifier.TARGETS.items():
            executable_suffix = ".exe" if target.startswith("windows-") else ""
            payloads = {
                f"CX-Engine/cx-engine{executable_suffix}": b"engine binary fixture",
                f"CX-Engine/cx-cli{executable_suffix}": b"cli binary fixture",
                "CX-Engine/README.md": b"Quickstart project instructions",
                "CX-Engine/config.json": b"{}\n",
                "CX-Engine/scripts/main.cx": b'broadcast "ready"\n',
                "LICENSES.txt": b"BSD and SFML license sections",
                "README.md": b"Runtime instructions",
            }
            manifest = {
                "product": "CX Engine",
                "version": self.version,
                "target": target,
                "package_type": "minimal-starter-runtime",
                "files": [
                    {
                        "path": name,
                        "size_bytes": len(data),
                        "sha256": hashlib.sha256(data).hexdigest(),
                    }
                    for name, data in sorted(payloads.items())
                ],
            }
            members = dict(payloads)
            members[verifier.MANIFEST_NAME] = (json.dumps(manifest, sort_keys=True) + "\n").encode()
            archive_name = f"CXEngine-v{self.version}-{target}{suffix}"
            archive_path = root / archive_name
            if suffix == ".zip":
                with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
                    for name, data in members.items():
                        archive.writestr(name, data)
            else:
                with tarfile.open(archive_path, "w:gz") as archive:
                    import io
                    for name, data in members.items():
                        info = tarfile.TarInfo(name)
                        info.size = len(data)
                        archive.addfile(info, io.BytesIO(data))
            digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
            (root / (archive_name + ".sha256")).write_text(f"{digest}  {archive_name}\n", encoding="utf-8")

        tag = f"v{self.version}"
        reference = (
            f"<div>Version-matched reference · CX Engine {tag}</div>"
            + "".join(
                f'<a href="https://github.com/{self.repository}/blob/{tag}/docs/{guide}">guide</a>'
                for guide in ("syntax.md", "engine-settings.md", "world-and-entities.md")
            )
        )
        (root / f"0_CX-Docs-v{self.version}.html").write_text(reference, encoding="utf-8")

    def test_accepts_all_six_runtime_packages_and_versioned_reference(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "assets"
            self.create_valid_release(root)
            self.assertEqual(verifier.verify_release_directory(root, self.version, self.repository), 13)

    def test_rejects_incomplete_asset_set(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "assets"
            self.create_valid_release(root)
            (root / f"CXEngine-v{self.version}-windows-arm64.zip").unlink()
            with self.assertRaisesRegex(RuntimeError, "asset set is incomplete"):
                verifier.verify_release_directory(root, self.version, self.repository)

    def test_rejects_incorrect_checksum_sidecar(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "assets"
            self.create_valid_release(root)
            sidecar = root / f"CXEngine-v{self.version}-windows-arm64.zip.sha256"
            sidecar.write_text("0" * 64 + "  wrong.zip\n", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "checksum sidecar"):
                verifier.verify_release_directory(root, self.version, self.repository)

    def test_rejects_unpinned_reference_guide(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "assets"
            self.create_valid_release(root)
            reference = root / f"0_CX-Docs-v{self.version}.html"
            reference.write_text("Version-matched reference v0.10.14-preview.7", encoding="utf-8")
            with self.assertRaisesRegex(RuntimeError, "not pinned"):
                verifier.verify_release_directory(root, self.version, self.repository)


    def test_rejects_duplicate_top_level_project_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "assets"
            self.create_valid_release(root)
            archive_path = root / f"CXEngine-v{self.version}-windows-x86_64.zip"
            members = verifier.read_archive(archive_path)
            extra = b'broadcast "duplicate"\n'
            members["project/scripts/main.cx"] = extra
            manifest = json.loads(members[verifier.MANIFEST_NAME])
            manifest["files"].append({
                "path": "project/scripts/main.cx",
                "size_bytes": len(extra),
                "sha256": hashlib.sha256(extra).hexdigest(),
            })
            members[verifier.MANIFEST_NAME] = (
                json.dumps(manifest, sort_keys=True) + "\n"
            ).encode()
            with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED) as packed:
                for name, data in members.items():
                    packed.writestr(name, data)
            with self.assertRaisesRegex(RuntimeError, "duplicate top-level project"):
                verifier.verify_runtime_archive(archive_path, "windows-x86_64", self.version)


if __name__ == "__main__":
    unittest.main()
