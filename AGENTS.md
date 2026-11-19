# Agent instructions

Before inspecting or changing this project, read `wilibsp/AGENTS.md` completely
through EOF. That file is the authoritative FreeWili 2 BSP and app contract.
It is intentionally long: if your reader truncates output, continue from the
last line in additional chunks until you reach EOF. Do not act after reading
only its first chunk.

After that contract is loaded, read this repository's README and any additional
instructions below.

## This repository

- `wilibsp/` is a git submodule pinned to a tested commit. Do not edit it here;
  bump the pin deliberately and re-verify on hardware.
- Use `tools\dev` (Windows) / `python tools/dev.py` instead of `fw build` /
  `fw ramrun`, which only know the wilibsp tree. Device verbs (`screenshot`,
  `press`, `hold`, `rtt`, ...) pass straight through to wilibsp's `fw`.
- `src/vp_game.c` is pure C and host-tested by `tools\dev test`; keep
  hardware calls in `src/main.c`.
- Record on-board verification in `docs/` (see the existing findings file).
