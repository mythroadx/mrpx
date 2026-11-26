"""Headless regression of bundled MRP applications using fixed pixel baselines."""

import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
RESOURCES = (
    Path("assets", "runtime", "cfunction.ext"),
    Path("assets", "runtime", "mythroad", "system", "gb12.uc2"),
    Path("assets", "runtime", "mythroad", "system", "gb16.uc2"),
    Path("assets", "runtime", "mythroad", "dsm_gm.mrp"),
    Path("tests", "fixtures", "mrp", "mpc.mrp"),
    Path("tests", "fixtures", "mrp", "asm.mrp"),
    Path("tests", "fixtures", "mrp", "asm_thumb.mrp"),
)


@dataclass(frozen=True)
class Case:
    name: str
    package: str
    sha256: str
    height: int = 320
    action: tuple[str, ...] = ()
    action_marker: str = ""


CASES = (
    Case("launcher", "dsm_gm.mrp",
         "b1a35423f64d220169a3ff7aac7fe456df60609be9e09d2b5aa71a1c252f7204"),
    Case("mpc", "mpc.mrp",
         "4eef29fb9c62d5997463126d04cdd6bd394637c2d3670cb72d6f19d71f71601b",
         height=295),
    Case("arm", "asm.mrp",
         "ab754e366aee580d18f56c612a513d05474737b57bcd26ace58f28ef61690271"),
    Case("thumb", "asm_thumb.mrp",
         "fdef71f46e297861746cb77f4a41a8ef1700740c834ba8fe761fdb7703dacb2a"),
    Case("arm-key", "asm.mrp",
         "9e27da152e7048f46a0ea8fa2f89bb0917487ef3bad7e620a2ba24a0d7a821ab",
         action=("--right-softkey-after", "200"),
         action_marker="Sent right softkey for screenshot capture."),
    Case("thumb-touch", "asm_thumb.mrp",
         "4cc5c663731e80ebe10f50d51e286e9af2c6903c934881c94f6245b145ede91a",
         action=("--click-right-after", "200"),
         action_marker="Clicked lower-right softkey for screenshot capture."),
)


@dataclass
class Result:
    name: str
    command: list[str]
    passed: bool = False
    returncode: int | None = None
    error: str | None = None
    elapsed_seconds: float = 0


def stage_runtime(destination: Path) -> None:
    for source in RESOURCES:
        if source.name == "cfunction.ext":
            target = destination / source.name
        elif source.suffix == ".uc2":
            target = destination / "mythroad" / "system" / source.name
        else:
            target = destination / "mythroad" / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / source, target)


def read_pixels(path: Path) -> bytes:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise ValueError("Invalid BMP header")
    file_size, = struct.unpack_from("<I", data, 2)
    offset, dib_size = struct.unpack_from("<II", data, 10)
    width, signed_height, planes, bits, compression = struct.unpack_from("<iiHHI", data, 18)
    if width != 240 or abs(signed_height) != 320:
        raise ValueError(f"Expected 240x320 BMP, got {width}x{signed_height}")
    if dib_size < 40 or planes != 1 or bits not in (24, 32) or compression != 0:
        raise ValueError("Expected uncompressed 24-bit or 32-bit BMP")
    stride = ((width * bits + 31) // 32) * 4
    if file_size != len(data) or offset < 14 + dib_size or offset + stride * 320 > len(data):
        raise ValueError("Truncated or inconsistent BMP data")
    pixels = bytearray()
    for y in range(320):
        row = 319 - y if signed_height > 0 else y
        start = offset + row * stride
        if bits == 24:
            pixels.extend(data[start:start + width * 3])
        else:
            for x in range(width):
                pixel = start + x * 4
                pixels.extend(data[pixel:pixel + 3])
    return bytes(pixels)


def validate_capture(case: Case, returncode: int, log: str, screenshot: Path) -> None:
    if returncode != 0:
        raise ValueError(f"Simulator exited with code {returncode}")
    markers = (
        f"VM started: package={case.package} module=start.mr",
        f"Saved screenshot: {screenshot}",
        case.action_marker,
    )
    for marker in markers:
        if marker and marker not in log:
            raise ValueError(f"Missing log marker: {marker}")
    for diagnostic in (
        "Invalid Guest", "Unterminated Guest", "Unaligned Guest",
        "Invalid host pointer", "Tracing mem_invalid", "my_malloc", "my_free",
    ):
        if diagnostic in log:
            raise ValueError(f"Runtime diagnostic: {diagnostic}")
    pixels = read_pixels(screenshot)
    region = pixels[:240 * case.height * 3]
    if all(region[index:index + 3] == region[:3] for index in range(0, len(region), 3)):
        raise ValueError("Screenshot is a single color")
    actual = hashlib.sha256(region).hexdigest()
    if actual != case.sha256:
        raise ValueError(f"Pixel baseline mismatch: expected {case.sha256}, got {actual}")


def run_case(case: Case, simulator: Path, output: Path, timeout: float) -> Result:
    directory = output / case.name
    directory.mkdir()
    screenshot = directory / "screenshot.bmp"
    command = [
        str(simulator), "--mrp", case.package, "--module", "start.mr",
        "--screenshot", str(screenshot), "--capture-delay", "1200", *case.action,
    ]
    result = Result(case.name, command)
    started = time.monotonic()
    try:
        with tempfile.TemporaryDirectory(prefix="vmrp-regression-") as temporary:
            runtime = Path(temporary)
            stage_runtime(runtime)
            environment = dict(
                os.environ, VMRP_RUNTIME_DIR=str(runtime), VMRP_DISABLE_NETWORK="1",
                SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
            )
            log_path = directory / "stdout.log"
            with log_path.open("wb") as log:
                completed = subprocess.run(
                    command, cwd=runtime, env=environment, stdout=log,
                    stderr=subprocess.STDOUT, timeout=timeout, check=False,
                )
            result.returncode = completed.returncode
            validate_capture(
                case, completed.returncode,
                log_path.read_text(encoding="utf-8", errors="replace"), screenshot,
            )
        result.passed = True
    except subprocess.TimeoutExpired:
        result.error = f"Simulator timed out after {timeout:g} seconds"
    except (OSError, ValueError) as error:
        result.error = str(error)
    result.elapsed_seconds = round(time.monotonic() - started, 3)
    return result


def positive_seconds(value: str) -> float:
    seconds = float(value)
    if not 0 < seconds < float("inf"):
        raise argparse.ArgumentTypeError("Timeout must be finite and greater than zero")
    return seconds


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simulator", type=Path, default=ROOT / "bin" / ("main.exe" if os.name == "nt" else "main"))
    parser.add_argument("--output", type=Path, default=ROOT / "build" / "mrp-regression")
    parser.add_argument("--case", action="append", choices=[case.name for case in CASES])
    parser.add_argument("--timeout", type=positive_seconds, default=15.0)
    args = parser.parse_args()
    simulator = args.simulator.resolve()
    missing = [path for path in (simulator, *(ROOT / source for source in RESOURCES))
               if not path.is_file()]
    if missing:
        parser.error("Missing input files: " + ", ".join(str(path) for path in missing))
    args.output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=args.output.resolve()))
    results = []
    for case in CASES:
        if args.case and case.name not in args.case:
            continue
        result = run_case(case, simulator, output, args.timeout)
        results.append(result)
        status = "PASS" if result.passed else "FAIL"
        detail = f": {result.error}" if result.error else ""
        print(f"{status} {case.name} ({result.elapsed_seconds:.3f}s){detail}", flush=True)
    passed = all(result.passed for result in results)
    report = {
        "passed": passed,
        "simulator": str(simulator),
        "resources": {str(source): hashlib.sha256((ROOT / source).read_bytes()).hexdigest()
                      for source in RESOURCES},
        "cases": [asdict(result) for result in results],
    }
    (output / "results.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Results: {output}")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
