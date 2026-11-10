// Faster boot (2026-11-10). Measured on the console with scud_boot_timing (scud_boot_timing.cpp), 0.0.17 at 1 GHz:
// from the start to the title ~16 s, of which 7.6 s in romset_load_zip (scud.zip, 41 ROMs, 60 MB packed into 98 MB:
// the game's own miniz, translated from PowerPC, inflates at ~12 MB/s and reads the SD 64 KB at a time).
//
// scud_native_unzip: the game's mz_zip_reader_extract_to_mem_no_alloc1 (0x8236F990; mz_zip_reader_extract_to_heap
// and the romset loaders end in it) done on the host. The zip is the one the game opened with mz_zip_reader_init_file
// (0x8236F800, hooked to note its path; mz_zip_reader_end 0x8236E700 forgets it). Its central directory is read here
// once; miniz numbers the files in central directory order, so the game's file index is the same entry. The entry's
// compressed bytes come in one read, zlib inflates them straight into the game's buffer and its CRC-32 is checked.
// Anything else (stored-only flags, a size that does not fit, an unknown method, a CRC mismatch, a zip this code
// cannot parse) goes to the game's own code, so the result is the same either way.

#include <algorithm>
#include <cstdint>
#include <strings.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <zlib.h>
#include <zstd.h>

#include <fcntl.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <condition_variable>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_BOOL(scud_native_unzip, true, "Scud",
                    "Unpack the game's ROM zips with the host's zlib (much faster boot); off: the game's own code");

namespace scud::fast_boot {

std::filesystem::path g_game_root;  // set by ScudApp (game_data_root)

namespace {

struct Entry {
  uint16_t method = 0;
  uint16_t flags = 0;
  uint32_t crc = 0;
  uint64_t comp = 0;
  uint64_t uncomp = 0;
  uint64_t local = 0;  // the local header's offset in the file
  std::string name;
};

struct Zip {
  std::string path;
  std::vector<Entry> entries;
};

std::mutex g_lock;
std::unordered_map<uint32_t, std::shared_ptr<Zip>> g_zips;  // by the guest's mz_zip_archive
uint64_t g_native = 0, g_fallback = 0, g_bytes = 0;
std::atomic<uint64_t> g_read_us{0}, g_inflate_us{0}, g_read_bytes{0};

uint64_t NowUs() {
  return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()).count());
}

// pread in one go (no stdio buffer in between), retried until all of it or an error.
bool ReadAt(int fd, uint64_t at, void* dst, size_t n) {
  uint8_t* p = static_cast<uint8_t*>(dst);
  while (n) {
    const ssize_t r = pread(fd, p, n, off_t(at));
    if (r <= 0) return false;
    p += r;
    at += uint64_t(r);
    n -= size_t(r);
  }
  return true;
}

uint16_t Le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

// "game:\scud.zip" (or a bare relative name) -> the host file under game_root.
std::string HostPath(const char* guest) {
  std::string s = guest;
  const size_t colon = s.find(":\\");
  if (colon != std::string::npos) {
    if (s.compare(0, colon, "game") != 0) return {};
    s = s.substr(colon + 2);
  }
  for (char& c : s) if (c == '\\') c = '/';
  if (g_game_root.empty()) return {};
  return (g_game_root / s).string();
}

// The central directory (no zip64: these archives are well under 4 GB). Entries in directory order.
std::shared_ptr<Zip> Parse(const std::string& path) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return nullptr;
  std::shared_ptr<Zip> zip;
  do {
    if (std::fseek(f, 0, SEEK_END) != 0) break;
    const long size = std::ftell(f);
    if (size < 22) break;
    const long tail = std::min<long>(size, 65535 + 22);
    std::vector<uint8_t> end(size_t(tail), 0);
    if (std::fseek(f, size - tail, SEEK_SET) != 0 || std::fread(end.data(), 1, end.size(), f) != end.size()) break;
    long eocd = -1;
    for (long i = tail - 22; i >= 0; --i) {
      if (Le32(&end[size_t(i)]) == 0x06054b50) { eocd = i; break; }
    }
    if (eocd < 0) break;
    const uint8_t* e = &end[size_t(eocd)];
    const uint32_t count = Le16(e + 10), cd_size = Le32(e + 12), cd_ofs = Le32(e + 16);
    if (count == 0xFFFF || cd_ofs == 0xFFFFFFFFu) break;  // zip64: not here
    // Bytes before the archive proper (as miniz's m_file_archive_start_ofs).
    const long eocd_pos = size - tail + eocd;
    const long start = eocd_pos - long(cd_size) - long(cd_ofs);
    if (start < 0) break;
    std::vector<uint8_t> cd(cd_size);
    if (std::fseek(f, start + long(cd_ofs), SEEK_SET) != 0 || std::fread(cd.data(), 1, cd.size(), f) != cd.size()) break;
    auto z = std::make_shared<Zip>();
    z->path = path;
    size_t p = 0;
    bool ok = true;
    for (uint32_t i = 0; i < count; ++i) {
      if (p + 46 > cd.size() || Le32(&cd[p]) != 0x02014b50) { ok = false; break; }
      const uint8_t* h = &cd[p];
      Entry en;
      en.flags = Le16(h + 8);
      en.method = Le16(h + 10);
      en.crc = Le32(h + 16);
      en.comp = Le32(h + 20);
      en.uncomp = Le32(h + 24);
      en.local = uint64_t(start) + Le32(h + 42);
      if (p + 46 + Le16(h + 28) > cd.size()) { ok = false; break; }
      en.name.assign(reinterpret_cast<const char*>(h + 46), Le16(h + 28));
      z->entries.push_back(en);
      p += 46 + Le16(h + 28) + Le16(h + 30) + Le16(h + 32);
    }
    if (ok) zip = std::move(z);
  } while (false);
  std::fclose(f);
  return zip;
}

// The entry into dst (exactly uncomp bytes). False: let the game do it.
bool Extract(const Zip& zip, const Entry& en, uint8_t* dst) {
  if (en.flags & 1) return false;  // encrypted
  if (en.method != 0 && en.method != 8) return false;
  const int fd = open(zip.path.c_str(), O_RDONLY);
  if (fd < 0) return false;
  bool ok = false;
  do {
    uint8_t lh[30];
    const uint64_t t0 = NowUs();
    if (!ReadAt(fd, en.local, lh, 30) || Le32(lh) != 0x04034b50) break;
    const uint64_t data = en.local + 30 + Le16(lh + 26) + Le16(lh + 28);
    if (en.method == 0) {
      if (en.comp != en.uncomp || !ReadAt(fd, data, dst, size_t(en.uncomp))) break;
      g_read_us += NowUs() - t0;
      g_read_bytes += en.comp;
    } else {
      std::vector<uint8_t> packed(size_t(en.comp));
      if (!ReadAt(fd, data, packed.data(), packed.size())) break;
      const uint64_t t1 = NowUs();
      g_read_us += t1 - t0;
      g_read_bytes += en.comp;
      z_stream s{};
      if (inflateInit2(&s, -MAX_WBITS) != Z_OK) break;
      s.next_in = packed.data();
      s.avail_in = uInt(packed.size());
      s.next_out = dst;
      s.avail_out = uInt(en.uncomp);
      const int r = inflate(&s, Z_FINISH);
      const uint64_t out = s.total_out;
      inflateEnd(&s);
      g_inflate_us += NowUs() - t1;
      if (r != Z_STREAM_END || out != en.uncomp) break;
    }
    if (uint32_t(crc32(0L, dst, uInt(en.uncomp))) != en.crc) {
      REXLOG_WARN("[boot] native unzip: CRC mismatch in {} (the game's own code redoes it)", zip.path);
      break;
    }
    ok = true;
  } while (false);
  close(fd);
  return ok;
}

// The ROM set first: while romset_load_zip runs, the JOYPOLIS course build (another thread, started at the same time)
// waits in hz_stream, so the title's ROMs have the SD card and a core to themselves.
std::mutex g_romset_lock;
std::condition_variable g_romset_done;
bool g_romset_running = false;

}  // namespace

}  // namespace scud::fast_boot

using namespace scud::fast_boot;

// mz_bool mz_zip_reader_init_file(mz_zip_archive*, const char* path, mz_uint32 flags)
extern "C" REX_FUNC(__imp__sub_8236F800);
REX_HOOK_RAW(sub_8236F800) {
  const uint32_t zip = ctx.r3.u32;
  const uint32_t name = ctx.r4.u32;
  std::string guest_name = name ? reinterpret_cast<const char*>(base + name) : "";
  __imp__sub_8236F800(ctx, base);
  if (!REXCVAR_GET(scud_native_unzip) || ctx.r3.u32 == 0 || !zip) return;
  const std::string path = HostPath(guest_name.c_str());
  std::shared_ptr<Zip> parsed = path.empty() ? nullptr : Parse(path);
  std::lock_guard<std::mutex> lock(g_lock);
  if (parsed) {
    REXLOG_INFO("[boot] native unzip: {} ({} files) for the game's zip at 0x{:08X}", guest_name, parsed->entries.size(), zip);
    g_zips[zip] = std::move(parsed);
  } else {
    REXLOG_WARN("[boot] native unzip: {} not readable here ('{}'): the game's own code unpacks it", guest_name, path);
    g_zips.erase(zip);
  }
}

// mz_bool mz_zip_reader_end(mz_zip_archive*)
extern "C" REX_FUNC(__imp__sub_8236E700);
REX_HOOK_RAW(sub_8236E700) {
  {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_zips.erase(ctx.r3.u32) && (g_native || g_fallback)) {
      REXLOG_INFO("[boot] native unzip: {} entries native ({:.1f} MB), {} by the game; reading {:.1f} MB took {} ms, "
                  "inflating {} ms", g_native, double(g_bytes) / (1024.0 * 1024.0), g_fallback,
                  double(g_read_bytes.load()) / (1024.0 * 1024.0), g_read_us.load() / 1000, g_inflate_us.load() / 1000);
    }
  }
  __imp__sub_8236E700(ctx, base);
}

// mz_bool mz_zip_reader_extract_to_mem_no_alloc1(mz_zip_archive*, mz_uint file_index, void* buf, size_t buf_size,
//                                                mz_uint flags, void* user_read_buf, size_t user_read_buf_size,
//                                                const mz_zip_archive_file_stat* st)
extern "C" REX_FUNC(__imp__sub_8236F990);
REX_HOOK_RAW(sub_8236F990) {
  constexpr uint32_t kCompressedData = 0x0400;  // MZ_ZIP_FLAG_COMPRESSED_DATA
  if (REXCVAR_GET(scud_native_unzip) && !(ctx.r7.u32 & kCompressedData) && ctx.r5.u32) {
    std::shared_ptr<Zip> zip;
    {
      std::lock_guard<std::mutex> lock(g_lock);
      auto it = g_zips.find(ctx.r3.u32);
      if (it != g_zips.end()) zip = it->second;
    }
    const uint32_t index = ctx.r4.u32;
    if (zip && index < zip->entries.size()) {
      const Entry& en = zip->entries[index];
      if (en.uncomp <= ctx.r6.u32 && Extract(*zip, en, base + ctx.r5.u32)) {
        std::lock_guard<std::mutex> lock(g_lock);
        ++g_native;
        g_bytes += en.uncomp;
        ctx.r3.u64 = 1;
        return;
      }
    }
    std::lock_guard<std::mutex> lock(g_lock);
    ++g_fallback;
  }
  __imp__sub_8236F990(ctx, base);
}

// int romset_load_zip(const char* zip, romset_t*) (0x8224E470): marks the ROM set's load (see g_romset_running).
extern "C" REX_FUNC(__imp__sub_8224E470);
REX_HOOK_RAW(sub_8224E470) {
  {
    std::lock_guard<std::mutex> lock(g_romset_lock);
    g_romset_running = true;
  }
  const uint64_t t0 = NowUs();
  __imp__sub_8224E470(ctx, base);
  {
    std::lock_guard<std::mutex> lock(g_romset_lock);
    g_romset_running = false;
  }
  g_romset_done.notify_all();
  REXLOG_INFO("[boot] the ROM set loaded in {} ms (native unzip {})", (NowUs() - t0) / 1000,
              REXCVAR_GET(scud_native_unzip) ? "on" : "off");
}

// int hz_stream(mz_zip_archive* zip, const char* name, int (*cb)(void* user, const uint8_t* data, unsigned n), void* user)
// (0x8224EAA8, romset.obj): the hornet / JOYPOLIS course loader's way into dayto2pe.zip. The game's: miniz's
// extract iterator, 64 KB at a time to cb, stopping if cb says 0. Here: the entry inflated by zlib, then given to cb in
// the same 64 KB pieces, from a buffer of the game's malloc. Measured before: 72 calls, 25.6 s of the 27 s the
// JOYPOLIS course takes to build (in the background, while the menus run).
extern "C" REX_FUNC(__imp__sub_825B3A70);  // the game's malloc(size)
extern "C" REX_FUNC(__imp__sub_825B3A10);  // the game's free(p)
extern "C" REX_FUNC(__imp__sub_8224EAA8);
REX_HOOK_RAW(sub_8224EAA8) {
  const uint32_t zip_addr = ctx.r3.u32, name_addr = ctx.r4.u32, cb = ctx.r5.u32, user = ctx.r6.u32;
  if (REXCVAR_GET(scud_native_unzip)) {
    std::unique_lock<std::mutex> lock(g_romset_lock);
    g_romset_done.wait_for(lock, std::chrono::seconds(20), [] { return !g_romset_running; });
  }
  std::shared_ptr<Zip> zip;
  if (REXCVAR_GET(scud_native_unzip) && name_addr && cb) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_zips.find(zip_addr);
    if (it != g_zips.end()) zip = it->second;
  }
  const Entry* en = nullptr;
  if (zip) {
    const char* want = reinterpret_cast<const char*>(base + name_addr);
    for (const Entry& e : zip->entries) {
      if (e.name.size() == std::strlen(want) && strncasecmp(e.name.c_str(), want, e.name.size()) == 0) {
        en = &e;
        break;
      }
    }
  }
  std::vector<uint8_t> data;
  if (en && en->uncomp <= (256u << 20)) {
    data.resize(size_t(en->uncomp));
    if (!Extract(*zip, *en, data.data())) data.clear(), en = nullptr;
  } else {
    en = nullptr;
  }
  if (!en) {
    __imp__sub_8224EAA8(ctx, base);
    return;
  }
  constexpr uint32_t kPiece = 0x10000;
  ctx.r3.u64 = kPiece;
  __imp__sub_825B3A70(ctx, base);
  const uint32_t buf = ctx.r3.u32;
  if (!buf) {
    ctx.r3.u64 = 0;
    return;
  }
  PPCFunc* const call = rex::runtime::ResolveIndirectFunction(cb);
  uint32_t ok = 1;
  for (size_t at = 0; at < data.size(); at += kPiece) {
    const uint32_t n = uint32_t(std::min<size_t>(kPiece, data.size() - at));
    std::memcpy(base + buf, data.data() + at, n);
    ctx.r3.u64 = user;
    ctx.r4.u64 = buf;
    ctx.r5.u64 = n;
    call(ctx, base);
    if (ctx.r3.u32 == 0) {
      ok = 0;
      break;
    }
  }
  ctx.r3.u64 = buf;
  __imp__sub_825B3A10(ctx, base);
  {
    std::lock_guard<std::mutex> lock(g_lock);
    ++g_native;
    g_bytes += data.size();
  }
  ctx.r3.u64 = ok;
}

// ---------------------------------------------------------------------------------------------------------------------
// scud_image_cache: the front end's pictures, kept decoded on the SD (<game_root>/../cache/ui). Measured: the game's
// stb_image (translated) takes 1.8 s for the 8 PNGs of the menus, and its resample_area 1.1 s for two scaled copies.
//
// load_png (0x8223B170, ui_gfx.obj): uint32_t* load_png(const char* dir, const char* name, int* w, int* h): stb_image,
// then its own pixel order, in a buffer from the game's malloc (0x825B3A70; the caller frees it). The first time its
// result is saved (keyed by the PNG's size and time, and stb_image's vertical flip setting); later the bytes are put
// back in a buffer from the same malloc.
// resample_area (0x82238FB8): void resample_area(const uint32_t* src, int sw, int sh, uint32_t* dst, int dw, int dh),
// a pure function: its output is saved keyed by the source pixels' CRC-32 and Adler-32 and the sizes.
// Both give the game exactly the bytes its own code made; a missing or unreadable cache file just runs that code.

REXCVAR_DEFINE_BOOL(scud_image_cache, true, "Scud",
                    "Keep the menus' decoded pictures on the SD (faster boot after the first); off: decode every time");

namespace scud::fast_boot {
namespace {

std::filesystem::path CacheDir() {
  if (g_game_root.empty()) return {};
  return g_game_root.parent_path() / "cache" / "ui";
}

uint32_t Be32(const uint8_t* p) { return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(p)); }
void PutBe32(uint8_t* p, uint32_t v) { *reinterpret_cast<uint32_t*>(p) = __builtin_bswap32(v); }

constexpr uint32_t kMagic = 0x53434932;  // "SCI2": the pixels zstd-compressed (an 8 MB raw picture read in ~150 ms)

struct Header {
  uint32_t magic, key0, key1, key2, w, h;
};

bool ReadCache(const std::filesystem::path& file, const Header& want, std::vector<uint8_t>& pixels) {
  std::FILE* f = std::fopen(file.string().c_str(), "rb");
  if (!f) return false;
  Header h{};
  bool ok = std::fread(&h, sizeof(h), 1, f) == 1 && h.magic == want.magic && h.key0 == want.key0 &&
            h.key1 == want.key1 && h.key2 == want.key2 && (want.w == 0 || (h.w == want.w && h.h == want.h)) &&
            h.w && h.h && uint64_t(h.w) * h.h <= (64u << 20);
  if (ok) {
    std::vector<uint8_t> packed;
    if (std::fseek(f, 0, SEEK_END) == 0) {
      const long end = std::ftell(f);
      packed.resize(end > long(sizeof(h)) ? size_t(end) - sizeof(h) : 0);
      ok = !packed.empty() && std::fseek(f, long(sizeof(h)), SEEK_SET) == 0 &&
           std::fread(packed.data(), 1, packed.size(), f) == packed.size();
    } else {
      ok = false;
    }
    if (ok) {
      pixels.resize(size_t(h.w) * h.h * 4);
      const size_t r = ZSTD_decompress(pixels.data(), pixels.size(), packed.data(), packed.size());
      ok = !ZSTD_isError(r) && r == pixels.size();
    }
  }
  std::fclose(f);
  if (ok && want.w == 0) {
    const_cast<Header&>(want).w = h.w;
    const_cast<Header&>(want).h = h.h;
  }
  return ok;
}

void WriteCache(const std::filesystem::path& file, const Header& h, const uint8_t* pixels) {
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  const std::string tmp = file.string() + ".tmp";
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return;
  const size_t raw = size_t(h.w) * h.h * 4;
  std::vector<uint8_t> packed(ZSTD_compressBound(raw));
  const size_t n = ZSTD_compress(packed.data(), packed.size(), pixels, raw, 3);
  const bool ok = !ZSTD_isError(n) && std::fwrite(&h, sizeof(h), 1, f) == 1 &&
                  std::fwrite(packed.data(), 1, n, f) == n;
  std::fclose(f);
  if (ok) {
    // The Switch's file system does not replace an existing file on rename.
    std::filesystem::remove(file, ec);
    std::filesystem::rename(tmp, file, ec);
  }
  else std::filesystem::remove(tmp, ec);
}

std::string SafeName(std::string s) {
  for (char& c : s) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) c = '_';
  return s;
}

}  // namespace
}  // namespace scud::fast_boot

extern "C" REX_FUNC(__imp__sub_8223B170);
REX_HOOK_RAW(sub_8223B170) {
  const uint32_t dir = ctx.r3.u32, name = ctx.r4.u32, pw = ctx.r5.u32, ph = ctx.r6.u32;
  if (!REXCVAR_GET(scud_image_cache) || !dir || !name || !pw || !ph || g_game_root.empty()) {
    __imp__sub_8223B170(ctx, base);
    return;
  }
  const std::string guest = std::string(reinterpret_cast<const char*>(base + dir)) + "\\" +
                            reinterpret_cast<const char*>(base + name);
  const std::string host = HostPath(guest.c_str());
  std::error_code ec;
  const uint64_t size = host.empty() ? 0 : uint64_t(std::filesystem::file_size(host, ec));
  const auto when = ec ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(host, ec);
  // stb_image's vertical flip: thread-local if set there (the TLS block at *r13, +644 set, +648 value), else global.
  const uint8_t* tls = base + Be32(base + ctx.r13.u32);
  const uint32_t flip = Be32(tls + 644) ? Be32(tls + 648) : Be32(base + 0x8359E158);
  if (host.empty() || ec || size == 0) {
    __imp__sub_8223B170(ctx, base);
    return;
  }
  Header key{kMagic, uint32_t(size), uint32_t(when.time_since_epoch().count() ^ (when.time_since_epoch().count() >> 32)),
             flip ? 1u : 0u, 0, 0};
  const std::filesystem::path file = CacheDir() / (SafeName(guest) + ".px");
  std::vector<uint8_t> pixels;
  if (ReadCache(file, key, pixels)) {
    ctx.r3.u64 = pixels.size();
    __imp__sub_825B3A70(ctx, base);
    const uint32_t buf = ctx.r3.u32;
    if (buf) {
      std::memcpy(base + buf, pixels.data(), pixels.size());
      PutBe32(base + pw, key.w);
      PutBe32(base + ph, key.h);
      ctx.r3.u64 = buf;
      return;
    }
    ctx.r3.u64 = dir;  // no memory: the game's own path (which will fail the same way)
    ctx.r4.u64 = name;
    ctx.r5.u64 = pw;
    ctx.r6.u64 = ph;
  }
  __imp__sub_8223B170(ctx, base);
  const uint32_t buf = ctx.r3.u32;
  if (!buf) return;
  key.w = Be32(base + pw);
  key.h = Be32(base + ph);
  if (key.w && key.h && uint64_t(key.w) * key.h <= (64u << 20)) {
    WriteCache(file, key, base + buf);
    REXLOG_INFO("[boot] image cache: {} saved ({}x{})", guest, key.w, key.h);
  }
}

extern "C" REX_FUNC(__imp__sub_82238FB8);
REX_HOOK_RAW(sub_82238FB8) {
  const uint32_t src = ctx.r3.u32, dst = ctx.r6.u32;
  const int32_t sw = ctx.r4.s32, sh = ctx.r5.s32, dw = ctx.r7.s32, dh = ctx.r8.s32;
  if (!REXCVAR_GET(scud_image_cache) || !src || !dst || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 ||
      uint64_t(sw) * sh > (64u << 20) || uint64_t(dw) * dh > (64u << 20) || g_game_root.empty()) {
    __imp__sub_82238FB8(ctx, base);
    return;
  }
  const size_t in_bytes = size_t(sw) * sh * 4;
  const uint32_t c = uint32_t(crc32(0L, base + src, uInt(in_bytes)));
  const uint32_t a = uint32_t(adler32(1L, base + src, uInt(in_bytes)));
  Header key{kMagic, c, a, uint32_t(sw) << 16 | uint32_t(sh), uint32_t(dw), uint32_t(dh)};
  char leaf[96];
  std::snprintf(leaf, sizeof(leaf), "resample_%08X%08X_%dx%d_%dx%d.px", c, a, sw, sh, dw, dh);
  const std::filesystem::path file = CacheDir() / leaf;
  std::vector<uint8_t> pixels;
  if (ReadCache(file, key, pixels)) {
    std::memcpy(base + dst, pixels.data(), pixels.size());
    return;
  }
  __imp__sub_82238FB8(ctx, base);
  WriteCache(file, key, base + dst);
  REXLOG_INFO("[boot] image cache: a {}x{} -> {}x{} scaling saved", sw, sh, dw, dh);
}
