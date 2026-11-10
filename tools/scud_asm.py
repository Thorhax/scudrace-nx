#!/usr/bin/env python3
"""Prints one function's PowerPC listing from the generated code, with call targets named from default.map.

  python3 tools/scud_asm.py <address | map name substring> [--max N]

The listing is the `// insn` comments the code generator writes above each translated instruction, so it is exactly
what was recompiled. Names come from assets/game_root/default.map (the first symbol at each address)."""
import glob
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, 'app', 'generated', 'default')
MAP = os.path.join(ROOT, 'assets', 'game_root', 'default.map')


def load_map():
    by_addr, by_name = {}, {}
    rx = re.compile(r'^\s*0003:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+f\s')
    for line in open(MAP, encoding='latin-1'):
        m = rx.match(line)
        if m:
            a = int(m.group(2), 16)
            by_addr.setdefault(a, m.group(1))
            by_name.setdefault(m.group(1), a)
    return by_addr, by_name


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    limit = int(sys.argv[sys.argv.index('--max') + 1]) if '--max' in sys.argv else 400
    by_addr, by_name = load_map()
    key = sys.argv[1]
    if re.fullmatch(r'(0x)?[0-9A-Fa-f]{8}', key):
        addr = int(key, 16)
    else:
        hits = [(n, a) for n, a in by_name.items() if key in n]
        if len(hits) != 1:
            for n, a in sorted(hits, key=lambda h: h[1])[:30]:
                print(f'{a:08X} {n}')
            sys.exit(f'{len(hits)} matches for {key!r}' if hits else f'no match for {key!r}')
        addr = hits[0][1]
    name = f'sub_{addr:08X}'
    for path in glob.glob(os.path.join(GEN, '*recomp*.cpp')):
        text = open(path, errors='replace').read()
        i = text.find(f'DEFINE_REX_FUNC({name})')
        if i < 0:
            continue
        body = text[i:text.find('\n}\n', i)]
        print(f'== {name} {by_addr.get(addr, "")}')
        n = 0
        for line in body.split('\n'):
            s = line.strip()
            if s.startswith('loc_'):
                print(s)
            elif s.startswith('// '):
                insn = s[3:]
                m = re.match(r'b(l)?\s+0x([0-9a-f]{8})$', insn)
                if m and int(m.group(2), 16) in by_addr:
                    insn += '    ; ' + by_addr[int(m.group(2), 16)]
                print('   ', insn)
                n += 1
                if n >= limit:
                    print('    ...')
                    return
        return
    sys.exit(f'{name} not found in the generated code')


if __name__ == '__main__':
    main()
