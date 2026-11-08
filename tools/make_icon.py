#!/usr/bin/env python3
"""Wrap the local icon.png in a Windows .ico container.

Windows 10/11 accept PNG-compressed icons. The source PNG is a local build
asset and is deliberately not committed.
"""
from __future__ import annotations

import struct
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: make_icon.py icon.png icon.ico", file=sys.stderr)
        return 2

    src = Path(sys.argv[1])
    dst = Path(sys.argv[2])
    png = src.read_bytes()
    if not png.startswith(b"\x89PNG\r\n\x1a\n") or len(png) < 24:
        print(f"{src}: not a PNG", file=sys.stderr)
        return 1

    width, height = struct.unpack(">II", png[16:24])
    if not (1 <= width <= 256 and 1 <= height <= 256):
        print(f"{src}: icon must be at most 256x256", file=sys.stderr)
        return 1

    # ICONDIR + one ICONDIRENTRY, followed by the PNG payload. A dimension of
    # 256 is stored as 0 in the ICO directory.
    ico = struct.pack("<HHH", 0, 1, 1)
    ico += struct.pack(
        "<BBBBHHII",
        0 if width == 256 else width,
        0 if height == 256 else height,
        0,
        0,
        1,
        32,
        len(png),
        22,
    )
    ico += png
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(ico)
    print(f"icon: {src} ({width}x{height}) -> {dst}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
