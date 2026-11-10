// The addresses both renderers hook (banjo-nx's tools/renut_gpu_import.py generated this list for N&B). sa2-nx
// (2026-10-20): SA2's. mk9-nx (2026-10-20): MK9's (docs/mk9-renut-census.md).
// scudrace-nx: only D3DDevice_Swap is shared (src/scud_frame_counter.cpp calls Renut's side when its renderer is
// chosen). The draws, BeginVertices, BeginRingAlloc and CBlocker::Check are plain Renut hooks here: Scud Race has no
// other hook at those addresses (docs/scud-renut-census.md, risk 1).
#pragma once

#include <rex/ppc.h>

extern "C" void renut_native_sub_82379040(PPCContext& ctx, uint8_t* base);  // D3DDevice_Swap (scud_frame_counter.cpp)
