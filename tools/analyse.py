#!/usr/bin/env python3
"""First-pass analysis of the three Power Drift 68000 programs.

This does not replace the recompiler's recursive descent; it gives the build a
sane seed set and a quick health check:

  * reset SSP/PC from the first two vectors;
  * every exception vector that points into the program;
  * direct BSR/JSR/JMP/Bcc/LEA/PEA targets that land in the image;
  * aligned 32-bit values that point at plausible code;
  * a byte/word histogram and the reset disassembly.

Outputs `analysis.json` and `entries_<cpu>.txt` (one hex address per line), which
`recompile.py` feeds to m68krecomp.
"""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter
from pathlib import Path

CPUS = ("main", "subx", "suby")


def be32(data: bytes, at: int) -> int:
    return struct.unpack_from(">I", data, at)[0]


def be16(data: bytes, at: int) -> int:
    return struct.unpack_from(">H", data, at)[0]


def plausible_code(data: bytes, addr: int) -> bool:
    """Cheap filter: aligned, inside the image, and not all 00/FF around it."""
    if addr & 1 or addr + 4 > len(data):
        return False
    window = data[addr:addr + 16]
    if not window or all(b == 0 for b in window) or all(b == 0xFF for b in window):
        return False
    return True


def scan_direct_targets(data: bytes) -> Counter:
    """Find obvious direct control-flow targets. Intentionally conservative."""
    hits: Counter = Counter()
    n = len(data) - 4
    for at in range(0, n, 2):
        op = be16(data, at)
        target = None
        # JSR/JMP absolute long
        if op in (0x4EB9, 0x4EF9):
            target = be32(data, at + 2)
        # JSR/JMP (d16,PC)
        elif op in (0x4EBA, 0x4EFA):
            target = (at + 2 + struct.unpack_from(">h", data, at + 2)[0]) & 0xFFFFFF
        # BSR / BRA / Bcc: low byte 0 means a word displacement, otherwise
        # an 8-bit displacement relative to PC+2.
        elif (op & 0xFF00) in (0x6000, 0x6100):
            disp8 = op & 0xFF
            if disp8 == 0:
                disp = struct.unpack_from(">h", data, at + 2)[0]
            else:
                disp = disp8 - 256 if disp8 & 0x80 else disp8
            target = (at + 2 + disp) & 0xFFFFFF
        # LEA (d16,PC),An / PEA (d16,PC)
        elif (op & 0xF8FF) == 0x41FA or op == 0x487A:
            target = (at + 2 + struct.unpack_from(">h", data, at + 2)[0]) & 0xFFFFFF
        if target is not None and target >= 0x100 and plausible_code(data, target):
            hits[target] += 1
    return hits


def scan_pointers(data: bytes) -> Counter:
    """Aligned longwords that point at plausible code (tables, task lists)."""
    hits: Counter = Counter()
    for at in range(0, len(data) - 4, 2):
        v = be32(data, at)
        if v >= 0x100 and plausible_code(data, v):
            hits[v] += 1
    return hits


def analyse(name: str, data: bytes, include_pointers: bool = False) -> dict:
    ssp = be32(data, 0)
    pc = be32(data, 4) & 0xFFFFFF
    vectors = []
    for i in range(2, 64):
        v = be32(data, i * 4) & 0xFFFFFF
        # Unused vectors are zero; the first 0x100 bytes are the vector table,
        # never code, so don't mistake them for it.
        if v >= 0x100 and plausible_code(data, v):
            vectors.append({"vector": i, "address": v, "name": vector_name(i)})

    direct = scan_direct_targets(data)
    pointers = scan_pointers(data)
    entries = {pc}
    entries.update(v["address"] for v in vectors)
    entries.update(direct)
    if include_pointers:
        entries.update(pointers)
    entries = sorted(a for a in entries if plausible_code(data, a))

    histogram = Counter(data)
    return {
        "cpu": name,
        "size": len(data),
        "reset_ssp": ssp,
        "reset_pc": pc,
        "vectors": vectors,
        "direct_targets": len(direct),
        "pointer_targets": len(pointers),
        "entry_count": len(entries),
        "entries": entries,
        "byte_histogram_top": histogram.most_common(8),
    }


def vector_name(i: int) -> str:
    names = {
        2: "bus error", 3: "address error", 4: "illegal instruction",
        5: "zero divide", 6: "CHK", 7: "TRAPV", 8: "privilege violation",
        9: "trace", 10: "line-A", 11: "line-F", 24: "spurious",
        25: "level 1", 26: "level 2 (timer)", 27: "level 3",
        28: "level 4 (vblank)", 29: "level 5", 30: "level 6",
        31: "level 7", 32: "TRAP #0",
    }
    if i in names:
        return names[i]
    if 33 <= i <= 47:
        return f"TRAP #{i - 32}"
    return f"vector {i}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("romdir", type=Path, help="folder written by prepare_rom.py")
    ap.add_argument("--out", type=Path, default=Path("work/analysis"))
    ap.add_argument("--with-pointers", action="store_true",
                    help="add every plausible code pointer to the entries file (large)")
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    summary = {}
    for cpu in CPUS:
        path = args.romdir / f"{cpu}.bin"
        if not path.exists():
            print(f"warning: missing {path}")
            continue
        data = path.read_bytes()
        info = analyse(cpu, data, args.with_pointers)
        (args.out / f"{cpu}.json").write_text(json.dumps(info, indent=2) + "\n")
        (args.out / f"entries_{cpu}.txt").write_text(
            "".join(f"{a:06X}\n" for a in info["entries"])
        )
        # A compact terminal report.
        print(f"{cpu}: {len(data):,} bytes, reset PC ${info['reset_pc']:06X}, "
              f"SSP ${info['reset_ssp']:08X}")
        print(f"  vectors: {len(info['vectors'])}, direct targets: {info['direct_targets']}, "
              f"pointer targets: {info['pointer_targets']}, seeds: {info['entry_count']}")
        for v in info["vectors"]:
            print(f"    vector {v['vector']:2d} {v['name']:<20} ${v['address']:06X}")
        summary[cpu] = {k: info[k] for k in
                        ("size", "reset_ssp", "reset_pc", "entry_count",
                         "direct_targets", "pointer_targets")}

    (args.out / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
