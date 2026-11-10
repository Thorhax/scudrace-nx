#!/usr/bin/env python3
"""Writes the loaded image of a retail XEX2 (AES-encrypted, basic or no compression) for the analysis tools, without
running the game (usf4-nx made out/default_image.bin with a run of the game and USF4_DUMP_IMAGE).

  python3 tools/xex_image.py assets/game_root/default.xex out/default_image.bin

Needs the `cryptography` package. LZX-compressed (normal compression) executables are not handled.
"""
import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

RETAIL_KEY = bytes.fromhex('20B185A59D28FDC340583FBB0896BF91')
DEVKIT_KEY = bytes(16)


def aes_ecb_decrypt(key, data):
    d = Cipher(algorithms.AES(key), modes.ECB()).decryptor()
    return d.update(data) + d.finalize()


def aes_cbc_decrypt(key, data):
    d = Cipher(algorithms.AES(key), modes.CBC(bytes(16))).decryptor()
    return d.update(data) + d.finalize()


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    xex = open(sys.argv[1], 'rb').read()
    if xex[:4] != b'XEX2':
        sys.exit('not an XEX2 file')
    _flags, pe_off, _res, sec_off, opt_count = struct.unpack_from('>5I', xex, 4)
    opts = {}
    for i in range(opt_count):
        key, value = struct.unpack_from('>II', xex, 24 + i * 8)
        opts[key] = value
    image_size = struct.unpack_from('>I', xex, sec_off + 4)[0]
    load_address = struct.unpack_from('>I', xex, sec_off + 0x110)[0]
    file_key = xex[sec_off + 0x150:sec_off + 0x160]

    fmt_off = opts.get(0x3FF)
    if fmt_off is None:
        sys.exit('no file format header (0x3FF)')
    info_size, encryption, compression = struct.unpack_from('>IHH', xex, fmt_off)
    payload = xex[pe_off:]
    raw_payload = payload
    if encryption == 1:
        payload = payload[:len(payload) & ~15]
        session = aes_ecb_decrypt(RETAIL_KEY, file_key)
        plain = aes_cbc_decrypt(session, payload)
        if compression == 1 and plain[:2] != b'MZ':
            session = aes_ecb_decrypt(DEVKIT_KEY, file_key)
            plain = aes_cbc_decrypt(session, payload)
        payload = plain

    if compression == 1:
        out = bytearray()
        pos = 0
        for i in range((info_size - 8) // 8):
            data_size, zero_size = struct.unpack_from('>II', xex, fmt_off + 8 + i * 8)
            out += payload[pos:pos + data_size]
            out += bytes(zero_size)
            pos += data_size
    elif compression == 0:
        out = bytearray(payload)
    elif compression == 2:
        # Normal (LZX) compression: a chain of blocks, each starting with the next block's size and SHA-1, then
        # chunks (u16 size + data) up to a zero size. The de-chained stream goes to out/tools/lzx_unpack
        # (tools/lzx/lzx_unpack.c, the SDK's libmspack), as in the SDK's XexModule::ReadImageCompressed.
        import os
        import subprocess
        import tempfile
        window_size, first_size = struct.unpack_from('>II', xex, fmt_off + 8)

        def dechain(data):
            stream = bytearray()
            pos, block_size = 0, first_size
            while block_size:
                block = data[pos:pos + block_size]
                if len(block) != block_size:
                    return None
                next_size = struct.unpack_from('>I', block, 0)[0]
                p = 24
                while True:
                    if p + 2 > len(block):
                        return None
                    chunk = (block[p] << 8) | block[p + 1]
                    p += 2
                    if not chunk:
                        break
                    stream += block[p:p + chunk]
                    p += chunk
                pos += block_size
                block_size = next_size
            return stream

        stream = dechain(payload)
        if stream is None and encryption == 1:  # not the retail key: try the devkit key
            enc = raw_payload[:len(raw_payload) & ~15]
            stream = dechain(aes_cbc_decrypt(aes_ecb_decrypt(DEVKIT_KEY, file_key), enc))
        if stream is None:
            sys.exit('LZX block chain unreadable (wrong key?)')
        tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'out', 'tools', 'lzx_unpack')
        with tempfile.TemporaryDirectory() as tmp:
            src, dst = os.path.join(tmp, 'in.lzx'), os.path.join(tmp, 'out.bin')
            open(src, 'wb').write(stream)
            subprocess.run([tool, src, dst, str(window_size), str(image_size)], check=True)
            out = bytearray(open(dst, 'rb').read())
    else:
        sys.exit(f'compression type {compression} not handled')
    if out[:2] != b'MZ':
        sys.exit('decryption failed: no MZ header')
    out = bytes(out[:image_size]).ljust(image_size, b'\0')
    open(sys.argv[2], 'wb').write(out)
    print(f'{sys.argv[2]}: load address 0x{load_address:08X}, {len(out):,} bytes')


if __name__ == '__main__':
    main()
