#!/usr/bin/env python3
"""nvl_game_list.py <default.map> > app/nvl_game.toml: non_volatile_as_local_only for the game's own code.

scudrace-nx (2026-11-09). The global non_volatile_as_local broke the Xbox libraries' code (a garbage D3D device, the
PC build and the console alike, even with the setjmp callers kept in ctx). The game's own objects (the Model 3
program as C, the sound board, the Real3D renderer, its menus and netplay) are plain C and hold the CPU time; their
functions keep r14-r31, f14-f31 and v14-v31/v64-v127 in C++ locals. Left out: every library function (an object named
lib:obj in the map), the linker's 'i' entries, coro.obj (the fibers) and the setjmp callers (m3_run).
"""
import re
import sys

SKIP_OBJ = {"i", "coro.obj"}
SKIP_NAME = {"m3_run"}  # calls setjmp (by name: the address moves between releases)
fns = []
for line in open(sys.argv[1], errors="replace"):
    m = re.match(r"\s*0003:\w{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+f\s+(\S+)", line)
    if not m:
        continue
    addr, obj = int(m.group(2), 16), m.group(3)
    if ":" in obj or obj in SKIP_OBJ or m.group(1) in SKIP_NAME or addr & 3:
        continue
    fns.append((addr, m.group(1), obj))
print("# Written by tools/nvl_game_list.py from default.map: non_volatile_as_local for the game's own functions only.")
print(f"# {len(fns)} functions.")
print("non_volatile_as_local_only = [")
for addr, name, obj in sorted(set(fns)):
    print(f"  0x{addr:08X},  # {obj} {name[:60]}")
print("]")
