"""Release contract tests use synthetic title files, never console binaries."""
import importlib.util
import json
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("ps5_release", ROOT / "tools/ps5-release.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.app = self.root / "app"
        files = ["eboot.bin", "sce_sys/icon0.png", "sce_module/libc.prx",
                 "assets/ui/atlas.rgba", "assets/ui/fonts/Inter-Regular.ttf",
                 "assets/ui/fonts/Inter-SemiBold.ttf", "LICENSE", "THIRD_PARTY_NOTICES.md"]
        for name in files:
            path = self.app / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"test-only fixture: " + name.encode())
        (self.app / "sce_sys/param.json").write_text(json.dumps({
            "titleId": "PPSA99221", "masterVersion": "10.00", "contentVersion": "10.000.000"}))
        self.elf = self.root / "client.elf"
        self.elf.write_bytes(b"\x7fELF synthetic app")
        self.broker = self.root / "broker.elf"
        self.broker.write_bytes(b"\x7fELF synthetic broker")
        self.license = self.root / "broker-license"
        self.license.write_text("test license")
        self.pkg = self.root / "validated.pkg"
        self.pkg.write_bytes(b"synthetic validated PKG fixture")
        self.config = self.root / "broker_config.hpp"
        self.config.write_text("test-only config")
        self.broker_json = self.root / "broker.json"
        self.broker_json.write_text(json.dumps({"elf_sha256": release.sha(self.broker),
                                               "config_sha256": release.sha(self.config)}))
        self.receipt = self.root / "receipt.json"
        release.record_build(self.elf, self.config, self.broker_json, self.receipt)
        # Override only the fixture runtime digest; the production CLI has no bypass.
        runtime = patch.object(release, "RUNTIME_SHA", release.sha(self.app / "sce_module/libc.prx"))
        runtime.start()
        self.addCleanup(runtime.stop)
        self.output = self.root / "out/release"

    def export(self, output=None):
        return release.export_release(self.app, self.pkg, self.broker, self.license,
                                      self.receipt, self.elf, output or self.output, "10.00")

    def test_dual_format_same_bytes_and_external_companion(self):
        self.export()
        with zipfile.ZipFile(self.output / "MirrorPSX-PS5-v10.00.zip") as archive:
            for file in self.app.rglob("*"):
                if file.is_file():
                    self.assertEqual(file.read_bytes(), archive.read("PPSA99221/" + file.relative_to(self.app).as_posix()))
            self.assertFalse(any("companion" in name.lower() for name in archive.namelist()))
        self.assertEqual(self.pkg.read_bytes(), (self.output / "MirrorPSX-PS5-v10.00.pkg").read_bytes())
        self.assertEqual(self.broker.read_bytes(), (self.output / "OpenProspero-SharedBroker.elf").read_bytes())
        manifest = json.loads((self.output / "release.json").read_text())
        self.assertTrue(manifest["external_companion_required"])
        self.assertEqual(manifest["folder_destination"], "/data/homebrew/PPSA99221")
        for entry in manifest["files"]:
            self.assertEqual(entry["sha256"], release.sha(self.output / entry["name"]))
        for line in (self.output / "SHA256SUMS.txt").read_text().splitlines():
            digest, name = line.split("  ", 1)
            self.assertEqual(digest, release.sha(self.output / name))

    def test_wrong_companion_is_rejected(self):
        self.broker.write_bytes(b"\x7fELF different broker")
        with self.assertRaisesRegex(ValueError, "do not match"):
            self.export()
        self.assertFalse(self.output.exists())

    def test_changed_app_is_rejected(self):
        self.elf.write_bytes(b"\x7fELF different app")
        with self.assertRaisesRegex(ValueError, "do not match"):
            self.export()

    def test_changed_config_requires_broker_rebuild(self):
        self.config.write_text("new config")
        with self.assertRaisesRegex(ValueError, "configuration changed"):
            release.record_build(self.elf, self.config, self.broker_json, self.receipt)

    def test_runtime_corruption_is_rejected(self):
        (self.app / "sce_module/libc.prx").write_bytes(b"damaged")
        with self.assertRaisesRegex(ValueError, "runtime checksum"):
            self.export()

    def test_missing_asset_is_rejected(self):
        (self.app / "assets/ui/atlas.rgba").unlink()
        with self.assertRaisesRegex(ValueError, "Missing title file"):
            self.export()

    def test_private_pairing_file_is_rejected(self):
        (self.app / "pairing.key").write_bytes(b"test secret")
        with self.assertRaisesRegex(ValueError, "Private/runtime"):
            self.export()

    def test_bundled_companion_is_rejected(self):
        (self.app / "openprospero").mkdir()
        (self.app / "openprospero/companion.elf").write_bytes(b"test")
        with self.assertRaisesRegex(ValueError, "Private/runtime"):
            self.export()

    def test_wrong_title_is_rejected(self):
        (self.app / "sce_sys/param.json").write_text(json.dumps({"titleId": "OTHER"}))
        with self.assertRaisesRegex(ValueError, "identity/version"):
            self.export()

    def test_existing_output_is_not_overwritten(self):
        self.export()
        marker = self.output / "existing"
        marker.write_text("preserve")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.export()
        self.assertEqual(marker.read_text(), "preserve")

    def test_nested_output_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "separate"):
            self.export(self.app / "release")


if __name__ == "__main__":
    unittest.main()
