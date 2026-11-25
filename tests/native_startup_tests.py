"""Exercise native launcher startup and executable-relative package loading."""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from tools.mrp_regression import CASES, read_pixels, validate_capture

ROOT = Path(__file__).resolve().parent.parent


class NativeStartupTests(unittest.TestCase):
    simulator: Path

    def setUp(self) -> None:
        temporary = tempfile.TemporaryDirectory(prefix="mrpx-startup-")
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        self.binary = self.directory / "program" / self.simulator.name
        self.binary.parent.mkdir()
        shutil.copy2(self.simulator, self.binary)
        shutil.copytree(ROOT / "assets" / "runtime", self.binary.parent, dirs_exist_ok=True)
        self.fixture = self.binary.parent / "mythroad" / "asm.mrp"
        shutil.copy2(ROOT / "tests" / "fixtures" / "mrp" / "asm.mrp", self.fixture)
        self.environment = os.environ.copy()
        self.environment.pop("VMRP_RUNTIME_DIR", None)
        self.environment.update(
            VMRP_DISABLE_NETWORK="1", SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
        )
        self.screenshot = self.directory / "screen.bmp"

    def run_program(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [str(self.binary), *arguments, "--screenshot", str(self.screenshot), "--capture-delay", "1200"],
            cwd=self.directory, env=self.environment, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", timeout=20,
        )

    def test_default_startup_opens_the_application_list(self) -> None:
        completed = self.run_program()
        self.assertEqual(completed.returncode, 0, completed.stdout)
        self.assertIn("bridge_dsm_init success", completed.stdout)
        self.assertIn("bridge_dsm_mr_start_dsm('dsm_gm.mrp','start.mr',NULL): 0x0", completed.stdout)
        pixels = read_pixels(self.screenshot)
        self.assertNotEqual(pixels, b"\0" * (240 * 320 * 3))
        explicit = self.run_program("--mrp", "mythroad/dsm_gm.mrp")
        self.assertEqual(explicit.returncode, 0, explicit.stdout)
        self.assertEqual(read_pixels(self.screenshot), pixels)

    def test_missing_default_launcher_is_an_explicit_error(self) -> None:
        (self.binary.parent / "mythroad" / "dsm_gm.mrp").unlink()
        completed = self.run_program()
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("Could not start application", completed.stdout)
        self.assertNotIn("bridge_dsm_init success", completed.stdout)

    def test_mythroad_relative_and_absolute_paths_load_the_original_package(self) -> None:
        before = hashlib.sha256(self.fixture.read_bytes()).digest()
        for path in ("mythroad/asm.mrp", str(self.fixture)):
            with self.subTest(path=path):
                completed = self.run_program("--mrp", path)
                validate_capture(CASES[2], completed.returncode, completed.stdout, self.screenshot)
        self.assertEqual(hashlib.sha256(self.fixture.read_bytes()).digest(), before)

    def test_outside_and_invalid_packages_are_explicit_errors(self) -> None:
        outside = self.directory / "outside.mrp"
        shutil.copy2(self.fixture, outside)
        invalid = self.fixture.with_name("invalid.mrp")
        invalid.write_bytes(b"not an MRP")
        for path in (str(outside), "../outside.mrp", "invalid.mrp"):
            with self.subTest(path=path):
                completed = self.run_program("--mrp", path)
                self.assertNotEqual(completed.returncode, 0)
                self.assertIn("Could not start application", completed.stdout)
                self.assertNotIn("bridge_dsm_init success", completed.stdout)

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--simulator", type=Path, required=True)
    args = parser.parse_args()
    NativeStartupTests.simulator = args.simulator.resolve()
    unittest.main(argv=[sys.argv[0]])
