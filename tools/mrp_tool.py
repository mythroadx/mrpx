#!/usr/bin/env python3
"""Inspect, extract, and rebuild indexed MRP packages without executing them."""

from __future__ import annotations

import argparse
import csv
import gzip
import hashlib
import json
import struct
import sys
import zlib
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

if __package__:
    from .mrp_bytecode import MAX_OUTPUT, decode_payload, decompress_stream
    from .mrp_to_markdown import PackageReport, ResourceEntry, parse_package
else:
    from mrp_bytecode import MAX_OUTPUT, decode_payload, decompress_stream
    from mrp_to_markdown import PackageReport, ResourceEntry, parse_package


MANIFEST = "mrp-manifest.json"
KEYWORDS = ("_mr_c_load", ".ext", "cfunction", "game.ext", "cfunction.ext")
API_MARKERS = ("_strCom", "_loadBuf", "_loads", "_execFile", "dofile")
PLUGIN_MARKERS = ("netpay", "flaengine", "brwcore", "skf_plugin")
MAX_ARCHIVE = 256 * 1024 * 1024
MAX_DEPTH = 200
MAX_ENTRIES = 100_000
RUNTIME_MAX_FILENAME = 128
OPCODE_NAMES = (
    "MOVE", "LOADK", "LOADBOOL", "LOADNIL", "GETUPVAL", "GETGLOBAL",
    "GETTABLE", "SETGLOBAL", "SETUPVAL", "SETTABLE", "NEWTABLE", "SELF",
    "ADD", "SUB", "MUL", "DIV", "POW", "UNM", "NOT", "CONCAT", "JMP",
    "EQ", "LT", "LE", "TEST", "CALL", "TAILCALL", "RETURN", "FORLOOP",
    "TFORLOOP", "TFORPREP", "SETLIST", "SETLISTO", "CLOSE", "CLOSURE",
    "BNOT", "BAND", "BOR", "BXOR",
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def header_crc32(data: bytes, end: int) -> int:
    if not 88 <= end <= len(data):
        raise ValueError("Invalid range for header CRC32")
    crc = zlib.crc32(data[:84])
    crc = zlib.crc32(b"\0" * 4, crc)
    return zlib.crc32(memoryview(data)[88:end], crc)


def header_checksum_mode(data: bytes) -> str:
    stored = struct.unpack_from("<I", data, 84)[0]
    if not stored:
        return "zero"
    if stored == header_crc32(data, len(data)):
        return "builder-crc32"
    declared = struct.unpack_from("<I", data, 8)[0]
    if 88 <= declared < len(data) and stored == header_crc32(data, declared):
        return "declared-crc32"
    return "unknown"


def gbk_text(data: bytes) -> str:
    return data.decode("gbk", errors="replace")


def resource_format(data: bytes) -> str:
    if data.startswith(b"\x1f\x8b"):
        return "gzip"
    if data.startswith(b"\x1f\x9e"):
        return "legacy-gzip"
    if data.startswith(b"PK\x03\x04"):
        return "pkzip-local"
    if (len(data) >= 2 and data[0] & 15 == 8 and data[0] >> 4 <= 7
            and int.from_bytes(data[:2], "big") % 31 == 0):
        return "zlib-candidate"
    return "opaque-or-raw-deflate"


def unpack_gzip_with_bad_crc(data: bytes) -> bytes:
    if len(data) < 18 or data[2] != 8 or data[3] & 0xE0:
        raise ValueError("Invalid gzip header")
    flags = data[3]
    position = 10
    trailer = len(data) - 8
    if flags & 4:
        if position + 2 > trailer:
            raise ValueError("Truncated gzip extra field")
        length = int.from_bytes(data[position:position + 2], "little")
        position += 2 + length
    for flag in (8, 16):
        if flags & flag:
            end = data.find(b"\0", position, trailer)
            if end < 0:
                raise ValueError("Unterminated gzip header field")
            position = end + 1
    if flags & 2:
        position += 2
    if position > trailer:
        raise ValueError("Truncated gzip header")
    content = decompress_stream(data[position:trailer], "gzip deflate", -15)
    crc, size = struct.unpack_from("<II", data, trailer)
    if size != (len(content) & 0xFFFFFFFF):
        raise ValueError("gzip: uncompressed size mismatch")
    if crc == zlib.crc32(content):
        raise ValueError("gzip: not a CRC mismatch")
    return content


def unpack_resource(data: bytes, raw_deflate: bool = False,
                    allow_bad_gzip_crc: bool = False) -> tuple[str, bytes]:
    kind = resource_format(data)
    if raw_deflate:
        if kind != "opaque-or-raw-deflate":
            raise ValueError("raw-deflate override conflicts with a gzip/zlib header")
        kind, wbits = "raw-deflate", -15
    elif kind == "gzip":
        wbits = 31
    elif kind == "zlib-candidate":
        kind, wbits = "zlib", 15
    elif kind in ("legacy-gzip", "pkzip-local"):
        raise ValueError(f"{kind}: recognized but not supported by this extractor")
    else:
        if len(data) > MAX_OUTPUT:
            raise ValueError(f"Stored resource exceeds {MAX_OUTPUT} bytes")
        return "stored", data
    try:
        return kind, decompress_stream(data, kind, wbits)
    except zlib.error as error:
        if kind == "gzip" and allow_bad_gzip_crc and "incorrect data check" in str(error):
            return "gzip-crc-mismatch", unpack_gzip_with_bad_crc(data)
        raise ValueError(f"{kind}: {error}") from error


@dataclass
class Constant:
    index: int
    offset: int
    kind: str
    value: bytes | int | None


@dataclass
class Prototype:
    path: str
    source: bytes | None
    line: int
    nups: int
    params: int
    vararg: int
    stack: int
    constants: list[Constant]
    children: list[Prototype]
    code: list[int]
    code_offset: int


@dataclass
class Chunk:
    version: int
    byte_order: str
    root: Prototype


class ChunkReader:
    def __init__(self, data: bytes):
        if len(data) > MAX_OUTPUT:
            raise ValueError(f"Chunk exceeds {MAX_OUTPUT} bytes")
        self.data = data
        self.pos = 0
        self.order = "<"
        self.nodes = 0

    def take(self, size: int, label: str) -> bytes:
        if size < 0 or size > len(self.data) - self.pos:
            raise ValueError(f"Truncated {label} at 0x{self.pos:X}")
        start = self.pos
        self.pos += size
        return self.data[start:self.pos]

    def byte(self, label: str) -> int:
        return self.take(1, label)[0]

    def integer(self, label: str, signed: bool = True) -> int:
        return struct.unpack(self.order + ("i" if signed else "I"), self.take(4, label))[0]

    def count(self, label: str, minimum_bytes: int = 1) -> int:
        value = self.integer(label)
        if value < 0:
            raise ValueError(f"Negative {label}: {value}")
        if value > MAX_ENTRIES or value * minimum_bytes > len(self.data) - self.pos:
            raise ValueError(f"Implausible {label}: {value} at 0x{self.pos - 4:X}")
        return value

    def string(self, label: str) -> bytes | None:
        size = self.integer(label + " length", signed=False)
        if size == 0:
            return None
        data = self.take(size, label)
        if not data.endswith(b"\0"):
            raise ValueError(f"{label} lacks trailing NUL at 0x{self.pos - size:X}")
        return data[:-1]

    def function(self, path: str, depth: int) -> Prototype:
        if depth > MAX_DEPTH:
            raise ValueError(f"Prototype nesting exceeds {MAX_DEPTH}")
        self.nodes += 1
        if self.nodes > MAX_ENTRIES:
            raise ValueError("Too many prototypes")
        source = self.string("source")
        line = self.integer("lineDefined")
        if line < 0:
            raise ValueError("Negative lineDefined")
        nups = self.byte("nups")
        params = self.byte("numparams")
        vararg = self.byte("vararg")
        stack = self.byte("maxstack")
        lines = self.count("line count", 4)
        self.take(lines * 4, "line vector")
        locals_count = self.count("local count", 12)
        for _ in range(locals_count):
            self.string("local name")
            for label in ("local start", "local end"):
                if self.integer(label) < 0:
                    raise ValueError(f"Negative {label}")
        upvalues = self.count("upvalue name count", 4)
        if upvalues and upvalues != nups:
            raise ValueError(f"Upvalue name count {upvalues} differs from nups {nups}")
        for _ in range(upvalues):
            self.string("upvalue name")
        count = self.count("constant count")
        constants: list[Constant] = []
        for index in range(count):
            offset = self.pos
            tag = self.byte("constant tag")
            if tag == 0:
                constants.append(Constant(index, offset, "nil", None))
            elif tag == 3:
                constants.append(Constant(index, offset, "number", self.integer("number")))
            elif tag == 4:
                value = self.string("string constant")
                if value is None:
                    raise ValueError(f"Null string constant at 0x{offset:X}")
                constants.append(Constant(index, offset, "string", value))
            else:
                raise ValueError(f"Unsupported constant tag {tag} at 0x{offset:X}")
        child_count = self.count("prototype count", 32)
        children = [self.function(f"{path}.{i}", depth + 1) for i in range(child_count)]
        code_count = self.count("instruction count", 4)
        code_offset = self.pos
        code = [self.integer("instruction", signed=False) for _ in range(code_count)]
        return Prototype(path, source, line, nups, params, vararg, stack,
                         constants, children, code, code_offset)

    def parse(self) -> Chunk:
        if self.take(4, "signature") != b"\x1bMRP":
            raise ValueError("Not an MRP chunk (expected ESC MRP)")
        version = self.byte("version")
        if not 0x50 <= version <= 0x80:
            raise ValueError(f"Unsupported MRP version 0x{version:02X}")
        endian = self.byte("endianness")
        if endian not in (0, 1):
            raise ValueError(f"Invalid endianness flag {endian}")
        self.order = "<" if endian else ">"
        if version == 0x50:
            sizes = self.take(8, "0x50 type widths")
            if sizes != bytes((4, 4, 4, 6, 8, 9, 9, 4)):
                raise ValueError(f"Unsupported 0x50 type widths: {sizes.hex(' ')}")
            test_number = self.integer("0x50 test number")
            if test_number != 31415926:
                raise ValueError(f"Unexpected 0x50 test number {test_number}")
        root = self.function("0", 0)
        if self.pos != len(self.data):
            raise ValueError(f"Trailing chunk data at 0x{self.pos:X} ({len(self.data) - self.pos} bytes)")
        return Chunk(version, "little" if endian else "big", root)


def walk_prototypes(proto: Prototype):
    yield proto
    for child in proto.children:
        yield from walk_prototypes(child)


def string_constants(chunk: Chunk):
    for proto in walk_prototypes(chunk.root):
        for item in proto.constants:
            if item.kind == "string" and isinstance(item.value, bytes):
                yield proto, item


def global_references(chunk: Chunk) -> list[dict[str, object]]:
    result: list[dict[str, object]] = []
    for proto in walk_prototypes(chunk.root):
        for pc, instruction in enumerate(proto.code):
            opcode = instruction & 0x3f
            if opcode not in (5, 7):
                continue
            index = (instruction >> 6) & 0x3ffff
            if index >= len(proto.constants):
                raise ValueError(f"Global constant index {index} out of range in {proto.path}:{pc}")
            item = proto.constants[index]
            if item.kind != "string" or not isinstance(item.value, bytes):
                raise ValueError(f"Global constant is not a string in {proto.path}:{pc}")
            result.append({
                "prototype": proto.path,
                "pc": pc,
                "offset": proto.code_offset + 4 * pc,
                "operation": "get" if opcode == 5 else "set",
                "name": gbk_text(item.value),
            })
    return result


def chunk_report(chunk: Chunk) -> dict[str, object]:
    constants: list[dict[str, object]] = []
    opcodes: Counter[str] = Counter()
    for proto in walk_prototypes(chunk.root):
        for item in proto.constants:
            entry: dict[str, object] = {
                "prototype": proto.path, "index": item.index,
                "offset": item.offset, "type": item.kind,
            }
            if isinstance(item.value, bytes):
                entry["value"] = gbk_text(item.value)
                entry["bytes_hex"] = item.value.hex()
            else:
                entry["value"] = item.value
            constants.append(entry)
        for instruction in proto.code:
            op = instruction & 0x3f
            opcodes[OPCODE_NAMES[op] if op < len(OPCODE_NAMES) else f"UNKNOWN_{op}"] += 1
    return {
        "version": f"0x{chunk.version:02X}",
        "byte_order": chunk.byte_order,
        "profile": "32-bit int, size_t, Instruction and integer mrp_Number",
        "prototype_count": sum(1 for _ in walk_prototypes(chunk.root)),
        "constants": constants,
        "global_references": global_references(chunk),
        "opcode_counts": dict(sorted(opcodes.items())),
    }


def read_package(path: Path, data: bytes | None = None) -> tuple[PackageReport, bytes]:
    size = path.stat().st_size
    if size > MAX_ARCHIVE:
        raise ValueError(f"Archive exceeds {MAX_ARCHIVE} bytes")
    if data is None:
        data = path.read_bytes()
    if len(data) > MAX_ARCHIVE:
        raise ValueError(f"Archive exceeds {MAX_ARCHIVE} bytes")
    report = parse_package(path, data)
    if report.errors:
        raise ValueError("; ".join(report.errors))
    return report, data


def stored_bytes(data: bytes, entry: ResourceEntry) -> bytes:
    end = entry.offset + entry.length
    if entry.offset < 0 or end > len(data):
        raise ValueError(f"Resource outside archive: {entry.name}")
    return data[entry.offset:end]


def source_layout(report: PackageReport, data: bytes) -> tuple[list[bytes], bytes, int, bool]:
    cursor = int(report.metadata["Index start"])
    expected_record = int(report.metadata["Index end"])
    canonical = True
    names = []
    for entry in report.entries:
        name_length = struct.unpack_from("<I", data, cursor)[0]
        name_field = data[cursor + 4:cursor + 4 + name_length]
        names.append(name_field)
        cursor += 16 + name_length
        prefix = struct.pack("<I", name_length) + name_field + struct.pack("<I", entry.length)
        if entry.offset != expected_record + len(prefix) or data[expected_record:entry.offset] != prefix:
            canonical = False
        expected_record = entry.offset + entry.length
    tail_start = max([int(report.metadata["Index end"])]
                     + [entry.offset + entry.length for entry in report.entries])
    undeclared = len(data) - int(report.metadata["Declared file size"])
    return names, data[tail_start:], undeclared, canonical


def audio_signature(data: bytes) -> str:
    if data.startswith(b"MThd"):
        return "MThd"
    if data.startswith(b"RIFF") and data[8:12] == b"WAVE":
        return "RIFF-WAVE"
    if data.startswith(b"ID3"):
        return "ID3"
    if len(data) >= 2 and data[0] == 0xff and data[1] & 0xe0 == 0xe0:
        return "mp3-frame-candidate"
    return "other"


def scan_one(path: Path, root: Path) -> dict[str, object]:
    result: dict[str, object] = {
        "package": path.relative_to(root).as_posix(), "class": "C",
        "error": None, "file_count": 0, "resources": [],
        "keywords": [], "api_markers": [], "plugin_markers": [],
        "ext_files": [], "entry_count": 0,
    }
    try:
        if path.stat().st_size > MAX_ARCHIVE:
            raise ValueError(f"Archive exceeds {MAX_ARCHIVE} bytes")
        data = path.read_bytes()
        result.update({"size": len(data), "sha256": digest(data), "magic": data[:4].hex()})
        report, data = read_package(path, data)
        result.update({
            "display_name": gbk_text(data[28:52].split(b"\0", 1)[0]),
            "internal_name": gbk_text(data[16:28].split(b"\0", 1)[0]),
            "file_count": len(report.entries), "warnings": report.warnings,
            "index_start": report.metadata["Index start"],
            "index_end": report.metadata["Index end"],
            "header_checksum": header_checksum_mode(data),
        })
        resources = []
        for entry in report.entries:
            raw = stored_bytes(data, entry)
            resource: dict[str, object] = {
                "index": entry.index, "name": gbk_text(entry.raw_name),
                "name_hex": entry.raw_name.hex(), "offset": entry.offset,
                "stored_size": entry.length, "reserved": entry.reserved,
                "signature_hex": raw[:8].hex(), "format_hint": resource_format(raw),
                "duplicate_case_insensitive": entry.duplicate,
            }
            try:
                kind, content = unpack_resource(raw)
                resource["validation"] = "opaque-or-raw-deflate" if kind == "stored" else kind
                resource["decoded_size"] = len(content) if kind != "stored" else None
                resource["examined_signature_hex"] = content[:12].hex()
                if kind == "stored":
                    try:
                        decompress_stream(raw, "raw-deflate probe", -15)
                        resource["complete_raw_deflate_candidate"] = True
                    except (ValueError, zlib.error):
                        resource["complete_raw_deflate_candidate"] = False
                extension = Path(entry.name.lower()).suffix
                if extension == ".ext":
                    lowered = content.lower()
                    resource["native_ascii_markers"] = [
                        word for word in (*KEYWORDS, *PLUGIN_MARKERS)
                        if word.encode("ascii") in lowered
                    ]
                if extension in (".mid", ".midi", ".wav", ".mp3"):
                    resource["audio_signature"] = audio_signature(content)
                if extension == ".mrp":
                    resource["nested_mrpg"] = content.startswith(b"MRPG")
            except ValueError as error:
                resource["validation"] = "error"
                resource["decode_error"] = str(error)
            resources.append(resource)
        result["resources"] = resources
        name_fields, trailing, undeclared, canonical = source_layout(report, data)
        rebuild_files = [(name, entry.reserved, stored_bytes(data, entry))
                         for name, entry in zip(name_fields, report.entries)]
        rebuilt = build_archive(bytearray(data[:int(report.metadata["Index start"])]),
                                rebuild_files, trailing, undeclared)
        result["raw_rebuild_equal"] = rebuilt == data
        result["canonical_layout"] = canonical
        result["trailing_size"] = len(trailing)
        ext_files = [gbk_text(entry.raw_name) for entry in report.entries
                     if entry.raw_name.lower().endswith(b".ext")]
        result["ext_files"] = ext_files
        starts = [entry for entry in report.entries if entry.raw_name == b"start.mr"]
        if not starts:
            starts = [entry for entry in report.entries if entry.raw_name == b"start.m"]
        result["entry_count"] = len(starts)
        if not starts:
            raise ValueError("No exact start.mr or start.m entry")
        selected = starts[0]
        result["entry"] = gbk_text(selected.raw_name)
        result["entry_index"] = selected.index
        result["entry_offset"] = selected.offset
        kind, bytecode = decode_payload(stored_bytes(data, selected))
        result["entry_compression"] = kind
        result["bytecode_size"] = len(bytecode)
        result["bytecode_sha256"] = digest(bytecode)
        if len(bytecode) >= 5 and bytecode[:4] == b"\x1bMRP":
            result["version"] = f"0x{bytecode[4]:02X}"
        chunk = ChunkReader(bytecode).parse()
        values = [item.value.lower() for _, item in string_constants(chunk)
                  if isinstance(item.value, bytes)]
        hits = [word for word in KEYWORDS if any(word.encode("ascii") in value for value in values)]
        result["keywords"] = hits
        result["api_markers"] = [
            word for word in API_MARKERS
            if any(word.lower().encode("ascii") in value for value in values)
        ]
        result["plugin_markers"] = [
            word for word in PLUGIN_MARKERS if any(word.encode("ascii") in value for value in values)
        ]
        result["global_references"] = global_references(chunk)
        result["opcode_counts"] = chunk_report(chunk)["opcode_counts"]
        result["class"] = "B" if ext_files or hits else "A"
    except (OSError, ValueError, struct.error) as error:
        result["error"] = str(error)
    return result


def catalog(root: Path, output: Path) -> dict[str, object]:
    if not root.is_dir():
        raise ValueError(f"Input is not a directory: {root}")
    paths = sorted((p for p in root.rglob("*")
                    if p.is_file() and p.suffix.lower() == ".mrp"),
                   key=lambda p: p.relative_to(root).as_posix().casefold())
    if not paths:
        raise ValueError(f"No .mrp files in {root}")
    rows = [scan_one(path, root) for path in paths]
    classes = Counter(str(row["class"]) for row in rows)
    versions = Counter(str(row["version"]) for row in rows if "version" in row)
    magics = Counter(str(row["magic"]) for row in rows if "magic" in row)
    checksums = Counter(str(row["header_checksum"]) for row in rows if "header_checksum" in row)
    entry_formats = Counter(str(row["entry_compression"]) for row in rows if "entry_compression" in row)
    formats: Counter[str] = Counter()
    validation: Counter[str] = Counter()
    reserved: Counter[str] = Counter()
    audio: Counter[str] = Counter()
    nested_packages: set[str] = set()
    nested_count = 0
    raw_candidates = 0
    errors = []
    for row in rows:
        for entry in row["resources"]:
            formats[str(entry["format_hint"])] += 1
            validation[str(entry["validation"])] += 1
            reserved[str(entry["reserved"])] += 1
            if entry.get("complete_raw_deflate_candidate"):
                raw_candidates += 1
            if "decode_error" in entry:
                errors.append({"package": row["package"], "name": entry["name"],
                               "index": entry["index"], "error": entry["decode_error"]})
            if entry.get("nested_mrpg"):
                nested_count += 1
                nested_packages.add(str(row["package"]))
            if "audio_signature" in entry:
                suffix = Path(str(entry["name"]).lower()).suffix
                audio[f"{suffix}:{entry['audio_signature']}"] += 1
    plugin_hits = Counter(word for row in rows for word in row["plugin_markers"])
    api_hits = Counter(word for row in rows for word in row["api_markers"])
    candidates = sorted((row for row in rows if row["class"] == "A"),
                        key=lambda row: (int(row["size"]), str(row["package"])))[:20]
    document: dict[str, object] = {
        "schema": "mrp-catalog-v1",
        "classification_note": (
            "A/B are filename and selected startup constant heuristics, not proof of "
            "runtime behavior. C includes unreadable or unsupported startup chunks."
        ),
        "startup_lookup": "first exact start.mr, otherwise first exact start.m",
        "compression_note": (
            "Resource formats are header hints. Raw deflate has no signature; "
            "opaque-or-raw-deflate is not an exact uncompressed count. A failed "
            "decode is recorded separately and does not change A/B startup classification."
        ),
        "api_markers_note": (
            "Case-insensitive substring matches in selected startup string constants; "
            "these are not proof of calls or native-extension dependency."
        ),
        "header_checksum_note": (
            "Comparison with CRC32 over the physical or declared bytes with offset "
            "0x54 zeroed; an unknown value is not proof of corruption."
        ),
        "summary": {
            "packages": len(rows), "classes": dict(sorted(classes.items())),
            "versions": dict(sorted(versions.items())), "magics": dict(sorted(magics.items())),
            "header_checksums": dict(sorted(checksums.items())),
            "entry_compression": dict(sorted(entry_formats.items())),
            "resource_format_hints": dict(sorted(formats.items())),
            "resource_validation": dict(sorted(validation.items())),
            "resource_decode_errors": errors,
            "complete_raw_deflate_candidates": raw_candidates,
            "nested_mrpg_resources": nested_count,
            "nested_mrpg_outer_packages": len(nested_packages),
            "audio_signature_counts": dict(sorted(audio.items())),
            "startup_api_markers": dict(sorted(api_hits.items())),
            "startup_plugin_markers": dict(sorted(plugin_hits.items())),
            "raw_rebuild_identical": sum(bool(row.get("raw_rebuild_equal")) for row in rows),
            "reserved_values": dict(sorted(reserved.items())),
            "top20": [row["package"] for row in candidates],
        },
        "packages": rows,
    }
    output.mkdir(parents=True, exist_ok=True)
    json_path, csv_path = output / "mrp_catalog.json", output / "mrp_catalog.csv"
    if json_path.exists() or csv_path.exists():
        raise ValueError(f"Catalog output already exists in {output}")
    fields = (
        "package", "display_name", "internal_name", "size", "sha256", "file_count", "header_checksum",
        "class", "keywords", "api_markers", "plugin_markers", "ext_files", "entry",
        "entry_index", "entry_count",
        "entry_offset", "entry_compression", "version", "bytecode_size",
        "bytecode_sha256", "error",
    )
    with json_path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(document, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    with csv_path.open("x", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            flat = dict(row)
            flat["keywords"] = "; ".join(row["keywords"])
            flat["api_markers"] = "; ".join(row["api_markers"])
            flat["plugin_markers"] = "; ".join(row["plugin_markers"])
            flat["ext_files"] = "; ".join(row["ext_files"])
            writer.writerow(flat)
    return document


def write_new(path: Path, data: bytes) -> None:
    with path.open("xb") as stream:
        stream.write(data)


def unpack_archive(path: Path, output: Path, raw_indices: set[int],
                   allow_bad_gzip_crc: bool = False) -> None:
    report, data = read_package(path)
    if any(index < 1 or index > len(report.entries) for index in raw_indices):
        raise ValueError("Raw-deflate index is outside the resource list")
    index_start = int(report.metadata["Index start"])
    decoded: list[tuple[ResourceEntry, bytes, str, bytes, bytes, str | None]] = []
    total = 0
    name_fields, trailing, undeclared_tail_size, canonical_layout = source_layout(report, data)
    for entry, name_field in zip(report.entries, name_fields):
        stored = stored_bytes(data, entry)
        kind, content = unpack_resource(stored, entry.index in raw_indices, allow_bad_gzip_crc)
        warning = None
        if kind == "gzip-crc-mismatch":
            kind = "gzip"
            warning = "gzip CRC mismatch accepted explicitly; content integrity is unverified"
            print(f"WARNING: {entry.name}: {warning}", file=sys.stderr)
        total += len(content)
        if total > MAX_ARCHIVE:
            raise ValueError(f"Total decoded resources exceed {MAX_ARCHIVE} bytes")
        decoded.append((entry, name_field, kind, content, stored, warning))
    if not canonical_layout:
        print("WARNING: Noncanonical record layout will be normalized when repacking.", file=sys.stderr)
    output.mkdir(parents=True, exist_ok=False)
    (output / "files").mkdir()
    (output / "stored").mkdir()
    files = []
    for entry, name_field, kind, content, stored, warning in decoded:
        relative = f"files/{entry.index:06d}.bin"
        original = f"stored/{entry.index:06d}.bin"
        write_new(output / relative, content)
        write_new(output / original, stored)
        files.append({
            "index": entry.index, "name": gbk_text(entry.raw_name),
            "name_hex": entry.raw_name.hex(), "name_field_hex": name_field.hex(),
            "reserved": entry.reserved,
            "path": relative, "stored_path": original, "compression": kind,
            "decoded_sha256": digest(content), "stored_sha256": digest(stored),
            "warning": warning,
        })
    if trailing:
        write_new(output / "trailing.bin", trailing)
    manifest = {
        "schema": "mrp-tool-unpack-v1", "source_sha256": digest(data),
        "header_checksum": header_checksum_mode(data),
        "header_prefix_hex": data[:index_start].hex(), "files": files,
        "canonical_layout": canonical_layout,
        "trailing": {"path": "trailing.bin", "sha256": digest(trailing)} if trailing else None,
        "undeclared_tail_size": undeclared_tail_size,
        "note": "Unknown stored streams may be raw deflate; use --raw-index to identify them.",
    }
    with (output / MANIFEST).open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(manifest, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def manifest_file(root: Path, value: object, expected: str, limit: int = MAX_OUTPUT) -> bytes:
    if value != expected:
        raise ValueError(f"Unexpected manifest path: {value!r}")
    path = root / expected
    if path.is_symlink() or path.parent.is_symlink():
        raise ValueError(f"Symlink not allowed: {expected}")
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError(f"Manifest path escapes input directory: {expected}")
    if path.stat().st_size > limit:
        raise ValueError(f"Resource exceeds {limit} bytes: {expected}")
    return path.read_bytes()


def encode_resource(content: bytes, kind: str) -> bytes:
    if kind == "stored":
        return content
    if kind == "gzip":
        return gzip.compress(content, mtime=0)
    if kind == "zlib":
        return zlib.compress(content)
    if kind == "raw-deflate":
        compressor = zlib.compressobj(wbits=-15)
        return compressor.compress(content) + compressor.flush()
    raise ValueError(f"Unknown compression: {kind!r}")


def files_from_manifest(
    root: Path,
) -> tuple[bytearray, list[tuple[bytes, int, bytes]], bytes, int, dict[str, object]]:
    with (root / MANIFEST).open(encoding="utf-8") as stream:
        manifest = json.load(stream)
    if not isinstance(manifest, dict) or manifest.get("schema") != "mrp-tool-unpack-v1":
        raise ValueError("Unsupported unpack manifest")
    try:
        header = bytearray.fromhex(manifest["header_prefix_hex"])
        items = manifest["files"]
    except (KeyError, TypeError, ValueError) as error:
        raise ValueError("Invalid manifest header or files") from error
    if not 240 <= len(header) <= MAX_ARCHIVE or header[:4] != b"MRPG" or not isinstance(items, list):
        raise ValueError("Invalid manifest header or resource list")
    if not items or len(items) > MAX_ENTRIES:
        raise ValueError("Empty or excessive manifest resource list")
    files: list[tuple[bytes, int, bytes]] = []
    for index, item in enumerate(items, 1):
        if not isinstance(item, dict) or item.get("index") != index:
            raise ValueError(f"Invalid manifest resource index {index}")
        try:
            name = bytes.fromhex(item["name_hex"])
            name_field = bytes.fromhex(item["name_field_hex"]) if "name_field_hex" in item else name + b"\0"
            reserved = item["reserved"]
            kind = item["compression"]
            content = manifest_file(root, item["path"], f"files/{index:06d}.bin")
            old_stored = manifest_file(root, item["stored_path"], f"stored/{index:06d}.bin",
                                       MAX_ARCHIVE)
            if digest(old_stored) != item["stored_sha256"]:
                raise ValueError(f"Stored checksum mismatch for resource {index}")
            if digest(content) == item["decoded_sha256"]:
                payload = old_stored
            else:
                payload = encode_resource(content, kind)
        except (KeyError, TypeError, ValueError) as error:
            raise ValueError(f"Invalid resource {index}: {error}") from error
        if (not name or b"\0" in name or not name_field or len(name_field) >= 256
                or name_field.split(b"\0", 1)[0] != name):
            raise ValueError(f"Invalid resource name at index {index}")
        if len(name_field) >= RUNTIME_MAX_FILENAME:
            print(f"WARNING: Resource {index} exceeds the archived runtime filename limit.", file=sys.stderr)
        if not isinstance(reserved, int) or not 0 <= reserved <= 0xffffffff:
            raise ValueError(f"Invalid reserved field at index {index}")
        files.append((name_field, reserved, payload))
    tail_item = manifest.get("trailing")
    trailing = b""
    if tail_item is not None:
        if not isinstance(tail_item, dict):
            raise ValueError("Invalid trailing data metadata")
        try:
            trailing = manifest_file(root, tail_item["path"], "trailing.bin", MAX_ARCHIVE)
            if digest(trailing) != tail_item["sha256"]:
                raise ValueError("Trailing data checksum mismatch")
        except (KeyError, TypeError) as error:
            raise ValueError("Invalid trailing data metadata") from error
    undeclared_tail_size = manifest.get("undeclared_tail_size", 0)
    if (not isinstance(undeclared_tail_size, int)
            or not 0 <= undeclared_tail_size <= len(trailing)):
        raise ValueError("Invalid undeclared trailing size")
    return header, files, trailing, undeclared_tail_size, manifest


def files_from_directory(root: Path) -> tuple[bytearray, list[tuple[bytes, int, bytes]], bytes, int]:
    paths = sorted((p for p in root.rglob("*") if p.is_file()),
                   key=lambda p: p.relative_to(root).as_posix())
    if not paths or len(paths) > MAX_ENTRIES:
        raise ValueError("Input directory is empty or has too many files")
    header = bytearray(240)
    header[:4] = b"MRPG"
    header[16:25] = b"pack.mrp\0"
    header[28:37] = b"MRP pack\0"
    files = []
    for path in paths:
        relative = path.relative_to(root)
        if path.is_symlink() or any((root / p).is_symlink() for p in relative.parents if str(p) != "."):
            raise ValueError(f"Symlink not allowed: {relative}")
        if path.stat().st_size > MAX_OUTPUT:
            raise ValueError(f"Resource exceeds {MAX_OUTPUT} bytes: {relative}")
        try:
            name = relative.as_posix().encode("gbk")
        except UnicodeEncodeError as error:
            raise ValueError(f"Resource name is not GBK: {relative}") from error
        if not name or b"\0" in name or len(name) + 1 >= RUNTIME_MAX_FILENAME:
            raise ValueError(f"Resource name exceeds the archived runtime limit: {relative}")
        files.append((name + b"\0", 0, path.read_bytes()))
    return header, files, b"", 0


def build_archive(header: bytearray, files: list[tuple[bytes, int, bytes]],
                  trailing: bytes = b"", undeclared_tail_size: int = 0) -> bytes:
    index_start = len(header)
    index_end = index_start + sum(16 + len(name) for name, _, _ in files)
    index, records = bytearray(), bytearray()
    for name, reserved, payload in files:
        prefix = struct.pack("<I", len(name)) + name + struct.pack("<I", len(payload))
        offset = index_end + len(records) + len(prefix)
        if offset + len(payload) > min(MAX_ARCHIVE, 0xffffffff):
            raise ValueError("Packed archive exceeds supported size")
        index += struct.pack("<I", len(name)) + name
        index += struct.pack("<III", offset, len(payload), reserved)
        records += prefix + payload
    total = index_end + len(records) + len(trailing)
    if total > min(MAX_ARCHIVE, 0xffffffff):
        raise ValueError("Packed archive exceeds supported size")
    struct.pack_into("<III", header, 4, index_end - 8, total - undeclared_tail_size, index_start)
    return bytes(header + index + records + trailing)


def pack_directory(root: Path, output: Path, force_builder_crc32: bool = False) -> None:
    if not root.is_dir():
        raise ValueError(f"Input is not a directory: {root}")
    if output.resolve().is_relative_to(root.resolve()):
        raise ValueError("Output archive must be outside the input directory")
    manifest = root / MANIFEST
    metadata = None
    if manifest.is_file():
        header, files, trailing, undeclared, metadata = files_from_manifest(root)
    else:
        header, files, trailing, undeclared = files_from_directory(root)
    result = build_archive(header, files, trailing, undeclared)
    mode = metadata.get("header_checksum") if metadata is not None else "builder-crc32"
    if mode not in (None, "zero", "builder-crc32", "declared-crc32", "unknown"):
        raise ValueError(f"Unknown header checksum mode: {mode!r}")
    if force_builder_crc32 or mode in ("builder-crc32", "declared-crc32"):
        end = len(result) if force_builder_crc32 or mode == "builder-crc32" else len(result) - undeclared
        updated = bytearray(result)
        struct.pack_into("<I", updated, 84, header_crc32(result, end))
        result = bytes(updated)
    elif (mode in (None, "unknown") and result[84:88] != b"\0" * 4
          and metadata is not None and digest(result) != metadata.get("source_sha256")):
        print("WARNING: Modified archive retains an unrecognized header checksum; "
              "runtime compatibility is unverified. Use --force-builder-crc32 "
              "only when the builder CRC32 format is appropriate.", file=sys.stderr)
    write_new(output, result)


def png_chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def bitmap_png(data: bytes, width: int, height: int, order: str) -> bytes:
    if width <= 0 or height <= 0 or width * height > 16_777_216:
        raise ValueError("Invalid bitmap dimensions")
    if len(data) != width * height * 2:
        raise ValueError(f"Expected {width * height * 2} RGB565 bytes, got {len(data)}")
    rows = bytearray()
    for y in range(height):
        rows.append(0)
        for x in range(width):
            offset = (y * width + x) * 2
            pixel = int.from_bytes(data[offset:offset + 2], order)
            r, g, b = (pixel >> 11) & 31, (pixel >> 5) & 63, pixel & 31
            rows.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) + png_chunk(b"IDAT", zlib.compress(rows)) + png_chunk(b"IEND", b"")


def add_common_output(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--output", type=Path, help="Output path; existing files are never overwritten")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    scan = commands.add_parser("scan", help="Catalog .mrp packages recursively")
    scan.add_argument("directory", type=Path)
    add_common_output(scan)
    unpack = commands.add_parser("unpack", help="Extract every indexed entry and a rebuild manifest")
    unpack.add_argument("file", type=Path)
    unpack.add_argument("--raw-index", type=int, action="append", default=[],
                        help="Treat this 1-based entry as raw deflate; repeatable")
    unpack.add_argument("--allow-bad-gzip-crc", action="store_true",
                        help="Extract gzip CRC mismatches with warnings; integrity is unverified")
    add_common_output(unpack)
    pack = commands.add_parser("pack", help="Rebuild an unpacked or ordinary resource directory")
    pack.add_argument("directory", type=Path)
    pack.add_argument("output", type=Path)
    pack.add_argument("--force-builder-crc32", action="store_true",
                      help="Replace the header checksum with CRC32 over the full archive")
    const = commands.add_parser("const", help="Print all constants from an uncompressed MRP chunk as JSON")
    const.add_argument("file", type=Path)
    add_common_output(const)
    bmp = commands.add_parser("bmp", help="Convert headerless RGB565 pixels to PNG")
    bmp.add_argument("file", type=Path)
    bmp.add_argument("--width", type=int, required=True)
    bmp.add_argument("--height", type=int, required=True)
    bmp.add_argument("--byte-order", choices=("little", "big"), default="little")
    add_common_output(bmp)
    args = parser.parse_args(argv)
    try:
        if args.command == "scan":
            output = args.output or Path("mrp_scan_report")
            doc = catalog(args.directory, output)
            print(json.dumps(doc["summary"], ensure_ascii=False, indent=2))
            print(f"Catalog: {output / 'mrp_catalog.json'}; {output / 'mrp_catalog.csv'}")
        elif args.command == "unpack":
            output = args.output or args.file.with_name(args.file.stem + "_unpacked")
            unpack_archive(args.file, output, set(args.raw_index), args.allow_bad_gzip_crc)
            print(f"Extracted to {output}; unknown streams remain stored unless --raw-index is specified.")
        elif args.command == "pack":
            pack_directory(args.directory, args.output, args.force_builder_crc32)
            print(f"Packed {args.output}; runtime compatibility requires separate testing.")
        elif args.command == "const":
            data = args.file.read_bytes()
            result = chunk_report(ChunkReader(data).parse())
            text = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
            if args.output:
                write_new(args.output, text.encode("utf-8"))
            else:
                sys.stdout.write(text)
        elif args.command == "bmp":
            data = args.file.read_bytes()
            output = args.output or args.file.with_suffix(".png")
            write_new(output, bitmap_png(data, args.width, args.height, args.byte_order))
            print(f"RGB565 PNG: {output}")
        return 0
    except (OSError, ValueError, struct.error, json.JSONDecodeError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
