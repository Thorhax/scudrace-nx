#!/usr/bin/env bash
# Lays out the Switch SD card folder in out/nx/sdcard/switch/scudrace-nx: the NRO (the deko3d renderer's GLSL is inside
# it), the DKSH cache tools/linux/deko3d_chain.sh compiled (named after the NRO: the game reads
# <nro name>_deko3d_shaders.bin beside it), Scud's pipeline list (so the game builds the listed pipelines at start), plus
# a README. The game files are yours and stay out of it (game_root: default.xex, the ROMs, ...); so does online.ini.
# scudrace-nx (2026-11-09) from mk9-nx's.
#
#   tools/linux/package_switch.sh [build dir, default out/nx-dk-pgo-use]
#       -> out/nx/sdcard/switch/scudrace-nx and out/nx/scudrace-nx-switch.zip
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${1:-$root/out/nx-dk-pgo-use}"
nro="$build/scud.nro"
cache="$root/out/deko3d_shaders/deko3d_shaders.bin"
[ -f "$nro" ] || { echo "$nro missing: build the Switch target first (docs/building.md, Switch)" >&2; exit 1; }
[ -f "$cache" ] || { echo "$cache missing: tools/linux/deko3d_chain.sh cache" >&2; exit 1; }
[ "$cache" -nt "$root/app/out/renut_switch/deko3d_glsl.bin" ] ||
  { echo "$cache is older than the GLSL package: tools/linux/deko3d_chain.sh cache" >&2; exit 1; }
dest="$root/out/nx/sdcard/switch/scudrace-nx"
rm -rf "$root/out/nx/sdcard"
mkdir -p "$dest"
cp "$nro" "$dest/scudrace-nx.nro"  # named after its folder: the Homebrew Menu shows the folder as the game
cp "$cache" "$dest/scudrace-nx_deko3d_shaders.bin"
python3 -I -c "import lzma,sys; open(sys.argv[2],'wb').write(lzma.open(sys.argv[1]).read())" \
  "$root/shaders/scud_pipeline_list.xz" "$dest/pipeline_list.bin"
cat > "$dest/README.txt" <<'TXT'
scudrace-nx: Scud Race (the Xbox 360 build) on the Switch, test build.
Put this folder on the SD card as sdmc:/switch/scudrace-nx, and copy your own game files next to the NRO:
    sdmc:/switch/scudrace-nx/game_root/      <- default.xex and the files beside it
Launch with a title takeover (hold R while starting a game, then open the Homebrew Menu), or a forwarder with full
memory. Not from the Album: it has too little memory.
The first start writes scud.toml (your options) next to the NRO and keeps a log in logs/ (send it back after a test).
scudrace-nx_deko3d_shaders.bin holds the shaders already compiled, so the first race does not stop to compile them; a
shader it does not have is compiled the first time it is drawn and added to it for the next time.
TXT
(cd "$root/out/nx/sdcard" && rm -f ../scudrace-nx-switch.zip && python3 -I -c "
import os,zipfile
with zipfile.ZipFile('../scudrace-nx-switch.zip','w',zipfile.ZIP_DEFLATED) as z:
    for d,_,fs in os.walk('switch'):
        for f in fs: z.write(os.path.join(d,f))
")
ls -la "$dest" "$root/out/nx/scudrace-nx-switch.zip"
