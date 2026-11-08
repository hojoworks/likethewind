Vendored Musashi 68000 core

Source: https://github.com/kstenerud/Musashi
License: MIT, Copyright Karl Stenerud (see the header of each source file)

Used by:
- the recompiler, for disassembly text and cycle counts;
- the runtime, as the interpreter fallback for addresses with no translated
  entry.

`m68kops.c` / `m68kops.h` are generated at build time by `m68kmake` from
`m68k_in.c`. `m68kconf.h` is the project's 68000-only configuration.
