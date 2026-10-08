"""Package the locally generated Unity iOS Xcode project for Git LFS.

The source is copied into a ZIP and split into 512 MiB pieces. No game files are
uploaded by this script; review the staged directory before pushing a private repo.
"""

import argparse
import hashlib
import json
from pathlib import Path
import zipfile

DEFAULT_SOURCE = Path(__file__).resolve().parents[1] / "analysis" / "unity-export-744825" / "ios-xcode-744825"
DEFAULT_OUTPUT = Path(__file__).resolve().parent / "package"
PART_SIZE = 512 * 1024 * 1024
ARCHIVE_NAME = "ios-xcode-744825.zip"


def package(source: Path, output: Path) -> dict:
    source = source.resolve(strict=True)
    output.mkdir(parents=True, exist_ok=True)
    output = output.resolve()
    if not (source / "Unity-iPhone.xcodeproj" / "project.pbxproj").is_file():
        raise ValueError(f"Not a Unity iOS Xcode project: {source}")
    if source == output or source in output.parents or output in source.parents:
        raise ValueError("The package directory must be outside the Xcode project")
    if list(output.glob("*.part-*")):
        raise FileExistsError("Existing archive parts found; choose an empty output directory")

    archive = output / ARCHIVE_NAME
    if archive.exists():
        raise FileExistsError(archive)
    files = sorted(path for path in source.rglob("*") if path.is_file())
    if not files:
        raise ValueError("The Xcode project is empty")
    try:
        with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6, allowZip64=True) as zf:
            for index, path in enumerate(files, start=1):
                zf.write(path, arcname=f"{source.name}/{path.relative_to(source).as_posix()}")
                if index % 500 == 0:
                    print(f"Added {index}/{len(files)} files", flush=True)

        digest = hashlib.sha256()
        parts = []
        with archive.open("rb") as input_file:
            for index in range((archive.stat().st_size + PART_SIZE - 1) // PART_SIZE):
                part = output / f"{ARCHIVE_NAME}.part-{index:03d}"
                with part.open("wb") as output_file:
                    remaining = PART_SIZE
                    while remaining:
                        block = input_file.read(min(4 * 1024 * 1024, remaining))
                        if not block:
                            break
                        output_file.write(block)
                        digest.update(block)
                        remaining -= len(block)
                parts.append({"name": part.name, "bytes": part.stat().st_size})
                print(f"Wrote {part.name}", flush=True)

        manifest = {
            "archive": ARCHIVE_NAME,
            "sha256": digest.hexdigest(),
            "bytes": archive.stat().st_size,
            "files": len(files),
            "parts": parts,
        }
        (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        return manifest
    finally:
        archive.unlink(missing_ok=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    print(json.dumps(package(args.source, args.output), indent=2))
