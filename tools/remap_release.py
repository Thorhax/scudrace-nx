#!/usr/bin/env python3
"""remap_release.py: carry guest addresses from one Scud Race release to the next, by the linker maps.

Each address is placed in the map symbol that contains it (symbol name + object, publics and statics), then moved to
the same symbol + offset in the new map. Code is checked against both images: the containing function's
instructions, with the fields a relink changes masked (branch targets, lis/addi/load/store displacements), must
match, or the entry is reported CHANGED. Data symbols are checked by size (the distance to the next symbol).

  tools/remap_release.py OLD.map NEW.map OLD_image.bin NEW_image.bin --addr 82125CF0 ...
  tools/remap_release.py ... --rewrite FILE...      rewrite 0x82xxxxxx in place (only OK entries), report the rest
Images come from codegen with REXGLUE_DUMP_IMAGE=<file> (base 0x82000000).
"""
import argparse
import bisect
import re
import sys

BASE = 0x82000000
LINE = re.compile(r'^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{8})\s+(f\s+)?(\S.*)?$')


def load_map(path):
    syms = []  # (addr, key, is_func)
    for line in open(path, errors='replace'):
        m = LINE.match(line)
        if not m or m.group(1) == '0000':
            continue
        addr = int(m.group(4), 16)
        if addr < BASE:
            continue
        obj = (m.group(6) or '').strip()
        syms.append((addr, (m.group(3), obj), bool(m.group(5))))
    syms.sort()
    # keep one symbol per address (the first listed), index by key
    out, seen = [], set()
    for s in syms:
        if out and out[-1][0] == s[0]:
            continue
        out.append(s)
    by_key = {}
    dup = set()
    for s in out:
        if s[1] in by_key:
            dup.add(s[1])
        by_key[s[1]] = s
    for k in dup:  # ambiguous names (same static in one object twice): unusable
        del by_key[k]
    return out, [s[0] for s in out], by_key


def size_of(syms, i):
    return syms[i + 1][0] - syms[i][0] if i + 1 < len(syms) else 0


def mask(word):
    op = word >> 26
    if op == 18:  # b/bl
        return word & 0xFC000003
    if op == 16:  # bc: target relative, stays the same within a function, but mask anyway for calls across
        return word & 0xFFFF0003
    if op in (14, 15, 24, 25) or 32 <= op <= 55 or op in (58, 62):  # addi/addis/ori/oris, D-form loads/stores
        return word & 0xFFFF0000
    return word


def words(img, addr, n):
    o = addr - BASE
    b = img[o:o + 4 * n]
    return [int.from_bytes(b[i:i + 4], 'big') for i in range(0, len(b), 4)]


class Remap:
    def __init__(self, a):
        self.old, self.oa, _ = load_map(a.old_map)
        self.new, _, self.nk = load_map(a.new_map)
        self.oi = open(a.old_image, 'rb').read()
        self.ni = open(a.new_image, 'rb').read()
        self.na = [s[0] for s in self.new]
        self.nidx = {s[0]: i for i, s in enumerate(self.new)}

    def __call__(self, x):
        i = bisect.bisect_right(self.oa, x) - 1
        if i < 0:
            return None, 'MISSING', 'before the first symbol'
        addr, key, func = self.old[i]
        off = x - addr
        osize = size_of(self.old, i)
        if key not in self.nk:
            return None, 'MISSING', f'{key[0]} ({key[1]}) +{off:#x} not in the new map'
        naddr = self.nk[key][0]
        nsize = size_of(self.new, self.nidx[naddr])
        name = f'{key[0][:50]}+{off:#x}'
        if func:
            n = osize // 4
            if osize != nsize:
                return naddr + off, 'CHANGED', f'{name}: size {osize:#x} -> {nsize:#x}'
            a, b = words(self.oi, addr, n), words(self.ni, naddr, n)
            diff = sum(1 for p, q in zip(a, b) if mask(p) != mask(q))
            if diff:
                return naddr + off, 'CHANGED', f'{name}: {diff}/{n} instructions differ'
            return naddr + off, 'OK', name
        return self.data(x, name)

    # Data: the map has no symbols for most statics, so a data address is carried by the code that uses it. Every
    # lis/addi/load/store in a function whose effective address is within 0x40 bytes below x votes for
    # (the same instruction in the new release's function) + the same distance.
    def refs(self, img, syms, i):
        addr = syms[i][0]
        n = size_of(syms, i) // 4
        ws = words(img, addr, n)
        hi, out = {}, []
        for k, w in enumerate(ws):
            op, rd, ra, imm = w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF
            simm = imm - 0x10000 if imm & 0x8000 else imm
            if op == 15 and ra == 0:  # lis
                hi[rd] = (imm << 16) & 0xFFFFFFFF
                continue
            if op == 14 and ra in hi:  # addi
                ea = (hi[ra] + simm) & 0xFFFFFFFF
                out.append((k, ea))
                hi[rd] = ea
                continue
            if (32 <= op <= 55 or op in (58, 62)) and ra in hi:
                d = simm & ~3 if op in (58, 62) else simm
                out.append((k, (hi[ra] + d) & 0xFFFFFFFF))
                if op < 36 or 40 <= op <= 43 or op in (46, 58):  # loads write rd
                    hi.pop(rd, None)
                continue
            if op in (18, 19):  # branches: keep (straight-line approximation)
                continue
            hi.pop(rd, None)
        return ws, out

    def build_refs(self):
        import difflib
        self.vote = {}  # old EA -> list of new EA
        nk = self.nk
        for i, (addr, key, func) in enumerate(self.old):
            if not func or key not in nk:
                continue
            j = self.nidx[nk[key][0]]
            ow, orefs = self.refs(self.oi, self.old, i)
            if not orefs:
                continue
            nw, nrefs = self.refs(self.ni, self.new, j)
            nmap = dict(nrefs)
            if [mask(w) for w in ow] == [mask(w) for w in nw]:
                pos = {k: k for k, _ in orefs}
            else:
                sm = difflib.SequenceMatcher(None, [mask(w) for w in ow], [mask(w) for w in nw], autojunk=False)
                pos = {}
                for a0, b0, size in sm.get_matching_blocks():
                    for t in range(size):
                        pos[a0 + t] = b0 + t
            for k, ea in orefs:
                if k in pos and pos[k] in nmap:
                    self.vote.setdefault(ea, []).append(nmap[pos[k]])
        self.vkeys = sorted(self.vote)

    def data(self, x, name):
        if not hasattr(self, 'vote'):
            self.build_refs()
        from collections import Counter
        c = Counter()
        exact = 0
        i = bisect.bisect_right(self.vkeys, x) - 1
        if i >= 0 and x - self.vkeys[i] <= 0x40:  # the nearest referenced address at or below x only
            ea = self.vkeys[i]
            for n in self.vote[ea]:
                c[n + (x - ea)] += 1
            if ea == x:
                exact = len(self.vote[ea])
        if not c:
            return None, 'MISSING', f'{name}: no code reference near it'
        best, cnt = c.most_common(1)[0]
        tot = sum(c.values())
        why = f'data {name}: {cnt}/{tot} references agree' + (f', {exact} exact' if exact else ', none exact')
        return best, ('OK' if cnt * 10 >= tot * 8 and (exact or cnt >= 3) else 'CHANGED'), why


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('old_map'); ap.add_argument('new_map'); ap.add_argument('old_image'); ap.add_argument('new_image')
    ap.add_argument('--addr', nargs='*', default=[])
    ap.add_argument('--rewrite', nargs='*', default=[])
    ap.add_argument('--dry', action='store_true')
    ap.add_argument('--only', help='file of addresses: rewrite only these (others are left as they are)')
    ap.add_argument('--force', nargs='*', default=[], help='OLD=NEW pairs checked by hand')
    a = ap.parse_args()
    r = Remap(a)
    only = {int(l.split()[0], 16) for l in open(a.only) if l.strip()} if a.only else None
    forced = {int(p.split('=')[0], 16): int(p.split('=')[1], 16) for p in a.force}
    for s in a.addr:
        x = int(s, 16)
        n, st, why = r(x)
        print(f'{x:08X} -> {n or 0:08X} {st} {why}')
    pat = re.compile(r'(?<![0-9A-Za-z])(0x)?(8[23][0-9A-Fa-f]{6})(?![0-9A-Za-z])')
    bad = 0
    for path in a.rewrite:
        text = open(path).read()
        cache = {}

        def sub(m):
            nonlocal bad
            x = int(m.group(2), 16)
            if x not in cache:
                if x in forced:
                    cache[x] = (forced[x], 'OK', 'by hand')
                elif only is not None and x not in only:
                    cache[x] = (x, 'OK', 'not listed')
                else:
                    cache[x] = r(x)
            n, st, why = cache[x]
            if st != 'OK':
                return m.group(0)
            up = not any(c in 'abcdef' for c in m.group(2))
            return (m.group(1) or '') + (f'{n:08X}' if up else f'{n:08x}')

        new = pat.sub(sub, text)
        for x, (n, st, why) in sorted(cache.items()):
            if st != 'OK':
                bad += 1
                print(f'{path}: {x:08X} {st} {why}' + (f' (would be {n:08X})' if n else ''))
        moved = sum(1 for x, (n, st, _) in cache.items() if st == 'OK' and n != x)
        print(f'{path}: {len(cache)} addresses, {moved} moved, {sum(1 for v in cache.values() if v[1] != "OK")} left')
        if not a.dry and new != text:
            open(path, 'w').write(new)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
