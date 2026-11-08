# Third-party notices

LikeTheWind is MIT-licensed. It includes or derives from the
following projects. Their licences apply to their parts; this file is a summary,
not a replacement for the licence texts shipped with each project.

## NeoGeoRecomp / m68krecomp

- <https://github.com/sp00nznet/neogeorecomp>
- MIT License, Copyright (c) 2026 Ned Heller

`tools/m68krecomp/` is the 68000→C recompiler from NeoGeoRecomp, adapted to read
flat Y Board images and to emit `likethewind/` headers. `runtime/include/likethewind/cpu.h`
and `runtime/src/cpu.cpp` are adapted from its `cpu.h`/`cpu.c`. The recompiler's
output is generated locally from the user's ROM and is never committed.

## Musashi

- <https://github.com/kstenerud/Musashi>
- MIT License, Copyright Karl Stenerud

`tools/musashi/` contains the 68000 interpreter/disassembler used by the
recompiler and (in later milestones) as the runtime fallback and verification
oracle. The opcode tables (`m68kops.c`) are generated at build time by
`m68kmake`. `tools/musashi/softfloat/` is part of Musashi and carries its own
licence headers.

## superzazu/z80

- <https://github.com/superzazu/z80>
- MIT License, Copyright Nicolas Allemand

`vendor/z80/` contains the Z80 core used by the sound CPU runtime. Its MIT
licence text is included there.

## ymfm

- <https://github.com/aaronsgiles/ymfm>
- BSD-3-Clause, Copyright Aaron Giles

`vendor/ymfm/` contains the YM2151/OPM core used by the sound runtime. Its
BSD-3-Clause licence text is included there.

## MAME

- <https://github.com/mamedev/mame>
- BSD-3-Clause for `src/mame/sega/segaybd.cpp`, `sega16sp.cpp`, `segaic16.cpp`,
  `315_5296.cpp`, `segapcm.cpp`, `msm6253.cpp`

Used as the behavioural reference for the Y Board and as the source of the ROM
file table (names, sizes, CRC-32/SHA-1). The runtime code here is our own; no
MAME source is copied into it. Any future direct port of a MAME device keeps the
BSD-3 notice in that file.

## MiSTer Sega Y Board core

- <https://github.com/rossops/Arcade-SegaYBoard_MiSTer>
- GPL-3.0

Read as a hardware/behavioural reference. No GPL code is copied into this MIT
project. If that changes, the affected files must be GPL-3.0 and the project's
licence must be revisited.

## miniz

- <https://github.com/richgel999/miniz>
- MIT License / public domain

`vendor/miniz/` is the ZIP reader used by the built-in ROM preparer, so a
release can verify and unpack the user's `pdrift.zip` without Python.

## SDL2

- <https://libsdl.org/>
- zlib License

The host window/input layer. SDL2 is downloaded into `vendor/` at build time
and is not committed.

## Zig

- <https://ziglang.org/>
- MIT License

Used as a portable C/C++ toolchain during the bootstrap build. Zig is downloaded
into `vendor/` and is not part of the release.

## Power Drift / Sega Y Board

Power Drift, Sega, and Sega Y Board are trademarks of SEGA Corporation. This is
an unofficial, non-commercial preservation project, not affiliated with or
endorsed by SEGA. No game ROMs or game assets are included or distributed.
