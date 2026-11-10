// The 3D image's height, fixed or following the GPU's time (dynamic resolution). NOT pixel-identical: the user's call.
//
// The game draws the 3D scene into a target of its own and its composite scales it to the display; the 2D layers
// (HUD, text) are drawn at the display's size after that. target_for (r3d_d3d9.obj, 0x8221BFF0, called once per frame
// before the scene) takes the target's height from the game's video settings: 0x83195458 the height, 0x8319545C its
// override flag (without it a 720-line port gives 480), rounded to 384 / 480 / 540 / 720 (one target per height; the
// game makes a new one at a change). So a height written there before target_for is the whole resolution change.
//
// Dynamic: the GPU's time a frame (Renut's timestamps, StatsGpuFrame) averaged over ~16 frames. Above
// scud_dynamic_gpu_ms the height steps down to the highest step predicted to fit; it steps up when the step above is
// predicted under scud_dynamic_up_ms for scud_dynamic_up_s in a row. The prediction scales by the pixel ratio at
// first and then by what was measured at the last change between those two heights (the scene is not all pixel
// work). Holds: a change waits scud_dynamic_hold_s after the last one (each makes a target).
// From renut-nx's banjo_renut_dynamic_* (docs/enhancements-reference.md), on the game's own heights.
#include <algorithm>
#include <chrono>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(scud_render_height, 0, "Scud",
                     "The 3D image's height (384, 480, 540 or 720; the composite scales it to the display, the HUD "
                     "stays at the display's). 0: the game's own setting (or the dynamic resolution when on)");
REXCVAR_DEFINE_BOOL(scud_dynamic_resolution, false, "Scud",
                    "The 3D image's height follows the GPU's time (scud_dynamic_low..720 lines); not pixel-identical");
REXCVAR_DEFINE_INT32(scud_dynamic_low, 540, "Scud", "Dynamic resolution: the lowest height (384, 480 or 540)");
REXCVAR_DEFINE_DOUBLE(scud_dynamic_gpu_ms, 15.0, "Scud",
                      "Dynamic resolution: step down while the GPU's average time a frame is above this (ms)");
REXCVAR_DEFINE_DOUBLE(scud_dynamic_up_ms, 12.5, "Scud",
                      "Dynamic resolution: step up when the step above is predicted under this (ms)");
REXCVAR_DEFINE_DOUBLE(scud_dynamic_up_s, 4.0, "Scud", "Dynamic resolution: seconds the step above must fit first");
REXCVAR_DEFINE_DOUBLE(scud_dynamic_hold_s, 1.0, "Scud", "Dynamic resolution: seconds between two changes");

extern "C" volatile double g_renut_gpu_frame_ms;
extern "C" volatile unsigned long long g_renut_gpu_frame_seq;
extern "C" REX_FUNC(__imp__sub_8221BFF0);  // target_for

namespace {
using Clock = std::chrono::steady_clock;
constexpr uint32_t kHeightVa = 0x83195458, kFlagVa = 0x8319545C;
constexpr int kSteps[] = {384, 480, 540, 720};

struct Dyn {
  int height = 720;
  double avg = 0.0;  // GPU ms a frame, moving average
  int samples = 0;
  unsigned long long seq = 0;
  Clock::time_point changed{}, up_since{};
  bool up_ok = false;
  // measured cost ratio between neighbouring steps: ratio[i] = time at kSteps[i] / time at kSteps[i+1]
  double ratio[3] = {0, 0, 0};
  double before_change = 0.0;  // the average just before the last change
  int from = 0;                // the height before the last change
  int frames_since = 0;
  uint64_t changes = 0;
  Clock::time_point report{};
  double sum_h = 0;
  uint64_t frames = 0;
};
Dyn g;

int Index(int h) {
  for (int i = 0; i < 4; ++i)
    if (kSteps[i] == h) return i;
  return 3;
}
// Predicted time at height 'to' from the time 't' at height 'at'.
double Predict(double t, int at, int to) {
  int i = Index(at), j = Index(to);
  double v = t;
  while (i < j) {  // up
    v = g.ratio[i] > 0 ? v / g.ratio[i] : v * double(kSteps[i + 1]) * kSteps[i + 1] / (double(kSteps[i]) * kSteps[i]);
    ++i;
  }
  while (i > j) {  // down
    v = g.ratio[i - 1] > 0 ? v * g.ratio[i - 1]
                           : v * double(kSteps[i - 1]) * kSteps[i - 1] / (double(kSteps[i]) * kSteps[i]);
    --i;
  }
  return v;
}

void Change(int to, Clock::time_point now) {
  g.before_change = g.avg;
  g.from = g.height;
  g.height = to;
  g.changed = now;
  g.up_ok = false;
  g.frames_since = 0;
  g.samples = 0;  // the average starts again at the new height
  ++g.changes;
}

int DynamicHeight() {
  const auto now = Clock::now();
  if (g.changed == Clock::time_point{}) g.changed = g.report = now;
  const unsigned long long seq = g_renut_gpu_frame_seq;
  if (seq != g.seq) {
    g.seq = seq;
    const double ms = g_renut_gpu_frame_ms;
    g.avg = g.samples == 0 ? ms : g.avg + (ms - g.avg) / 16.0;
    ++g.samples;
    ++g.frames_since;
    // 30 frames after a change between neighbours: the measured ratio.
    if (g.frames_since == 30 && g.from && g.before_change > 0) {
      const int a = Index(g.from), b = Index(g.height);
      if (a - b == 1) g.ratio[b] = std::clamp(g.avg / g.before_change, 0.3, 1.0);
      if (b - a == 1) g.ratio[a] = std::clamp(g.before_change / g.avg, 0.3, 1.0);
    }
  }
  const double hold = REXCVAR_GET(scud_dynamic_hold_s);
  const bool held = now - g.changed < std::chrono::duration<double>(hold) || g.samples < 16;
  const int low = std::clamp(int(REXCVAR_GET(scud_dynamic_low)), 384, 720);
  if (!held) {
    const double budget = REXCVAR_GET(scud_dynamic_gpu_ms);
    if (g.avg > budget && g.height > low) {
      int to = low;
      for (int i = Index(g.height) - 1; i >= 0 && kSteps[i] >= low; --i) {
        if (Predict(g.avg, g.height, kSteps[i]) <= budget) {
          to = kSteps[i];
          break;
        }
      }
      if (to < low) to = low;
      Change(to, now);
    } else if (g.height < 720) {
      const int up = kSteps[Index(g.height) + 1];
      if (Predict(g.avg, g.height, up) <= REXCVAR_GET(scud_dynamic_up_ms)) {
        if (!g.up_ok) {
          g.up_ok = true;
          g.up_since = now;
        } else if (now - g.up_since >= std::chrono::duration<double>(REXCVAR_GET(scud_dynamic_up_s))) {
          Change(up, now);
        }
      } else {
        g.up_ok = false;
      }
    }
  }
  g.sum_h += g.height;
  ++g.frames;
  if (now - g.report >= std::chrono::seconds(10)) {
    REXLOG_INFO("[scud] dynamic resolution, 10 s: {:.0f} lines on average, now {} (GPU {:.2f} ms average), {} "
                "changes; measured ratios 384/480 {:.2f}, 480/540 {:.2f}, 540/720 {:.2f}",
                g.frames ? g.sum_h / double(g.frames) : 0.0, g.height, g.avg, g.changes, g.ratio[0], g.ratio[1],
                g.ratio[2]);
    g.report = now;
    g.sum_h = 0;
    g.frames = 0;
    g.changes = 0;
  }
  return g.height;
}
}  // namespace

REX_HOOK_RAW(sub_8221BFF0) {
  int height = REXCVAR_GET(scud_render_height);
  if (height <= 0 && REXCVAR_GET(scud_dynamic_resolution)) height = DynamicHeight();
  if (height > 0) {
    *reinterpret_cast<uint32_t*>(base + kHeightVa) = __builtin_bswap32(uint32_t(height));
    *reinterpret_cast<uint32_t*>(base + kFlagVa) = __builtin_bswap32(1u);
  }
  __imp__sub_8221BFF0(ctx, base);
}
