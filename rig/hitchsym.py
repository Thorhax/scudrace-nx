#!/usr/bin/env python3
# hitchsym.py <elf> <rex_profile.log> [block_from] [block_to]: the "during the hitches" sections of rex_profile.log
# (blocks numbered from 1), stack lines symbolized, threads that were waiting >= 95 % left out.
import bisect, os, pickle, re, subprocess, sys
elf = os.path.abspath(sys.argv[1]); cache = elf + '.syms'
if os.path.exists(cache) and os.path.getmtime(cache) > os.path.getmtime(elf):
    A, N = pickle.load(open(cache, 'rb'))
else:
    nm = subprocess.run(['docker', 'run', '--rm', '-v', f'{os.path.dirname(elf)}:{os.path.dirname(elf)}', 'devkitpro/devkita64',
                         '/opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm', '-n', '-C', '--defined-only', elf],
                        capture_output=True, text=True).stdout
    A, N = [], []
    for l in nm.splitlines():
        p = l.split(None, 2)
        if len(p) == 3 and p[1] in 'tTwW':
            A.append(int(p[0], 16)); N.append(p[2])
    pickle.dump((A, N), open(cache, 'wb'))
def fn(x):
    i = bisect.bisect_right(A, x) - 1
    return re.sub(r'\((?!anonymous namespace\)).*', '', N[i])[:60] if i >= 0 else '?'
lo = int(sys.argv[3]) if len(sys.argv) > 3 else 1; hi = int(sys.argv[4]) if len(sys.argv) > 4 else 10**9
block = 0; inh = False; skip = False
for l in open(sys.argv[2], errors='replace'):
    if l.startswith('==== '): block += 1; inh = False; continue
    if l.startswith('== during the hitches'):
        inh = lo <= block <= hi
        if inh: print(f'\n#### block {block}: {l.strip()}')
        continue
    if not inh: continue
    if l.startswith('-- thread') and 'samples during' not in l: inh = False; continue
    m = re.match(r'-- thread "([^"]*)": (\d+) samples during the hitches, (\d+)% waiting', l)
    if m: skip = int(m.group(3)) >= 95; (not skip) and print(l.rstrip()); continue
    if skip: continue
    s = re.sub(r'image\+0x([0-9a-f]+)', lambda m: fn(int(m.group(1), 16)), l.rstrip())
    print(s[:330])
