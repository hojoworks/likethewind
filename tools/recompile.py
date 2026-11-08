#!/usr/bin/env python3
"""Translate the three Power Drift 68000 programs with the vendored recompiler.

Usage:
    python tools/recompile.py --rom work/roms --analysis work/analysis \
        --out work/generated --recomp build/tools/m68krecomp.exe
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

CPUS = (
    ("main", "m_", 8),
    ("subx", "x_", 4),
    ("suby", "y_", 4),
)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--rom", type=Path, default=Path("work/roms"))
    ap.add_argument("--analysis", type=Path, default=Path("work/analysis"))
    ap.add_argument("--out", type=Path, default=Path("work/generated"))
    ap.add_argument("--recomp", type=Path, default=Path("build/tools/m68krecomp.exe"))
    ap.add_argument("--no-pointer-scan", action="store_true")
    ap.add_argument("--no-entries", action="store_true",
                    help="ignore analysis/entries_*.txt and extra_entries_*.txt "
                         "(use the ROM's vectors only)")
    args = ap.parse_args()

    if not args.recomp.exists():
        print(f"error: recompiler not built: {args.recomp}", file=sys.stderr)
        return 1

    args.out.mkdir(parents=True, exist_ok=True)
    for old in args.out.glob("*"):
        if old.is_file():
            old.unlink()

    for cpu, prefix, files in CPUS:
        rom = args.rom / f"{cpu}.bin"
        if not rom.exists():
            print(f"error: missing {rom}; run prepare_rom.py first", file=sys.stderr)
            return 1
        cmd = [
            str(args.recomp),
            "--raw", str(rom),
            "--base", "0",
            "--prefix", prefix,
            "--out", str(args.out),
            "--files", str(files),
        ]
        entries = args.analysis / f"entries_{cpu}.txt"
        extra_entries = args.analysis / f"extra_entries_{cpu}.txt"
        if not args.no_entries:
            for path in (entries, extra_entries):
                if path.exists():
                    cmd += ["--entries", str(path)]
        if args.no_pointer_scan:
            cmd.append("--no-pointer-scan")
        print(" ".join(cmd))
        r = subprocess.run(cmd, text=True, capture_output=True)
        if r.returncode != 0:
            print(r.stdout, end="")
            print(r.stderr, end="", file=sys.stderr)
            return r.returncode
        for line in r.stderr.splitlines():
            if line.startswith("[m68krecomp]"):
                print(f"  {line}")

    # A small manifest so the build knows exactly what to compile.
    sources = sorted(p.name for p in args.out.glob("*.c"))
    (args.out / "sources.txt").write_text("\n".join(sources) + "\n")
    print(f"wrote {len(sources)} C files to {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
