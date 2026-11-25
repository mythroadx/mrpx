import argparse
import os
import shutil
import time
import uuid
import zipfile
from pathlib import Path


def create_archive(source: Path, destination: Path, executables: list[str]) -> None:
    source = source.resolve(strict=True)
    destination = destination.resolve()
    if destination.is_relative_to(source):
        raise ValueError("Archive output must be outside the package tree")
    for name in executables:
        if not (source / name).is_file():
            raise ValueError(f"Missing package executable: {name}")
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "315532800"))
    if not 315532800 <= epoch <= 4354819199:
        raise ValueError("SOURCE_DATE_EPOCH must be within the ZIP date range")
    date = time.gmtime(epoch)[:6]
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_name(f"{destination.name}.{uuid.uuid4().hex}.tmp")
    try:
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path in sorted(source.rglob("*")):
                if path.is_symlink():
                    raise ValueError(f"Package must not contain symlinks: {path}")
                if not path.is_file():
                    continue
                relative = path.relative_to(source).as_posix()
                info = zipfile.ZipInfo(f"{source.name}/{relative}", date)
                info.create_system = 3
                info.external_attr = (0o100755 if relative in executables else 0o100644) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                with path.open("rb") as input_file, archive.open(info, "w") as output_file:
                    shutil.copyfileobj(input_file, output_file)
        temporary.replace(destination)
    finally:
        temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Create a release ZIP with portable executable permissions")
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--executable", action="append", default=[])
    args = parser.parse_args()
    create_archive(args.source, args.destination, args.executable)
