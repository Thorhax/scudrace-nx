#!/usr/bin/env python3
"""Retargets Renut's sources (app/src/renut_engine, taken from mk9-nx) from MK9's Direct3D to Scud Race's.

Scud Race links XDK 2.0.21256, whose CDevice layout is SA2's (docs/scud-renut-census.md, table 2): every device offset
mk9-nx wrote with an "[SA2 x]" annotation goes back to x. Function and data addresses come from the census (table 1),
each from Scud's linker map by name and checked against its code. Hooks with no Scud counterpart (the linker dropped
functions the game never calls) are compiled out.

  python3 tools/scud_renut_retarget.py [--check]

Idempotent: a second run changes nothing. --check only reports what would change.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RENUT = os.path.join(ROOT, 'app', 'src', 'renut_engine')

# MK9 function -> Scud function (docs/scud-renut-census.md, table 1).
FUNCS = {
    '82BF6108': '823844E8',  # Direct3D_CreateDevice
    '82C014C8': '82394428',  # D3D::InitializePresentationParameters
    '82C01260': '82393A58',  # D3D::InitializeApiState
    '82BF5F90': '82384370',  # D3D::InitializeAsyncResources (MK9's query block setup)
    '82BF57A8': '82383EB8',  # D3D::InitializeAsyncCommandBuffers
    '82BEB450': '8237CF28',  # D3D::InitializeGprAllocation
    '82BFD090': '8238BFC0',  # D3DDevice_InsertCallback
    '82BF6928': '82378B80',  # D3D::SynchronizeToPresentationInterval (the internal one, not the public wrapper)
    '82BFCD60': '8238BC98',  # CDevice::KickOff
    '82BFCBD8': '8238B958',  # CDevice::KickOffSegment
    '82BFCFA0': '8238BED0',  # CDevice::BeginRingBig
    '82BFB948': '8238A688',  # D3D::RingBufferDeviceAllocate
    '82BFBB78': '8238AAD0',  # CDevice::BeginRingAlloc (MK9's "AllocateInlineData")
    '82BFC0B0': '8238ABB8',  # CDevice::StartNewSegment (MK9's ring segment allocator)
    '82BFD0E8': '8238C018',  # D3DDevice_SetRingBufferParameters
    '82BF6D58': '82379040',  # D3DDevice_Swap
    '82BFBC60': '8238A8B8',  # CDevice::BlockOnFence
    '82BF5680': '82383D88',  # CBlocker::Check
    '82BFCEA0': '8238BDD8',  # D3DDevice_IsFencePending
    '82BF09E0': '82381E18',  # D3DDevice_DrawVertices
    '82BF0DF8': '82382230',  # D3DDevice_DrawIndexedVertices
    '82BF03B8': '82381DD0',  # D3DDevice_DrawVerticesUP
    '82BEFEF8': '82381910',  # D3DDevice_BeginVertices
    '82BEF680': '82381708',  # D3DDevice_ClearF (public; MK9 hooked its internal ClearF of the same signature)
    '82BFB4C8': '8238A1C0',  # D3DDevice_Resolve (frame 160, stack args at 252/260 as MK9)
    '82BE5318': '823833F0',  # D3D::DestroyResource
    '82BEB740': '8237D148',  # XGRegisterVertexShader
    '82BE9B98': '8237B5B0',  # XGRegisterPixelShader
    '82BEA068': '8237BB10',  # D3DDevice_CreateVertexDeclaration
    '823667E0': '823729B0',  # XMemFree (xapilib; MK9's game XMemFree)
}
# Hooks of functions Scud does not link (census: not in the map): compiled out.
DROPPED = {'82BF0940': 'D3DDevice_DrawIndexedVerticesUP', '82BFF878': 'D3DVertexShader_Bind'}
# MK9 (or N&B, never changed by mk9-nx) data addresses -> Scud's (census table 1).
DATA = {
    '0x8339BF90': '0x825E4180',  # D3DVERSION (copied to device +11008)
    '0x82000658': '0x820005A0',  # __imp_VdGlobalDevice
    '0x82F597B4': '0x83662578',  # __Fusion
    '0x820018DC': '0x8209E7B0',  # g_DefaultViewport
    '0x82F39E34': '0x83662558',  # XPhysicalAlloc's no-large-pages word
    '0x820052F8': '0x8209E190',  # g_FormatTable
    '0x82FA36C8': '0x8366255C',  # g_HiZAllocated
    '0x82E506F0': '0x825C3A20',  # g_RenderStateInit
    '0x82E50BD0': '0x825C3EE0',  # g_SamplerStateInit
    '0x82FAD204': '0x834C8B5C',  # D3D__NoExtraVertexInBeginExport
    '0x82FAD208': '0x834C8B60',  # D3D__NoExtraVerticesInEndExport
}

FILES = []
for d, _, names in os.walk(RENUT):
    FILES += [os.path.join(d, n) for n in names if n.endswith(('.cpp', '.h'))]


def retarget(text):
    out = []
    for line in text.split('\n'):
        # Device offsets: "constexpr ... = <mk9>;  // ... [SA2 <sa2>" -> <sa2>.
        m = re.match(r'^(\s*constexpr\s+\S+\s+\w+(?:\[\])?\s*=\s*)(\{[^}]*\}|0x[0-9A-Fa-f]+|\d+)(;.*\[SA2 )([^\];]+)(.*)$', line)
        if m and 'mk9-nx' in line and not re.search(r'(Va|Slot)\s*=', line):  # offsets only, not addresses
            sa2 = m.group(4).strip()
            new = '{' + sa2 + '}' if m.group(2).startswith('{') else sa2
            if m.group(2) != new:
                line = f'{m.group(1)}{new}{m.group(3)}{m.group(4)}{m.group(5)}'.replace('mk9-nx:', 'scud (= SA2; mk9-nx was', 1)
                if 'scud (= SA2; mk9-nx was' in line:
                    line = line.replace('scud (= SA2; mk9-nx was', f'scud = SA2, mk9-nx {m.group(2)}:', 1)
        for mk9, scud in FUNCS.items():
            line = re.sub(rf'sub_{mk9}\b', f'sub_{scud}', line)
        for mk9, scud in DATA.items():
            line = re.sub(rf'\b{mk9}\b', scud, line, flags=re.I)
        out.append(line)
    return '\n'.join(out)


def drop_hooks(text):
    for mk9, name in DROPPED.items():
        # A hook macro line and, for a RAW hook, its body: wrap from the macro to the closing brace at column 0.
        pattern = re.compile(rf'^(BANJO_RENUT_HOOK(?:_RAW|_SHARED)?\(sub_{mk9}\b.*?)$', re.M)
        m = pattern.search(text)
        if not m or f'scud: {name}' in text:
            continue
        start = m.start()
        if '{' in m.group(1):
            end = text.index('\n}\n', start) + 3
        else:
            end = m.end() + 1
        block = text[start:end]
        text = (text[:start] + f'#if 0  // scud: {name} is not in Scud Race (the linker dropped it: never called)\n'
                + block + '#endif\n' + text[end:])
    return text


def main():
    check = '--check' in sys.argv
    changed = 0
    for path in sorted(FILES):
        old = open(path, encoding='utf-8').read()
        new = retarget(drop_hooks(old))
        if new != old:
            changed += 1
            print(('would change ' if check else 'changed ') + os.path.relpath(path, ROOT))
            if not check:
                open(path, 'w', encoding='utf-8', newline='\n').write(new)
    left = []
    for path in FILES:
        for i, line in enumerate(open(path, encoding='utf-8'), 1):
            if re.search(r'sub_82[BC][0-9A-F]{5}\b', line) and not line.lstrip().startswith('//'):
                left.append(f'{os.path.relpath(path, ROOT)}:{i}: {line.strip()[:120]}')
    print(f'{changed} files {"to change" if check else "changed"}; MK9 0x82B/0x82C addresses left in code: {len(left)}')
    for l in left:
        print('  ' + l)


if __name__ == '__main__':
    main()
