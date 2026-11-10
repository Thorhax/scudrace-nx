// Scud Race: what Renut's sources (taken from mk9-nx) call outside the renderer that has no meaning in this game.
// mk9-nx's Switch button icons redraw MK9's Xbox prompt atlases at upload (render/textures.cpp); Scud Race has none.

#include <cstdint>

#include "mk9_button_icons.h"

namespace mk9::button_icons {
bool Candidate(uint32_t, uint32_t) { return false; }
bool Paint(uint8_t*, uint32_t, uint32_t, uint32_t) { return false; }
uint64_t Hash(const uint8_t*, uint32_t, uint32_t, uint32_t) { return 0; }
}  // namespace mk9::button_icons

// mk9-nx defines these in its PM4-ring renderer (src/me/me_native_system.cpp), which Scud Race does not build: the
// frame recorder (sa2_capture.cpp) asks it for its presents, and Renut's pacing (sa2_renut_pacing.cpp) reads the
// relaxed-vsync switch.
#include "cpu/sa2_enhancements.h"
#include "me/me_native_system.h"

SA2_ENHANCEMENT(g_vsync_relaxed, sa2_vsync_relaxed, sa2::enh::kNonCpu, true,
                "Relaxed vsync: the guest's vblank follows its swaps (one per swap, at most one per 1/60 s), so a "
                "frame that misses 60 fps is shown when it is ready instead of at the next vblank");

namespace me::native {
uint64_t PresentedSwaps() { return 0; }
void SetPresentHook(void (*)(uint64_t)) {}
}  // namespace me::native

// tools/ro_constants.py's PC check counts the folded loads it compared (scud_pch.h's RexRoCheck; mk9-nx defines it in a
// source not built here). RexRoMismatch is in renut_engine/sa2_compat/sa2_compat.cpp.
extern "C" volatile unsigned long long g_rex_ro_checked = 0;

// renut-nx's renderer reports Banjo's hot native copies (app/src/cpu/banjo_hot_guest.cpp) every 10 s: Scud has none.
namespace banjo::hot {
void Report() {}
}  // namespace banjo::hot
