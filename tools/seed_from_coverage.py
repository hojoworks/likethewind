#!/usr/bin/env python3
"""Turn a coverage report's fallback PCs into persistent recompiler seeds.

`analyse.py` regenerates `entries_<cpu>.txt` from scratch, so gaps found at
runtime should not be appended there. This tool writes them to
`extra_entries_<cpu>.txt` instead; `recompile.py` passes both files to the
recompiler.

Usage:
    python tools/seed_from_coverage.py work/coverage.txt
    python tools/seed_from_coverage.py work/coverage.txt work/coverage2.txt
    python tools/seed_from_coverage.py --reset work/coverage.txt
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path

CPUS = ("main", "subx", "suby")
DEFAULT_SIZES = {"main": 0x80000, "subx": 0x40000, "suby": 0x40000}
SECTION = re.compile(r"CPU \d+ \((\w+)\)")
FALLBACK = re.compile(r"  \$([0-9A-Fa-f]+) count=")


def rom_sizes(romdir: Path) -> dict[str, int]:
    sizes = dict(DEFAULT_SIZES)
    for cpu in CPUS:
        path = romdir / f"{cpu}.bin"
        if path.exists():
            sizes[cpu] = path.stat().st_size
    return sizes


def parse_reports(reports: list[Path], sizes: dict[str, int]) -> dict[str, set[int]]:
    pcs = {cpu: set() for cpu in CPUS}
    for report in reports:
        cpu = None
        for line in report.read_text().splitlines():
            m = SECTION.match(line)
            if m:
                cpu = m.group(1)
                continue
            m = FALLBACK.match(line)
            if m and cpu in pcs:
                addr = int(m.group(1), 16)
                if 0 <= addr < sizes[cpu]:
                    pcs[cpu].add(addr)
    return pcs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("reports", nargs="+", type=Path,
                    help="coverage report(s) written by --coverage")
    ap.add_argument("--analysis", type=Path, default=Path("work/analysis"))
    ap.add_argument("--rom", type=Path, default=Path("work/roms"))
    ap.add_argument("--reset", action="store_true",
                    help="discard existing extra_entries_<cpu>.txt files first")
    args = ap.parse_args()

    sizes = rom_sizes(args.rom)
    found = parse_reports(args.reports, sizes)
    args.analysis.mkdir(parents=True, exist_ok=True)

    for cpu in CPUS:
        path = args.analysis / f"extra_entries_{cpu}.txt"
        existing: set[int] = set()
        if path.exists() and not args.reset:
            for line in path.read_text().splitlines():
                line = line.strip()
                if line:
                    existing.add(int(line, 16))
        before = len(existing)
        merged = existing | found[cpu]
        path.write_text("".join(f"{a:06X}\n" for a in sorted(merged)))
        print(f"{cpu}: {len(merged) - before} new, {len(merged)} total "
              f"-> {path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
