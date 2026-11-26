#!/usr/bin/env python3
"""Extract an MRP entry and explain its first 64 bytes without executing it."""

from __future__ import annotations

import argparse
import hashlib
import struct
import sys
import zlib
from pathlib import Path

if __package__:
    from .mrp_to_markdown import PackageReport, ResourceEntry, parse_package
else:
    from mrp_to_markdown import PackageReport, ResourceEntry, parse_package


MAX_OUTPUT = 16 * 1024 * 1024


def decompress_stream(payload: bytes, name: str, wbits: int, limit: int = MAX_OUTPUT) -> bytes:
    if limit < 1:
        raise ValueError("Output limit must be positive")
    decoder = zlib.decompressobj(wbits)
    decoded = decoder.decompress(payload, limit + 1)
    if len(decoded) > limit:
        raise ValueError(f"{name}: decompressed output exceeds {limit} bytes")
    if not decoder.eof:
        raise ValueError(f"{name}: truncated compressed stream")
    if decoder.unused_data:
        raise ValueError(f"{name}: trailing data or concatenated streams are not supported")
    return decoded


def select_entry(report: PackageReport, name: str | None) -> ResourceEntry:
    candidates = (name,) if name is not None else ("start.mr", "start.m")
    for candidate in candidates:
        matches = [entry for entry in report.entries if entry.raw_name == candidate.encode("gbk")]
        if len(matches) > 1:
            raise ValueError(f"Ambiguous entry: {candidate!r} occurs more than once")
        if matches:
            return matches[0]
    raise ValueError(f"Entry not found: {', '.join(candidates)}")


def decode_payload(payload: bytes, limit: int = MAX_OUTPUT) -> tuple[str, bytes]:
    if limit < 1:
        raise ValueError("Output limit must be positive")
    if payload.startswith((b"\x1bMRP", b"\x1bLua")):
        if len(payload) > limit:
            raise ValueError(f"Uncompressed payload exceeds {limit} bytes")
        return "uncompressed", payload

    formats = (("gzip", 31), ("zlib", 15), ("raw-deflate", -15))
    if payload.startswith(b"\x1f\x8b"):
        formats = formats[:1]
    elif (len(payload) >= 2 and payload[0] & 15 == 8 and payload[0] >> 4 <= 7
          and int.from_bytes(payload[:2], "big") % 31 == 0):
        formats = formats[1:2]

    errors = []
    for name, wbits in formats:
        try:
            decoded = decompress_stream(payload, name, wbits, limit)
        except zlib.error as error:
            errors.append(f"{name}: {error}")
            continue
        if not decoded:
            raise ValueError(f"{name}: empty entry")
        return name, decoded
    raise ValueError(
        f"Cannot decode entry (stored prefix: {payload[:16].hex(' ')}). "
        + "; ".join(errors)
        + ". Unknown uncompressed/encrypted formats are not guessed."
    )


def describe_prefix(data: bytes) -> tuple[str, list[str]]:
    notes = ["Chunk body; not interpreted"] * min(64, len(data))
    position = 0

    def take(size: int, label: str) -> bytes:
        nonlocal position
        end = position + size
        if size < 0 or end > len(data):
            raise ValueError(f"Truncated {label} at 0x{position:X}")
        for offset in range(position, min(end, len(notes))):
            notes[offset] = f"{label}; byte {offset - position + 1}/{size}"
        result = data[position:end]
        position = end
        return result

    if not data.startswith((b"\x1bMRP", b"\x1bLua")):
        return "Unknown signature; no decryption or header removal inferred", notes
    magic = take(4, "Signature: ESC + MRP" if data[1:4] == b"MRP" else "Signature: ESC + Lua")
    version = take(1, f"Chunk format version = 0x{data[4]:02X}" if len(data) > 4 else "version")[0]
    endian = take(1, "Byte order: 1=little, 0=big")[0]
    if endian not in (0, 1):
        raise ValueError(f"Invalid byte-order flag: {endian}")
    if magic != b"\x1bMRP" or version != 0x80:
        return "Recognized signature; only MRP 0x80 body headers are interpreted", notes

    order = "<" if endian else ">"

    def integer(label: str, signed: bool = True, nonnegative: bool = True) -> int:
        if position + 4 > len(data):
            raise ValueError(f"Truncated {label} at 0x{position:X}")
        value = struct.unpack_from(order + ("i" if signed else "I"), data, position)[0]
        take(4, f"{label} = {value}")
        if nonnegative and value < 0:
            raise ValueError(f"Negative {label}: {value}")
        return value

    source_length = integer("Source-name length including NUL (32-bit candidate)", signed=False)
    source = take(source_length, "Source-name bytes including NUL")
    if source and not source.endswith(b"\0"):
        raise ValueError("Source-name is not NUL-terminated under the 32-bit profile")
    integer("Function lineDefined")
    for name in ("nups", "numparams", "is_vararg", "maxstacksize"):
        value = data[position] if position < len(data) else "missing"
        take(1, f"Function {name} = {value}")
    line_count = integer("Line-info count")
    if position + line_count * 4 > len(data):
        raise ValueError("Truncated line-info vector under the 32-bit profile")
    for index in range(min(line_count, max(0, (len(notes) - position + 3) // 4))):
        integer(f"Line-info[{index}]", nonnegative=False)
    return (
        "MRP 0x80; header interpreted with 32-bit int/size_t from the archived target profile. "
        "This is not validation of the complete chunk or of game compatibility.",
        notes,
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("--entry", help="Exact GBK resource name; default: start.mr, then start.m")
    parser.add_argument("--output", type=Path, required=True, help="New file for decoded bytes; never overwritten")
    args = parser.parse_args(argv)
    try:
        report = parse_package(args.package)
        if report.errors:
            raise ValueError("; ".join(report.errors))
        for warning in report.warnings:
            print(f"WARNING: {warning}", file=sys.stderr)
        entry = select_entry(report, args.entry)
        package = args.package.read_bytes()
        if len(package) != report.file_size or entry.offset + entry.length > len(package):
            raise ValueError("Package changed after index validation")
        stored = package[entry.offset:entry.offset + entry.length]
        compression, decoded = decode_payload(stored)
        interpretation, notes = describe_prefix(decoded)
        extensions = [item.name for item in report.entries if item.raw_name.lower().endswith(b".ext")]

        print(f"Package: {args.package}")
        print(f"Package size: {len(package)}")
        print(f"Package SHA256: {hashlib.sha256(package).hexdigest()}")
        print(f"Index: 0x{report.metadata['Index start']:X}..0x{report.metadata['Index end']:X} (end exclusive)")
        print(f"Entry: {entry.name}")
        print(f"Payload offset: 0x{entry.offset:X}; stored size: {entry.length}")
        print(f"Stored prefix: {stored[:16].hex(' ')}")
        print(f"Compression: {compression}; decoded size: {len(decoded)}")
        print(f"Decoded SHA256: {hashlib.sha256(decoded).hexdigest()}")
        print(f"EXT entries: {', '.join(extensions) if extensions else 'none by filename'}")
        if extensions:
            print("WARNING: EXT presence needs dependency analysis; do not assume this is a pure-script game.")
        print(f"Interpretation: {interpretation}")
        print(f"First {len(notes)} decoded bytes:")
        for offset in range(0, len(notes), 16):
            print(f"  {offset:04X}: {decoded[offset:offset + 16].hex(' ')}")
        print("Per-byte candidates (offset, hex, ASCII, interpretation):")
        for offset, note in enumerate(notes):
            value = decoded[offset]
            character = chr(value) if 32 <= value < 127 else "."
            print(f"  {offset:04X}  {value:02X}  {character}  {note}")

        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open("xb") as output:
            output.write(decoded)
        print(f"Extracted without modification: {args.output}")
        return 0
    except (OSError, ValueError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
