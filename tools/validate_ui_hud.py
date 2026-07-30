"""Validate the replacement Enhanced Edition gameplay HUD atlas."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

EXPECTED_WIDTH = 4096
EXPECTED_HEIGHT = 4096
EXPECTED_FOURCC = b"DXT5"
ALLOWED_MIP_COUNTS = {1, 13}
DDS_HEADER_SIZE = 128


def read_u32(header: bytes, offset: int) -> int:
    return struct.unpack_from("<I", header, offset)[0]


def validate(path: Path) -> list[str]:
    errors: list[str] = []
    with path.open("rb") as stream:
        header = stream.read(DDS_HEADER_SIZE)

    if len(header) < DDS_HEADER_SIZE:
        return [f"file is shorter than the {DDS_HEADER_SIZE}-byte DDS header"]
    if header[:4] != b"DDS ":
        return ["DDS magic is missing"]

    header_size = read_u32(header, 4)
    height = read_u32(header, 12)
    width = read_u32(header, 16)
    mip_count = read_u32(header, 28)
    pixel_format_size = read_u32(header, 76)
    fourcc = header[84:88]

    if header_size != 124:
        errors.append(f"header size is {header_size}, expected 124")
    if pixel_format_size != 32:
        errors.append(f"pixel-format size is {pixel_format_size}, expected 32")
    if width != EXPECTED_WIDTH or height != EXPECTED_HEIGHT:
        errors.append(
            f"dimensions are {width}x{height}, "
            f"expected {EXPECTED_WIDTH}x{EXPECTED_HEIGHT}"
        )
    if fourcc != EXPECTED_FOURCC:
        errors.append(
            f"FourCC is {fourcc.decode('ascii', errors='replace')!r}, "
            f"expected {EXPECTED_FOURCC.decode()!r}"
        )
    if mip_count not in ALLOWED_MIP_COUNTS:
        errors.append(
            f"mip count is {mip_count}, expected one of "
            f"{sorted(ALLOWED_MIP_COUNTS)}"
        )
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("texture", type=Path)
    args = parser.parse_args()

    if not args.texture.is_file():
        parser.error(f"file does not exist: {args.texture}")

    errors = validate(args.texture)
    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1

    print(
        f"PASS: {args.texture} is "
        f"{EXPECTED_WIDTH}x{EXPECTED_HEIGHT} DXT5 with "
        f"{read_u32(args.texture.read_bytes()[:DDS_HEADER_SIZE], 28)} "
        f"mip level(s)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
