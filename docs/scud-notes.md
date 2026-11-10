# Scud Race (wanszai's Xbox 360 build): port notes

Target: wanszai/Scud-Race release 0.0.14 (2026-10-30), `Scud.Race-X360.zip`: `default.xex` (LZX-compressed,
encrypted with the devkit key, XDK 2.0.21256) and **`default.map`**, the full linker map. The game is the Model 3
program ROM recompiled to C (`b000.obj`-`b120.obj`) plus a native Real3D renderer (`r3d_*`), the sound board
(`sound`, `dsb`, `snd_native`, the 68000 driver as a coroutine), netplay (`link_sess`, `rrlink`, `online`) and
its menus (`ui_menu`). Shaders are compiled at run time by `D3DXCompileShader` from HLSL in the image.

The tree started as a copy of mk9-nx (git archive of 86e1b48 + its SDK third-party sources). `app/CMakeLists.mk9-reference.txt`
and the `mk9_*`, `me/`, `renut_engine/` sources are mk9-nx's, kept for the renderer stages; they are not built yet.

## Code generation (2026-11-03)

- `app/scud_manifest.toml`, project `scud`: `rexglue codegen` takes 12 s, 157 files, 78 MB.
- `tools/scud_map_overrides.py` writes `app/scud_map.toml` from the map: `setjmp`/`longjmp` (LIBCMT, 0x8258D7B0 /
  0x8258D490), the `[rexcrt]` host replacements, and `[functions]` for the 64 map entry points discovery missed
  (vtable thunks, `dispatch.obj` handler tables, XAudio2 thunks). Only section 0003 (`.text`): `_fltused` in .data
  is marked `f` in the map and broke registration ("SetFunction: address 825DADD0 outside all registered module
  ranges"). Re-running keeps the entries already in `app/scud_map.toml`.
- **Fibers are required.** `coro.obj` (`m3_coro_create/resume/yield`) runs the sound board's 68000 driver as a
  coroutine on XAPI fibers (`ConvertThreadToFiber` 0x82348A68, `CreateFiber` 0x82348B48, `SwitchToFiber` 0x82348C88,
  `DeleteFiber` 0x82348C28): ~767 round trips per video field. `M3_CORO=thread` would use threads instead (too slow).
  The SDK's rexcrt fibers (`sdk/src/kernel/crt/threading.cpp`, asm context switch on the Switch) are bound through
  `[rexcrt]`; the game treats fiber handles as opaque. Without them codegen reported "Unresolved b target 0x8234AC30"
  (`XapiFiberSwapContext`).
- `tools/switch_check.py`: 0 truncated, 0 wrong, 0 missed tables; the two "odd shapes" are virtual calls in the
  shader compiler (`XGRAPHICS::VRegInfo::Make`, `_statfp`). `tools/pointer_fixpoint.py`: 1 function accepted
  (in `app/scud_overrides.toml`), then fixpoint.
- `tools/xex_image.py` now handles LZX (block chain in Python, `out/tools/lzx_unpack` from `tools/lzx/lzx_unpack.c` on
  the SDK's libmspack) and the devkit key: `out/default_image.bin`.

## First PC boots (stage 3, emulated GPU, Linux, Intel HD 530)

- The game writes under `game:\` (`scudrace.ini`, NVRAM, `abort.txt`, logs): `allow_game_relative_writes` is turned on
  by `ScudApp` (the SDK mounts the game root read-only otherwise, and the game's abort report is lost).
- Saves: `XamContentCreate` 'scud_saves' works (`~/.local/share/scud/...`).
- **ROM set (resolved 2026-11-03):** the user's `scuda.zip` (MAME parent program ROMs epr-19688..19691, all 41 files
  match the table) is used as `game_root/scud.zip`. The first `scud.zip` was the `scudau` clone; the game says
  "romset: refused: %s is a clone-set (scudau) program chip" (silent without a log stream). Original diagnosis: `main` -> `romset_load_zip` -> `zip_is_standard_split` (0x8222B368) checks the zip
  against a 41-entry table (name, size, CRC32 at 0x8207C508) and a list of 4 rejected CRCs. The `scud.zip` here has
  the program ROMs `epr-19731.17`-`epr-19734.20` (CRCs 3EE6447E 23E864BB 6565E29A BE897336, exactly the rejected
  list); the game needs **`epr-19688.17`-`epr-19691.20`** (all 37 other files match). The recompiled code is that
  program revision, so another revision cannot work. `main` then returns 2 silently, the CRT's exit closes every
  FILE, and the hornet thread (dayto2pe.zip, `pre_main`) dies in `_lseeki64` -> `inval_trap` ("a C library call
  with an invalid parameter", `abort.txt`). Expected zip MD5 (README): D2D619B0EE5CBF74596431B28080C420.
- `dayto2pe.zip` here (MD5 9D749C4F...) is opened and read; whether its CRCs match is not checked yet.

## Diagnostic hooks used (out/diag/invarg.cpp, not built by default)

`SCUD_EXTRA_SOURCES=<file>`: hooks by address (`REX_HOOK_RAW(sub_X)` + `__imp__sub_X`). Useful ones: `inval_trap`
0x821F0CB0, `m3_fprintf` 0x821CD330 (the game's stderr; silent without a log stream), `_fsopen` 0x8258BA50, `fclose`
0x8258B3C8, `romset_load_zip` 0x8222B5C8. A guest stack walk must stop at page-aligned back chains (a read at the
stack top faults). `m3_stage_init` 0x822198D0 is a folded address shared by 130 symbols.

## Console rig

`rig/rig.env` (git-ignored) copied from mk9-nx with this port's log and temp folders; sys-botbase and FTP answered
(2026-11-03). Nothing run on the console yet.

## Boot with the right ROM set (2026-11-03)

- PC: 60 fps, the disclaimer splash (`ui/disclaimersplash.png`) drawn by the render thread (`render_thread` ->
  `rt_loop` -> `ui_present`). Every game thread runs: `main` in `model3recomp_run`, the board thread (sound DSP), the
  mix thread (the 68000 coroutine on fibers), the 3D workers, the Hornet build of dayto2pe.zip (3 `grp_load` OK).
- `[fps]` lines now carry the board's field rate (u64 at 0x825DCAF8, `model3recomp_frame_count`): ~57 fields/s for
  ~15 s, then 0 while the splash stays. The drive board's Z80 keeps ticking (main loop alive): the board looks paused
  behind the splash. **Open:** A/START reach the game (`XInputGetState` 0x1000/0x0010, `platform_ui_buttons` 0x01 /
  0x40) but the splash stays (snapshots identical from 15 s to 70 s). To find: what dismisses the splash.
- On a headless PC, Vulkan cannot present to Xvfb (no DRI3): only the snapshot module (`scud_snapshot_s`) shows frames.
- **Jump table cap:** the drive board's Z80 dispatcher `rt_0100` (0x8219D720) switches over 3258 cases (table
  0x8219D7E0, Z80 PC - 0x100). Codegen without `REX_MAX_JUMP_TABLE_ENTRIES` kept 512 and the console trapped
  ("Switch case out of range", brk #0x3e8, Z80 PC 0x333). `tools/codegen.sh` now sets 4096; always use it.
  `tools/switch_check.py` did not report this table (its bound comes after `addi r11,r11,-256`).
- `tools/codegen.sh` no longer runs mk9-nx's `post_codegen.py` (MK9's optimised flavour / Renut store cards).

## First console runs (2026-11-03, emulated Xenos GPU over NVK, Mesa 26.2.2)

- Build: `out/nx` (devkitpro/devkita64, `-DREXGLUE_SWITCH_NVK_SDK=out/tools/mesa-26.2.2-repacked/switch`), 636 steps,
  no LTO, NRO 45 MB. Archived: `out/scud_0.1.0-{a,b}.{nro,elf}`.
- SD: `/switch/scudrace-nx/` with `game_root/` (default.xex, scud.zip = scuda, dayto2pe.zip, ui/, Music/) and
  `scud.toml` (rig config `rig/configs/NL`: `log_network` to the PC on **port 28774**; 28773 is mk9's listener).
  Launch: title takeover (R + A on the home menu's first title) -> hbmenu -> Y -> nxlink `-p scudrace-nx/scudrace-nx.nro`.
- 0.1.0-a: boots, saves created on the SD, both zips read, `scud_mpeg.bin` written, pipelines created; ~6 fps; then
  the drive-board switch trap above.
- 0.1.0-b: ~6 fps; ~6 s into drawing, `Protect: failure 0xCE01` (kernel memory blocks exhausted by the emulated
  GPU's per-page write watching) and an unhandled guest physical write fault at 0xFE33E000. No cvar turns the
  watching off. **The emulated GPU is not a route on the console** (speed and memory blocks): next is Renut's
  Direct3D-level renderer on deko3d, as mk9-nx (`app/CMakeLists.mk9-reference.txt`, `docs/mk9-renut-census.md`),
  with the D3D functions named by the map.

## The "splash stall" was the same jump table (2026-11-03, fixed)

The board stopped ~15 s after boot because `rt_0100`'s switch was truncated again: the PC build's automatic codegen
step (`scud_codegen` in generated/rexglue.cmake) re-runs `rexglue` without `REX_MAX_JUMP_TABLE_ENTRIES`, so it wrote
512 cases back. On the PC the trap (SIGILL) at Z80 PC 0x333 was resumed by the SDK's handler and spun forever on
`__builtin_trap()` (gdb: two samples on the same line), so `model3recomp_end_frame` never returned: no more fields, no
more `ui_field`, and the disclaimer never advanced. **SDK change:** `max_jump_table_entries` defaults to 4096
(`sdk/src/codegen/codegen_flags.cpp`); `tools/codegen.sh` runs `rexgluerd` (the PC tree's build, the one the automatic
step runs). Check after any codegen: `grep -c 'case 3257:' app/generated/default/scud_recomp.*.cpp` = 1.

After the fix (PC, emulated GPU, Intel HD 530): board 57.5 fields/s steadily (Model 3: 57.52 Hz); splash -> MAIN
MENU (Single Player, Multiplayer, Options, Records, Exit; v0.0.14) -> SINGLE PLAYER (Grand Prix, Normal, Export) ->
the arcade attract mode fully drawn (cars, tunnel, town, ranking, View Change demo, BPR credits):
`docs/pc_attract_2026-11-03.png`. 3D frames 2-9 fps on this PC's emulated GPU.

UI state (for scripts and diagnostics): `ui_covering` 0x8221A060 reads the UI phase (u32 at 0x834167B0; 1, 3, 4, 5, 7
cover the game); `ui_field` 0x822242D8 runs once per board field from `model3recomp_end_frame` 0x82121D58.

## Shaders (2026-11-03)

`scud_dump_shaders = true` (src/scud_shader_dump.cpp, hooks on D3DDevice_CreateVertexShader / CreatePixelShader):
17 containers at boot (9 VS, 8 PS); `tools/sa2_dump_shaders.py` finds 16 of them precompiled in the image, so the
library is 17 shaders (`out/renut_shader_dump`). The deko3d chain (XenosRecomp from mk9-nx's PC build, SPIRV-Cross,
uam, copied to out/) translates and compiles all 17 for the Switch (`app/out/renut_switch/deko3d_glsl.bin`, 175 KB).
Note: mk9-nx's GLSL passes include MK9-shaped ones (bonerow, bonesel, ...); check them if a shader looks wrong.

## Renut retarget (in progress)

Scud's XDK 2.0.21256 Direct3D uses **SA2's CDevice layout** (verified: alloc 24704, +24392, +12240, +12812, +12832,
+13068/13072, +12904, +13920, +13600, +16968), so mk9-nx's `[SA2 x]` annotations give the offsets. Function addresses
come from the map by name; the full table is `docs/scud-renut-census.md`. Scud's XDK also has D3D worker threads
(KickOffOnWorkerQueue), which MK9's did not.

## Renut on the PC (2026-11-03): textured 3D

Retarget: `tools/scud_renut_retarget.py` (idempotent; device offsets = SA2's, addresses from the census), then by hand:
`kGlobalQwordVa` 0x825C3FD0 and `kVdGlobalDeviceImportSlot` 0x8200059C (the offset rule must not touch addresses);
`backend/queued.cpp` `kDeltaPrivateUnreadEnd` 12224 (SA2's; MK9 had 12096; the static_assert of 40 read chunks holds);
`InitializePresentationParameters` after the three setups (Scud's own order); the draws, BeginVertices, BeginRingAlloc
and CBlocker::Check plain hooks (nothing else hooks them here); Swap shared with `scud_frame_counter.cpp`.
App: `SCUD_RENUT_RENDERER` (ON), `src/scud_renut_glue.cpp` (MK9 button-icon stubs, `g_vsync_relaxed`, the two `me`
present calls the frame recorder makes), `scud_switch_defaults.toml` (sa2_renderer = renut, renut_gpu_backend = deko3d).

Three bugs found on the way, each a general one:
1. **Fetch constants without a texture object** (`render/textures.cpp` BindTextureLocked): the 3D vertex shader reads
   its per-instance data from fetch 17 (4096x32 float4), which the game writes as a fetch constant with no
   D3DBaseTexture bound (device slot 17 null). Renut returned the null texture; now it binds from the fetch constant,
   keyed 0xF0000000 | base page.
2. **Store cards** (tools/store_cards.py, now run by codegen.sh and by the build's automatic codegen): the store_cards
   compare-and-swap rule assumed the optimised codegen's `cr0.eq = ...`; the classic codegen writes `ctx.cr0.eq`.
3. **XenosRecomp `GetTextureGradients` was not implemented** (`shader_recompiler.cpp`): the fetch fell through to the
   early return and wrote nothing, so HLSL's ddx/ddy produced stale registers. Scud's 3D pixel shaders take their
   texture LOD from ddx/ddy of the UVs (clamped to the last mip): every polygon was one texel of its smallest mip, flat
   colours. Now (ddx(s.x), ddx(s.y), ddy(s.x), ddy(s.y)) in pixel shaders, zero in vertex shaders. Found with a
   BANJO_HLSL_OVERRIDE debug shader writing interpolators / samples as colour (tf.x = 1 textured; samples constant per
   polygon). **Affects every port whose shaders use ddx/ddy.**
Result: `docs/pc_renut_2026-11-03.png` (attract mode fully textured on the GTX 960M through plume/Vulkan).

## Switch on deko3d (2026-11-03): playable

Build `out/nx-dk` (docs/building.md's Switch line as mk9-nx: `-DREXGLUE_SWITCH_NO_NVK=ON`, SPIRV-Cross / uam from
out/deko3d_tools through tools/linux/hostrun.sh, `-v /:/host:ro`); the shader chain as tools/linux/deko3d_chain.sh
(17 shaders). Wrappers copied from mk9-nx (dxc-linux, out/deko3d_tools/bin/*) had mk9-nx's absolute paths: fixed.
NRO 36 MB, archived `out/scud_0.2.0-renut-{a,b}.{nro,elf}`; `b` is installed as sdmc:/switch/scudrace-nx/scudrace-nx.nro.

Console run (title takeover, nxlink, network log on port 28774):
- Splash, attract (all four courses, ranking, View Change demo), main menu, Single Player, course select, car select
  (911, F40, Viper, McLaren F1), transmission, a race on Beginner (Day): all drawn and textured.
- Menus / attract / selects 60 fps, board 57.5 fields/s. Race: 32-53 fps while the first variants compile (12 uam
  compiles, avg 378 ms), then a steady 46-48 fps, board 53-58 fields/s. Renut: GPU 18-20 ms a frame, render thread
  ~11 ms, ~1600 draws a frame: **GPU-bound** (next: the GPU clock / profile, draw counts, the resolution).
- Buttons: positional Xbox layout: Switch B = Xbox A (confirm), A = back, + = START (opens the port's main menu in
  attract), ZR = accelerator, d-pad navigates.
- Seen: the road surface right in front of the camera sometimes shows a noisy yellow/orange pattern on deko3d (grey on
  the PC through Vulkan): to check (the GLSL passes, derivatives on deko3d, a mip choice).
- The SD's scud.toml still has `log_network` to this PC (rig config NL): remove it for normal play.

## Optimisation round (2026-11-04)

Race measurements: `rig/race.sh <nro> <label> [config] [s]` (splash, Single Player, Beginner, the first car, ZR held;
[fps], Renut's GPU line). GPU profile: `renut_gpu_profile = 10.0` + `banjo_renut_diag_gpu_shaders = true`.

- Docked the GPU is already at 768 MHz. On deko3d Renut always draws 1280x720 (`renut_resolution` is not taken there).
- **GPU:** 17.1 of 19.9 ms was one pixel shader, FD5D018F1474A584 (the opaque 3D, ~1200 draws a frame, the game's 960x720
  viewport), every pixel on the sheet path (4-8 raw 16-bit texel fetches + the decode in the shader; the game's decoded
  atlas path, tf.x 2, is never taken: the round-17 texture cache makes 4 entries at boot and no more, reason not found).
  Its translated shader always held the alpha test's discard (`g_banjoAlphaFunc` 0: the run-time test), so the GPU
  could not test depth before shading. **`banjo_deko3d_alpha_spec = true`** (now in scud_switch_defaults.toml): GPU
  19.8 -> 13.7 ms, FD5D 17.1 -> 10.8 ms, image checked (sa2-nx had left it off for flat surfaces in Ryujinx: likely the
  getGradients bug).
- **CPU:** the optimised codegen flavour (`tools/codegen.sh --flavor optimised`; the manifest includes
  codegen_flavor.toml; `tools/post_codegen.py` runs after every codegen again, with `SCUD_POST_SKIP=ro,float,cold,direct`
  to bisect). Bisected: **`non_volatile_as_local` breaks this game** (setjmp / longjmp users: m3_run and the D3DX shader
  compiler that runs at boot: the first Swap saw an 8192x8192 front buffer, then a stall); `skip_msr` is off too
  (D3D's worker threads). Both commented in app/perf_overrides.toml. ro_constants' PC check needs `g_rex_ro_checked`
  (src/scud_renut_glue.cpp).
  Result on the console (scud_0.3.0-opt-a): race **56-58 fps, board 57.3-57.8 fields/s**, GPU 14.1 ms, game thread
  9.4 ms of work a frame. Image checked.
- **Frame pacing (the user sees skips):** the game's own settings (VIDEO SETTINGS: FRAME RATE UNLOCKED, VSYNC ON, the
  defaults) draw a picture every vertical blank with the poses blended ("59.94 unique pictures a second"). Under Renut
  two pacers stack: the game's (QueryPerformanceCounter + Sleep in rt_loop) and Renut's PaceSwap (each Swap held to the
  display's next refresh); ~10 ms of work + ~7.5 ms held = ~17.5 ms a frame, ~200 frames a 10 s late: one frame in five
  waits a refresh more. `sa2_vsync_relaxed = false`: worse (51-53). `sa2_renut_vblank_pacing = false`: windows of 60.0
  fps, but the board drops to 47-49 fields/s at times (the game slows). **Open:** make one pacer follow the other
  (e.g. PaceSwap release at the next refresh without the extra hold when the game paced itself; or the game's refresh
  estimate from the display's vsync event).
- **Networking (SDK, sdk/src/system/xsocket.cpp, xam_net.cpp):** `mk9_guest_sockets` (mk9-nx: off, socket() failed) is
  on for Scud (app + switch defaults). Socket options translated from Winsock (levels, names, big-endian values, the
  receive / send timeouts as timeval), FIONBIO / FIONREAD translated, guest sockaddr_in converted both ways (BSD layout on
  Horizon; ports and addresses were byte-swapped in sendto / recvfrom). Both sides now open their sockets; the Switch
  hosts a SYSTEM LINK lobby, the PC's JOIN lists "no sessions found on the LAN": **open** (discovery traffic not seen yet:
  [net] logs bind / sendto now; XNetGetTitleXnAddr still answers 127.0.0.1).
- **The game's auto-updater** (update.obj) downloads a newer release from http://match.xbpaprium.com/SCUD/x360/ into
  game:\ as soon as sockets work, replacing default.xex (this port is the recompiled 0.0.14: new data under old code).
  It did so once on the PC (restored; the download kept in scudrace/release/hub-update-2026-11-03). Now off:
  `scud_auto_update = false` (hook on url_off 0x821EBF90). The game also contacts its hub at boot (DNS + TCP).
- ONLINE (wanszai's hub) not tested: it needs a player name and creates a visible session on a third party's server.
- New tools: tools/scud_xref.py (who builds an address / string), `scud_env` (the game's m3_getenv options),
  `scud_game_output` (its stdout / stderr; its own diagnostics are compiled out on the 360 build), rig/race.sh,
  rig/link.sh. The "orange stripes" road is seen on the PC too (a bridge at a low camera): not deko3d-specific.

## Optimisation round (2026-11-03 evening): scud_0.3.2-np-b

- **2D layers on the GPU (`scud_gpu_2d`, Switch default on):** the tilegen's 2D layers (HUD, text, sky) were drawn on
  the CPU by `layers_thread` -> `tilegen_render_layer_state` -> `render_pass_fast` (0x8216D708): 44% of a core in a
  race. The game has a GPU path (`be_begin_` -> `tg2d_draw`), chosen in `r3d_d3d9_x360_start` (flag 0x825C203C) when
  M3_2D_GPU is not '0', the tilegen snapshot state is on, and `tg2d_selftest` (0x82207150) passes. The self-test
  compares `tg2d_draw`'s output with the CPU render through a readback (`tg_read`), which Renut does not answer: -1,
  so the CPU path. `scud_gpu_2d` skips the self-test (returns 0); src/scud_game_env.cpp logs the choice. Images checked
  on the PC (attract) and on the console (race HUD).
- **Native `m3_sqrtf` (0x821CCDC8, src/scud_native_math.cpp):** a 25-step software square root, 18% of an r3d worker
  thread. Replaced by the hardware sqrt; bit-exact over all 2^32 inputs against a transcription of the listing
  (NaN quietened with sign/payload, negative -> 0xFFC00000, +-0 leaves f1).
- **Native `pace()` (0x821EE878, `scud_native_pace`, default on):** the board's 57.524 fields/s clock; same
  bookkeeping on the same globals, but the wait sleeps to 200 us before the due time instead of spinning on QPC for
  the last 1.5-2.5 ms of every field.
- **`M3_R3D_MT_SPIN=0` (Switch default through `scud_env`):** the r3d workers' `mt_wait_locked` (0x8213C000) spun
  (6% of a core) before blocking.
- Process cores: 3 (mask 0x7). Race profile before: ~320-400% CPU with the tilegen at 44%.
- Result (race, docked): from the first ~15 s on 58.4-60.1 fps, board 57.4-57.9; late frames per 10 s 17-32 (were
  23-48). **Race start still 50-56 fps:** 2265 draws a frame there (1500 later), render thread 15.2 ms and GPU 16.0 ms a
  frame: Renut's per-draw cost (render thread ~6.7 us/draw) and the GPU (768 MHz, max docked). `M3_D9_MERGE` is
  already on. Next: Renut per-draw CPU (DrawStateLocked, BindStreams, FillTextures, PushSharedConstants), GPU cost.
- Audio: "requests without data" at the race start went from 2+1 to 0 with GPU 2D (profile runs).

## renut-nx (ChanseyIsTheBest, 2026-11-03 head 11163d73): what is newer than our Renut (from mk9-nx)

Read at the user's pointer. Banjo's renderer, XDK 2.0.7978 (ours: 2.0.21256, SA2-layout offsets). No other port carries
the new renderer (peggle2-nx, same XDK as Scud, is on NVK; sa2-nx 1.0.4 has no fast draw path).
- **Four-core mode** (docs/four-core-mode.md; SDK threading_switch.cpp, ~340 lines): a forwarder's NPDM granting CPU
  0-3 (Atmosphere ExeFS replacement at sdmc:/atmosphere/contents/<id>/exefs/main.npdm, written by the app itself when
  it runs as that forwarder); `--wrap=svcSetThreadCoreMask` keeps game threads on 0-2, audio / mixer / shader compiles
  pinned to core 3. Their first run stuttered: the system uses about half of core 3. Sphaira's forwarder id for
  sdmc:/switch/scudrace-nx/scudrace-nx.nro: **05806BF419574000** (formula checked on Banjo's 05D6EA1333487000).
  Needs the user to make the forwarder (Sphaira: X, Install Forwarder, 39-bit).
- **Record-time fast draw path** (docs/record-time-draw-path.md; draw.cpp differs by ~7,700 lines): per-draw work on
  the render thread cut (Banjo 12.8 -> ~8 us/draw at 1020 MHz). Coupled to setter marks in the D3D hooks: a re-port.
- **Internal / dynamic resolution on deko3d** (deko3d-renderer.md section 27): the 3D at a lower height, the HUD at the
  display's; needs a split point (Banjo: after its composite) and a shader pass (scale codes). Not pixel-identical.
- **PGO + LTO** (docs/pgo-lto.md). Our CMake had lost mk9's LTO: `SCUD_SWITCH_LTO` (out/nx-dk-lto) added.

## renut-nx renderer merged (2026-11-03 night): scud_0.4.1-merge-lto

- Base of our engine: renut-nx 25940a65 (banjo-nx packages 36-36s). Banjo's changes since, 3-way merged onto ours
  (backup of the tree before: scratchpad pre_merge_backup.tgz). Banjo's title update 3 address remaps undone first
  (471 addresses mapped back by line pairs); TU3 device-layout and argument changes kept OUT of the hook files
  (device/draw/resource/shader hooks, d3d.h, d3d_native*): Scud's are SA2's layout. Kept from Banjo: the setter marks
  (banjo_renut_tags), the record-time fast draw path (99.9 % of Scud's draws fast on the console), pipeline / shader
  reuse, plume's deko3d back end whole (renut-nx's; mk9's compile probe / boost no-ops). Re-applied ours: mk9's fetch
  copy offset fix, mk9_bound_fetch_spec (spec 1300-1315; pipeline list version 6), sa2's guest-declaration fallback
  (FindGuestDeclarationLocked, both lookups), FieldOfOffset for Scud's layout (unmapped private area = kFieldPrivate),
  kStateTagChunks over both private-state places, the 32-byte last snapshot chunk, viewport 0x3168.
- Banjo's Switch defaults pinned back for Scud (scud_switch_defaults.toml): 720 everywhere, no SMAA / dynamic res,
  no fur / skin tricks, **banjo_renut_whole_buffers = false** (Scud rewrites its vertex buffers every frame: each draw
  proved the whole buffer, PagesClean 82 % of the render thread, 12-15 fps for the first 10 s of a race).
- Console (LTO): render thread in race 7.4-8.0 ms (was 10.9-11.6), race start 10.1 ms (was 15.2); late frames 14-25
  per 10 s; GPU 14.4-15.3 ms is now the limit. The user: "much smoother inside the race", slight stutter in the intro.
- Rig: race.sh / link.sh upload to scudrace-nx-test.nro (NXPATH overrides) so the installed NRO stays the user's.
- Game-side dynamic resolution (src/scud_dynamic_res.cpp, scud_dynamic_resolution, off): the game's own 3D target height
  (0x83019528 + flag 0x8301952C; 384/480/540/720), HUD stays sharp. Measured before the merge: GPU 11-12 ms but no fps
  gain (render thread bound then); worth re-testing now that the GPU is the limit at the start.

## PGO (2026-11-06): scud_0.4.2-pgo-a, installed

- Profile: scud_pgo_gen.nro (out/nx-dk-pgo-gen, SCUD_PGO=generate), two 90 s scripted races (NL config) streamed
  pgo/scud_pgo_{1,2}.stream to the SD; tools/pgo_merge.py -> 137 .gcda in pgo/ (76 of them scud_recomp's).
  out/nx-dk-pgo-use: out/nx_cfg_args.txt + -DSCUD_PGO=use -DSCUD_SWITCH_LTO=ON.
- Console, 2 x 90 s races each, same day (rig/race.sh, NL):
  | build | race fps (5 s windows) | render thread in race | GPU at the start |
  | 0.4.1-merge-lto | 59.30 / 59.06 | 7.76 / 7.56 ms | 14.7 ms |
  | 0.4.2-pgo-a     | 59.59 / 59.63 | 6.62 / 6.50 ms | 14.7 ms |
  Render thread -14 %; GPU unchanged (still the limit at the race start). Intro windows the same in both (39 / 46-51 /
  55-58 fps). Installed as /switch/scudrace-nx/scudrace-nx.nro (clean scud.toml).
- Remaining: about once every 10 s in a race a single 80-190 ms frame (both builds; 1-4 per 70 s). In the intro the
  board itself runs slow (49-52 fields/s, render 3.6 ms, GPU 9.9 ms): the game thread / CPU, not the renderer.
  scud_hitch_log_ms (new, rig config HITCH / PROFH) logs each long frame; frames over 30 ms are now the stack
  profiler's hitch windows (RexSwitchPerfHitch from scud_frame_counter.cpp, as mk9's).
- Rig: sys-botbase answered an empty title id at the home menu (not 0100000000001000); race.sh / link.sh now treat
  empty as the home menu (close_title there would press A on the first home-menu game).

## Hitches and the intro (2026-11-06): scud_0.4.5

- **Most of the rig's "hitch every ~10 s" was the rig**: with log_network the 10 s report's burst of lines made 70-210 ms
  frames. Without it (rig/race_sd.sh: SD log only, fetched after the run; config NONET) a 90 s race had one hitch.
  Players have no log_network. Measure hitches with race_sd.sh from now on.
- What players had: ~75 ms at the course intro's start and ~80 ms mid-race (frame ~9090), both where the renderer made
  a 32 MB + 16 MB deko3d buffer (upload ring / stream cache chunks the trims had given back) and re-made textures that
  banjo's 384 MB reserve (of a 512 MB budget) had released. New banjo_renut_keep_chunks (renut_engine tuning: upload
  ring not trimmed, empty stream-cache chunks kept, both while sbrk has >= 256 MB room) and
  banjo_renut_textures_reserve_mb = 64, both in scud_switch_defaults.toml. Race: 0 frames over 45 ms (2 runs).
- **Intro slow (39-48 fps for ~6 s): the GPU, 25-27 ms a frame** (render thread 8 ms). renut_gpu_profile +
  banjo_renut_diag_gpu_shaders: 11.2 ms in "draws 512x384 | PS 5B84D2A36E94E30B", the tilegen 2D layers (r3d_d3d9's
  ps2d, HLSL compiled at run time; source text in default.xex, see out/default_image.bin strings: "#if defined(R3D_TG)").
  Up to 6 candidates a pixel, each a chain of 5-6 dependent fetches, all of it float arithmetic; the intro's mostly
  transparent overlay text is the worst case. **app/shaders_native/ps_5B84D2A36E94E30B.glsl**: the same algorithm by
  hand (texelFetch, integer bit ops, the bit-table fetch as a shift, break at the first opaque pixel), the translation's
  header and tail; tools/linux/deko3d_chain.sh copies app/shaders_native/*.glsl over glsl_opt before packaging. uam:
  5.5 KB of code (10.6 before). Console: intro 40/49 fps windows -> 59.4/59.8; GPU peak 25.9 -> 16.3 ms; intro shots at
  the same frame pixel-identical (rig: INTRO_SHOTS=1 race.sh). The console compiles it once per NRO name (DKSH cache).
- New diagnostics: scud_hitch_log_ms ([hitch] lines; frames > 30 ms also feed the stack profiler's hitch windows),
  banjo_renut_stats_1s ([renut1s]: render thread / GPU each second), banjo_renut_report_s (0 = no 10 s report; its own
  time logged when > 5 ms), rig/hitchsym.py (symbolised "during the hitches"), rig/hitches.sh.
- Installed 2026-11-06: scud_0.4.5-a as /switch/scudrace-nx/scudrace-nx.nro (clean scud.toml); its DKSH cache seeded
  with the test build's (has the native shader: 0 uam compiles on the installed path). Old caches backed up in
  scud-logs/rig/sd_backup_20261106. Confirmation (race_sd, defaults only): race 59.77-59.88 fps, 0 frames over 45 ms
  in races; intro 58.8-59.2 fps; left: 70 + 100 ms at the attract -> menu load (frames ~1486).

## The 3D pixel shader by hand, cache, menu chunks (2026-11-06 / 09): scud_0.4.7-b, installed

- ps_FD5D018F1474A584 (ps3d9o, the opaque 3D) and ps_CD0F7080E288829E (ps3d9, with the discards) from r3d_d3d9's HLSL
  ("#if defined(R3D_PS)" in default.xex): app/shaders_native/src/ps3d9_body.glsl, put between each translation's header
  and tail by tools/gen_ps3d9_native.py (run on the translation: a backup of glsl_opt from before the native copy). The
  sheet path's wraps / mirrors / mip origins / texel decode in integers; the sheet read as the HLSL does (textureLod at
  the texel centre, normalised). uam: 22.0 KB code, 67 GPRs -> 15.1 KB, 46 GPRs.
- **Variant a (texelFetch on the sheet) hung the whole console at boot** (2026-11-06 08:14: no network for hours, the log
  ends in garbage ~12 s in; the user power-cycled it). Not explained (sheet rows past the texture's height?). Do not
  texelFetch the sheet. Variant b ran clean (pinged every second).
- Console (GPUPROF, 40 s): **GPU in race 14.5 -> 7.2 ms a frame** (15.07 -> 7.38 ms over the profile windows); intro shot
  at 4 s (the full 3D scene) pixel-identical to the translation's; later shots differ only by timing. Race 59.7-59.9 fps
  (the game paces at the board's rate: the gain is headroom).
- Handheld clocks could not be tried: the console runs hoc-clk (Horizon OC, title 00FF0000636C6BFF; memory 1862.4 MHz
  docked, not the stock 1600), which puts the clocks back at once. scud_sim_gpu_mhz / scud_sim_emc_khz (test only,
  default off; rig/configs/HANDHELD) set GPU 460.8 / memory 1331.2 and log the real clocks every 5 s; useful only with
  hoc-clk paused (needs the user). Note: every docked number here had overclocked memory.
- Shader cache for players: tools/deko3d_variant_cache.py reads pipeline list version 6 (fetch_types at 252 -> 920 + k,
  tess_kind at 268 -> 904); shaders/scud_pipeline_list.xz is Scud's list (the console's pipeline_list.bin; mk9's is
  deko3d_pipeline_list.xz); deko3d_chain.sh cache uses it: 31 variants in 1 s, the 19 a race uses byte-identical to the
  console's own. tools/linux/package_switch.sh rewritten for Scud (scudrace-nx.nro, scudrace-nx_deko3d_shaders.bin,
  pipeline_list.bin, README; out/nx/scudrace-nx-switch.zip). Console with only that cache: 0 uam compiles.
- Attract -> menu (frames ~1486, 70 + 100 ms): an upload ring took a 32 MB chunk (one allocation over 16 MB) and a 16 MB
  one there. banjo_renut_spare_upload_mb = 48 (scud_switch_defaults.toml): each ring makes one 48 MB chunk at boot; rings
  128 MB all session (were 96 -> 144), no chunk made mid-run (AddChunk logs one after Init). Frames there now ~68 + 74 ms:
  the rest is the game's own (main thread in pace, board at 57.6 fields/s). banjo_renut_stream_chunks_min (default 0)
  did nothing for Scud: its stream pool stays at 32 MB.
- Installed 2026-11-09: scud_0.4.7-b as /switch/scudrace-nx/scudrace-nx.nro with the packaged cache (backups of the
  old caches: scud-logs/rig/sd_backup_20261109), clean scud.toml. Installed run: race 59.7 fps, 0 uam compiles.

## Courses, the draw floor, dynamic resolution (2026-11-09): scud_0.4.8-a, installed

- rig/race.sh COURSE=n steers right n times on the course select (left stick, setStick). The 0.4 s pulse moved all the way
  to Expert for n = 1, 2 and 3 (the three runs were all Expert: 20 cars, 3 laps); shorten the pulse to pick the others.
- Expert (GPUPROF, 0.4.7-b): the race start is the heaviest part of any race (~2,800 draws a frame): render thread up to
  11 ms, GPU up to 10.3 ms (FD5D 7.0 ms of it); after ~20 s 5-7 ms. 59.9 fps.
- Gather (R3D_GATHER=1 in tools/gen_ps3d9_native.py: two textureGathers when the 2 x 2 footprint is contiguous in the
  sheet): pixel-identical, no faster (7.41 vs 7.38 ms). Off.
- Floor: R3D_MIN=1 (measurement only: the 3D shaders output the vertex colour) on the Expert start: FD5D 6.0 ms against
  7.0 shaded. The rest is the draws themselves (raster of many small polygons, ~2 us a draw), not the shading; the
  vertex shader (vs_2637A3F8F5EC8D30 / vs_AD094B66E4C08FD3: 9 KB, 74-75 GPRs, 9 texture reads a vertex, all taken by
  selects) is a candidate but small by estimate. Pushed constants are ~190 bytes a draw (not the cost).
- scud_render_height = 540 on the Expert start: GPU peak 10.3 -> 7.9 ms. Forced dynamic changes (scud_dynamic_gpu_ms 8):
  4 changes 720 <-> 540 mid-race, no frame over 45 ms. **scud_dynamic_resolution = true** in scud_switch_defaults.toml
  (15 ms threshold): docked never changes (13 reports at 720 in a 90 s session); it is handheld's safety valve.
- Installed 2026-11-09: scud_0.4.8-a (0.4.7-b + dynamic resolution on; the same shaders) with the packaged cache, clean
  scud.toml: race 59.8 fps, 0 uam compiles, intro shots pixel-identical to 0.4.7-b's.

## Handheld GPU clock through Horizon OC (2026-11-09): not applied
- The user allowed CPU / GPU clock changes in Horizon OC (never memory). scud_clock_log (new, read only: clkrst GPU /
  memory rates every 5 s with the [fps] line; rig/configs/CLOCKLOG) instead of scud_sim_gpu_mhz, which also sets memory.
- hoc-clk 2.5.x keeps per-title clocks in /config/horizon-oc/config.ini as sections `[<16-hex tid>]` with
  `docked_gpu=<MHz>` (source: Horizon-OC tag 2.5.1, file/config.cpp; crc32 in [values] is hoc.kip's checksum, not the
  ini's). The takeover title is 010051F00AC5E000. Added `docked_gpu=384` (only that key): the GPU stayed at 768.0 MHz
  (memory 1862.4) for the whole race, Expert start GPU 9.4 ms as docked. Cause unknown (an overlay override or the GPU
  governor would win over a title profile; hoc-clk's log needs /config/horizon-oc/log.flag and a reboot). config.ini
  put back byte for byte, log.flag removed. With no title target hoc-clk puts the GPU back to stock every tick when the
  real rate differs, which is why clkrst changes from the game do not stick.
- Estimate instead: 384 MHz is half of 768, so GPU times about double: Expert start ~20 ms at 720 lines, ~16 at 540,
  so dynamic resolution (15 ms) would go to 540 / 480 there and back to 720 later in the race (5-7 ms docked -> 10-14).

## Handheld is CPU-bound at 1 GHz; non-volatile registers as locals for the game's code (2026-11-09)
- The user played a race undocked (scud_084.log, 0.4.8-a): GPU 7-8 ms (dynamic resolution stayed at 720), but 34-38
  fps at the start and 50-55 later, render thread 12-13 ms a frame: the CPU at the stock 1020 MHz. Docked numbers
  before had the user's CPU overclock. The user then set the CPU to 1020 MHz docked too (GPU 768, memory 1862.4 as
  before; scud_clock_log now logs the CPU too): docked reproduces it (rig/configs/PROFCLK, Expert start: intro 37.3,
  race 52.4 fps, 133 frames over 45 ms in 60 s).
- Profile at 1020 MHz (3 cores, ~335 % wanted): sound board thread 63 % (sn_sample, the DSP programs dspc_p112 /
  p126, UpdateSlot, the 68000 driver snd2_fg, snd_bus_*), Renut's render thread 55-74 % (core 1), main game thread
  57 %, guest render thread 45-55 % (CaptureDelta 18 %), 3D workers 20-29 % each. Names from
  assets/game_root/default.map (exact addresses).
- SDK: non_volatile_as_local_only (opt-in list) and non_volatile_as_local_keep (opt-out with the global option).
  The global option with the five setjmp callers kept in ctx (plus native wrappers saving ctx around them) still
  broke the libraries (console: a guest write through r1 = 0 at boot; PC: the 8192-wide resolve, as on 11-04): the
  setjmp callers were not the whole story. Now: global off, app/nvl_game.toml (tools/nvl_game_list.py) lists the game's
  own 8165 functions (no library objects, no coro.obj, not m3_run). PC boots to the attract (image fine); console
  races clean (0.4.9-nvl-b): sound thread 63 -> 50 %, main 57 -> 51 %; fps within noise before PGO (intro 42.4,
  race 50.9, 83 frames over 45 ms). PGO retrained (pgo/scud_pgo_3/4 streams, old profile in the scratchpad).
- With the PGO retrained (clean rebuild: .gcda changes do not trigger one): **scud_0.4.9-a** (= nvl-c). Console at
  CPU 1020 MHz, Expert, rig/configs/LEANCLK (no profilers), A/B/A the same hour:
  | build | intro fps | race fps | frames over 45 ms (60 s) |
  | 0.4.9-nvl-c | 46.4 / 46.7 | 57.39 / 57.76 | 26 / 26 |
  | 0.4.8-clk2  | 38.4        | 52.29         | 94      |
  Audio counters (profiled run) as before (no empty buffers, same peaks); race image fine. **Installed** 2026-11-09
  (installed run: intro 47.3, race 56.93, 29 over 45 ms), clean scud.toml; the shader cache is unchanged.

## Upstream 0.0.17 (2026-11-10): scudrace-nx-0017 tree, scud_0.5.0
- wanszai/Scud-Race released 0.0.15 (11-01), 0.0.16 and 0.0.17 (11-07: the hotfix of 0.0.16's wheel force feedback).
  Changes per the release notes: H-pattern / sequential shifting, Miami server, records by region (0.0.15); the
  environment light glitch fixed, player names over online cars, the solo menus without a selection timer (gas
  proceeds, brake goes back; SELECT TIMER in SINGLE PLAYER turns it back on), majority vote / host picks, the hidden
  Joypolis track (needs dayto2pe.zip, which game_root has), Super Beginner online (0.0.16). The X360 zip has
  default.xex, default.map, ui/ (unchanged) and online.ini (the same values).
- Port tree: a copy of scudrace-nx (scudrace/scudrace-nx-0017, its own out/; 0.0.14's tree untouched). text +160 KB,
  .bss +1.5 MB: every address moved.
- **tools/remap_release.py** (old map, new map, both images from REXGLUE_DUMP_IMAGE): a function address moves with
  its map symbol, checked by comparing the instructions with relink-sensitive fields masked (CHANGED when the size or
  an instruction differs). A data address is carried by the code that uses it: every lis/addi/load/store whose
  address is that one (or the nearest referenced below it, within 0x40) votes with the same instruction in the new
  release's function (difflib alignment when the function changed). The map's own symbol for a data address is NOT
  enough (most statics have none: the nearest symbol put pace's clock 0xC8E70 bytes off). --only <file> limits the
  rewrite to listed addresses (Renut's sources also carry MK9 / SA2 addresses in comments that happen to fall in
  Scud's range), --force OLD=NEW for checked-by-hand ones.
- Changed by hand: pace() (now 0x8220EB20) has a new restart request (u32 0x830FD16C: when set it is cleared and the
  clock starts again from now): added to the native pace. r3d_d3d9_x360_start (0x82230AE0) is shorter but the 2D flag
  store (0x825E21D0) is the same. scud_map.toml regenerated (tools/scud_map_overrides.py), nvl_game.toml regenerated
  (tools/nvl_game_list.py now skips m3_run by name), tools/scud_renut_retarget.py's table remapped.
- Renut: three vertex shaders changed (the light fix): not in the build's table, so their draws were skipped (race
  3D black, HUD fine). Captured by the console (shader_capture/ beside the NRO), added to out/renut_shader_dump
  (+ manifest rows), PC rebuild (shader table), tools/linux/deko3d_chain.sh, Switch rebuild: race image fine.
- Tree copy pitfalls: absolute paths in app/thirdparty/.../dxc-bin/bin/x64/dxc-linux, out/deko3d_tools/bin/{spirv-cross,
  uam} and app/out/renut_switch/*.cpp (.incbin) pointed at the old tree; scud_renut_gpu.cmake uses
  out/renut_shader_dump (copied from the old tree).
- rig: race.sh / race_sd.sh take SD_DIR (the SD folder, default scudrace-nx; tests of 0.0.17 used
  /switch/scudrace-nx-0017 with the 0.0.17 game files) and MENU_DOWNS (SINGLE PLAYER's START is 5 lines down now).
- Console, CPU 1020 MHz, Expert, LEANCLK, no PGO yet (scud_0.5.0-u17-b): intro 42.8, race 54.94 fps (0.4.9-a with
  PGO: 46.5 / 57.5).
- SDK, two locks off hot paths (race profile of 0.4.9-nvl-b: the most contended ones):
  - KernelState::LookupFiber: the sound driver's coroutine (coro.obj) calls SwitchToFiber all the time, and each
    lookup took the process-wide global_critical_region_ (13-20 % of that thread's stack samples waited on it). Now a
    per-thread cache of the last two fibers, valid while fiber_generation_ (bumped by every change of fiber_map_) is
    unchanged.
  - XObject::GetNativeObject (every KeSetEvent / KeWaitForSingleObject on a guest dispatcher object): an object
    already made is looked up without the global lock (StashHandle writes the handle, then the signature with a
    release fence).
- PGO retrained on 0.0.17 (out/nx-dk-pgo-gen, two 90 s races: Expert and Beginner, streams in out/pgo_streams;
  0017's pgo/). shaders/scud_pipeline_list.xz from the console's pipeline_list.bin after the 0.0.17 races (116
  pipelines, 38 variants). The NRO's menu name is "Scud Race (0.0.17)".
- Console, CPU 1020 MHz, Expert, LEANCLK, the same hour (60 s races):
  | build | intro fps | race fps | frames over 45 ms (over 60) |
  | 0.4.9-a (0.0.14, installed) | 45.8 | 57.44 | 31 (8) |
  | 0.5.0 PGO (0.0.17) | 45.6 / 45.4 | 57.56 / 57.45 | 18 (2) / 17 (2) |
  | 0.5.0-a installed as /switch/scudrace-nx-0017 | 46.1 | 56.97 | 20 (3) |
- **Installed (2026-11-10) beside the 0.0.14 one:** /switch/scudrace-nx-0017/scudrace-nx-0017.nro (+ its DKSH cache
  grown on the console, pipeline_list.bin, clean scud.toml, game_root with the 0.0.17 default.xex / map / ui and the
  same ROMs, music folder, online.ini, the save folder copied from the 0.0.14 install). /switch/scudrace-nx is
  unchanged (0.4.9-a, 0.0.14).

## Allocator and lock contention (2026-11-10): scud_0.4.9-b / scud_0.5.0-b, installed
- Race profile (0.5.0-a, PROFCLK): the three game threads and Renut's render thread want ~3.3-4 cores of 3. Stacks
  through newlib's malloc lock (one lock for the process, rmutexLock) and the SDK's global lock:
  - Renut's capture pool kept at most 2048 spare DrawCaptures (kMaxFree), but an Expert start draws ~2265 times a
    frame: every frame the spill was deleted and allocated again (new DrawCapture + its 13 KB state vector), the
    guest render thread waiting on the malloc lock. kMaxFree = 8192 (app/src/renut_engine/gpu/backend/queued.cpp).
  - rex::thread::TimerQueue: a recurring timer's forward_list node was popped and pushed again at every firing (the
    guest timestamp timer, ~1000 a second): now moved with splice_after (sdk/src/core/timer_queue.cpp).
  - KeWaitForMultipleObjects / NtWaitForMultipleObjectsEx: the object vector reserved up front.
  - (with 0.5.0-a already) KernelState::LookupFiber per-thread cache; XObject::GetNativeObject lock-free for objects
    already made.
- Console, CPU 1020 MHz, Expert, LEANCLK, 60 s:
  | build | intro fps | race fps | frames over 45 ms (over 60) |
  | 0.5.0-a (0.0.17) | 45.0 / 46.1 | 57.49 / 56.54 | 14 (4) / 27 (4) |
  | 0.5.0-b (0.0.17) | 48.9 / 48.5 | 58.09 / 57.97 | 4 (1) / 5 (1) |
  | 0.4.9-a (0.0.14) | 47.2 | 57.12 | 27 (7) |
  | 0.4.9-b (0.0.14) | 48.7 | 58.04 | 5 (3) |
  Audio counters (profiled run) as before. Installed: 0.4.9-b as /switch/scudrace-nx/scudrace-nx.nro (run: intro 49.2,
  race 57.86, 6 over 45 ms), 0.5.0-b as /switch/scudrace-nx-0017/scudrace-nx-0017.nro (intro 50.2, race 57.49, 2);
  clean scud.toml in both. Rollback: out/scud_0.4.9-a.nro, out/scud_0.5.0-a.nro.
- 0.0.17 also builds the hidden Joypolis course from dayto2pe.zip at every boot (dcourse.obj pre_main thread: build(),
  miniz deflate at flags 1 = lazy parsing, inflate): ~20 s at ~70 % of a core from ~20 s after launch. zpack keeps the
  raw bytes when deflate fails or does not shrink, so TDEFL_GREEDY_PARSING_FLAG (0x4000, tdefl_compress_fast) would
  be safe; not done (only matters when a race starts within ~40 s of launch).

- Later experiments (0.0.17 tree only, not installed):
  - mfmsr without the lock (sdk/resources/templates/codegen/pch_h.inja: REX_CHECK_GLOBAL_LOCK from a thread_local
    depth of interrupt-off sections; it took the global lock only to read a count that, once the lock is held, can
    only be the thread's own). Same answers. scud_0.5.0-c (old PGO profile): race 57.33 / 57.59 vs 0.5.0-b 58.07 /
    57.81: the regenerated code (76 of 157 files) no longer matches the profile, so not a fair test: any codegen
    change needs PGO retrained.
  - fiber_switch_scalar_only (SDK cvar, sdk/src/kernel/crt/threading.cpp + PPCContext::SaveNonVolatilesScalar): the
    DSB's Z80 driver (dsb_driver.obj) and the 68000 driver (driver2.obj) run as coroutines (coro.obj) on fibers; the
    switches were ~45 % of that thread (memmove of 1.6 KB each way + SwitchToFiber), ~6 % of a core. Only coro.obj
    (no vector or float non-volatiles in any of its 8 functions) and nvl game code are on a suspended fiber's stack
    (callers of m3_coro_yield / m3_coro_resume all in nvl_game.toml; m3_run has no vector non-volatiles either). PC:
    attract audio dumped (audio_dump_output_s, SDL ALSA) off vs on: identical spectrum bands, peak, envelope, no
    clicks.
  - **Console lost (2026-11-10 03:23):** PGO training build with both (scud_0.5.0-pgogen2): race 1 (Expert) fine;
    race 2 (Beginner) launched, then sys-botbase / FTP / ping all stopped before the race began (screenshots empty
    from 03:23:28). Cause unknown (crash, hang, sleep or network); the 11-06 loss was at a title close. Console tests
    stopped; fiber_switch_scalar_only left OFF in scud_switch_defaults.toml until retested. Installed builds
    (0.4.9-b, 0.5.0-b) have neither change.
- **JOYPOLIS (0.0.17's hidden course) works in the port (PC, 2026-11-10):** dcourse.obj builds it at boot from
  dayto2pe.zip (chips CRC-checked, not the zip's MD5: ours differs from the README's and builds): "[scud] JOYPOLIS
  course from dayto2pe.zip: built (6.6 s on the PC)" (hook on pre_main, src/scud_game_env.cpp). The code: at SELECT A
  COURSE with **MEDIUM highlighted**, D-pad **down, up, then Y** (up released first; all within 90 fields), logged
  "[scud] the JOYPOLIS code was entered". (code_field 0x82122178: buttons from platform_code_buttons = d-pad down 1,
  up 2, Y 4; model3recomp_course_code's 2nd argument is the highlighted course, it must be 2; M3_TEST_CODE =
  "field:d,field:u,field:y" scripts it.) Raced on the PC from the scripted input: Daytona 2 scenery, 40 cars, 4 laps.
  Some walls show magenta patches (a texture the port draws wrong, or the course's own colours: not compared with the
  original yet). On the Switch: not tried yet (its pipelines are not in the shader cache: first run compiles them).
  On the Switch's positional layout Y is the top face button (X).
  - Fixed: the magenta / noise textures and flat white walls on JOYPOLIS came from its texture worker
    (M3_DCOURSE_TEXASYNC, records unpacked off the game thread): the renderer misses what that thread writes.
    M3_DCOURSE_TEXASYNC=0 (game-thread unpacking) draws it right (PC): now in the Switch defaults' scud_env.
- **0.5.0-d installed (2026-11-10, after the console came back):** 0.5.0-b's generated code and PGO profile exactly
  (the mfmsr template change and the fiber_switch_scalar_only SDK change taken out; copies in the session scratchpad)
  plus M3_DCOURSE_TEXASYNC=0 in the defaults and the JOYPOLIS log hooks. Expert, 1020 MHz: 0.5.0-b 57.66 / 0.5.0-d
  57.70 race fps back to back. JOYPOLIS raced on the Switch (rig: `COURSE=0 JOYCODE=1 rig/race_sd.sh ...`, the code
  by rig/joycode.py; the course-select cursor starts on MEDIUM, COURSE=1 is EXPERT): built at boot in 26.5 s,
  textures right, race 52.7 fps (40 cars), 6 frames over 45 ms in 90 s. rig/race.sh launched nothing when the console
  sat on hbmenu's NetLoader screen (the first run after it came back); the next run worked.
- **The 03:23 loss was an Atmosphère kernel panic** (the user's screen, 2026-11-10). No crash or fatal report (a kernel
  panic writes none). That run's SD log (scud_021.log, console clock 12 h behind the PC) ends 14 s after launch, while
  loading, after a routine GPU-region growth (normal runs do the same and go on), followed by 3.4 KB of ARM64 code bytes
  instead of text: the SD write cut off mid-flush. Its log is buffered, so the panic may be later than the last line
  (ping stopped ~1.5 min after it). A user-mode change (fiber register save, mfmsr depth) cannot panic the kernel by
  itself; suspects are below the game: the memory overclock (1862 MHz docked) after hours of load, or a kernel issue.
- **Both experiments retested (2026-11-10 morning, user's OK; no crash in 8 launches incl. 2 PGO training races):**
  - fiber_switch_scalar_only, one NRO (0.5.0-e, 0.5.0-d's profile; the scalar save/restore added after
    RestoreNonVolatiles so no line moves), off/on/on/off by scud.toml (rig/configs/LEANCLKFS): race 56.91 / 57.08 /
    57.06 / 57.56. No gain.
  - mfmsr depth (pch_h.inja), PGO retrained (out/pgo_streams_msr): 0.5.0-f vs 0.5.0-d, f/d/f/d: race 57.66 / 57.83 /
    57.07 / 57.30, long frames 1 / 2 / 2 / 4. No gain.
  - Both dropped; the tree is back to 0.5.0-d's sources, generated code and profile (experiments in the session
    scratchpad: u17/fiber_scalar, u17/msr_exp, scratchpad/pch_h.inja.mfmsr).
  - Note: tools/codegen.sh today writes 7 functions differently from the 02:02 run (integer load + reinterpret instead
    of REX_LOAD_F32 in 6; the hooked code_entered call in code_field); same behaviour. app/generated is 0.5.0-d's.

## Widescreen and the menu's EXIT (2026-11-10)
- The game supports other aspect ratios itself (OPTIONS > VIDEO SETTINGS > SCREEN; b007.obj port_aspect: modes 0 4:3
  496 wide, 1 16:9 682, 2 16:10 614, 3 64:27 910, 4 32:9 1366, 5 5:4 480; M3_DEBUG_ASPECT=<mode> in scud_env
  overrides the menu). 16:9 draws a wider view (not stretched), HUD at the edges; the menus stay 4:3. Console, 0.0.17,
  1020 MHz, Expert: race 54.2 / 53.96 fps at 16:9 vs ~57.5 at 4:3 (8-10 frames over 45 ms a minute vs 3-5).
- The choice is in scudrace.nv (with scudrace.ini, in the save folder), written only when the game quits through the
  main menu's EXIT; HOME-closing loses menu changes. EXIT then crashed ("The software was closed because an error
  occurred", 2345-0104 break): ReXApp::OnDestroy freed the window before the runtime, and the mouse-and-keyboard input
  driver's destructor detached from the freed window and threw. Fixed in sdk/src/ui/rex_app.cpp (runtime first):
  EXIT saves and returns to the home menu. Installed: 0.4.9-c (/switch/scudrace-nx) and 0.5.0-g
  (/switch/scudrace-nx-0017). The 0.0.17 save now has 16:9 selected (set during the test).

## Closing from the home menu saves (0.5.0-h, 2026-11-10)
- The SDK locks the exit (appletLockExit, cvar switch_close_saves, default on); a HOME > Close request sets
  RexSwitchExitRequested and gives the game switch_close_grace_ms (8000) to quit itself before the window is closed as
  before; ~SwitchWindowedAppContext unlocks. app/src/scud_game_env.cpp hooks x360_pad_exit_chord (0x82213BA8) to say
  yes then, so platform_poll (0x82211348) runs the menu EXIT's path: x360_files_save_now, x360_save_close,
  platform_x360_finish. The direct call in scud_recomp.26.cpp was turned back into sub_82213BA8 (same line count: the
  PGO profile still applies).
- Console: SCREEN 16:9 -> 4:3 then HOME-close: nv rewritten (byte 122994 1 -> 0), home menu, no crash report;
  relaunch showed 4:3; back to 16:9 the same way; a close mid-race (Expert, 16:9: 54-55 fps) saved too.
  Installed as /switch/scudrace-nx-0017 (0.5.0-h). The 0.0.14 install (0.4.9-c) does not have it.
- Note: the game's frame is the Model 3 board's field, 57.5 per second ("board 57.5 fields/s"): ~57.5 fps is the cap.

## Unlimited time cheat (0.5.0-i, 2026-11-10)
- `scud_cheat_time = true` in scud.toml. The race countdown is the word at board RAM 0x104010 in fields (3420 = 59.5 s
  at an Expert start; checkpoints add; 0 outside a race), found with `scud_time_log`. A hook on pokes (0x8224C2C0, run
  once per field by on_field 0x8224D038; the direct call in scud_recomp.41.cpp reverted, same line count) holds it at
  the highest value reached; 0 is left alone. (model3recomp_port_ffb_telemetry's table pairs names with offsets one
  entry off: its "time_up" slot is this countdown.)
- Console: Expert, 10 s driving then 80 s idle: countdown held at 3420, lap clock 1'24", HUD 59, race went on.
- Installed (0017) with scud_cheat_time = true; that folder's scud.toml is rig/configs/user_0017_scud.toml (put it
  back after rig runs instead of the clean 207-byte file).

## Quick menu (cheats) and faster boot (0.5.0-j, 2026-11-10)
- Quick menu: app/src/scud_cheats.cpp, adapted from Shaolin Monks NX's port/common/quick_menu.cpp (GPL-2.0-or-later,
  Dolphin NX / YabaSanshiro lineage; this repository is GPLv3). Minus + Plus opens and closes it (sdk
  switch_input_driver.cpp DispatchMenuShortcuts: fires F7, then RexSwitchInputQuietUntilReleased so the chord never
  reaches the game; L + R + Left still works). It takes the achievements overlay slot (ScudApp::
  CreateAchievementsOverlay; this game has none), so it exists only while open; ReXApp's input callback gives the game
  nothing while it is open (rex_app.cpp). Buttons read from ImGui's gamepad keys by letter: D-pad moves, A selects,
  Left / Right flip, B back / close. Pages: Resume game, Cheats > (Unlimited time). A change is written to scud.toml at
  once, only its own line (SaveLine). The first try (ImGui navigation) did not take the focus on the console.
- Boot measured with scud_boot_timing (app/src/scud_boot_timing.cpp, default off), console, 1020 MHz: start -> title
  16.3 s. romset_load_zip 7.6 s (translated miniz: ~12 MB/s), scud_mpeg.bin ~2.1 s, board boot ~2.5 s, ui_gfx_load
  (stb_image, 8 PNGs 1920x1080) 1.8 s, scale_pieces (resample_area x2) 1.2 s; the JOYPOLIS build (hz_stream) 27 s in
  the background.
- app/src/scud_fast_boot.cpp:
  - scud_native_unzip (on): mz_zip_reader_init_file (0x8236F800) noted + central directory parsed natively;
    mz_zip_reader_extract_to_mem_no_alloc1 (0x8236F990) and hz_stream (0x8224EAA8) done with pread + zlib + CRC check,
    the game's code as fallback. hz_stream waits while romset_load_zip (0x8224E470) runs (the ROMs get the SD).
    ROM set 7.6 -> 3.9 s (60 MB read 1.1 s, inflate 1.6 s, the game's own ROM work ~1.2 s); JOYPOLIS 27 -> 19.7 s.
  - scud_image_cache (on): load_png (0x8223B170) results and resample_area (0x82238FB8) outputs kept zstd-compressed in
    <game_root>/../cache/ui (4 MB); guest malloc 0x825B3A70. ui_gfx_load 1.8 -> 0.48 s, scale_pieces 1.2 -> 0.21 s
    from the second boot. (The Switch's rename does not replace a file: removed first.) An unpacked ROM cache was not
    done: it would read 98 MB instead of 60 from an SD that is the limit.
  - Start -> title ~10.4 s from the second boot (was 16.3).
- Left as they are: the board boot's sound_board_init spin(0x5000000) (~2.6 s of emulated cycles; shortening it changes
  the emulated board's boot) and the Real3D selftest (~2.4 s; not only a diagnostic: it sets flags such as
  0x82152C00 from what it measures). Both could be cached / skipped with a careful diff of what they change.
- Found by the early HOME-close test: closing in the first ~25 s (JOYPOLIS build still running) ended with 2345-0101
  after the game had saved (that thread followed a cleared pointer; FATAL "invalid function 0x00000000"). sdk
  function_dispatcher.cpp parks such a thread once the game's main thread has returned (g_guest_main_exited, set
  before "Execution complete"). Console: closed 22 s after launch, home menu in ~5 s, no report.
- Race at 16:9 Expert 1020 MHz, 60 s, 0.5.0-i vs j: 50.92 / 51.43 / 51.28 (i, j, i): no change.

## 1785 MHz, always first place (0.5.0-k, 2026-11-10)
- CPU 1785 MHz (the user's Horizon OC; GPU 768, memory 1862.4): Expert 16:9, 60 s, 0.5.0-j: 59.90 / 59.90 fps
  (presented; the board at its 57.5 fields), 0-1 five-second windows with a frame over 45 ms; ROM set 2.7 s.
- scud_ram_dump_every (test only): board RAM 0..3 MB to <folder>/dumps every N fields in a race.
- From those dumps: cars 0x300 apart from 0x181100 (player), +0x04 car index, +0x94 progress, +0xD4 rank (0-based);
  0x1056A0 the cars in race order; 0x10401C the count. race_order_places (0x822E6008) sorts (one pass) and writes ranks.
- scud_cheat_first: hook on race_order_places; after it, the player swaps rank and slot with the leader. Console: HUD
  01ST all race (car on the walls). Not checked: the result screen at the finish.
- No damage: the game has no damage model to turn off.
- The user confirmed always-first-place: the HUD shows the real position during the race, but the result was 1st
  after finishing 26th (the HUD in those console tests showed 01ST: the cheat's order holds at the result).
- The user's 0017 scud.toml now has input_xbox_layout = false (A confirms, B back, by letter; the game's prompts read
  "A SELECT B BACK"). Rig configs leave the default (by position): the rig scripts press B to confirm.

## Quick menu: return to the main menu, exit; Thorhax intro; new icon (0.5.0-l, 2026-11-10)
- Quick menu main page: Resume game, Cheats >, Return to the main menu, Exit game (the last two ask for a second A).
  Return: the pause menu's QUIT TO MENU sequence (pause_input 0x822406E8) run from the pokes hook on the game thread:
  pause_set(0, "closed: QUIT TO MENU"), UI state at 0x8359E490 (+16544 = 0; single player: +16548 = 1, +0 = field
  count, ui_game_restart 0x82248888, menu_open 0x8223CD88; link: values_read, mp_leave), +14148 = 1, +16448 = 0.
  Exit: g_scud_request_quit makes x360_pad_exit_chord say yes, so the game saves and quits as the menu's EXIT.
  Console: from a race to MAIN MENU; Exit -> home menu in ~1 s, scudrace.nv rewritten, no crash report.
- Thorhax intro (app/thorhax_intro, mk9-nx's) built in: CMake thorhax_intro_add(scud), Start() first in
  ScudApp::OnPreSetup. The tree's thorhax_intro.mpg is already v13 (make_intro.sh on thorhax_port_intro_v13.mp4 gives
  the same bytes). Console: intro shown, then the game's disclaimer.
- app/icon.jpg: scud_newicon.jpg (496x384) fitted into 256x256 on a blurred copy of itself (the old one in the session
  scratchpad).
