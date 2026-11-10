// The game's own diagnostics and options, reachable from the host:
//   scud_env = "NAME=value;NAME2=value2"   environment strings put into the game's table (m3_putenv) right after its
//                                           platform_x360_boot sets its own (M3_REALTIME, M3_SOUND_THREAD, ...). The
//                                           game reads its options with m3_getenv (M3_D9_*, M3_R3D_*, M3_DEBUG_*, ...).
//   scud_game_output = true                 the game's stdout / stderr (the CRT's _write on descriptors 1 and 2: its
//                                           [d9-check], [link], romset ... lines) in the log as "[game]".
// Addresses from default.map of release 0.0.14: platform_x360_boot 0x822111C8, m3_putenv 0x821D1628, _write 0x825BE678.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xthread.h>

REXCVAR_DEFINE_STRING(scud_env, "", "Scud",
                      "Environment strings for the game (NAME=value, separated by ';'), e.g. M3_D9_TEXCACHE=0");
REXCVAR_DEFINE_BOOL(scud_game_output, false, "Scud", "Log the game's stdout / stderr ([game] lines)");

extern "C" REX_FUNC(__imp__sub_822111C8);  // platform_x360_boot
extern "C" REX_FUNC(__imp__sub_821D1628);  // m3_putenv(const char*)
extern "C" REX_FUNC(__imp__sub_825BE678);  // _write(int fd, const void*, unsigned)

REX_HOOK_RAW(sub_822111C8) {
  __imp__sub_822111C8(ctx, base);
  const std::string list = REXCVAR_GET(scud_env);
  if (list.empty()) return;
  auto* thread = rex::system::XThread::GetCurrentThread();
  auto* memory = thread ? thread->kernel_state()->memory() : nullptr;
  if (!memory) return;
  const uint64_t r3 = ctx.r3.u64;
  for (size_t at = 0; at < list.size();) {
    size_t end = list.find(';', at);
    if (end == std::string::npos) end = list.size();
    const std::string item = list.substr(at, end - at);
    at = end + 1;
    if (item.find('=') == std::string::npos) continue;
    // The table keeps the pointer: the string lives in guest memory for the whole run.
    const uint32_t guest = memory->SystemHeapAlloc(uint32_t(item.size() + 1));
    if (!guest) continue;
    std::memcpy(base + guest, item.c_str(), item.size() + 1);
    ctx.r3.u64 = guest;
    __imp__sub_821D1628(ctx, base);
    REXLOG_INFO("[scud] game environment: {}", item);
  }
  ctx.r3.u64 = r3;
}

REX_HOOK_RAW(sub_825BE678) {
  if (REXCVAR_GET(scud_game_output) && (ctx.r3.u32 == 1 || ctx.r3.u32 == 2) && ctx.r4.u32 && ctx.r5.u32 < 65536) {
    static std::mutex mutex;
    static std::string pending;
    std::lock_guard lock(mutex);
    pending.append(reinterpret_cast<const char*>(base + ctx.r4.u32), ctx.r5.u32);
    for (size_t nl; (nl = pending.find('\n')) != std::string::npos; pending.erase(0, nl + 1)) {
      std::string line = pending.substr(0, nl);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      REXLOG_INFO("[game] {}", line);
    }
  }
  __imp__sub_825BE678(ctx, base);
}

// The game's own updater (update.obj) downloads a newer release from its hub into game:\ (default.xex, default.map, the
// ui files), keeping the old ones as *.old. This port is the recompiled code of one release (0.0.14): a newer
// default.xex under it would hand the old code another image's data. url_off() (0x8220C238) is the updater's "no URL"
// test (the URL holds NOT-SET): it answers "off" here unless scud_auto_update is set.
REXCVAR_DEFINE_BOOL(scud_auto_update, false, "Scud",
                    "Let the game's updater download a newer release into game_root (off: this build is the recompiled "
                    "code of release 0.0.14, and a newer default.xex would not match it)");
extern "C" REX_FUNC(__imp__sub_8220C238);
REX_HOOK_RAW(sub_8220C238) {
  if (!REXCVAR_GET(scud_auto_update)) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXLOG_INFO("[scud] the game's updater is off (scud_auto_update = false)");
    }
    ctx.r3.u64 = 1;
    return;
  }
  __imp__sub_8220C238(ctx, base);
}

// The tilegen's 2D layers on the GPU (r3d_d3d9.obj's tg2d_draw) or on the CPU (layers_thread): r3d_d3d9_x360_start
// (0x82230AE0 in 0.0.17) sets the choice at 0x825E21D0 from M3_2D_GPU, the tilegen snapshot state and tg2d_selftest
// (0x82228D20, 0 = passed). Logged once at boot.
extern "C" REX_FUNC(__imp__sub_82228D20);
extern "C" REX_FUNC(__imp__sub_82230AE0);
REXCVAR_DEFINE_BOOL(scud_gpu_2d, false, "Scud",
                    "Draw the tilegen's 2D layers on the GPU without the game's self-test (its GPU readback compare, "
                    "which Renut's renderer does not answer): the CPU layer render is a whole core in a race");
REX_HOOK_RAW(sub_82228D20) {
  if (REXCVAR_GET(scud_gpu_2d)) {
    REXLOG_INFO("[scud] tg2d_selftest skipped (scud_gpu_2d = true)");
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_82228D20(ctx, base);
  REXLOG_INFO("[scud] tg2d_selftest: {} (0 = the GPU 2D layers passed)", int32_t(ctx.r3.u32));
}
REX_HOOK_RAW(sub_82230AE0) {
  __imp__sub_82230AE0(ctx, base);
  const uint32_t flag = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + 0x825E21D0));
  REXLOG_INFO("[scud] 2D layers on the {} (flag {})", flag > 0 ? "GPU" : "CPU", int32_t(flag));
}


// 0.0.17: the hidden JOYPOLIS course is built from Daytona USA 2's ROMs (dayto2pe.zip) by dcourse.obj's pre_main
// (0x821EBE70), a thread the game starts at boot: build() (0x821E4B58) returns the course pack, kept at 0x82FE0098
// (null: no dayto2pe.zip, or its chips did not check). Its diagnostics are compiled out of the 360 build; logged here
// with the time it took (it decompresses and repacks the course, ~20 s of a core on the Switch).
extern "C" REX_FUNC(__imp__sub_821EBE70);
REX_HOOK_RAW(sub_821EBE70) {
  const auto start = std::chrono::steady_clock::now();
  __imp__sub_821EBE70(ctx, base);
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  const uint32_t pack = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + 0x82FE0098));
  REXLOG_INFO("[scud] JOYPOLIS course from dayto2pe.zip: {} ({} ms)", pack ? "built" : "not built", ms.count());
}

// 0.0.17: code_field (0x82122178) calls code_entered (0x82120AE0) when the JOYPOLIS code is put in at the course
// select with MEDIUM highlighted (code_field needs the cursor, model3recomp_course_code's 2nd argument, at 2): D-pad
// down, up, then Y (released up first), within 90 fields; logged so a test sees it. On the PC the stick's second
// steer skips MEDIUM: steer to EXPERT, then once back left.
extern "C" REX_FUNC(__imp__sub_82120AE0);
REX_HOOK_RAW(sub_82120AE0) {
  REXLOG_INFO("[scud] the JOYPOLIS code was entered (select step {}, {} {})", ctx.r3.u32, ctx.r4.u32, ctx.r5.u32);
  __imp__sub_82120AE0(ctx, base);
}


// 2026-11-10: closing the software from the home menu saves first. The SDK locks the exit and reports the system's
// close request (RexSwitchExitRequested, sdk windowed_app_context_switch.cpp); this makes the game's exit chord
// (x360_pad_exit_chord, 0x82213BA8: the buttons held 58 fields) say yes, so platform_poll (0x82211348) quits the way
// the menu's EXIT does: x360_files_save_now (scudrace.nv), x360_save_close, platform_x360_finish. The call in
// platform_poll goes through this hook (scud_recomp.26.cpp, not the direct __imp__ call).
// The quick menu's "Exit game" and "Return to the main menu" (scud_cheats.cpp): requests carried out on the game thread.
std::atomic<bool> g_scud_request_quit{false};
std::atomic<bool> g_scud_request_menu{false};

#if defined(__SWITCH__)
extern "C" bool RexSwitchExitRequested(void);
#endif
extern "C" REX_FUNC(__imp__sub_82213BA8);
REX_HOOK_RAW(sub_82213BA8) {
  __imp__sub_82213BA8(ctx, base);
  if (g_scud_request_quit.load()) {
    static bool logged_menu = false;
    if (!logged_menu) {
      logged_menu = true;
      REXLOG_INFO("[scud] quick menu: Exit game: the game saves and quits (as the menu's EXIT)");
    }
    ctx.r3.u64 = 1;
    return;
  }
#if defined(__SWITCH__)
  if (RexSwitchExitRequested()) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      REXLOG_INFO("[scud] closed from the home menu: the game saves and quits (as the menu's EXIT)");
    }
    ctx.r3.u64 = 1;
  }
#endif
}

void ScudRamDump(uint64_t field, uint8_t* base);
void ScudQuitToMenu(PPCContext& ctx, uint8_t* base);

// 2026-11-10: unlimited time (scud_cheat_time). The race's countdown is the word at board RAM 0x104010, in fields
// (3420 = 59.5 s at a race's start on Expert; a checkpoint adds to it; 0 outside a race), found with scud_time_log on
// the console. The board RAM is the guest pointer at 0x82600128. With the cheat, the countdown is held at the highest
// value it reached (a checkpoint still adds its time, then that holds); 0 is left alone, so the race's end, the menus
// and the attract work as before. pokes (0x8224C2C0, main.obj, M3_POKE) runs once per field from on_field
// (0x8224D038), on the game thread: the call there goes through this hook (scud_recomp.41.cpp).
REXCVAR_DEFINE_BOOL(scud_cheat_time, false, "Scud", "Cheat: the race's time never runs out");
REXCVAR_DEFINE_BOOL(scud_cheat_first, false, "Scud", "Cheat: always first place");

// Always first place (scud_cheat_first), found from RAM dumps of a race (scud_ram_dump_every): the 20 cars are 0x300
// bytes apart from 0x181100 (the player's), each with its 0-based race rank in the byte at +0xD4; the cars in race order
// are the words at 0x1056A0 (first place first). Each field the player is put first: it swaps rank and table slot with
// the car the game had first, so both stay a consistent order. Applied right after the game's own ordering,
// race_order_places (0x822E6008, b058.obj: one sorting pass over that table by the cars' progress at +0x94, then each
// car's rank), so everything that reads the order later in the field (the HUD, the results) sees the player first.
void ScudCheatFirst(uint8_t* ram) {
  constexpr uint32_t kPlayer = 0x181100, kStride = 0x300, kRank = 0xD4, kOrder = 0x1056A0;
  auto be32 = [&](uint32_t a) { return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(ram + a)); };
  auto put32 = [&](uint32_t a, uint32_t v) { *reinterpret_cast<uint32_t*>(ram + a) = __builtin_bswap32(v); };
  const uint8_t rank = ram[kPlayer + kRank];
  if (rank == 0 || rank >= 20) return;
  if (be32(kOrder + 4u * rank) != kPlayer) return;  // not the race table we know: leave it
  const uint32_t leader = be32(kOrder);
  if (leader < kPlayer || leader > kPlayer + 19 * kStride || (leader - kPlayer) % kStride != 0) return;
  if (ram[leader + kRank] != 0) return;
  ram[leader + kRank] = rank;
  ram[kPlayer + kRank] = 0;
  put32(kOrder, kPlayer);
  put32(kOrder + 4u * rank, leader);
}
REXCVAR_DEFINE_BOOL(scud_time_log, false, "Scud", "Log the race's countdown (fields) every 60 fields");
extern "C" REX_FUNC(__imp__sub_8224C2C0);
REX_HOOK_RAW(sub_8224C2C0) {
  const uint64_t field = ctx.r3.u64;
  __imp__sub_8224C2C0(ctx, base);
  ScudRamDump(field, base);
  if (g_scud_request_menu.exchange(false)) ScudQuitToMenu(ctx, base);
  const bool cheat = REXCVAR_GET(scud_cheat_time);
  const bool log = REXCVAR_GET(scud_time_log);
  if (!cheat && !log) return;
  const uint32_t ram_guest = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + 0x82600128));
  if (!ram_guest) return;
  if (!cheat && !log) return;
  uint32_t* countdown = reinterpret_cast<uint32_t*>(base + ram_guest + 0x104010);
  const uint32_t left = __builtin_bswap32(*countdown);
  static uint32_t held = 0;
  if (cheat) {
    if (left == 0) {
      held = 0;
    } else if (left >= held) {
      if (held == 0) REXLOG_INFO("[scud] unlimited time: the countdown is held at {} fields", left);
      held = left;
    } else {
      *countdown = __builtin_bswap32(held);
    }
  }
  if (log && field % 60 == 0) {
    REXLOG_INFO("[scud] field {}: countdown {} fields (held {})", field, left, held);
  }
}

// Diagnostics (2026-11-10): scud_ram_dump_every = N saves board RAM 0..3 MB every N fields while a race's countdown runs,
// to <game_root>/../dumps/f<field>.bin (finding the race's variables for the quick menu's cheats).
REXCVAR_DEFINE_INT32(scud_ram_dump_every, 0, "Scud", "Test only: dump board RAM 0..3 MB every N fields in a race");
namespace scud::fast_boot { extern std::filesystem::path g_game_root; }
void ScudRamDump(uint64_t field, uint8_t* base) {
  const int every = REXCVAR_GET(scud_ram_dump_every);
  if (every <= 0 || field % uint64_t(every) != 0) return;
  const uint32_t ram_guest = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + 0x82600128));
  if (!ram_guest) return;
  const uint8_t* ram = base + ram_guest;
  if (__builtin_bswap32(*reinterpret_cast<const uint32_t*>(ram + 0x104010)) == 0) return;
  std::error_code ec;
  const auto dir = scud::fast_boot::g_game_root.parent_path() / "dumps";
  std::filesystem::create_directories(dir, ec);
  const std::string path = (dir / ("f" + std::to_string(field) + ".bin")).string();
  if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
    std::fwrite(ram, 1, 0x300000, f);
    std::fclose(f);
    REXLOG_INFO("[scud] RAM dump at field {}: {}", field, path);
  }
}

extern "C" REX_FUNC(__imp__sub_822E6008);
REX_HOOK_RAW(sub_822E6008) {
  __imp__sub_822E6008(ctx, base);
  if (!REXCVAR_GET(scud_cheat_first)) return;
  const uint32_t ram_guest = __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + 0x82600128));
  if (ram_guest) ScudCheatFirst(base + ram_guest);
}

// The pause menu's QUIT TO MENU, as pause_input (0x822406E8) does it when that item is chosen: pause_set(0, "closed: QUIT
// TO MENU"); then in single player a flag and the field count in the UI state (0x8359E490) and ui_game_restart
// (0x82248888: m3_state_request_jump(5532)), menu_open (0x8223CD88); in a link session values_read and mp_leave. The
// game logs "[ui] QUIT TO MENU: the game is back in its attract". Called on the game thread (from pokes, on_field).
extern "C" REX_FUNC(__imp__sub_82240628);  // pause_set(int, const char*)
extern "C" REX_FUNC(__imp__sub_82121228);  // model3recomp_frame_count
extern "C" REX_FUNC(__imp__sub_82248888);  // ui_game_restart
extern "C" REX_FUNC(__imp__sub_8223CD88);  // menu_open
extern "C" REX_FUNC(__imp__sub_8223C560);  // values_read
extern "C" REX_FUNC(__imp__sub_8223CD20);  // mp_leave
void ScudQuitToMenu(PPCContext& ctx, uint8_t* base) {
  constexpr uint32_t kUi = 0x8359E490;
  auto put32 = [&](uint32_t a, uint32_t v) { *reinterpret_cast<uint32_t*>(base + a) = __builtin_bswap32(v); };
  auto get32 = [&](uint32_t a) { return __builtin_bswap32(*reinterpret_cast<const uint32_t*>(base + a)); };
  REXLOG_INFO("[scud] quick menu: Return to the main menu (the pause menu's QUIT TO MENU)");
  ctx.r3.u64 = 0;
  ctx.r4.u64 = 0x8207EA98;  // "closed: QUIT TO MENU"
  __imp__sub_82240628(ctx, base);
  put32(kUi + 16544, 0);
  if (get32(kUi + 16392) != 0) {
    __imp__sub_8223C560(ctx, base);
    put32(kUi + 356, 0);
    put32(kUi - 696, 0);
    __imp__sub_8223CD20(ctx, base);
  } else {
    put32(kUi + 16548, 1);
    __imp__sub_82121228(ctx, base);
    *reinterpret_cast<uint64_t*>(base + kUi) = __builtin_bswap64(ctx.r3.u64);
    __imp__sub_82248888(ctx, base);
    __imp__sub_8223CD88(ctx, base);
  }
  put32(kUi + 14148, 1);  // the confirm button counts as held (the game's own path stores its state here)
  put32(kUi + 16448, 0);
}
