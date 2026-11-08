#!/usr/bin/env python3
"""Summarize PCM sample roles from an --audio-trace log.

The Y-Board's 16 hardware PCM voices are shared by the music driver and the
effect driver. This report groups every control write by sample id, source
voice, control value and the mixer group: 0 = music voice, 1 = SFX, 2 =
engine. It works on both game traces and standalone song traces.

Usage:
    build\\LikeTheWind.exe ... --audio-trace > work\\pcm.log
    python tools\\pcm_roles.py work\\pcm.log

    build\\LikeTheWindMixer.exe --track 91 --frames 600 --trace ^
        --no-save --headless --fast 2> work\\song91.log
    python tools\\pcm_roles.py work\\song91.log
"""
from __future__ import annotations

import argparse
import re
from collections import Counter, defaultdict
from pathlib import Path

LINE = re.compile(
    r"^\[pcm\].*voice=(\d+) sample=([0-9A-Fa-f]+) ctrl=([0-9A-Fa-f]+) "
    r"freq=([0-9A-Fa-f]+) group=(\d+)"
)


def read_text(path: Path) -> str:
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16")
    return data.decode("utf-8", "replace")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("logs", nargs="+", type=Path)
    args = ap.parse_args()

    samples: dict[int, Counter] = defaultdict(Counter)
    voices: dict[int, Counter] = defaultdict(Counter)
    controls: dict[int, Counter] = defaultdict(Counter)
    groups: dict[int, Counter] = defaultdict(Counter)

    for path in args.logs:
        for line in read_text(path).splitlines():
            m = LINE.match(line)
            if not m:
                continue
            voice = int(m.group(1))
            sample = int(m.group(2), 16)
            ctrl = int(m.group(3), 16)
            group = int(m.group(5))
            if sample in (0x00, 0xFF):
                continue
            samples[sample][group] += 1
            voices[sample][voice] += 1
            controls[sample][ctrl] += 1
            groups[sample][group] += 1

    if not samples:
        print("no [pcm] sample triggers found")
        return 1

    print("sample group writes voices controls")
    for sample in sorted(samples):
        group = groups[sample].most_common(1)[0][0]
        vs = ",".join(f"{v}:{n}" for v, n in sorted(voices[sample].items()))
        cs = ",".join(f"{c:02X}:{n}" for c, n in sorted(controls[sample].items()))
        print(f"  {sample:02X}    {group}     {sum(samples[sample].values()):4d}  "
              f"{vs:28s} {cs}")
    print("\nrole: group 0 = music voice, group 1 = SFX, group 2 = engine. "
          "Music is the default; only the known effect ids are pulled out.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
