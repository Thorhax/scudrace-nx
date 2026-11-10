#!/usr/bin/env python3
"""Finds the functions that build an address with lis/addi (or lis/ori) in the generated code, by address or by a
string in the image.

  python3 tools/scud_xref.py <0xADDRESS | "string">

Names come from assets/game_root/default.map; the image is out/default_image.bin (tools/xex_image.py)."""
import bisect
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
B = 0x82000000


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    image = open(os.path.join(ROOT, 'out', 'default_image.bin'), 'rb').read()
    key = sys.argv[1]
    targets = set()
    if re.fullmatch(r'0x[0-9A-Fa-f]{8}', key):
        targets.add(int(key, 16))
    else:
        at = -1
        needle = key.encode('latin-1') + b'\0'
        while (at := image.find(needle, at + 1)) >= 0:
            if image[at - 1] == 0:
                targets.add(B + at)
    if not targets:
        sys.exit('not found')
    print('targets:', ', '.join(f'{t:08X}' for t in sorted(targets)))
    syms = {}
    for line in open(os.path.join(ROOT, 'assets', 'game_root', 'default.map'), encoding='latin-1'):
        m = re.match(r'^\s*0003:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+f\s', line)
        if m:
            syms.setdefault(int(m.group(2), 16), m.group(1))
    starts = sorted(syms)
    for path in sorted(glob.glob(os.path.join(ROOT, 'app', 'generated', 'default', '*recomp*.cpp'))):
        fn, lis = 0, {}
        for line in open(path, errors='replace'):
            m = re.match(r'DEFINE_REX_FUNC\(sub_([0-9A-F]+)\)', line)
            if m:
                fn, lis = int(m.group(1), 16), {}
                continue
            m = re.match(r'\s*// lis (r\d+),(-?\d+)', line)
            if m:
                lis[m.group(1)] = int(m.group(2))
                continue
            m = re.match(r'\s*// (addi|ori) (r\d+),(r\d+),(-?\d+)', line)
            if m and m.group(3) in lis:
                hi = (lis[m.group(3)] & 0xFFFF) << 16
                v = (hi + int(m.group(4))) & 0xFFFFFFFF if m.group(1) == 'addi' else hi | int(m.group(4))
                if v in targets:
                    i = bisect.bisect_right(starts, fn) - 1
                    print(f'{v:08X} in sub_{fn:08X} {syms.get(starts[i], "?") if i >= 0 else "?"}')


if __name__ == '__main__':
    main()
