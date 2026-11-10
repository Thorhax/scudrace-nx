// Native replacements of the game's small math helpers (compat.obj), bit for bit.
//
// m3_sqrtf (0x821D13A0): a software square root (a bit-by-bit loop of 25 steps, as musl's soft sqrtf), called by the
// 3D worker threads per vertex / node: 18% of one r3d thread in a race on the console. IEEE sqrt is correctly rounded
// as that loop is, so the hardware's gives the same bits; the special cases follow the listing (checked over all 2^32
// inputs against a transcription of it): a NaN comes back quiet with its sign and payload, a negative number or -inf
// as 0xFFC00000, +-0 leaves f1 as it was.
#include <cmath>
#include <cstdint>
#include <cstring>

#include <rex/hook.h>

REX_HOOK_RAW(sub_821D13A0) {
  ctx.fpscr.disableFlushMode();
  const float in = float(ctx.f1.f64);  // stfs
  uint32_t bits;
  std::memcpy(&bits, &in, 4);
  if ((bits & 0x7FFFFFFFu) == 0) return;  // +-0: f1 unchanged
  uint32_t out;
  if (std::isnan(in)) {
    out = bits | 0x00400000u;
  } else if (bits & 0x80000000u) {
    out = 0xFFC00000u;
  } else {
    const float r = std::sqrt(in);
    std::memcpy(&out, &r, 4);
  }
  float result;
  std::memcpy(&result, &out, 4);
  ctx.f1.f64 = double(result);
}

// pace() (platform_x360.obj, 0x8220EB20 in 0.0.17): the board's field clock, called once per field from platform_present_3d.
// Field n is due at start + n * 1e9 / 57524 us (57.524 fields a second) plus a drift that follows the audio queue
// (x360_audio_queued_ms against 100 ms); more than 100 ms late the clock starts again from now. The original waits
// for the due time with Sleep() down to 2.5 ms before it and then SPINS on QueryPerformanceCounter (1.5-2.5 ms of a
// core every field on Windows' 1 ms Sleep). Here the same bookkeeping on the same globals, and the wait sleeps on the
// host (the Switch's sleep is precise) to 200 us before the due time, then yields. scud_native_pace = false: the
// original.
#include <chrono>
#include <thread>

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(scud_native_pace, true, "Scud",
                    "The game's field clock (pace) waits by sleeping instead of spinning for the last 2.5 ms of each "
                    "field (a few % of a core on the Switch's 3 cores)");

extern "C" REX_FUNC(__imp__sub_8220EB20);  // pace
extern "C" REX_FUNC(__imp__sub_8220EAB0);  // qpc_us
extern "C" REX_FUNC(__imp__sub_822139F8);  // x360_audio_queued_ms

namespace {
inline uint64_t Ld64(const uint8_t* base, uint32_t a) {
  uint64_t v;
  std::memcpy(&v, base + a, 8);
  return __builtin_bswap64(v);
}
inline void St64(uint8_t* base, uint32_t a, uint64_t v) {
  v = __builtin_bswap64(v);
  std::memcpy(base + a, &v, 8);
}
inline uint32_t Ld32(const uint8_t* base, uint32_t a) {
  uint32_t v;
  std::memcpy(&v, base + a, 4);
  return __builtin_bswap32(v);
}
inline void St32(uint8_t* base, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(base + a, &v, 4);
}
constexpr uint32_t kStart = 0x83101A78;     // u64: the clock's start (us)
constexpr uint32_t kStartCopy = 0x830FD130; // u64: the start, kept at the first field
constexpr uint32_t kResetCopy = 0x830FD138; // u64: zeroed at the first field
constexpr uint32_t kDrift = 0x83101A70;     // u64: the audio drift (us)
constexpr uint32_t kDriftTotal = 0x83101A40; // u64: the audio drift, summed
constexpr uint32_t kCount = 0x83101A68;     // u64: fields since the start
constexpr uint32_t kLateFields = 0x830FD144; // u32: fields that came late
constexpr uint32_t kResyncs = 0x830FD148;   // u32: restarts of the clock
constexpr uint32_t kRestart = 0x830FD16C;   // u32: 0.0.17: set by the game to start the clock again from now (cleared here)
}  // namespace

REX_HOOK_RAW(sub_8220EB20) {
  static const bool native = REXCVAR_GET(scud_native_pace);
  if (!native) {
    __imp__sub_8220EB20(ctx, base);
    return;
  }
  __imp__sub_8220EAB0(ctx, base);
  const uint64_t now = ctx.r3.u64;
  uint64_t count;
  // 0.0.17: a restart request (kRestart) starts the clock again like the first field.
  const bool restart = Ld32(base, kRestart) != 0;
  if (restart) St32(base, kRestart, 0);
  if (restart || Ld64(base, kStart) == 0) {
    St64(base, kStart, now);
    St64(base, kStartCopy, now);
    St64(base, kDrift, 0);
    St64(base, kResetCopy, 0);
    count = 0;
  } else {
    count = Ld64(base, kCount);
  }
  count += 1;
  St64(base, kCount, count);
  __imp__sub_822139F8(ctx, base);
  int64_t drift = int64_t(Ld64(base, kDrift));
  if (const uint64_t queued = ctx.r3.u32) {
    int64_t d = (int64_t(queued) - 100) * 17384 / 2000;
    d = d > 348 ? 348 : (d < -348 ? -348 : d);
    drift += d;
    St64(base, kDrift, uint64_t(drift));
    St64(base, kDriftTotal, Ld64(base, kDriftTotal) + uint64_t(d));
  }
  const uint64_t offset = count * 1000000000ull / 57524ull;
  const uint64_t target = offset + uint64_t(drift) + Ld64(base, kStart);
  const int64_t late = int64_t(now - target);
  if (late > 100000) {
    St32(base, kResyncs, Ld32(base, kResyncs) + 1);
    St32(base, kLateFields, Ld32(base, kLateFields) + 1);
    St64(base, kStart, now - offset - uint64_t(drift));
    return;
  }
  if (late > 0) {
    St32(base, kLateFields, Ld32(base, kLateFields) + 1);
    return;
  }
  for (;;) {
    __imp__sub_8220EAB0(ctx, base);
    const uint64_t t = ctx.r3.u64;
    if (t >= target) break;
    const uint64_t left = target - t;
    if (left > 400)
      std::this_thread::sleep_for(std::chrono::microseconds(left - 200));
    else
      std::this_thread::yield();
  }
}
