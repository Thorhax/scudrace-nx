#!/usr/bin/env python3
"""Write app/scud_map.toml from the linker map that ships with Scud Race's Xbox 360 build (default.map).

The map lists every function the linker placed. Three things come from it:
  * setjmp_address / longjmp_address (LIBCMT setjmp.obj / longjmp.obj),
  * [rexcrt]: the XAPI/CRT functions the SDK replaces with host code (fibers are required: the sound board's 68000
    driver runs as a coroutine on fibers, coro.obj; the memory and string routines are host-native for speed),
  * [functions]: map entry points the code generator's discovery did not find (pointer-only targets: vtable thunks,
    handler tables). Pass the generated scud_init.cpp to compute them; without it this section is left empty.

usage: tools/scud_map_overrides.py assets/game_root/default.map [app/generated/default/scud_init.cpp] > new.toml
The entries of an existing app/scud_map.toml are kept (the generated tree already contains them), so the tool can be
run again after every codegen; move new.toml over app/scud_map.toml afterwards.
"""
import os
import re
import sys

# Host replacements taken from the SDK's REX_HOOK(rexcrt_*) list. File functions stay guest code (they go through
# the emulated kernel's NtCreateFile/NtReadFile like everything else).
REXCRT = [
    "ConvertThreadToFiber", "ConvertFiberToThread", "CreateFiber", "DeleteFiber", "SwitchToFiber",
    "memchr", "memcpy", "memcpy_s", "memmove", "memset",
    "strchr", "strcpy_s", "_stricmp", "strncmp", "strncpy", "strrchr", "strstr",
    "wcslen", "wcsncpy",
    "XMemCpy", "XMemSet", "XMemSet128",
]


def main():
    map_path = sys.argv[1]
    init_path = sys.argv[2] if len(sys.argv) > 2 else None
    funcs = {}  # address -> (name, object)
    by_name = {}
    line_re = re.compile(r"^\s*0003:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+f\s+(?:i\s+)?(\S+)")
    with open(map_path, encoding="latin-1") as f:
        for line in f:
            m = line_re.match(line)
            if not m:
                continue
            name, addr, obj = m.group(1), int(m.group(2), 16), m.group(3)
            if addr < 0x82000000:
                continue
            funcs.setdefault(addr, (name, obj))
            by_name.setdefault(name, addr)

    out = ["# Written by tools/scud_map_overrides.py from default.map (the linker map in Scud Race's Xbox 360 release).",
           "# Do not edit: run the tool again for a new release of the game.", ""]
    for key, name in (("setjmp_address", "setjmp"), ("longjmp_address", "longjmp")):
        if name in by_name:
            out.append(f"{key} = 0x{by_name[name]:08X}  # {name}, from the map")
    out += ["", "[rexcrt]"]
    for name in REXCRT:
        if name in by_name:
            out.append(f"{name} = 0x{by_name[name]:08X}")
        else:
            out.append(f"# {name}: not in the map")

    out += ["", "[functions]",
            "# Entry points in the linker map that automatic discovery missed (reached only through pointers)."]
    if init_path:
        with open(init_path) as f:
            known = {int(x, 16) for x in re.findall(r"\{\s*0x([0-9A-Fa-f]{8}),", f.read())}
        existing = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'app', 'scud_map.toml')
        if os.path.isfile(existing):
            kept = {int(x, 16) for x in re.findall(r'^"0x([0-9A-F]{8})" = ', open(existing).read(), re.M)}
            known -= kept
        for addr in sorted(funcs):
            if addr in known:
                continue
            name, obj = funcs[addr]
            if name == "XapiFiberSwapContext":
                continue  # only reached from SwitchToFiber, which the SDK replaces
            out.append(f'"0x{addr:08X}" = {{ }}  # {name} ({obj})')
    print("\n".join(out))


if __name__ == "__main__":
    main()
