#!/bin/sh
# link.sh <nro in out/> <label>: system link between the console (host) and this PC's build (join), both on the LAN.
# The console: Multiplayer -> System Link -> Host -> START (the lobby waits); the PC (tools/linux/boot_test.sh, its
# scripted pad): the splash, START, Multiplayer -> System Link -> Join. Screenshots of both, and the [net] lines.
R=$(cd "$(dirname "$0")" && pwd); . "$R/common.sh"
nro=$1; label=$2
out="$L/$label"; mkdir -p "$out"
sed "s/@PC_IP@/$PC_IP/g" "$R/configs/NL/scud.toml" > "$T/scud.toml" && echo 'scud_game_output = true' >> "$T/scud.toml"
ftp put "$T/scud.toml" /switch/scudrace-nx/scud.toml
before=$(newest_net)
tid=$(python3 "$R/sb.py" title 2>/dev/null)  # empty: the home menu (sys-botbase answers nothing there)
if [ -n "$tid" ] && [ "$tid" != "0100000000001000" ]; then close_title; fi
sb "press R" "sleep 0.5" "click A" "sleep 2.5" "click A" "sleep 5" "release R" "sleep 2" "click Y" "sleep 3"
"$NXL" -a "$SWITCH_IP" -p "${NXPATH:-scudrace-nx/scudrace-nx-test.nro}" "$P/out/$nro" > /dev/null 2>&1
sleep 35
sb "click A" "sleep 4" "click PLUS" "sleep 2" "click DDOWN" "sleep 1" "click B" "sleep 3"  # Multiplayer
sb "click DDOWN" "sleep 1" "click B" "sleep 3" "click B" "sleep 4"                           # System Link, Host
sb "click DDOWN" "sleep 0.7" "click DDOWN" "sleep 0.7" "click DDOWN" "sleep 0.7" "click DDOWN" "sleep 0.7" \
   "click DDOWN" "sleep 0.7" "click DDOWN" "sleep 0.7" "click B" "sleep 4"                   # START: the lobby
sb shot "$out/host_lobby.jpg"
"$P/tools/linux/boot_test.sh" -t 130 -s "70 95 125" -- --sa2_renderer=renut --scud_game_output=true \
  "--scud_input_script=40 A 0.3;45 START 0.3;49 DOWN 0.2;51 A 0.3;54 DOWN 0.2;56 A 0.3;59 DOWN 0.2;61 A 0.3;75 A 0.3" \
  > "$out/pc.out" 2>&1 &
pc=$!
sleep 100; sb shot "$out/host_100s.jpg"
wait $pc
sb shot "$out/host_end.jpg"
cp "$P/out/pc/boot.log" "$out/pc.log"; cp "$P"/out/pc/captures/snap_*s.png "$out/" 2>/dev/null
log=$(newest_net); [ "$log" != "$before" ] && cp "$NET/$log" "$out/net.log"
echo "== console [net]"; grep -o '\[net\].*' "$out/net.log" | grep -v 'opt 5801' | head -20
echo "== PC [net]"; grep -o '\[net\].*' "$out/pc.log" | grep -v 'opt 5801' | head -20
