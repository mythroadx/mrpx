import csv
import contextlib
import gzip
import io
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest import mock

from tools import mrp_tool


def chunk(strings=(), numbers=(), order="<", version=0x80, child=False):
    def integer(value):
        return struct.pack(order + "i", value)

    def string(value):
        return struct.pack(order + "I", len(value) + 1) + value + b"\0"

    constants = b"".join(b"\4" + string(value) for value in strings)
    constants += b"".join(b"\3" + integer(value) for value in numbers)
    source = string(b"@start.mr")
    body = source + integer(0) + bytes((0, 0, 0, 4))
    body += integer(0) + integer(0) + integer(0)
    body += integer(len(strings) + len(numbers)) + constants
    body += integer(int(child))
    if child:
        body += (integer(0) + integer(0) + bytes((0, 0, 0, 2))
                 + integer(0) * 5 + integer(0))
    instructions = [5] if strings else []
    body += integer(len(instructions))
    body += b"".join(struct.pack(order + "I", item) for item in instructions)
    header = b"\x1bMRP" + bytes((version, order == "<"))
    if version == 0x50:
        header += bytes((4, 4, 4, 6, 8, 9, 9, 4)) + integer(31415926)
    return header + body


def archive(entries, padding=0, trailing=b""):
    names = [(name.encode("gbk") + b"\0" * (1 + padding), content, reserved)
             for name, content, reserved in entries]
    index_end = 240 + sum(16 + len(name) for name, _, _ in names)
    header = bytearray(240)
    header[:4] = b"MRPG"
    header[28:33] = b"Test\0"
    index, records = bytearray(), bytearray()
    for name, payload, reserved in names:
        prefix = struct.pack("<I", len(name)) + name + struct.pack("<I", len(payload))
        offset = index_end + len(records) + len(prefix)
        index += struct.pack("<I", len(name)) + name + struct.pack("<III", offset, len(payload), reserved)
        records += prefix + payload
    struct.pack_into("<III", header, 4, index_end - 8, index_end + len(records) + len(trailing), 240)
    return bytes(header + index + records + trailing)


class ChunkTests(unittest.TestCase):
    def test_constants_nested_functions_and_header_variants(self):
        for order, version in (("<", 0x80), (">", 0x80), ("<", 0x50)):
            with self.subTest(order=order, version=version):
                result = mrp_tool.chunk_report(mrp_tool.ChunkReader(
                    chunk((b"_mr_c_load", b"test.ext"), (-7,), order, version, child=True)
                ).parse())
                self.assertEqual(result["prototype_count"], 2)
                self.assertEqual([item["value"] for item in result["constants"]],
                                 ["_mr_c_load", "test.ext", -7])
                self.assertEqual(result["global_references"][0]["name"], "_mr_c_load")
                self.assertEqual(result["opcode_counts"], {"GETGLOBAL": 1})

    def test_rejects_truncation_trailing_data_and_unsupported_width(self):
        valid = chunk((b"hello",))
        for data in (valid[:-1], valid + b"x", b"\x1bLua\x80\1"):
            with self.subTest(size=len(data)):
                with self.assertRaises(ValueError):
                    mrp_tool.ChunkReader(data).parse()
        old = bytearray(chunk(version=0x50))
        old[7] = 8
        with self.assertRaisesRegex(ValueError, "type widths"):
            mrp_tool.ChunkReader(bytes(old)).parse()
        damaged = bytearray(valid)
        struct.pack_into("<I", damaged, 6, 0xffffffff)
        with self.assertRaisesRegex(ValueError, "Truncated source"):
            mrp_tool.ChunkReader(bytes(damaged)).parse()

    def test_explicitly_reports_unsupported_legacy_compression(self):
        for data in (b"\x1f\x9elegacy", b"PK\x03\x04legacy"):
            with self.subTest(data=data):
                with self.assertRaisesRegex(ValueError, "recognized but not supported"):
                    mrp_tool.unpack_resource(data)


class CatalogTests(unittest.TestCase):
    def test_classification_first_duplicate_and_missing_entry(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, output = root / "input", root / "output"
            source.mkdir()
            clean = gzip.compress(chunk((
                b"draw", b"netpay.mrp", b"_STRCOM", b"_loadBuf",
                b"_loads", b"_execFile", b"dofile",
            )))
            suspicious = gzip.compress(chunk((b"_mr_c_load",)))
            damaged = bytearray(gzip.compress(b"image"))
            damaged[-8] ^= 1
            (source / "a.mrp").write_bytes(archive([
                ("start.mr", clean, 0), ("start.mr", suspicious, 7),
                ("image.bmp", bytes(damaged), 0),
            ]))
            (source / "b.mrp").write_bytes(archive([
                ("start.mr", clean, 0), ("native.ext", b"MRPGCMAP cfunction.ext NETPAY", 0),
            ]))
            (source / "c.mrp").write_bytes(archive([("_start.mr", clean, 0)]))
            (source / "bad.mrp").write_bytes(b"NOPE")
            doc = mrp_tool.catalog(source, output)
            self.assertEqual(doc["summary"]["classes"], {"A": 1, "B": 1, "C": 2})
            self.assertEqual(doc["summary"]["header_checksums"], {"zero": 3})
            self.assertEqual(doc["summary"]["reserved_values"], {"0": 5, "7": 1})
            self.assertEqual(doc["summary"]["raw_rebuild_identical"], 3)
            self.assertEqual(len(doc["summary"]["resource_decode_errors"]), 1)
            self.assertIn("4e4f5045", doc["summary"]["magics"])
            rows = {row["package"]: row for row in doc["packages"]}
            self.assertEqual(rows["a.mrp"]["entry_count"], 2)
            self.assertEqual(rows["a.mrp"]["entry_index"], 1)
            self.assertEqual(rows["a.mrp"]["keywords"], [])
            self.assertEqual(rows["a.mrp"]["api_markers"], list(mrp_tool.API_MARKERS))
            self.assertEqual(doc["summary"]["startup_api_markers"],
                             {marker: 2 for marker in sorted(mrp_tool.API_MARKERS)})
            self.assertEqual(rows["a.mrp"]["plugin_markers"], ["netpay"])
            self.assertEqual(rows["b.mrp"]["ext_files"], ["native.ext"])
            self.assertEqual(rows["b.mrp"]["resources"][1]["native_ascii_markers"],
                             [".ext", "cfunction", "cfunction.ext", "netpay"])
            self.assertIn("No exact", rows["c.mrp"]["error"])
            with (output / "mrp_catalog.csv").open(encoding="utf-8-sig", newline="") as stream:
                csv_rows = {row["package"]: row for row in csv.DictReader(stream)}
                self.assertEqual(len(csv_rows), 4)
                self.assertEqual(csv_rows["a.mrp"]["api_markers"],
                                 "; ".join(mrp_tool.API_MARKERS))
            self.assertEqual(json.loads((output / "mrp_catalog.json").read_text(encoding="utf-8")), doc)
            with self.assertRaisesRegex(ValueError, "already exists"):
                mrp_tool.catalog(source, output)


class ArchiveTests(unittest.TestCase):
    def test_edit_updates_recognized_header_crc_without_changing_original_roundtrip(self):
        for declared_only in (False, True):
            with self.subTest(declared_only=declared_only), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                data = bytearray(archive([("a", gzip.compress(b"original"), 0)], trailing=b"TAIL"))
                if declared_only:
                    struct.pack_into("<I", data, 8, len(data) - 4)
                end = len(data) - 4 if declared_only else len(data)
                struct.pack_into("<I", data, 84, zlib.crc32(data[:end]))
                source = root / "source.mrp"
                source.write_bytes(data)
                unpacked = root / "unpacked"
                mrp_tool.unpack_archive(source, unpacked, set())
                manifest = json.loads((unpacked / mrp_tool.MANIFEST).read_text(encoding="utf-8"))
                self.assertEqual(manifest["header_checksum"],
                                 "declared-crc32" if declared_only else "builder-crc32")
                mrp_tool.pack_directory(unpacked, root / "same.mrp")
                self.assertEqual((root / "same.mrp").read_bytes(), data)
                (unpacked / "files" / "000001.bin").write_bytes(b"edited resource")
                mrp_tool.pack_directory(unpacked, root / "edited.mrp")
                edited = (root / "edited.mrp").read_bytes()
                end = int.from_bytes(edited[8:12], "little") if declared_only else len(edited)
                expected = zlib.crc32(edited[:84] + bytes(4) + edited[88:end])
                self.assertEqual(int.from_bytes(edited[84:88], "little"), expected)

    def test_unknown_header_checksum_is_preserved_with_warning_or_explicitly_replaced(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            original = bytearray(archive([("a", b"original", 0)]))
            struct.pack_into("<I", original, 84, 0x12345678)
            source = root / "source.mrp"
            source.write_bytes(original)
            unpacked = root / "unpacked"
            mrp_tool.unpack_archive(source, unpacked, set())
            mrp_tool.pack_directory(unpacked, root / "same.mrp")
            self.assertEqual((root / "same.mrp").read_bytes(), original)
            (unpacked / "files" / "000001.bin").write_bytes(b"edited")
            warnings = io.StringIO()
            with contextlib.redirect_stderr(warnings):
                mrp_tool.pack_directory(unpacked, root / "preserved.mrp")
            self.assertIn("unrecognized header checksum", warnings.getvalue())
            self.assertEqual((root / "preserved.mrp").read_bytes()[84:88], original[84:88])
            mrp_tool.pack_directory(unpacked, root / "forced.mrp", force_builder_crc32=True)
            forced = (root / "forced.mrp").read_bytes()
            self.assertEqual(int.from_bytes(forced[84:88], "little"),
                             zlib.crc32(forced[:84] + bytes(4) + forced[88:]))

    def test_roundtrip_preserves_duplicate_and_compressed_payload_bytes(self):
        compressor = zlib.compressobj(wbits=-15)
        raw = compressor.compress(b"raw data") + compressor.flush()
        original = archive([
            ("start.mr", gzip.compress(chunk((b"hello",)), mtime=0), 0),
            ("start.mr", zlib.compress(b"second"), 9),
            ("../../not-written", raw, 0),
            ("empty", gzip.compress(b"", mtime=0), 0),
        ])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, target = root / "source.mrp", root / "rebuilt.mrp"
            source.write_bytes(original)
            first, second = root / "first", root / "second"
            mrp_tool.unpack_archive(source, first, {3})
            self.assertFalse((root / "not-written").exists())
            self.assertEqual((first / "files" / "000003.bin").read_bytes(), b"raw data")
            self.assertEqual((first / "files" / "000004.bin").read_bytes(), b"")
            mrp_tool.pack_directory(first, target)
            self.assertEqual(target.read_bytes(), original)
            mrp_tool.unpack_archive(target, second, {3})
            self.assertEqual(
                [(first / "files" / f"{index:06d}.bin").read_bytes() for index in range(1, 5)],
                [(second / "files" / f"{index:06d}.bin").read_bytes() for index in range(1, 5)],
            )
            (first / "files" / "000002.bin").write_bytes(b"changed")
            changed = root / "changed.mrp"
            mrp_tool.pack_directory(first, changed)
            mrp_tool.unpack_archive(changed, root / "changed", {3})
            self.assertEqual((root / "changed" / "files" / "000002.bin").read_bytes(), b"changed")
            with self.assertRaises(FileExistsError):
                mrp_tool.pack_directory(first, target)

    def test_roundtrip_preserves_padded_names_and_undeclared_trailing_data(self):
        data = bytearray(archive([("start.mr", b"raw", 0)], padding=1, trailing=b"TAIL"))
        struct.pack_into("<I", data, 8, len(data) - 2)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, rebuilt = root / "source.mrp", root / "rebuilt.mrp"
            source.write_bytes(data)
            mrp_tool.unpack_archive(source, root / "out", set())
            mrp_tool.pack_directory(root / "out", rebuilt)
            self.assertEqual(rebuilt.read_bytes(), data)

    def test_roundtrip_allows_compressed_stream_larger_than_decoded_limit(self):
        content = bytes(range(256)) * (mrp_tool.MAX_OUTPUT // 256)
        compressed = gzip.compress(content, compresslevel=0, mtime=0)
        self.assertGreater(len(compressed), mrp_tool.MAX_OUTPUT)
        original = archive([("small", compressed, 0)])
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, rebuilt = root / "source.mrp", root / "rebuilt.mrp"
            source.write_bytes(original)
            mrp_tool.unpack_archive(source, root / "out", set())
            mrp_tool.pack_directory(root / "out", rebuilt)
            self.assertEqual(rebuilt.read_bytes(), original)

    def test_rejects_corruption_and_manifest_path_traversal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "input.mrp"
            damaged = bytearray(gzip.compress(b"hello"))
            damaged[-8] ^= 1
            source.write_bytes(archive([("a", bytes(damaged), 0)]))
            with self.assertRaisesRegex(ValueError, "gzip"):
                mrp_tool.unpack_archive(source, root / "bad", set())
            self.assertFalse((root / "bad").exists())
            warnings = io.StringIO()
            with contextlib.redirect_stderr(warnings):
                mrp_tool.unpack_archive(source, root / "recovered", set(), True)
            self.assertIn("CRC mismatch", warnings.getvalue())
            manifest = json.loads((root / "recovered" / mrp_tool.MANIFEST).read_text(encoding="utf-8"))
            self.assertIn("CRC mismatch", manifest["files"][0]["warning"])
            self.assertEqual((root / "recovered" / "files" / "000001.bin").read_bytes(), b"hello")
            damaged[-4] ^= 1
            with self.assertRaisesRegex(ValueError, "size mismatch"):
                mrp_tool.unpack_resource(bytes(damaged), allow_bad_gzip_crc=True)
            named = bytearray(gzip.compress(b"hello"))
            named[3] = 8
            named[10:10] = b"example\0"
            named[-8] ^= 1
            self.assertEqual(mrp_tool.unpack_resource(bytes(named), allow_bad_gzip_crc=True),
                             ("gzip-crc-mismatch", b"hello"))
            source.write_bytes(archive([("a", b"data", 0)]))
            extracted = root / "extracted"
            mrp_tool.unpack_archive(source, extracted, set())
            manifest_path = extracted / mrp_tool.MANIFEST
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            manifest["files"][0]["path"] = "../input.mrp"
            manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Unexpected manifest path"):
                mrp_tool.pack_directory(extracted, root / "rebuilt.mrp")

    def test_rejects_excessive_total_decoded_size(self):
        data = archive([("a", gzip.compress(b"a" * 400), 0),
                        ("b", gzip.compress(b"b" * 400), 0)])
        self.assertLess(len(data), 400)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "input.mrp"
            source.write_bytes(data)
            with mock.patch.object(mrp_tool, "MAX_ARCHIVE", 400):
                with self.assertRaisesRegex(ValueError, "Total decoded"):
                    mrp_tool.unpack_archive(source, root / "output", set())
            self.assertFalse((root / "output").exists())

    def test_index_end_plus_eight_boundary(self):
        valid = archive([("a", b"data", 0)])
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "input.mrp"
            for delta in (-1, 1):
                damaged = bytearray(valid)
                length = struct.unpack_from("<I", damaged, 4)[0]
                struct.pack_into("<I", damaged, 4, length + delta)
                path.write_bytes(damaged)
                with self.subTest(delta=delta):
                    with self.assertRaises(ValueError):
                        mrp_tool.read_package(path)

    def test_plain_directory_pack_and_existing_output_protection(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "resources"
            source.mkdir()
            (source / "start.mr").write_bytes(chunk())
            target = root / "new.mrp"
            mrp_tool.pack_directory(source, target)
            packed = target.read_bytes()
            self.assertEqual(int.from_bytes(packed[84:88], "little"),
                             zlib.crc32(packed[:84] + bytes(4) + packed[88:]))
            report, data = mrp_tool.read_package(target)
            self.assertEqual(mrp_tool.stored_bytes(data, report.entries[0]), chunk())
            with self.assertRaisesRegex(ValueError, "outside"):
                mrp_tool.pack_directory(source, source / "bad.mrp")
            (source / ("a" * 127)).write_bytes(b"long name")
            with self.assertRaisesRegex(ValueError, "runtime limit"):
                mrp_tool.pack_directory(source, root / "long.mrp")


class BitmapTests(unittest.TestCase):
    def test_raw_rgb565_png_pixels_and_length_validation(self):
        png = mrp_tool.bitmap_png(b"\x00\xf8\xe0\x07", 2, 1, "little")
        self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
        self.assertEqual(struct.unpack_from(">II", png, 16), (2, 1))
        length = struct.unpack_from(">I", png, 33)[0]
        self.assertEqual(zlib.decompress(png[41:41 + length]), b"\0\xff\0\0\0\xff\0")
        with self.assertRaisesRegex(ValueError, "Expected"):
            mrp_tool.bitmap_png(b"\0", 2, 1, "little")


if __name__ == "__main__":
    unittest.main()
