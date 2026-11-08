#!/usr/bin/env python3
"""Verify a Power Drift MAME ROM set and write the Y Board's flat images.

Usage:
    python tools/prepare_rom.py pdrift.zip --out work/roms
    python tools/prepare_rom.py /path/to/pdrift-folder --out work/roms

The source may be a zip or a folder. Every file is checked by size, CRC-32 and
SHA-1 before anything is written. The output contains only bytes from the user's
own dump and is gitignored.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import zipfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from romsets import PDRIFT  # noqa: E402


class RomError(Exception):
    pass


def read_sources(path: Path) -> dict[str, bytes]:
    """Return {basename: bytes} from a zip or a folder (case-insensitive)."""
    if path.is_dir():
        out = {}
        for p in path.rglob("*"):
            if p.is_file():
                out.setdefault(p.name.lower(), p.read_bytes())
        return out
    if path.is_file() and zipfile.is_zipfile(path):
        out = {}
        with zipfile.ZipFile(path) as z:
            for info in z.infolist():
                if info.is_dir():
                    continue
                out[info.filename.rsplit("/", 1)[-1].lower()] = z.read(info)
        return out
    raise RomError(f"not a zip or folder: {path}")


def verify(name: str, data: bytes, size: int, crc: int, sha1: str) -> None:
    if len(data) != size:
        raise RomError(f"{name}: expected {size} bytes, got {len(data)}")
    got_crc = zlib.crc32(data) & 0xFFFFFFFF
    if got_crc != crc:
        raise RomError(f"{name}: CRC-32 {got_crc:08x}, expected {crc:08x}")
    got_sha = hashlib.sha1(data).hexdigest()
    if got_sha != sha1:
        raise RomError(f"{name}: SHA-1 {got_sha}, expected {sha1}")


def build_region(spec: dict, sources: dict[str, bytes], verbose: bool) -> bytes:
    kind = spec["kind"]
    out = bytearray([spec.get("fill", 0)]) * spec["size"]

    for entry in spec["files"]:
        name, size, crc, sha1, offset = entry[:5]
        copies = entry[5] if len(entry) > 5 else 1
        key = name.lower()
        if key not in sources:
            # Real dumps sometimes carry a different EPROM label (the PCM
            # 11755/11756 pair is a known example). Match by size + CRC and
            # report the name actually used.
            found = None
            for k, d in sources.items():
                if len(d) == size and (zlib.crc32(d) & 0xFFFFFFFF) == crc:
                    found = k
                    break
            if found is None:
                raise RomError(f"missing file: {name} (no file with its size/CRC)")
            print(f"  note: {name} is named {found} in this dump; using it", file=sys.stderr)
            key = found
        data = sources[key]
        verify(name, data, size, crc, sha1)
        if verbose:
            print(f"  ok  {name:<20} {size:>8,} bytes  crc32={crc:08x}")

        if kind == "flat":
            for k in range(copies):
                at = offset + k * size
                if at + size > len(out):
                    raise RomError(f"{name}: copy {k} past region end")
                out[at:at + size] = data
        elif kind == "w16":
            # offset is a byte lane (0 = even, 1 = odd) at a 16-bit word base
            lane = offset & 1
            base = offset & ~1
            if base + size * 2 > len(out):
                raise RomError(f"{name}: past region end")
            out[base + lane:base + lane + size * 2:2] = data
        elif kind == "x64":
            # offset = base + lane, lane 0..7; each source byte is one byte of
            # an eight-byte (64-bit) word.
            lane = offset & 7
            base = offset & ~7
            if base + size * 8 > len(out):
                raise RomError(f"{name}: past region end")
            out[base + lane:base + lane + size * 8:8] = data
        else:
            raise RomError(f"unknown interleave {kind!r}")

    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("source", type=Path, help="pdrift.zip or a folder of its files")
    ap.add_argument("--out", type=Path, default=Path("work/roms"), help="output folder")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    try:
        sources = read_sources(args.source)
        if not args.quiet:
            print(f"source: {args.source} ({len(sources)} files)")

        # Reject a wrong set early: the four main program files are the identity.
        required = {f[0].lower() for f in PDRIFT["regions"]["main"]["files"]}
        missing = sorted(required - set(sources))
        if missing:
            raise RomError(
                "this does not look like the MAME `pdrift` set; missing "
                + ", ".join(missing)
            )

        args.out.mkdir(parents=True, exist_ok=True)
        manifest = {"set": PDRIFT["set"], "title": PDRIFT["title"], "regions": {}}
        for region, spec in PDRIFT["regions"].items():
            if not args.quiet:
                print(f"[{region}]")
            data = build_region(spec, sources, not args.quiet)
            name = f"{region}.bin"
            (args.out / name).write_bytes(data)
            manifest["regions"][region] = {
                "file": name,
                "size": len(data),
                "sha1": hashlib.sha1(data).hexdigest(),
            }
            if not args.quiet:
                print(f"  -> {name}  {len(data):,} bytes  sha1={manifest['regions'][region]['sha1']}")

        (args.out / "rom.json").write_text(json.dumps(manifest, indent=2) + "\n")
        if not args.quiet:
            print(f"wrote {args.out / 'rom.json'}")
        return 0
    except RomError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
