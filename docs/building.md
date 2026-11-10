# Building scudrace-nx

How the 1.0.0 release was built, on Linux with devkitPro's Docker image. This records the steps actually used; it has
not yet been run from a fresh clone, so expect to adjust paths. Nothing from the game is in the repository: you put
your own files in `assets/game_root/` and generate the code from them.

## 1. Game files

Put wanszai's 0.0.17 Xbox 360 package (`default.xex`, `default.map`, `ui/`, `icon.png`, `online.ini`) and your ROM
sets (`scud.zip`, optionally `dayto2pe.zip`) in `assets/game_root/` (see the README for the exact files). Git ignores
the folder.

## 2. SDK third-party sources and the code generator (PC)

```sh
python3 tools/fetch_thirdparty.py                     # the SDK's third-party sources (ReXGlue SDK v0.10.0)
cmake -S sdk -B sdk/out/linux-amd64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
cmake --build sdk/out/linux-amd64 --target rexglue    # tools/codegen.sh runs sdk/out/linux-amd64/rexgluerd
python3 tools/xex_image.py                            # out/default_image.bin, used by the analysis tools
```

## 3. Translate the game's code

```sh
tools/codegen.sh        # app/generated (157 files, ~78 MB) from assets/game_root/default.xex
grep -c 'case 3257:' app/generated/default/scud_recomp.*.cpp   # must find it once (jump tables of up to 4096 cases)
```

The code generator's options for this game are `app/scud_manifest.toml`, `app/scud_map.toml` (from `default.map`,
`tools/scud_map_overrides.py`) and `app/scud_overrides.toml`. A few call sites in the generated code are pointed back at
the port's hooks after generation (`tools/restore_hook_calls.py`, see `docs/scud-notes.md`).

## 4. The Switch NRO

The NRO is configured with the arguments in this form (from the release build's `out/nx_cfg_args.txt`), inside
`devkitpro/devkita64`:

```sh
P=$PWD
docker run --rm -v "$P:$P" -w "$P" -e DEVKITPRO=/opt/devkitpro devkitpro/devkita64 bash -c '
  cmake -S app -B out/nx-dk -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=$PWD/tools/switch/cmake/switch-devkitA64.cmake \
    -DREXGLUE_SWITCH_NO_NVK=ON \
    -DBANJO_DEKO3D_SPIRV_CROSS=$PWD/out/deko3d_tools/bin/spirv-cross \
    -DBANJO_DEKO3D_UAM=$PWD/out/deko3d_tools/bin/uam \
    -DBANJO_DEKO3D_GLSL_CPP=$PWD/app/out/renut_switch/deko3d_glsl_switch.cpp &&
  cmake --build out/nx-dk -j 7'
```

- `out/deko3d_tools` (spirv-cross and uam for the PC) come from `tools/deko3d_build_tools.sh`; the GLSL package from
  `tools/build_shader_library.sh` / `tools/deko3d_shader_package.py`.
- The release adds `-DSCUD_SWITCH_LTO=ON -DSCUD_PGO=use` with a profile in `pgo/` (made by a `-DSCUD_PGO=generate`
  build and two scripted races, `tools/pgo_merge.py`). The profile is not in the repository; without it, leave
  `SCUD_PGO` unset.

The result is `out/nx-dk/scud.nro`. Copy it to the SD as `/switch/scudrace-nx/scudrace-nx.nro`.

## Testing on the console

`rig/` drives a Switch over the LAN (sys-botbase, FTP, nxlink): `rig/race.sh` launches a build and runs a scripted
race; see `rig/README.md` and `rig/rig.env.example`.
