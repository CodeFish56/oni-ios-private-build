"""Recompress a signed IPA without changing any signed file contents.

zsign may wrap files in DEFLATE streams with negligible compression. IPA
signatures cover the bundle files, not their ZIP encoding, so repacking the
archive makes USB transfer practical while preserving the signed payload.
"""

from __future__ import annotations

import copy
from pathlib import Path
import shutil
import sys
import zipfile


def repack(source: Path, destination: Path) -> None:
    if source.resolve() == destination.resolve():
        raise ValueError("source and destination must differ")

    with zipfile.ZipFile(source, "r") as src, zipfile.ZipFile(
        destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6,
        allowZip64=True,
    ) as dst:
        for entry in src.infolist():
            info = copy.copy(entry)
            info.compress_type = zipfile.ZIP_DEFLATED
            with src.open(entry, "r") as input_stream, dst.open(
                info, "w", force_zip64=True,
            ) as output_stream:
                shutil.copyfileobj(input_stream, output_stream, 4 * 1024 * 1024)

    with zipfile.ZipFile(destination, "r") as dst:
        bad = dst.testzip()
        if bad:
            raise RuntimeError(f"Corrupt archive entry: {bad}")
        print(f"Repacked {len(dst.infolist())} entries: {destination.stat().st_size} bytes")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: repack_ipa.py SOURCE_IPA DESTINATION_IPA")
    repack(Path(sys.argv[1]), Path(sys.argv[2]))
