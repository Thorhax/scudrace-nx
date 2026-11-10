// Shader containers for Renut's shader library (cvar scud_dump_shaders): Scud Race compiles its HLSL at run time
// (D3DXCompileShader) and creates its shaders through Direct3D's own D3DDevice_CreateVertexShader (0x8237D240) /
// D3DDevice_CreatePixelShader (0x8237D058), r3 = the container (magic 0x102A1100 | vertex, +4 header bytes, +8
// microcode bytes). Each distinct container goes to shaders/ next to the executable as vs_<xxh3>.bin / ps_<xxh3>.bin,
// the layout tools/linux/deko3d_chain.sh and XenosRecomp read (as mk9-nx's mk9_dump_shaders).

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_set>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>

#define XXH_INLINE_ALL
#include "xxhash.h"

REXCVAR_DEFINE_BOOL(scud_dump_shaders, false, "Scud",
                    "Write every shader container the game creates to shaders/ next to the executable "
                    "(vs_<hash>.bin / ps_<hash>.bin), for Renut's shader library");

namespace {

uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

std::mutex g_mutex;
std::unordered_set<uint64_t> g_dumped;

void Dump(const uint8_t* base, uint32_t blob_address) {
  if (!REXCVAR_GET(scud_dump_shaders) || !blob_address) return;
  const uint32_t magic = Load32(base, blob_address);
  if ((magic & 0xFFFFFFFEu) != 0x102A1100u) {
    REXLOG_WARN("[shaders] container at {:08X} has magic {:08X}, skipped", blob_address, magic);
    return;
  }
  const bool vertex = magic & 1;
  const uint32_t header = Load32(base, blob_address + 4), ucode = Load32(base, blob_address + 8);
  if (header < 12 || header > (1u << 20) || !ucode || ucode > (1u << 20)) {
    REXLOG_WARN("[shaders] implausible {} container at {:08X} (header {}, microcode {}), skipped", vertex ? "VS" : "PS",
                blob_address, header, ucode);
    return;
  }
  const uint8_t* blob = base + blob_address;
  const size_t size = size_t(header) + ucode;
  const uint64_t hash = XXH3_64bits(blob, size);
  {
    std::lock_guard lock(g_mutex);
    if (!g_dumped.insert(hash).second) return;
  }
  const auto folder = rex::filesystem::GetExecutableFolder() / "shaders";
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  std::ofstream(folder / fmt::format("{}_{:016x}.bin", vertex ? "vs" : "ps", hash), std::ios::binary)
      .write(reinterpret_cast<const char*>(blob), std::streamsize(size));
  REXLOG_INFO("[shaders] {} {:016x} ({} bytes)", vertex ? "VS" : "PS", hash, size);
}

}  // namespace

extern "C" REX_FUNC(__imp__sub_8237D240);
REX_HOOK_RAW(sub_8237D240) {  // D3DDevice_CreateVertexShader(const DWORD *pFunction)
  Dump(base, ctx.r3.u32);
  __imp__sub_8237D240(ctx, base);
}

extern "C" REX_FUNC(__imp__sub_8237D058);
REX_HOOK_RAW(sub_8237D058) {  // D3DDevice_CreatePixelShader(const DWORD *pFunction)
  Dump(base, ctx.r3.u32);
  __imp__sub_8237D058(ctx, base);
}
