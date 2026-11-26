#!/usr/bin/env python3
"""Index extracted Mythroad CHM API topics and compare them with archived C sources."""

from __future__ import annotations

import argparse
import csv
import json
import re
import sys
from html.parser import HTMLParser
from pathlib import Path


class VisibleText(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.skipped = 0
        self.parts: list[str] = []

    def handle_starttag(self, tag: str, _attrs: list[tuple[str, str | None]]) -> None:
        if tag in ("head", "script", "style"):
            self.skipped += 1

    def handle_endtag(self, tag: str) -> None:
        if tag in ("head", "script", "style") and self.skipped:
            self.skipped -= 1

    def handle_data(self, data: str) -> None:
        if not self.skipped:
            self.parts.append(data)


def extract_signature(document: str, name: str, sdk: bool) -> tuple[str | None, str]:
    parser = VisibleText()
    parser.feed(document)
    text = re.sub(r"\s+", " ", " ".join(parser.parts))
    marker = re.search(r"API\s*定义\s*[:：]?", text) if sdk else None
    if marker:
        text = text[marker.end():]
        confidence = "definition-heading"
    elif sdk:
        confidence = "heuristic"
    else:
        text = re.split(r"\bDescription\b", text, maxsplit=1, flags=re.IGNORECASE)[0]
        confidence = "platform-header"
    name_pattern = r"\s*_\s*".join(re.escape(part) for part in name.split("_"))
    match = re.search(r"(?<!\w)(?P<decl_name>" + name_pattern + r")\s*\(",
                      text, re.IGNORECASE)
    callback = False
    if match is None:
        match = re.search(r"\(\s*\*\s*(?P<decl_name>" + name_pattern + r")\s*\)\s*\(",
                          text, re.IGNORECASE)
        if match is None:
            return None, "missing"
        callback = True
    depth = 1
    end = match.end()
    while end < len(text) and depth:
        depth += (text[end] == "(") - (text[end] == ")")
        end += 1
    prefix = text[:match.start()].rstrip()
    result_type = re.search(
        r"((?:(?:const|unsigned|signed|struct)\s+)?[A-Za-z_]\w*(?:\s*\*)*)$",
        prefix,
    )
    if depth or end - match.start() > 500 or result_type is None:
        return None, "missing"
    declared_name = re.sub(r"\s*_\s*", "_", match.group("decl_name"))
    declarator = f"(*{declared_name})" if callback else declared_name
    return f"{result_type.group(1)} {declarator}{text[match.end() - 1:end]}", confidence


def strip_c_comments_and_literals(text: str) -> str:
    pattern = r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(
        pattern,
        lambda match: "".join("\n" if char == "\n" else " " for char in match.group()),
        text,
        flags=re.DOTALL,
    )


def closing_brace(text: str, start: int) -> int:
    depth = 0
    for pos in range(start, len(text)):
        if text[pos] == "{":
            depth += 1
        elif text[pos] == "}":
            depth -= 1
            if depth == 0:
                return pos
    raise ValueError(f"Unmatched C brace at offset {start}")


def source_definitions(text: str, label: str) -> dict[str, list[str]]:
    clean = strip_c_comments_and_literals(text)
    result: dict[str, list[str]] = {}
    pattern = (r"(?m)^[ \t]*(?!(?:if|while|for|switch|return)\b)"
               r"[A-Za-z_][\w \t*]*?\b((?:mr|mrc)_[A-Za-z_]\w*)"
               r"[ \t]*\([^;{}]*\)[ \t]*\{")
    for match in re.finditer(pattern, clean):
        line = clean.count("\n", 0, match.start(1)) + 1
        result.setdefault(match.group(1), []).append(f"{label}:{line}")
    return result


def selector_mismatch(selector: str, signature: str | None) -> bool:
    if signature is None:
        return False
    code = re.search(r"\bcode\s*=\s*(0[xX][0-9A-Fa-f]+|[0-9]+)\b", signature)
    if code is None:
        return False
    try:
        return int(selector, 0) != int(code.group(1), 0)
    except ValueError:
        return False


def switch_cases(text: str, function: str, header: str) -> tuple[dict[int, int], list[str]]:
    clean = strip_c_comments_and_literals(text)
    macros = {
        match.group(1): int(match.group(2), 0)
        for match in re.finditer(
            r"(?m)^\s*#define\s+([A-Za-z_]\w*)\s+(0[xX][0-9A-Fa-f]+|[0-9]+)\b",
            header,
        )
    }
    match = re.search(r"\b" + re.escape(function) + r"\s*\([^;{}]*\)\s*\{", clean)
    if match is None:
        raise ValueError(f"Missing function: {function}")
    function_end = closing_brace(clean, match.end() - 1)
    switch = re.search(r"\bswitch\s*\(\s*code\s*\)\s*\{", clean[match.end():function_end])
    if switch is None:
        raise ValueError(f"Missing code switch in {function}")
    start = match.end() + switch.end() - 1
    end = closing_brace(clean, start)
    cases: dict[int, int] = {}
    unresolved: list[str] = []
    depth = 1
    for token in re.finditer(r"[{}]|\bcase\s+([A-Za-z_]\w*|0[xX][0-9A-Fa-f]+|[0-9]+)\s*:",
                             clean[start + 1:end]):
        value = token.group(0)
        if value == "{":
            depth += 1
        elif value == "}":
            depth -= 1
        elif depth == 1:
            label = token.group(1)
            if label.isdecimal() or label.lower().startswith("0x"):
                number = int(label, 0)
            elif label in macros:
                number = macros[label]
            else:
                unresolved.append(label)
                continue
            cases[number] = clean.count("\n", 0, start + 1 + token.start()) + 1
    return cases, unresolved


def index_docs(sky: Path, sdk: Path, source: Path, output: Path) -> dict[str, object]:
    dsm = (source / "dsm.c").read_text(encoding="utf-8")
    runtime = (source / "mythroad.c").read_text(encoding="utf-8")
    header = (source / "include" / "mrporting.h").read_text(encoding="utf-8")
    definitions = source_definitions(dsm, "dsm.c")
    for name, locations in source_definitions(runtime, "mythroad.c").items():
        definitions.setdefault(name, []).extend(locations)
    switches = {name: switch_cases(dsm, name, header) for name in ("mr_plat", "mr_platEx")}

    rows: list[dict[str, object]] = []
    for directory, doc_set, sdk_doc in ((sky, "skyengine", False), (sdk, "mythroad-api", True)):
        if not directory.is_dir():
            raise ValueError(f"Extracted document directory not found: {directory}")
        prefix = "mrc_" if sdk_doc else "mr_"
        for path in sorted(directory.glob("*.htm"), key=lambda item: item.name.casefold()):
            if not path.stem.startswith(prefix):
                continue
            name, _, selector = path.stem.partition("(")
            signature, confidence = extract_signature(
                path.read_bytes().decode("gb18030"), name, sdk_doc
            )
            status = "not-a-selector"
            if name in switches and selector.endswith(")"):
                label = selector[:-1]
                try:
                    code = int(label, 0)
                except ValueError:
                    status = "range-not-compared"
                else:
                    cases, unresolved = switches[name]
                    status = (f"case:dsm.c:{cases[code]}" if code in cases else
                              "indeterminate" if unresolved else "default")
            rows.append({
                "document": doc_set,
                "topic": path.name,
                "api": name,
                "selector": selector[:-1] if selector.endswith(")") else "",
                "signature": signature,
                "signature_source": confidence,
                "selector_mismatch": selector_mismatch(
                    selector[:-1] if selector.endswith(")") else "", signature
                ),
                "exact_source_definitions": definitions.get(name, []),
                "dsm_dispatch": status,
            })
    if not rows:
        raise ValueError("No API pages found in extracted document directories")
    unresolved = {name: values[1] for name, values in switches.items() if values[1]}
    result: dict[str, object] = {
        "schema": "mrp-doc-api-index-v1",
        "note": (
            "Signatures are mechanically extracted and require review. Exact textual "
            "source matches do not prove availability, and a switch case may be a stub."
        ),
        "summary": {
            "topics": len(rows),
            "skyengine": sum(row["document"] == "skyengine" for row in rows),
            "mythroad_api": sum(row["document"] == "mythroad-api" for row in rows),
            "signature_missing": sum(row["signature"] is None for row in rows),
            "signature_heuristic": sum(row["signature_source"] == "heuristic" for row in rows),
            "selector_mismatches": sum(bool(row["selector_mismatch"]) for row in rows),
            "unresolved_c_cases": unresolved,
        },
        "topics": rows,
    }
    if unresolved:
        print(f"WARNING: Unresolved C case labels: {unresolved}", file=sys.stderr)
    output.mkdir(parents=True, exist_ok=True)
    json_path, csv_path = output / "api_index.json", output / "api_index.csv"
    if json_path.exists() or csv_path.exists():
        raise ValueError(f"Index output already exists in {output}")
    with json_path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    fields = ("document", "topic", "api", "selector", "signature",
              "signature_source", "selector_mismatch", "exact_source_definitions", "dsm_dispatch")
    with csv_path.open("x", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            flat = dict(row)
            flat["exact_source_definitions"] = "; ".join(row["exact_source_definitions"])
            writer.writerow(flat)
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("skyengine", type=Path, help="Extracted SKYENGINE API CHM directory")
    parser.add_argument("mythroad_api", type=Path, help="Extracted Mythroad API CHM directory")
    parser.add_argument("--source", type=Path, required=True, help="Archived mythroad source directory")
    parser.add_argument("--output", type=Path, required=True, help="New output directory")
    args = parser.parse_args(argv)
    try:
        result = index_docs(args.skyengine, args.mythroad_api, args.source, args.output)
        print(json.dumps(result["summary"], ensure_ascii=False, indent=2))
        return 0
    except (OSError, UnicodeError, ValueError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
