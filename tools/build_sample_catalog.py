#!/usr/bin/env python3
"""Build a PCM sample catalog from --audio-trace logs.

The Sega PCM voice registers contain a sample id in the voice's first byte,
but the actual start/end/loop addresses come from the Z80 driver. Trace logs
record those fields, so the game and mixer can replay any sample that has been
seen in a song or in gameplay without disassembling the sound driver.

Usage:
    python tools/build_sample_catalog.py work/sample_song_*.log \
        work/sample_game_*.log > work/sample_catalog.cfg
"""
from __future__ import annotations

import glob
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

LINE = re.compile(
    r"^\[pcm\].*voice=(\d+) sample=([0-9A-Fa-f]+) ctrl=([0-9A-Fa-f]+) "
    r"freq=([0-9A-Fa-f]+) group=(\d+) addr=([0-9A-Fa-f]+) "
    r"end=([0-9A-Fa-f]+) loop=([0-9A-Fa-f]+) bank=([0-9A-Fa-f]+)"
)
FREQ_LINE = re.compile(
    r"^\[pcmfreq\].*voice=(\d+) sample=([0-9A-Fa-f]+) freq=([0-9A-Fa-f]+)"
)


def read_text(path: Path) -> str:
    data = path.read_bytes()
    if data.startswith((b"\xff\xfe", b"\xfe\xff")):
        return data.decode("utf-16", "ignore")
    return data.decode("utf-8", "ignore")


def main(paths: list[Path]) -> int:
    variants: dict[int, Counter] = defaultdict(Counter)
    freqs: dict[int, Counter] = defaultdict(Counter)
    groups: dict[int, Counter] = defaultdict(Counter)
    voices: dict[int, Counter] = defaultdict(Counter)
    expanded: list[Path] = []
    for path in paths:
        matches = [Path(p) for p in glob.glob(str(path))]
        expanded.extend(matches or [path])
    for path in expanded:
        for line in read_text(path).splitlines():
            fm = FREQ_LINE.match(line)
            if fm:
                sample = int(fm.group(2), 16)
                freq = int(fm.group(3), 16)
                if sample and freq and freq != 0xFF:
                    freqs[sample][freq] += 1
                continue
            m = LINE.match(line)
            if not m:
                continue
            voice = int(m.group(1))
            sample = int(m.group(2), 16)
            ctrl = int(m.group(3), 16)
            freq = int(m.group(4), 16)
            group = int(m.group(5))
            addr = int(m.group(6), 16)
            end = int(m.group(7), 16)
            loop = int(m.group(8), 16)
            bank_ctrl = int(m.group(9), 16)
            if sample == 0 or (ctrl & 1):
                continue
            bank = (bank_ctrl & 0xF8) << 13
            start = (bank + ((addr >> 8) & 0x1FFFFF)) & 0x1FFFFF
            loop_at = (bank + loop) & 0x1FFFFF
            # The end register is the high address byte; playback stops when
            # addr >> 16 reaches end + 1.
            end_at = (bank + ((end + 1) << 8)) & 0x1FFFFF
            variants[sample][(start, end_at, loop_at, freq, bank_ctrl)] += 1
            groups[sample][group] += 1
            voices[sample][voice] += 1

    print("# id start end loop freq bank group voices hits")
    for sample in sorted(variants):
        (start, end, loop, freq, bank_ctrl), hits = variants[sample].most_common(1)[0]
        if freqs[sample]:
            freq = freqs[sample].most_common(1)[0][0]
        group = groups[sample].most_common(1)[0][0]
        vlist = ",".join(str(v) for v, _ in sorted(voices[sample].items()))
        print(f"{sample:02X} {start:06X} {end:06X} {loop:06X} {freq:02X} "
              f"{bank_ctrl:02X} {group} {vlist} {hits}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        raise SystemExit(2)
    raise SystemExit(main([Path(p) for p in sys.argv[1:]]))
