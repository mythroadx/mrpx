import contextlib
import gzip
import io
import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from tools import mrp_bytecode as bytecode


def chunk(order: str = "<") -> bytes:
    source = b"@start.mr\0"
    return (
        b"\x1bMRP\x80" + bytes([order == "<"])
        + struct.pack(order + "I", len(source)) + source
        + struct.pack(order + "i", 0) + bytes([0, 0, 0, 4])
        + struct.pack(order + "i", 8)
        + struct.pack(order + "8i", *range(2, 10))
    )


def package(entries: list[tuple[str, bytes]]) -> bytes:
    encoded = [(name.encode("gbk") + b"\0", payload) for name, payload in entries]
    index_end = 240 + sum(16 + len(name) for name, _ in encoded)
    header = bytearray(240)
    header[:4] = b"MRPG"
    index, records = bytearray(), bytearray()
    for name, payload in encoded:
        records += struct.pack("<I", len(name)) + name + struct.pack("<I", len(payload))
        offset = index_end + len(records)
        index += struct.pack("<I", len(name)) + name + struct.pack("<III", offset, len(payload), 0)
        records += payload
    struct.pack_into("<III", header, 4, index_end - 8, index_end + len(records), 240)
    return bytes(header + index + records)


class BytecodeInspectionTests(unittest.TestCase):
    def test_compression_formats_preserve_decoded_bytes(self) -> None:
        data = chunk()
        compressor = zlib.compressobj(wbits=-15)
        raw = compressor.compress(data) + compressor.flush()
        for name, payload in (
            ("gzip", gzip.compress(data)), ("zlib", zlib.compress(data)),
            ("raw-deflate", raw), ("uncompressed", data),
        ):
            with self.subTest(format=name):
                self.assertEqual(bytecode.decode_payload(payload), (name, data))

    def test_corruption_truncation_trailing_bytes_and_output_limit_are_rejected(self) -> None:
        compressed = gzip.compress(chunk())
        damaged = bytearray(compressed)
        damaged[-8] ^= 1
        for payload, message in (
            (bytes(damaged), "Cannot decode"),
            (compressed[:-2], "truncated"),
            (compressed + b"extra", "trailing"),
            (b"not a compressed chunk", "Cannot decode"),
        ):
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    bytecode.decode_payload(payload)
        self.assertEqual(bytecode.decode_payload(compressed, len(chunk()))[1], chunk())
        with self.assertRaisesRegex(ValueError, "exceeds"):
            bytecode.decode_payload(compressed, len(chunk()) - 1)
        with self.assertRaisesRegex(ValueError, "exceeds"):
            bytecode.decode_payload(chunk(), len(chunk()) - 1)

    def test_each_prefix_byte_is_annotated_using_the_selected_byte_order(self) -> None:
        for order in ("<", ">"):
            with self.subTest(order=order):
                description, notes = bytecode.describe_prefix(chunk(order))
                self.assertIn("32-bit", description)
                self.assertEqual(len(notes), 64)
                self.assertIn("length including NUL", notes[6])
                self.assertIn("lineDefined = 0", notes[20])
                self.assertIn("maxstacksize = 4", notes[27])
                self.assertIn("Line-info[7] = 9", notes[63])
                self.assertFalse(any("not interpreted" in note for note in notes))
        data = bytearray(chunk())
        struct.pack_into("<i", data, 32, -1)
        self.assertIn("Line-info[0] = -1", bytecode.describe_prefix(bytes(data))[1][32])

    def test_invalid_header_fields_fail_instead_of_guessing(self) -> None:
        for data, message in (
            (chunk()[:5], "Truncated"),
            (chunk()[:5] + b"\2" + chunk()[6:], "byte-order"),
            (chunk()[:6] + struct.pack("<I", 100000), "Truncated"),
            (chunk()[:-1], "line-info"),
            (chunk()[:28] + struct.pack("<i", -1), "Negative"),
        ):
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    bytecode.describe_prefix(data)
        description, notes = bytecode.describe_prefix(b"unknown format")
        self.assertIn("Unknown signature", description)
        self.assertEqual(len(notes), 14)

    def test_cli_uses_index_payload_offset_and_warns_about_ext(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path, output = root / "sample.mrp", root / "decoded.mr"
            path.write_bytes(package([
                ("start.mr", gzip.compress(chunk())), ("cfunction.ext", b"MRPGCMAP"),
            ]))
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                self.assertEqual(bytecode.main([str(path), "--output", str(output)]), 0)
            self.assertEqual(output.read_bytes(), chunk())
            self.assertIn("Compression: gzip", stdout.getvalue())
            self.assertIn("EXT presence needs dependency analysis", stdout.getvalue())
            self.assertIn("003F", stdout.getvalue())
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    bytecode.main([str(path), "--output", str(output)])
            self.assertEqual(error.exception.code, 2)
            self.assertEqual(output.read_bytes(), chunk())

    def test_entry_fallback_selection_and_malformed_index(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "sample.mrp"
            path.write_bytes(package([("start.m", chunk()), ("other.mr", chunk())]))
            report = bytecode.parse_package(path)
            self.assertEqual(bytecode.select_entry(report, None).name, "start.m")
            self.assertEqual(bytecode.select_entry(report, "other.mr").name, "other.mr")
            with self.assertRaisesRegex(ValueError, "not found"):
                bytecode.select_entry(report, "absent.mr")
            path.write_bytes(package([("start.mr", chunk()), ("start.mr", chunk())]))
            with self.assertRaisesRegex(ValueError, "Ambiguous"):
                bytecode.select_entry(bytecode.parse_package(path), None)
            data = bytearray(package([("start.mr", chunk())]))
            struct.pack_into("<I", data, 253, len(data) + 1)
            path.write_bytes(data)
            output = Path(temporary) / "decoded.mr"
            with contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    bytecode.main([str(path), "--output", str(output)])
            self.assertEqual(error.exception.code, 2)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
