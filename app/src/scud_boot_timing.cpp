// Boot timing (2026-11-10, cvar scud_boot_timing): how long the game spends in its loaders on the way to the title,
// to know what to speed up. Each hooked guest function logs every call over 20 ms and its running total.
#include <atomic>
#include <chrono>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(scud_boot_timing, false, "Scud", "Log the time of the game's loaders (ROM zips, UI images)");

namespace {
struct Stat {
  std::atomic<uint64_t> us{0};
  std::atomic<uint64_t> calls{0};
};
template <typename F>
void Timed(const char* name, Stat& s, F&& f) {
  if (!REXCVAR_GET(scud_boot_timing)) {
    f();
    return;
  }
  const auto t0 = std::chrono::steady_clock::now();
  f();
  const uint64_t us =
      std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
  const uint64_t total = s.us.fetch_add(us) + us;
  const uint64_t n = s.calls.fetch_add(1) + 1;
  if (us >= 20000) REXLOG_INFO("[boot] {}: {} ms (total {} ms in {} calls)", name, us / 1000, total / 1000, n);
}
}  // namespace

#define SCUD_TIMED(addr, label)                                         \
  extern "C" REX_FUNC(__imp__sub_##addr);                               \
  static Stat g_stat_##addr;                                            \
  REX_HOOK_RAW(sub_##addr) {                                            \
    Timed(label, g_stat_##addr, [&] { __imp__sub_##addr(ctx, base); }); \
  }

SCUD_TIMED(8236FE38, "mz_extract_to_heap")
SCUD_TIMED(8223B550, "ui_gfx_load")
SCUD_TIMED(8223B0C8, "stbi_load_main")
SCUD_TIMED(82239948, "scale_pieces")
