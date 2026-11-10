/* lzx_unpack <in.lzx> <out.bin> <window_size> <out_size>: decompresses the de-chained LZX stream of an XEX2 with
 * normal compression (tools/xex_image.py writes the stream). Built against the SDK's libmspack (lzxd.c, system.c):
 *   cc -O2 -I sdk/thirdparty/libmspack/libmspack/mspack tools/lzx/lzx_unpack.c \
 *      sdk/thirdparty/libmspack/libmspack/mspack/{lzxd,system}.c -o out/tools/lzx_unpack */
#include <stdio.h>
#include <stdlib.h>
#include "mspack.h"
#include "system.h"
#include "lzx.h"

int main(int argc, char** argv) {
  if (argc != 5) { fprintf(stderr, "usage: lzx_unpack in out window_size out_size\n"); return 2; }
  unsigned long window = strtoul(argv[3], 0, 0), out_size = strtoul(argv[4], 0, 0);
  int bits = 0;
  while ((1ul << bits) < window) bits++;
  struct mspack_system* sys = mspack_default_system;
  struct mspack_file* in = sys->open(sys, argv[1], MSPACK_SYS_OPEN_READ);
  struct mspack_file* out = sys->open(sys, argv[2], MSPACK_SYS_OPEN_WRITE);
  if (!in || !out) { fprintf(stderr, "cannot open files\n"); return 1; }
  struct lzxd_stream* s = lzxd_init(sys, in, out, bits, 0, 0x8000, (off_t)out_size, 0);
  if (!s) { fprintf(stderr, "lzxd_init failed\n"); return 1; }
  int r = lzxd_decompress(s, (off_t)out_size);
  lzxd_free(s);
  sys->close(in);
  sys->close(out);
  if (r) fprintf(stderr, "lzxd_decompress error %d\n", r);
  return r;
}
