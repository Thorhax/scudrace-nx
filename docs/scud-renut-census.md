# Renut's Direct3D hooks: MK9 addresses to Scud Race

Written 2026-11-03, from the Scud image's map (`assets/game_root/default.map`, XDK 2.0.21256) and the generated code
(`tools/scud_asm.py`). Scope: the Renut files that are compiled (`app/src/renut_engine/gpu/`: d3d/hooks/{device,draw,
resource,shader}.cpp, d3d/{d3d.h,d3d_native.h,d3d_native_state.cpp}, sa2_renut_pacing.cpp, banjo_shared_hooks.h,
render/, backend/, guest/{callbacks,command_buffers,guest_records,sa2_screen}.cpp). No source file was changed.

Key facts behind the tables:

- Scud's CDevice layout is SA2's. Every SA2 value annotated in the sources (`[SA2 x]`) that was checked matches a Scud
  instruction (table 2). In MK9 terms: every private field from the bound objects (+12112 on) is **+128** in Scud;
  the block 11008-11072 is unchanged.
- The live hook set is the SA2/MK9 one: `Sa2CreateDevice` (the original's type-2 device plus the hardware path's
  non-GPU parts), ring/fence hooks, draws, ClearF, Resolve, XGRegister*, CreateVertexDeclaration, DestroyResource.
  The **N&B-native code** still compiled in device.cpp, draw.cpp, resource.cpp and d3d_native_state.cpp
  (`Direct3D_CreateDevice_hook`, `InitializeApiState`, the command-buffer, tiling, gamma, caps, resource-creation
  hooks, `native::Clear/Resolve/SetFence`, `InitializeStateTables`, the memexport helpers) is **never registered**
  in this build. Its guest constants are **N&B's addresses, not MK9's** (mk9-nx never changed them). They are listed
  so nothing is missed. They don't matter at run time, but a `REX_IMPORT`/`REX_EXTERN` naming a function that
  doesn't exist in Scud still breaks the link.
- Scud's image ends near 0x825C....: **no MK9 address 0x82B.../0x82C... exists in Scud**, so every hook macro below
  must change, or the build has unresolved `__imp__sub_X` symbols / dead overrides.

## 1. Guest addresses used in code

Status: **OK** = verified from Scud code or data. **dead** = only reached from N&B-native code that isn't registered
(the value still has to be valid for the link, or be removed). **none** = no counterpart.

### d3d/hooks/device.cpp

| file:line | MK9 value | What | Scud value | Evidence | Status |
|---|---|---|---|---|---|
| device.cpp:1377, 1417, 1496 | sub_82BF6108 | Direct3D_CreateDevice (RAW hook + original) | **8235C728** | map `Direct3D_CreateDevice`. r4 type, r6 behavior, r7 params, r8 out (`mr r29,r4 / mr r30,r6 / mr r28,r7 / mr r27,r8`). `li r3,24704` MemAllocAligned. Type 2: `ori 11068,0x80`, MarkAsOutOfMemory, InitializeCommandBufferDevice | OK |
| device.cpp:1378, 1451 | sub_82C014C8 | D3D::InitializePresentationParameters (CDevice*, D3DPRESENT_PARAMETERS*) | **8236C668** | map `?InitializePresentationParameters@D3D@@YAHPAVCDevice@1@PBU_D3DPRESENT_PARAMETERS_@@@Z`. Stores 15228/15232/15236 | OK |
| device.cpp:1379, 1465 | sub_82C01260 | D3D::InitializeApiState (CDevice*) | **8236BC98** | map `?InitializeApiState@D3D@@YAXPAVCDevice@1@@Z` | OK |
| device.cpp:1384, 1458 | sub_82BF5F90 | MK9 extra: query block (+24272..24288) | **8235C5B0** `?InitializeAsyncResources@D3D` | Writes 24400/24404/24408 (= MK9 +128). Called from InitializeHardwareDevice 8236C8E8, `cmpwi r3,0; beq fail` (nonzero = success) | OK |
| device.cpp:1385, 1458 | sub_82BF57A8 | MK9 extra: block +24296..24328 | **8235C0F8** `?InitializeAsyncCommandBuffers@D3D` | Writes 24424/24432/24448/24452 (= MK9 +128). Same success test | OK |
| device.cpp:1386, 1458 | sub_82BEB450 | MK9 extra: shader-state buffers +14056..14064, +15092/15096 | **82355168** `?InitializeGprAllocation@D3D` | Writes 14184/14188/14192, 15220/15224 (= MK9 +128 exactly). Same success test | OK |
| device.cpp:1499 | sub_82BFD090 | D3DDevice_InsertCallback (dev, type, cb, ctx) | **82364200** | map `D3DDevice_InsertCallback`. r4..r6 moved to the internal `?InsertCallback@D3D` 82363588 | OK |
| device.cpp:1500 | sub_82BF6928 | D3D::SynchronizeToPresentationInterval (CDevice*, BOOL) | **82350DC0** (not the wrapper) | map `?SynchronizeToPresentationInterval@D3D@@YAXPAVCDevice@1@K@Z`. Reads 14000, 11028. `lbz 11069; ori 16; stb 11069` (the only setter of 0x10; Swap `andi. 239`). The public `D3DDevice_SynchronizeToPresentationInterval` 82351278 is `li r4,0; b 82350DC0`, and Present 823519E8 calls 82350DC0 directly, so hook the internal one | OK |
| device.cpp:1501 | sub_82BFCD60 | CDevice::KickOff | **82363ED8** | map `?KickOff@CDevice@D3D@@QAAPAKXZ`. Reads 11024, 13600, 11068; SetFence path `11069 & 2`, BlockOnFence(11036-2), `ori 11069,2`. Returns `lwz r3,48(r31)` | OK |
| device.cpp:1502 | sub_82BFCBD8 | CDevice::KickOffSegment | **82363B98** | map `?KickOffSegment@CDevice@D3D@@QAAXXZ`. Reads 13920, 13600, 13760 list, 15332. Ends in StartNewSegment. **Only caller is KickOff** (hooked), so this hook is effectively unreachable | OK |
| device.cpp:1503 | sub_82BFCFA0 | CDevice::BeginRingBig (dev, dwords) | **82364110** | map `?BeginRingBig@CDevice@D3D@@QAAPAKK@Z`. KickOff, then StartNewSegment, returns 48(r31) | OK |
| device.cpp:1504 | sub_82BFB948 | D3D::RingBufferDeviceAllocate (dev, flags, DWORD* size, align) | **823628C8** | map `?RingBufferDeviceAllocate@D3D@@YAPAXPAVCDevice@1@KPAKK@Z`. Callers: BeginRingAlloc, StartNewSegment | OK |
| device.cpp:1505 | sub_82BFBB78 (SHARED) | AllocateInlineData (dev, dwords, alignment) | **82362D10** `?BeginRingAlloc@CDevice@D3D@@QAAPAKKK@Z` | BeginVertices: `mr r4,r22 (dwords); li r5,16; bl 82362D10`. Reads 11069 & 0x20, 13920, then RingBufferDeviceAllocate / CommandBufferDeviceAllocate / GrowableAllocate; 13600; sets 11069 bit 0x20 on failure | OK |
| device.cpp:1506 | sub_82BFC0B0 | ring segment allocator (dev, dwords) | **82362DF8** `?StartNewSegment@CDevice@D3D@@QAAPAKK@Z` | Callers are exactly Swap, KickOffSegment, BeginRingBig (as the census says for MK9). Side buffer 17136, 11069 bit 0x20 | OK |
| device.cpp:1507 | sub_82BFD0E8 | D3DDevice_SetRingBufferParameters (dev, params) | **82364258** | map. Called from InitializeHardwareDevice (`addi r4,r29,72`) and UninitializeDevice | OK |
| device.cpp:1508 | sub_82BF6D58 (SHARED) | D3DDevice_Swap (dev, front, scaler params) | **82351280** | map `D3DDevice_Swap`. `mr r28,r4 / mr r21,r5`. 16968++, `stw 11056`, `stw 15344`, `andi. 11069,239`, `addi 15408` (gamma). Already hooked by `app/src/scud_frame_counter.cpp` (see risks) | OK |
| device.cpp:1509 | sub_82BFBC60 | CDevice::BlockOnFence (dev, fence, type, resource, nokick) | **82362AF8** | map `?BlockOnFence@CDevice@D3D@@QAAXKW4_D3DBLOCKTYPE@@PAUD3DResource@@_N@Z`. nokick `clrlwi. r11,r7,24`, 13600 test, KickOff, CBlocker loop | OK |
| device.cpp:1510 | sub_82BF5680 (SHARED) | CBlocker::Check (this) | **8235BFC8** | map `?Check@CBlocker@D3D@@QAAHXZ` | OK |
| device.cpp:1511 | sub_82BFCEA0 | D3DDevice_IsFencePending (fence) | **82364018** | map. Device from `lwz r11,1436(0x82000000)` (VdGlobalDevice, or +1440 VdGlobalXamDevice if process type 2); KickOff if fence == 11036; 11024 compare | OK |
| device.cpp:127 | kGlobalQwordVa 0x8339BF90 | global qword copied to +11008 | **0x825C3FD0** `?D3DVERSION@D3D@@3_KB` | InitializeCommonDeviceState 8236C350: `lis r11,-32164; ld r11,16336(r11); std r11,11008(r3)` | dead (N&B create hook only; the original does it in Scud) |
| device.cpp:136 | kVdGlobalDeviceImportSlot 0x82000658 | import slot of VdGlobalDevice | **0x8200059C** | map `__imp_VdGlobalDevice 8200059c`. InitializeHardwareDevice `lis r11,-32256; lwz r11,1436(r11); stw r31,0(r11)`. Live: `Sa2CreateDevice`, `native::GlobalDevice` (IsFencePending hook) | OK |
| device.cpp:1067 | kDeviceCapsVa 0x82183B00 | N&B's constant D3DCAPS9 (GetDeviceCaps) | none | Scud links no `D3DDevice_GetDeviceCaps` (not in the map). The hook that reads it isn't registered | none / not needed |
| device.cpp:1069 | kFusionVa 0x82F597B4 | "Fusion GPU" flag | **0x834C8B50** `__Fusion` | map (d3d9:resource.obj). Scud has no `IsFusion` function; the reader `D3D_IsFusion_hook` isn't registered | dead |

### d3d/hooks/draw.cpp

| file:line | MK9 value | What | Scud value | Evidence | Status |
|---|---|---|---|---|---|
| draw.cpp:52 | kFullViewport 0x820018DC | D3DVIEWPORT9 {0,0,0xFFFF,0xFFFF,0,1} | **0x82099760** `?g_DefaultViewport@D3D` | Image words: 0, 0, 0xFFFF, 0xFFFF, 0, 0x3F800000 | dead (only N&B's tiling/present paths) |
| draw.cpp:620 | sub_82BF09E0 (SHARED) | D3DDevice_DrawVertices (dev, prim, start, count) | **8235A058** | map. `mr r25,r4 / r23,r5 / r24,r6` | OK |
| draw.cpp:621 | sub_82BF0DF8 (SHARED) | D3DDevice_DrawIndexedVertices (dev, prim, base, start, count) | **8235A470** | map. `mr r18,r4 / r17,r5 / r21,r6 / r19,r7`. Game callers: be_draw, be_run, st_controls | OK |
| draw.cpp:622 | sub_82BF03B8 | D3DDevice_DrawVerticesUP (dev, prim, count, data, stride) | **8235A010** | map. BeginVertices(r3,r4,r5,stride=r7), memcpy(data r6, count*stride), `lwz 13844; stw 48` | OK |
| draw.cpp:623 | sub_82BEFEF8 (SHARED) | D3DDevice_BeginVertices (dev, prim, count, stride) | **82359B50** | map. `stb r10,12904` (stride/4), inline end `stw r11,13844` (also 13848/13856). **No game caller**: only DrawVerticesUP and ValidateStereo3DBlankRegion (from Swap) | OK |
| draw.cpp:624 | sub_82BF0940 | D3DDevice_DrawIndexedVerticesUP | none | Not in Scud's map (no `IndexedVerticesUP`, no `BeginIndexedVertices`). Linker dropped them | none |
| draw.cpp:628, 636 | sub_82BEF680 | ClearF (dev, flags, rect, color, f1 Z, r8 stencil) | **82359948** `D3DDevice_ClearF` | Public wrapper (callers: D3DDevice_Clear, **D3DDevice_BeginTiling**). Null rect becomes the bound target or depth (12816/12832) or the tiling extent (13556/13560); then `bl 823592C8` (D3D::ClearF, which takes `mr r22,r8` stencil, `fmr f31,f1` Z). r7 is clobbered here, so stencil is in r8 as in MK9 | OK |
| draw.cpp:646, 659 | sub_82BFB4C8 | D3DDevice_Resolve, stack args | **82362400** | map. `stwu r1,-160`, `lwz r30,260(r1); lwz r29,252(r1)` = caller's +100 / +92, as MK9 (frame 160, 252/260). Tail-calls D3DDevice_ResolveEx 823612A8. Callers: Present, **EndTiling**, game rt_resolve etc. | OK |

### d3d/hooks/resource.cpp

| file:line | MK9 value | What | Scud value | Evidence | Status |
|---|---|---|---|---|---|
| resource.cpp:50 | `__imp__sub_823667E0` (GameXMemFree) | the game's XMemFree(ptr, attrs) | **8234ABF0** `XMemFree` (xapilib) | `rlwinm. r11,r4,0,0,0` (physical bit) to XPhysicalFree 8234B6D8, else XMemHeapFree. Scud has no game-side XMemFree override. 0x823667E0 isn't a function in Scud (it falls inside D3D's perf-counter code), so the link fails as written | dead path, **link-relevant** |
| resource.cpp:97 | kGameNoLargePagesVa 0x82F39E34 | XPhysicalAlloc drops MEM_LARGE_PAGES while set | **0x834C8B30** (no map symbol) | XPhysicalAlloc 8234B5E0: `lis r11,-31923; lwz r11,-29904(r11); cmpwi; rlwinm r5,r5,0,3,1` (clears 0x20000000) | dead |
| resource.cpp:122 | kFormatTable 0x820052F8 | GPU format table, u16 per format | **0x82099138** `?g_FormatTable@D3D@@3QBGB` | u16 entries: 0x0001, 0x0001, 0x2008, 0x2010, ..., 0x3020 (bpp in the low byte, as N&B) | dead in resource.cpp |
| resource.cpp:134 | kHiZOwnerVa 0x82FA36C8 | HiZ owner word | **0x834C8B34** `?g_HiZAllocated@D3D@@3HA` | map (d3d9:texture.obj) | dead |
| resource.cpp:1696, 1704 | sub_82BE5318 | D3DResource_Destroy (resource) | **8235B630** `?DestroyResource@D3D@@YAXPAUD3DResource@@@Z` | Only caller: D3DResource_Release 8235B8D8 (17 game callers). Calls BlockOnFence | OK |

### d3d/hooks/shader.cpp

| file:line | MK9 value | What | Scud value | Evidence | Status |
|---|---|---|---|---|---|
| shader.cpp:479, 481 | sub_82BEB740 | XGRegisterVertexShader (shader, physical) | **82355388** | map. `stw r30,32(r31)` (physical at +0x20), container at `872(r31)` (+0x368). Caller: D3DDevice_CreateVertexShader 82355480 (copies the container to +872 first). Game calls Create* (make_shaders, present_pass_init) | OK |
| shader.cpp:486, 488 | sub_82BE9B98 | XGRegisterPixelShader (shader, physical) | **823537F0** | map. `stw r4,24(r3)` (+0x18). CreatePixelShader 82355298 copies the container to `addi r3,r31,40` (+0x28) | OK |
| shader.cpp:493, 495 | sub_82BEA068 | D3DDevice_CreateVertexDeclaration (elements) returns decl | **82353D50** | map. Walks 12-byte elements until `lhz Stream == 255`, XMemAlloc, XGSetVertexDeclaration, returns r31 | OK |
| shader.cpp:508, 510 | sub_82BFF878 | D3DVertexShader_Bind | none | Not in Scud's map. The game binds declarations with SetVertexDeclaration (+12240) | none |

### d3d/d3d_native_state.cpp (all N&B values; every reader is N&B-native, unregistered)

| file:line | MK9 value | What | Scud value | Evidence | Status |
|---|---|---|---|---|---|
| d3d_native_state.cpp:730 | kRenderStateTable 0x82E506F0 | {getter, setter, default} x 101 | **0x825C3A20** `?g_RenderStateInit@D3D` | 12-byte entries; entry 100 = {8235D200, 8235D1E0 `D3DDevice_SetRenderState_Buffer2Frames`, 1} | dead (InitializeStateTables) |
| d3d_native_state.cpp:731 | kSamplerStateTable 0x82E50BD0 | {getter, setter, default} x 20 | **0x825C3EE0** `?g_SamplerStateInit@D3D` | 20 x 12 = 0xF0 ends at 0x825C3FD0 (D3DVERSION) | dead |
| d3d_native_state.cpp:1365 | kFormatTable 0x820052F8 | as resource.cpp:122 | **0x82099138** | as above | dead (SetDepthClear) |
| d3d_native_state.cpp:1612 | kSkipExportNanVertex 0x82FAD204 | skip switch of DoBeginExportNanVertex | **0x834C8B5C** `D3D__NoExtraVertexInBeginExport` | DoBeginExportNanVertex 8235ADF0: `lis r11,-31923; lwz r11,-29860(r11); cmpwi; bne skip` | dead |
| d3d_native_state.cpp:1613 | kSkipExport512Pixels 0x82FAD208 | skip switch of DoEndExport512Pixels | **0x834C8B60** `D3D__NoExtraVerticesInEndExport` | DoEndExport512Pixels 8235AF48: `lwz r11,-29856(r11)` | dead |

### banjo_shared_hooks.h

| file:line | MK9 | Scud | Note |
|---|---|---|---|
| banjo_shared_hooks.h:8 | renut_native_sub_82BFBB78 | renut_native_sub_82362D10 | AllocateInlineData = BeginRingAlloc |
| banjo_shared_hooks.h:9 | renut_native_sub_82BF5680 | renut_native_sub_8235BFC8 | CBlocker::Check |
| banjo_shared_hooks.h:10 | renut_native_sub_82BEFEF8 | renut_native_sub_82359B50 | BeginVertices |
| banjo_shared_hooks.h:11 | renut_native_sub_82BF09E0 | renut_native_sub_8235A058 | DrawVertices |
| banjo_shared_hooks.h:12 | renut_native_sub_82BF0DF8 | renut_native_sub_8235A470 | DrawIndexedVertices |
| banjo_shared_hooks.h:13 | renut_native_sub_82BF6D58 | renut_native_sub_82351280 | Swap |

### Other compiled files

render/*, backend/*, guest/{callbacks,command_buffers,guest_records,sa2_screen}.cpp and sa2_renut_pacing.cpp hold **no
guest addresses in code**. Their hex addresses are all in comments, and guest_records uses heap lookups only.
sa2_screen.cpp returns 1280. The only device offset in sa2_renut_pacing.cpp is in table 2.

## 2. Device offsets

✓ = Scud uses the SA2 value (instruction cited). All the "+128 from MK9" offsets hold.

| Constant (file:line) | MK9 | SA2 (annotation) | Scud | Evidence in Scud |
|---|---|---|---|---|
| kDeviceAllocSize (d3d.h:229) | 0x5F80 | 0x6080 | 0x6080 ✓ | Direct3D_CreateDevice `li r3,24704; li r4,128; bl MemAllocAligned` |
| kDeviceOffsetCreationThreadId (d3d.h:282) | 11016 | same | 11016 ✓ | InitializeCommonDeviceState 8236C350 `stw r3,11016(r31)` after GetCurrentThreadId |
| kDeviceOffsetIsTitleProcess (d3d.h:283) | 11020 | same | 11020 ✓ | same function, `stw r11,11020(r31)` |
| kDeviceOffsetBehaviorFlags (d3d.h:284) | 24264 | 24392 | 24392 ✓ | CreateDevice `stw r30,24392(r31)` (default `oris 0x0C00` unless 0x100 / 0x3F000000, as the hook) |
| kDeviceOffsetVertexDeclaration (d3d.h:289) | 12120 | 12240 | 12240 ✓ | D3DDevice_SetVertexDeclaration 82353C48 `stw r4,12240(r3)` |
| kDeviceOffsetIndexBuffer (d3d.h:290, d3d_native.h:141) | 12684 | 12812 | 12812 ✓ | D3DDevice_SetIndices 8235EFC8 `stw 12812(r31)` |
| kDeviceOffsetRenderTargets (d3d.h:291, d3d_native.h:142) | 12688 | 12816 | 12816 ✓ | SetRenderTarget 8235F750 reads 12816..12828; ClearF `lwz r7,12816(r3)` |
| kDeviceOffsetDepthStencil (d3d.h:292, d3d_native.h:143) | 12704 | 12832 | 12832 ✓ | SetDepthStencilSurface 8235FAE0 `stw 12832(r3)`; GetDepthStencilSurface `lwz 12832(r3)` |
| kDeviceOffsetStreamSources (d3d.h:293, d3d_native.h:144) | 12708 | 12836 | 12836 ✓ | SetStreamSource 8235EEA8 `addi r11,r29,3209; rlwinm <<2; lwzx` (3209*4) |
| kDeviceOffsetStreamStrides (d3d.h:294, d3d_native.h:145) | 12776 | 12904 | 12904 ✓ | BeginVertices `stb r10,12904(r3)` (stride>>2); SetStreamSource `stb 12904(r11)` |
| kDeviceOffsetTextures (d3d.h:295, d3d_native.h:146) | 12792 | 12920 | 12920 ✓ | D3DDevice_SetTexture 82350160 `addi r11,r4,3230` (*4) |
| kDeviceOffsetPixelShader (d3d.h:296) | 12940 | 13068 | 13068 ✓ | D3DDevice_SetPixelShader 82353828 `stw 13068(r30)` |
| kDeviceOffsetVertexShader (d3d.h:297) | 12944 | 13072 | 13072 ✓ | SetVertexShader 82353A30 `stw 13072(r30)`; BeginVertices `lwz r11,13072(r3)` |
| kDeviceOffsetPredication (d3d.h:310), off::Predication (d3d_native.h:147) | 12956 | 13084 | 13084 ✓ | InitializeApiState `stw 13084`; BeginTiling `stw r9,13084(r31)` |
| off::RunPredicationState (d3d_native.h:148) | 12960 | 13088 | 13088 ✓ | InitializeApiState / BeginTiling `stw 13088` |
| off::PredicationSet (d3d_native.h:149) | 12964 | 13092 | 13092 ✓ | InitializeApiState / BeginTiling `stw 13092` |
| kDeviceSnapshotSize (d3d.h:317) | 12960 | 13088 | 13088 | derived (Predication + 4) |
| kDeviceOffsetCommandBuffer (d3d.h:313, d3d_native.h:155) | 13792 | 13920 | 13920 ✓ | BeginRingAlloc `lwz r3,13920(r31)`; KickOffSegment/StartNewSegment `lwz 13920` |
| kDeviceOffsetVerticesRingEnd (d3d.h:323) | 13716 | 13844 | 13844 ✓ | BeginVertices `stw r11,13844(r31)`; EndVertices `lwz r11,13844(r3); stw r11,48(r3)`; DrawVerticesUP same |
| kDeviceOffsetSwapCount (d3d.h:326) | 16840 | 16968 | 16968 ✓ | Swap `lwz/stw 16968(r31)` (+1) |
| kDeviceOffsetFenceTag (d3d.h:327; off::FenceTag) | 11036 | same | 11036 ✓ | KickOff `lwz r11,11036; addi r4,r11,-2`; IsFencePending |
| kDeviceOffsetSwapFenceTag (d3d.h:328) | 11056 | same | 11056 ✓ | Swap `stw 11056(r31)` |
| kDeviceOffsetPresentFenceTag (d3d.h:329) | 15216 | 15344 | 15344 ✓ | Swap `stw 15344`; StartNewSegment `lwz r4,15344; BlockOnFence; stw 0,15344` |
| kDeviceOffsetSwapFlags / kSwapFlagPending (d3d.h:330-331) | 11069 / 0x10 | same | ✓ | SyncPI `ori r11,r11,16; stb 11069`; Swap `andi. r11,r11,239` |
| kDeviceOffsetFlags / NoHardware 0x80 (device.cpp:107) | 11068 | same | ✓ | CreateDevice type 2 `ori r11,r11,128; stb 11068` |
| kDeviceOffsetFlags2 / ImmediateFences 0x02 (device.cpp:112) | 11069 | same | ✓ | KickOff `rlwinm. 11069 bit 0x2`, `ori r11,r11,2` |
| Flags2 bit 0x20 out-of-memory (device.cpp:1429, 1481) | 11069 | same | ✓ | MarkAsOutOfMemory 82362450 `ori r9,r9,32; stb 11069` |
| kDeviceOffsetFenceCompletion (device.cpp:115; off::FenceCompletion) | 11024 | same | 11024 ✓ | KickOff/BlockOnFence/IsFencePending `lwz r11,11024; lwz r11,0(r11)`; hardware path `stw r3,11024` (128 B) |
| swap scratch word (device.cpp:1432, literal) | 11028 | same | 11028 ✓ | hardware path `stw r3,11028` (32 B); SyncPI `lwz r11,11028(r31); addi 4` |
| kDeviceOffsetInKickOff (device.cpp:118) | 13472 | 13600 | 13600 ✓ | KickOff `lwz r11,13600`; BlockOnFence `lwz r11,13600(r3)`; BeginRingAlloc. **Also written by Start/StopWorkerQueue** (risks) |
| kDeviceOffsetSideBuffer (device.cpp:120; off::SideBuffer d3d_native.h:156) | 17008 | 17136 | 17136 ✓ | InitializeCommonDeviceState `li r3,4800; bl XMemAlloc; stw r3,17136` (4800 = kSideBufferSize 0x12C0) |
| kDeviceOffsetsMinusOne (device.cpp:123) | 21840, 21844, 15212 | 21968, 21972, 15340 | ✓ | InitializeCommandBufferDevice 8236C3C0 `stw -1` at 21968, 21972, 15340; InitializeHardwareDevice 21968/21972 |
| kDeviceOffsetGlobalQword (device.cpp:126) | 11008 | same | 11008 ✓ | `std r11,11008(r3)` from D3DVERSION |
| kDeviceOffsetCriticalSection0/1 (device.cpp:130-131) | 15220 / 15248 | 15348 / 15376 | ✓ | InitializeHardwareDevice `addi r3,r3,15348` / `addi r3,r31,15376`, RtlInitializeCriticalSection |
| kDeviceOffsetPerfFrequency (+4 reciprocal) (device.cpp:132) | 21868 | 21996 | 21996 ✓ | InitializeHardwareDevice `stfs f13,21996`, `fdivs; stfs f0,22000` |
| kDeviceOffsetTitleMinusOne (device.cpp:133) | 23380 (N&B, unannotated) | n/a | UNVERIFIED | N&B-native create path only (41 words). Not in Scud's InitializeHardwareDevice |
| kSa2DeviceOffsetGammaRamp (device.cpp:919) | 15280 | 15408 | 15408 ✓ | D3DDevice_SetGammaRamp 8235F120 `addi r31,15408` (memcpy dest); Swap `addi r26,r31,15408` |
| kDeviceOffsetPresentationInterval (sa2_renut_pacing.cpp:55) | 13872 | 14000 | 14000 ✓ | SyncPI 82350DC0 `lwz r11,14000(r31)`, mapped 0/1 to 1, 2 to 2, 4 to 3 (same as RefreshesPerFrame) |
| off::ScissorRect (d3d_native.h:140) | 12924 | 13052 | 13052 ✓ | D3DDevice_SetScissorRect 8235EDB0 `stw 13052..13064`; Present `lwz 13052..13064` |
| off::TilingSurfaces (d3d_native.h:150) | 12976 | 13104 | 13104 ✓ | D3DDevice_ClearF compares 13104..13120 with 12816..12832; BeginTiling `stw 13104..13120` |
| off::TileCount (d3d_native.h:151) | 12996 | 13124 | 13124 ✓ | BeginTiling `stw r5,13124(r3)` |
| off::TileRects (d3d_native.h:152) | 13000 | 13128 | 13128 ✓ | BeginTiling `addi r10,r3,13124` then `stw r9,4(r10)` (first rect at 13128) |
| off::TileOrigins (d3d_native.h:153) | 13240 | 13368 | 13368 ✓ | BeginTiling `addi r8,r3,13364` (pre-incremented); StartWorkerQueue `lwz 13368/13372` |
| off::TilingExtent (d3d_native.h:154) | 13428 | 13556 | 13556 ✓ | BeginTiling `stw 13556 / 13560`; D3DDevice_ClearF `lwz 13556/13560` |
| off::AutoDepthStencil (d3d_native.h:157) | 15100 | 15228 | 15228 ✓ | InitializePresentationParameters `stw 15228(r29)` |
| off::AutoFrontBuffer (d3d_native.h:158) | 15104 | 15232 | 15232 ✓ | IPP `stw 15232`; Present `lwz r4,15232(r31); bl D3DDevice_Swap` |
| off::AutoBackBuffer (d3d_native.h:159) | 15108 | 15236 | 15236 ✓ | IPP `stw 15236`; D3DDevice_GetBackBuffer `lwz 15236(r3)` |
| off::CommandBufferTagMask (d3d_native.h:133) | 11040 | same | 11040 ✓ | SetIndices / SetStreamSource / SetPixelShader `lwz 11040` |
| off::TilePredication / RunPredication (d3d_native.h:134-135) | 11060 / 11064 | same | ✓ | InitializeApiState and BeginTiling `stw 11060`, `stw 11064` |
| off::Flags..Flags3 (d3d_native.h:136-139) | 11068-11071 | same | ✓ | 11070: SetVertexShader `lbz/stb 11070`; 11071: SetRenderTarget `lbz/stb 11071` |
| off::Ring / RingLimit / RingGuarantee (d3d_native.h:105-107) | 0x30/0x34/0x38 | same | ✓ | BeginRingBig `lwz 48 / lwz 52`; BeginVertices `lwz 56 / lwz 48`; MarkAsOutOfMemory `stw 48/52/56` |
| m_ReferenceCount 0x3C (d3d.h:237; device.cpp:1065) | 0x3C | same | ✓ | D3DDevice_Release `addi r11,r3,60` + lwarx/stwcx |
| PredicatedPending2 0x28 (d3d_native.h:108) | 0x28 | same | ✓ | BeginVertices `ld r11,40(r31)` with the pending masks 0..32 |
| off::TextureFetch 0x480 / VertexFetch 0x6F0 | same | same | ✓ | SetStreamSource `addi r4,r11,222; rlwinm <<3` (222*8 = 0x6F0) |
| VS/PS float constants 1920 / 6016 (device.cpp:984-988 diag) | same | same | ✓ | Draw* `addi r6,r3,1920; li r5,16384` and `addi r6,r31,6016; li r5,17408` |
| Public packets (0x2880 Destination, 0x28C0 Window, 0x28CC Values, 0x2920 Program, 0x2934 Control, 0x2964 Tess, 0x29B8 Misc, 0x2A50 Point, 0x2780 bools = draw.cpp:4487's 10112) | same | same | ✓ | BeginVertices/Draw* SetPending_RenderStates bases `addi r6,r31,10368 / 10444 / 10528 / 10548 / 10596 / 10680 / 10832 / 10112`; SetScissorRect `stw 10436/10440` (0x28C4/0x28C8) |
| kDeviceOffsetTessellation* 0x2978/0x297C/0x2980 (d3d.h:301-303) | same | same | not checked (public area, inside m_TessellatorPacket 0x2964, whose base is verified) |
| kDeviceOffsetOwnerThread 0x2A88 (device.cpp:1066) | N&B | n/a | Scud uses **11016** | D3DDevice_AcquireThreadOwnership 8235C3A8 `stw r3,11016(r31)`. N&B-only hook, unregistered |
| kDeviceOffsetGammaRamp 0x3A9C (device.cpp:1217) | N&B | 15408 | 15408 | N&B-only gamma hooks (unregistered). Scud's ramp is +15408 (row above) |
| N&B raw private offsets in unregistered code: device.cpp 0x2E2C, 0x2FA0-0x2FE8, 0x3090, 0x351C, 0x359C, 0x34E8-0x3528, 0x4144, 0x5428-0x5434, 0x5DAC, 16712, 21560/21564 (lines 195-363, 449-450); 0x3338, 0x3374, 0x3378, 0x3384, 0x31A0, 0x31B0, 0x3A40-0x3A50, 0x34D0-0x34E4, 0x2E4C (723-840); 0x34E8/0x34EC/0x34F0/0x5434 (1110-1113); 0x5D90/0x5D94 (1224-1229); 0x541C, 0x3A48 (1345-1348); d3d_native_state.cpp 0x2F9B (1033), 0x3374 (1204), 0x34C0-0x34C8 (1607-1609); draw.cpp 0x3390, 0x33A0, 0x33A4 (419-421) | N&B | n/a | UNVERIFIED / not needed | Only N&B-native functions use them (`Direct3D_CreateDevice_hook`, Begin/EndCommandBuffer, ConfigureForOnePassZPass, GetDisplayMode, StoreGamma, `native::SetFence`, memexport, tiling). None is registered. If any is ever enabled for Scud, each needs re-deriving (N&B's private area is laid out differently: e.g. N&B's vertex declaration 0x2E2C = 11820 vs Scud 12240) |

## 3. Risks

1. **Shared hooks have no outer hook in Scud.** `BANJO_RENUT_HOOK_SHARED` only defines `renut_native_sub_X`; the
   game-side `REX_HOOK` has to call it. MK9 had those in `src/me/mk9_me_hooks.cpp` (BeginVertices, DrawVertices,
   DrawIndexedVertices), `mk9_gpu_wait.cpp` (CBlocker::Check) and `mk9_frame_counter.cpp` (Swap). Scud's build has only
   `src/scud_frame_counter.cpp`, which hooks **sub_82351280 (Swap) with `REX_HOOK_RAW` and calls only the original**.
   As things stand, Renut's DrawVertices, DrawIndexedVertices, BeginVertices, CBlocker::Check and Swap would never
   run. Either make them plain `BANJO_RENUT_HOOK`, or add outer hooks. For Swap, change scud_frame_counter.cpp to call
   `renut_native_sub_82351280` when `Active()`; a second `REX_FUNC(sub_82351280)` would be a duplicate symbol.
   AllocateInlineData (82BFBB78) was SHARED with no outer hook even in MK9, so it never ran there. Scud's
   BeginRingAlloc falls through to the hooked RingBufferDeviceAllocate, as MK9's did.
2. **Scud's game uses predicated tiling.** The game's `tl_open`/`tl_close` (called from be_draw, be_run,
   be_clear_depth, be_viewport, be_state, be_end_, st_aa_*) call `D3DDevice_BeginTiling` 82360050 /
   `D3DDevice_EndTiling` 823603C0. Neither is hooked in the MK9/SA2 set, so Scud's own tiling code runs on the
   no-hardware device. In this XDK (it has XPS worker queues):
   - BeginTiling → `StartWorkerQueue` 82373F10 calls the hooked KickOff and then **sets the "in KickOff" word +13600
     (`stw r11,13600` from +13608)** until EndTiling → `StopWorkerQueue` 82374E68 clears it. While it's nonzero,
     Renut's BlockOnFence_hook returns **without waiting** for an uncovered fence (the original does the same, but it
     relies on the GPU). Possible use of a resource that's still busy during tiled frames. UNVERIFIED in practice
     (+13608's value is unknown).
   - StopWorkerQueue → `QueueIndirectBuffer` 82363998 → `KickOffOnWorkerQueue` 82375050 (`KeInsertQueueDpc` on the
     DPC at +11464) when +21940/+23768 and behavior flag 0x100 allow it. Those fields and the DPC/worker thread are
     set up by `InitializeXps` 82375108, which belongs to the hardware path that `Sa2CreateDevice` skips. Without it
     the path should fall to `AddCallsToPrimaryBuffer` (the ring sink). UNVERIFIED: watch for a fault in
     KeInsertQueueDpc.
   - StopWorkerQueue and InitializeXps call the **internal** `D3D::InsertCallback` 82363588, which isn't hooked. Those
     callbacks go into the ring sink and never run.
   - The tile clears and resolves do reach Renut: BeginTiling calls the public `D3DDevice_ClearF` (hook it, not
     D3D::ClearF 823592C8), and EndTiling calls the public `D3DDevice_Resolve` and `D3DDevice_InsertCallback`. Every
     draw in the bracket is queued once. The renderer has to treat tiled targets as whole (the `off::Tile*` and
     Predication fields are verified for this).
   - KickOff/KickOffSegment: in Scud, KickOffSegment's only caller is KickOff, and KickOff itself calls
     `QueueIndirectBuffer` (the worker path) before KickOffSegment. Replacing KickOff with Renut's hook removes both
     the indirect-buffer queueing and the worker kick, which is what's wanted. Keep the KickOffSegment hook only for
     safety.
3. **The Scud hardware path does more than MK9's extras.** `InitializeHardwareDevice` 8236C8E8 runs, in order:
   critical sections, an event at +24460, `DriverInitialize` (VdInitializeEngines, interrupt callback),
   VdGlobalDevice, the fence words, `ExGetXConfigSetting` → +17124, SetRingBufferParameters, **InitializeAsyncResources,
   InitializeAsyncCommandBuffers, InitializeXps (CreateThread + KeInitializeDpc), InitializeGprAllocation**,
   21968/21972 = -1, XenonGpu_Initialize, InitializePresentationParameters, InitializeApiState, +13916 = 0, a block
   of addresses at +17128/+17132/+16968/+21984 handed to the kernel, KickOff, QueryPerformanceFrequency, XBDM, TrainEDRAM,
   ResetAllState, XBMInitialize. The port keeps MK9's three (mapped above) and skips InitializeXps. It also calls IPP
   **before** them, where Scud calls IPP after. Scud's IPP calls CreateTexture, CreateSurface, SetRenderTarget,
   SetDepthStencilSurface, SetGammaRamp/SetPWLGamma, SetScalerParameters and the Stereo3D Xam buffer, and writes
   +17116 and +21952..21960. That it works on the type-2 device is UNVERIFIED.
4. **SynchronizeToPresentationInterval:** hook the internal `D3D::SynchronizeToPresentationInterval` 82350DC0, not the
   public 82351278 (`li r4,0; b 82350DC0`). Present calls the internal one directly, and so do SimpleSwap/ComplexSwap
   (only from the hooked Swap). The public one's only caller is the XPS WorkerThread.
5. **ClearF and Resolve shapes:** the MK9 ClearF hook is at the public wrapper (82359948), which substitutes a rect
   when the caller passes none. The hook reads r5 before that, so "no rect" means the whole target (as in MK9). Z is in
   f1 and stencil in r8 (r7 is overwritten in the wrapper). Resolve's ClearStencil and pParameters are at the
   caller's +92 and +100, as MK9.
6. **IsFencePending** in Scud reads `VdGlobalXamDevice` (0x820005A0) when `KeGetCurrentProcessType()==2`. Renut's
   `native::GlobalDevice` uses only VdGlobalDevice (0x8200059C). Fine for the title process.
7. **BlockOnFence** in Scud is (dev, fence, type, D3DResource*, nokick in r7). The hook's 4th parameter (`no_kick`)
   is actually the resource pointer. It's ignored, so it's harmless; just don't start using it.
8. **Absent functions:** DrawIndexedVerticesUP, BeginIndexedVertices and D3DVertexShader_Bind are not in Scud's map
   (confirmed), so drop those hooks. The game calls D3DDevice_Present (4 callers), Clear (9), DrawVerticesUP (6),
   DrawIndexedVertices (3) and DrawVertices (1). BeginVertices has no game caller.
9. **Link-time leftovers:** `REX_IMPORT(__imp__sub_823667E0)` (resource.cpp:50) names a non-function in Scud. Use
   8234ABF0 (XMemFree) or drop the import with the dead N&B resource path. The N&B data addresses in table 1 (dead)
   compile but point into unrelated Scud memory. Map them as given there or remove them.
10. **Device creation details:** Scud's type-2 path (`InitializeCommandBufferDevice` 8236C3C0) already runs
    MarkAsOutOfMemory, the three -1 words, XenonGpu_Initialize and InitializeApiState, so Sa2CreateDevice's
    expectations (side buffer ring, 11069 bit 0x20) hold. XenonGpu_Initialize calls KickOff, which is hooked, before
    the hook's fence word exists. The original's fence word +11024 is 0 for a type-2 device, and `FencePending`
    dereferences it through GuestPtr, so 0 → false. This should be safe, but check that KickOff_hook → KickFence
    tolerates `completion == 0` on that first call (UNVERIFIED).
