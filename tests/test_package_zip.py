import tempfile
import unittest
import zipfile
from pathlib import Path

from scripts.package_zip import create_archive


class PackageZipTests(unittest.TestCase):
    def test_layout_bytes_and_unix_permissions(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "vmrp-v1.2.3-linux-amd64"
            (source / "bin").mkdir(parents=True)
            (source / "bin" / "main").write_bytes(b"executable")
            (source / "README.txt").write_bytes(b"instructions")
            destination = root / "release.zip"
            create_archive(source, destination, ["bin/main"])
            with zipfile.ZipFile(destination) as archive:
                binary = archive.getinfo(f"{source.name}/bin/main")
                readme = archive.getinfo(f"{source.name}/README.txt")
                self.assertEqual(archive.read(binary), b"executable")
                self.assertEqual(binary.create_system, 3)
                self.assertEqual(binary.external_attr >> 16, 0o100755)
                self.assertEqual(readme.external_attr >> 16, 0o100644)
                self.assertEqual(len(archive.infolist()), 2)

    def test_invalid_input_preserves_existing_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "package"
            source.mkdir()
            destination = root / "release.zip"
            destination.write_bytes(b"previous release")
            with self.assertRaisesRegex(ValueError, "Missing package executable"):
                create_archive(source, destination, ["bin/main"])
            self.assertEqual(destination.read_bytes(), b"previous release")
            with self.assertRaisesRegex(ValueError, "outside"):
                create_archive(source, source / "release.zip", [])
            self.assertEqual(list(root.glob("*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
