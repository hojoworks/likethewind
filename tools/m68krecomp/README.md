Vendored 68000→C recompiler

Source: https://github.com/sp00nznet/neogeorecomp (`tools/m68krecomp/`)
License: MIT, Copyright (c) 2026 Ned Heller

Local changes:
- removed the NeoGeo-specific `ng_recomp_main` and ROM loader;
- raw images get their reset/exception vectors as seeds;
- `--entries FILE` and unlimited `--entry` are supported;
- generated code includes `likethewind/` headers;
- every block in a routine is a switch case (fixes legitimate re-entry at a
  resume point);
- generated files use the caller's prefix.

The generated C is derived from the user's ROM and must never be committed.
