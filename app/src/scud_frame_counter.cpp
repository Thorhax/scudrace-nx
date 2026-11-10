// Presented frames per second in the log: a hook on the game's Direct3D swap (D3DDevice_Swap, 0x82379040 in
// default.map of release 0.0.14). The scripted gamepad counts its times in these frames.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#if defined(__SWITCH__)
#include <cstdlib>
#include <switch.h>
#endif

#if SA2_RENUT_RENDERER
#include "renut_engine/gpu/banjo_renut_select.h"
#include "renut_engine/gpu/banjo_shared_hooks.h"
#endif

REXCVAR_DEFINE_BOOL(scud_fps_log, true, "Scud", "Log the game's presented frames per second every 5 s");
REXCVAR_DEFINE_INT32(scud_hitch_log_ms, 0, "Scud", "Log each presented frame longer than this many ms ([hitch]; 0: off)");

REXCVAR_DEFINE_INT32(scud_sim_gpu_mhz, 0, "Scud",
                     "Test only: the GPU clock (MHz) set at the first frame, memory at 1331.2 MHz, restored at exit "
                     "(460: handheld's clocks on a docked console; 0: off)");
REXCVAR_DEFINE_INT32(scud_sim_emc_khz, 1331200, "Scud",
                     "Test only: the memory clock (kHz) with scud_sim_gpu_mhz (1600000 and GPU 768: docked's, to put "
                     "them back after a run closed from the home menu, where the exit's restore does not run)");

REXCVAR_DEFINE_BOOL(scud_clock_log, false, "Scud",
                    "Log the GPU and memory clocks every 5 s with the [fps] line (read only: sets nothing)");

std::atomic<uint64_t> g_scud_game_frames{0};

extern "C" void RexSwitchPerfHitch(uint64_t start, uint64_t end);  // sdk switch_perf.cpp (PC: me_pc_stubs.cpp)

namespace {

// The Model 3 board's video fields (model3recomp_frame_count reads this u64, 0x825FCC98 in release 0.0.14).
constexpr uint32_t kFieldCount = 0x825FCC98;

#if defined(__SWITCH__)
// scud_sim_gpu_mhz: handheld's GPU / memory clocks on a docked console (the rig cannot undock it). clkrst rates, read
// first and put back at exit (they would otherwise stay until the next performance configuration change).
u32 g_sim_gpu0 = 0, g_sim_emc0 = 0;
bool SimSet(PcvModuleId id, u32 hz, u32* was) {
  ClkrstSession ses{};
  if (R_FAILED(clkrstOpenSession(&ses, id, 3))) return false;
  if (was) clkrstGetClockRate(&ses, was);
  const bool ok = R_SUCCEEDED(clkrstSetClockRate(&ses, hz));
  clkrstCloseSession(&ses);
  return ok;
}
void SimRestore() {
  if (R_FAILED(clkrstInitialize())) return;
  if (g_sim_gpu0) SimSet(PcvModuleId_GPU, g_sim_gpu0, nullptr);
  if (g_sim_emc0) SimSet(PcvModuleId_EMC, g_sim_emc0, nullptr);
  clkrstExit();
}
u32 g_clock_cpu = 0;
void SimRead(u32& gpu, u32& emc) {
  gpu = emc = g_clock_cpu = 0;
  if (R_FAILED(clkrstInitialize())) return;
  ClkrstSession ses{};
  if (R_SUCCEEDED(clkrstOpenSession(&ses, PcvModuleId_GPU, 3))) { clkrstGetClockRate(&ses, &gpu); clkrstCloseSession(&ses); }
  if (R_SUCCEEDED(clkrstOpenSession(&ses, PcvModuleId_EMC, 3))) { clkrstGetClockRate(&ses, &emc); clkrstCloseSession(&ses); }
  if (R_SUCCEEDED(clkrstOpenSession(&ses, PcvModuleId_CpuBus, 3))) { clkrstGetClockRate(&ses, &g_clock_cpu); clkrstCloseSession(&ses); }
  clkrstExit();
}
void SimClocks() {
  const int mhz = REXCVAR_GET(scud_sim_gpu_mhz);
  if (mhz <= 0 || R_FAILED(clkrstInitialize())) return;
  const bool g = SimSet(PcvModuleId_GPU, u32(mhz) * 1000000u + (mhz == 460 ? 800000u : 0u), &g_sim_gpu0);
  const bool e = SimSet(PcvModuleId_EMC, u32(REXCVAR_GET(scud_sim_emc_khz)) * 1000u, &g_sim_emc0);
  u32 gpu = 0, emc = 0;
  ClkrstSession ses{};
  if (R_SUCCEEDED(clkrstOpenSession(&ses, PcvModuleId_GPU, 3))) { clkrstGetClockRate(&ses, &gpu); clkrstCloseSession(&ses); }
  if (R_SUCCEEDED(clkrstOpenSession(&ses, PcvModuleId_EMC, 3))) { clkrstGetClockRate(&ses, &emc); clkrstCloseSession(&ses); }
  clkrstExit();
  std::atexit(SimRestore);
  REXLOG_INFO("[sim] clocks: GPU {:.1f} -> {:.1f} MHz ({}), memory {:.1f} -> {:.1f} MHz ({})", g_sim_gpu0 / 1e6, gpu / 1e6,
              g ? "set" : "refused", g_sim_emc0 / 1e6, emc / 1e6, e ? "set" : "refused");
}
#endif

void Report(const uint8_t* base) {
  using clock = std::chrono::steady_clock;
  static clock::time_point window_start = clock::now();
  static uint64_t window_frames = 0;
  static uint64_t worst_us = 0;
  static clock::time_point last = clock::now();
  const auto now = clock::now();
  const uint64_t dt = uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now - last).count());
  last = now;
  if (dt > worst_us) worst_us = dt;
  // Frames over 30 ms are the stack profiler's hitch windows ("during the hitches" in rex_profile.log), as mk9's.
  if (dt > 30000) {
#if defined(__aarch64__)
    uint64_t end = 0;
    asm volatile("mrs %0, cntpct_el0" : "=r"(end));  // libnx's armGetSystemTick, 19.2 MHz
    const uint64_t length = dt * 192 / 10;
    RexSwitchPerfHitch(end > length ? end - length : 0, end);
#endif
  }
  if (const int32_t hitch = REXCVAR_GET(scud_hitch_log_ms); hitch > 0 && dt > uint64_t(hitch) * 1000) {
    REXLOG_INFO("[hitch] frame {}: {:.1f} ms", g_scud_game_frames.load(std::memory_order_relaxed), dt / 1000.0);
  }
  ++window_frames;
  const double elapsed = std::chrono::duration<double>(now - window_start).count();
  if (elapsed >= 5.0) {
    uint64_t fields;
    std::memcpy(&fields, base + kFieldCount, 8);
    fields = __builtin_bswap64(fields);
    static uint64_t last_fields = 0;
    if (REXCVAR_GET(scud_fps_log)) {
      REXLOG_INFO("[fps] {:.1f} fps over {:.1f} s ({} frames), longest frame {:.1f} ms, total {}; board {:.1f} fields/s",
                  window_frames / elapsed, elapsed, window_frames, worst_us / 1000.0,
                  g_scud_game_frames.load(std::memory_order_relaxed), (fields - last_fields) / elapsed);
#if defined(__SWITCH__)
      if (REXCVAR_GET(scud_sim_gpu_mhz) > 0 || REXCVAR_GET(scud_clock_log)) {
        u32 gpu = 0, emc = 0;
        SimRead(gpu, emc);
        REXLOG_INFO("[sim] now: CPU {:.1f} MHz, GPU {:.1f} MHz, memory {:.1f} MHz", g_clock_cpu / 1e6, gpu / 1e6, emc / 1e6);
      }
#endif
    }
    last_fields = fields;
    window_start = now;
    window_frames = 0;
    worst_us = 0;
  }
}


}  // namespace

extern "C" REX_FUNC(__imp__sub_82379040);
REX_HOOK_RAW(sub_82379040) {
#if SA2_RENUT_RENDERER
  // sa2_renderer = "renut": Renut's Swap (the frame goes to its render thread), as mk9-nx's frame counter.
  if (::banjo::renut_gpu::Active()) {
    renut_native_sub_82379040(ctx, base);
  } else
#endif
  {
    __imp__sub_82379040(ctx, base);
  }
#if defined(__SWITCH__)
  if (g_scud_game_frames.load(std::memory_order_relaxed) == 0) SimClocks();
#endif
  g_scud_game_frames.fetch_add(1, std::memory_order_relaxed);
  Report(base);
}
