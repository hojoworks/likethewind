#!/usr/bin/env python3
"""Build the synthetic 68000 image used by --selftest.

It exercises: reset vector, DBcc loop, MOVE.L to shared RAM, MOVE.W to shared
RAM, STOP, and the autovector table. No game data.
"""
from pathlib import Path
import struct

W = lambda x: struct.pack(">H", x)
L = lambda x: struct.pack(">I", x)

img = bytearray(0x20000)

def put(at, data): img[at:at + len(data)] = data

# Reset: SSP and PC.
put(0x000, L(0x001FF000))
put(0x004, L(0x00000200))

# Autovectors 2, 4, 6 -> a shared RTE handler.
for vec in (26, 28, 30):
    put(vec * 4, L(0x00000300))

# 000200: move.l #0,d0
put(0x200, W(0x203C) + L(0))
# 000206: moveq #5,d1
put(0x206, W(0x7205))
# 000208: addq.l #1,d0
put(0x208, W(0x5280))
# 00020A: dbra d1, $208   (disp relative to the extension word at 0x20C:
# 0x208 - 0x20C = -4)
put(0x20A, W(0x51C9) + W(0xFFFC))
# 00020E: move.l d0, $000C0000   (shared RAM, not $00C00000)
put(0x20E, W(0x23C0) + L(0x000C0000))
# 000214: move.w #$1234, $000C0004
put(0x214, W(0x33FC) + W(0x1234) + L(0x000C0004))
# 00021C: stop #$2700
put(0x21C, W(0x4E72) + W(0x2700))
# 000300: rte
put(0x300, W(0x4E73))

# 000400: a routine the static recompiler never sees (used by --fallbacktest).
# move.l #9,$000C0000 ; stop #$2700
put(0x400, W(0x23FC) + L(9) + L(0x000C0000))
put(0x40A, W(0x4E72) + W(0x2700))

out = Path(__file__).resolve().parent / "fixtures" / "selftest.bin"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_bytes(img)
print(f"wrote {out} ({len(img)} bytes)")
