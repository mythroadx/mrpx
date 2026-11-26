import hashlib
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools import mrp_regression as regression


class CaptureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.screenshot = self.directory / "screenshot.bmp"
        self.pixels = b"\x00\x00\xff" * 240 + b"\x00\x00\x00" * (240 * 319)
        self.case = regression.Case("test", "asm.mrp", hashlib.sha256(self.pixels).hexdigest())
        self.log = (
            "INFO: VM started: package=asm.mrp module=start.mr\n"
            f"Saved screenshot: {self.screenshot}\n"
        )

    def write_bmp(self, bits: int = 24, top_down: bool = False) -> None:
        rows = [self.pixels[start:start + 720] for start in range(0, len(self.pixels), 720)]
        if not top_down:
            rows.reverse()
        body = b"".join(rows)
        if bits == 32:
            body = b"".join(body[index:index + 3] + b"\xff" for index in range(0, len(body), 3))
        header = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
        header += struct.pack("<IiiHHIIiiII", 40, 240, -320 if top_down else 320,
                              1, bits, 0, len(body), 0, 0, 0, 0)
        self.screenshot.write_bytes(header + body)

    def test_pixel_normalization(self) -> None:
        for bits in (24, 32):
            for top_down in (False, True):
                with self.subTest(bits=bits, top_down=top_down):
                    self.write_bmp(bits, top_down)
                    regression.validate_capture(self.case, 0, self.log, self.screenshot)

    def test_exit_zero_without_screenshot_is_failure(self) -> None:
        with self.assertRaises(FileNotFoundError):
            regression.validate_capture(self.case, 0, self.log, self.screenshot)

    def test_failure_logs_and_exit_codes(self) -> None:
        self.write_bmp()
        for code, log in ((1, self.log), (0, ""), (0, self.log + "Invalid Guest range")):
            with self.subTest(code=code, log=log):
                with self.assertRaises(ValueError):
                    regression.validate_capture(self.case, code, log, self.screenshot)

    def test_corrupt_bmp_is_failure(self) -> None:
        for offset, replacement in ((0, b"XX"), (18, struct.pack("<i", 100)),
                                    (10, struct.pack("<I", 0)),
                                    (30, struct.pack("<I", 1))):
            with self.subTest(offset=offset):
                self.write_bmp()
                data = bytearray(self.screenshot.read_bytes())
                data[offset:offset + len(replacement)] = replacement
                self.screenshot.write_bytes(data)
                with self.assertRaises(ValueError):
                    regression.validate_capture(self.case, 0, self.log, self.screenshot)
        self.write_bmp()
        self.screenshot.write_bytes(self.screenshot.read_bytes()[:-1])
        with self.assertRaises(ValueError):
            regression.read_pixels(self.screenshot)

    def test_blank_or_changed_pixels_are_failure(self) -> None:
        for pixels, message in ((bytes(len(self.pixels)), "single color"),
                                (b"\x00\xff\x00" + self.pixels[3:], "baseline mismatch")):
            with self.subTest(message=message):
                self.pixels = pixels
                self.write_bmp()
                with self.assertRaisesRegex(ValueError, message):
                    regression.validate_capture(self.case, 0, self.log, self.screenshot)

    def test_clock_exclusion_does_not_hide_body_changes(self) -> None:
        height = 295
        case = regression.Case("clock", "asm.mrp",
                               hashlib.sha256(self.pixels[:240 * height * 3]).hexdigest(),
                               height=height)
        self.pixels = self.pixels[:240 * height * 3] + b"\xff\xff\xff" * (240 * 25)
        self.write_bmp()
        regression.validate_capture(case, 0, self.log, self.screenshot)
        self.pixels = b"\xff\xff\xff" + self.pixels[3:]
        self.write_bmp()
        with self.assertRaisesRegex(ValueError, "baseline mismatch"):
            regression.validate_capture(case, 0, self.log, self.screenshot)

    def test_timeout_records_failure_and_cleans_runtime(self) -> None:
        runtime_paths: list[Path] = []

        with patch.object(regression, "stage_runtime", side_effect=runtime_paths.append):
            with patch.object(regression.subprocess, "run",
                              side_effect=subprocess.TimeoutExpired("simulator", 0.01)):
                result = regression.run_case(self.case, Path("simulator"), self.directory, 0.01)
        self.assertFalse(result.passed)
        self.assertIn("timed out", result.error or "")
        self.assertTrue((self.directory / "test" / "stdout.log").is_file())
        self.assertEqual(len(runtime_paths), 1)
        self.assertFalse(runtime_paths[0].exists())

    def test_cleanup_error_cannot_report_success(self) -> None:
        with patch.object(regression.tempfile, "TemporaryDirectory") as temporary:
            temporary.return_value.__enter__.return_value = str(self.directory / "runtime")
            temporary.return_value.__exit__.side_effect = OSError("cleanup failed")
            with patch.object(regression, "stage_runtime"):
                with patch.object(regression.subprocess, "run") as run:
                    run.return_value.returncode = 0
                    with patch.object(regression, "validate_capture"):
                        result = regression.run_case(self.case, Path("simulator"), self.directory, 1)
        self.assertFalse(result.passed)
        self.assertEqual(result.error, "cleanup failed")


if __name__ == "__main__":
    unittest.main()
