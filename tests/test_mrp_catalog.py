import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

from tools import mrp_to_markdown as catalog


def write_bmp(path: Path) -> None:
    body = b"\x00\x00\xff" * 240 + b"\x00\x00\x00" * (240 * 319)
    header = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, 240, 320, 1, 24, 0, len(body), 0, 0, 0, 0)
    path.write_bytes(header + body)


class StartupCaptureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        (self.directory / "mythroad").mkdir()
        self.simulator = self.directory / "simulator"
        self.simulator.touch()
        (self.directory / "cfunction.ext").touch()
        self.package = catalog.PackageReport(path=self.directory / "game.mrp")
        self.calls = []

    def capture(self, *args, **kwargs):
        self.calls.append((args[7], kwargs))
        write_bmp(args[4])
        return subprocess.CompletedProcess([], 0, "")

    def run_capture(self) -> None:
        catalog.capture_previews(
            [self.package], self.directory / "report", self.simulator, self.directory,
            1200, 15, skip_sound_prompt=True, refresh_splash_screens=True,
        )

    def test_two_logos_then_sound_prompt_preserve_elapsed_time_and_input(self) -> None:
        with patch.object(catalog, "run_simulator_capture", side_effect=self.capture), \
                patch.object(catalog, "sound_prompt_score", side_effect=lambda _: int(len(self.calls) == 3)), \
                patch.object(catalog, "transient_screen_score", side_effect=lambda _: int(len(self.calls) in (1, 2, 4))):
            self.run_capture()
        delays = [call[0] for call in self.calls]
        self.assertEqual(delays[:3], [1200, 8200, 15200])
        self.assertGreater(delays[3], 15200)
        self.assertGreater(delays[4], delays[3])
        self.assertGreater(self.calls[3][1]["click_after_ms"], 15200)
        self.assertEqual(self.calls[4][1]["click_after_ms"], self.calls[3][1]["click_after_ms"])
        self.assertTrue(self.package.preview)

    def test_persistent_logo_is_not_published_as_a_game_preview(self) -> None:
        with patch.object(catalog, "run_simulator_capture", side_effect=self.capture), \
                patch.object(catalog, "sound_prompt_score", return_value=0), \
                patch.object(catalog, "transient_screen_score", return_value=1):
            self.run_capture()
        self.assertFalse(self.package.preview)

    def test_audio_key_fallback_is_replayed_after_a_later_logo(self) -> None:
        with patch.object(catalog, "run_simulator_capture", side_effect=self.capture), \
                patch.object(catalog, "sound_prompt_score", side_effect=lambda _: int(len(self.calls) in (2, 3))), \
                patch.object(catalog, "transient_screen_score", side_effect=lambda _: int(len(self.calls) in (1, 4))):
            self.run_capture()
        self.assertIn("click_after_ms", self.calls[2][1])
        self.assertGreater(self.calls[3][1]["right_softkey_after_ms"], 8200)
        self.assertEqual(self.calls[4][1]["right_softkey_after_ms"], self.calls[3][1]["right_softkey_after_ms"])

    def test_music_menu_selects_off_and_replays_that_selection_after_a_logo(self) -> None:
        with patch.object(catalog, "run_simulator_capture", side_effect=self.capture), \
                patch.object(catalog, "sound_prompt_score", side_effect=lambda _: int(len(self.calls) == 1)), \
                patch.object(catalog, "sound_menu_score", return_value=1), \
                patch.object(catalog, "transient_screen_score", side_effect=lambda _: int(len(self.calls) == 2)):
            self.run_capture()
        self.assertEqual(self.calls[1][1]["sound_menu_after_ms"], 5000)
        self.assertEqual(self.calls[2][1]["sound_menu_after_ms"], 5000)
        self.assertIsNone(self.calls[2][1]["click_after_ms"])
        self.assertTrue(self.package.preview)

    def test_reported_sound_screens_are_detected_without_mistaking_notices_for_audio(self) -> None:
        fixtures = Path(__file__).parent / "fixtures" / "catalog"
        for name in ("sound-blue", "sound-switch", "sound-two-lines"):
            with self.subTest(name=name):
                self.assertGreaterEqual(catalog.sound_prompt_score(fixtures / f"{name}.png"), 0.8)
                self.assertLess(catalog.entry_notice_score(fixtures / f"{name}.png"), 0.9)
        for name in ("entry-free", "entry-fee"):
            with self.subTest(name=name):
                self.assertGreaterEqual(catalog.entry_notice_score(fixtures / f"{name}.png"), 0.9)
                self.assertLess(catalog.sound_prompt_score(fixtures / f"{name}.png"), 0.8)

    def test_entry_notice_is_withheld_without_any_confirmation_input(self) -> None:
        with patch.object(catalog, "run_simulator_capture", side_effect=self.capture), \
                patch.object(catalog, "entry_notice_score", return_value=1), \
                patch.object(catalog, "sound_prompt_score", return_value=1):
            self.run_capture()
        self.assertEqual(len(self.calls), 1)
        self.assertFalse(self.package.preview)
        self.assertEqual(self.package.preview_status, "startup_screen")
        self.assertTrue(any("notice" in warning for warning in self.package.warnings))

    def test_resume_rejects_a_cached_entry_notice(self) -> None:
        self.package.path.write_bytes(b"package contents")
        output = self.directory / "catalog"
        output.mkdir()
        args = argparse.Namespace(
            download_base_url="https://github.com/vmrp/vmrp.github.io/tree/master/public/mrps",
            resume_capture=False, capture=False, duplicate_preview_min=7,
            sound_prompt_threshold=0.8, splash_threshold=0.9,
        )
        catalog.generate_catalog(self.directory, [self.package], output, args)
        path = output / "catalog.json"
        document = json.loads(path.read_text(encoding="utf-8"))
        entry = document["packages"][0]
        entry["capture_status"] = "candidate"
        entry["preview"] = f"previews/{entry['sha256']}.png"
        preview = output / entry["preview"]
        preview.write_bytes((Path(__file__).parent / "fixtures" / "catalog" / "entry-free.png").read_bytes())
        path.write_text(json.dumps(document), encoding="utf-8")
        args.resume_capture = True
        catalog.generate_catalog(self.directory, [self.package], output, args)
        document = json.loads(path.read_text(encoding="utf-8"))
        self.assertIsNone(document["packages"][0]["preview"])
        self.assertFalse(preview.exists())

    def test_png_output_preserves_colors_orientation_and_dimensions(self) -> None:
        bmp = self.directory / "preview.bmp"
        png = self.directory / "preview.png"
        write_bmp(bmp)
        catalog.write_preview_png(bmp, png)
        data = png.read_bytes()
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(struct.unpack_from(">II", data, 16), (240, 320))
        length = struct.unpack_from(">I", data, 33)[0]
        pixels = zlib.decompress(data[41:41 + length])
        self.assertEqual(len(pixels), 320 * 721)
        self.assertEqual(pixels[1:4], b"\0\0\0")
        self.assertEqual(pixels[319 * 721 + 1:319 * 721 + 4], b"\xff\0\0")
        self.assertEqual(catalog.read_catalog_png(png), catalog.read_pixels(bmp))
        damaged = bytearray(data)
        damaged[45] ^= 1
        png.write_bytes(damaged)
        with self.assertRaisesRegex(ValueError, "checksum"):
            catalog.read_catalog_png(png)

    def test_mask_translation_clips_rows_instead_of_wrapping(self) -> None:
        bmp = self.directory / "mask.bmp"
        write_bmp(bmp)
        data = bytearray(bmp.read_bytes())
        position = 54 + 318 * 720 + 2 * 3
        data[position:position + 3] = b"\xff\xff\xff"
        bmp.write_bytes(data)
        template = bytearray(64)
        template[7] = template[9] = 1
        encoded = base64.b64encode(zlib.compress(template)).decode("ascii")
        score = catalog.image_mask_score(bmp, ((0, 0, 8, 8),), (encoded,), tolerance_x=1)
        self.assertEqual(score, 1)

    def test_json_catalog_uses_encoded_external_links_and_no_local_paths(self) -> None:
        package_path = self.directory / "游戏 #.mrp"
        package_path.write_bytes(b"invalid package")
        package = catalog.parse_package(package_path)
        output = self.directory / "catalog"
        output.mkdir()
        args = argparse.Namespace(
            download_base_url="https://github.com/vmrp/vmrp.github.io/tree/master/public/mrps",
            resume_capture=False, capture=False, duplicate_preview_min=7,
        )
        catalog.generate_catalog(self.directory, [package], output, args)
        data = json.loads((output / "catalog.json").read_text(encoding="utf-8"))
        entry = data["packages"][0]
        self.assertEqual(entry["capture_status"], "invalid_package")
        self.assertIsNone(entry["preview"])
        self.assertTrue(entry["download_url"].endswith("/%E6%B8%B8%E6%88%8F%20%23.mrp"))
        self.assertIn("/blob/master/", entry["download_url"])
        self.assertNotIn(str(self.directory), json.dumps(data))
        self.assertFalse(list(output.rglob("*.mrp")))

    def test_shared_white_background_does_not_match_an_unrelated_logo(self) -> None:
        bmp = self.directory / "white.bmp"
        body = b"\xff\xff\xff" * (240 * 320)
        header = struct.pack("<2sIHHI", b"BM", 54 + len(body), 0, 0, 54)
        header += struct.pack("<IiiHHIIiiII", 40, 240, 320, 1, 24, 0, len(body), 0, 0, 0, 0)
        bmp.write_bytes(header + body)
        template = bytearray([1] * 100)
        template[5] = 0
        encoded = base64.b64encode(zlib.compress(template)).decode("ascii")
        self.assertLess(catalog.image_mask_score(bmp, ((0, 0, 10, 10),), (encoded,)), 0.9)

    def test_native_capture_delay_is_relative_to_input_but_timeout_covers_the_whole_run(self) -> None:
        self.package.path.touch()
        staged = self.directory / "mythroad" / "capture.mrp"
        with patch.object(catalog.subprocess, "run") as run:
            catalog.run_simulator_capture(
                self.simulator, self.directory, self.package.path, staged,
                self.directory / "preview.bmp", "start.mr", {}, 20200, 15,
                click_after_ms=15200,
            )
        command = run.call_args.args[0]
        self.assertEqual(command[command.index("--capture-delay") + 1], "5000")
        self.assertEqual(command[command.index("--click-right-after") + 1], "15200")
        self.assertGreater(run.call_args.kwargs["timeout"], 20.2)
        self.assertFalse(staged.exists())

    def test_contact_sheet_review_withholds_only_matching_package_contents(self) -> None:
        self.package.path.write_bytes(b"package contents")
        output = self.directory / "catalog"
        output.mkdir()
        digest = hashlib.sha256(self.package.path.read_bytes()).hexdigest()
        review = output / "reviews.json"
        review.write_text(json.dumps({"schema_version": 1, "withheld": {digest: "audio_prompt"}}))
        args = argparse.Namespace(
            download_base_url="https://github.com/vmrp/vmrp.github.io/tree/master/public/mrps",
            resume_capture=False, capture=False, duplicate_preview_min=7,
        )
        catalog.generate_catalog(self.directory, [self.package], output, args)
        document = json.loads((output / "catalog.json").read_text(encoding="utf-8"))
        self.assertEqual(document["summary"]["review_withheld"], 1)
        self.assertEqual(document["packages"][0]["capture_status"], "review_required")
        self.assertIsNone(document["packages"][0]["preview"])
        self.package.path.write_bytes(b"different package contents")
        catalog.generate_catalog(self.directory, [self.package], output, args)
        document = json.loads((output / "catalog.json").read_text(encoding="utf-8"))
        self.assertEqual(document["summary"]["review_withheld"], 0)
        self.assertEqual(document["packages"][0]["capture_status"], "not_captured")


if __name__ == "__main__":
    unittest.main()
import argparse
import base64
import hashlib
import json
