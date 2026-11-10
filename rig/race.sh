#!/bin/sh
# race.sh <nro in out/> <label> [config dir] [race seconds]: launch on the console, skip the splash, Single Player ->
# START -> Beginner (Day) -> the first car, automatic, then hold the accelerator for the race (default 40 s) with a
# screenshot every 10 s; prints the race's [fps] lines and Renut's GPU line. Logs in <MK9_LOGS>/<label>/.
# Buttons: the positional Xbox layout (Switch B = Xbox A confirm, + = START, ZR = accelerator).
R=$(cd "$(dirname "$0")" && pwd); . "$R/common.sh"
nro=$1; label=$2; cfg=${3:-}; secs=${4:-40}
[ -f "$P/out/$nro" ] || { echo "no out/$nro" >&2; exit 2; }
out="$L/$label"; mkdir -p "$out"
if [ -n "$cfg" ]; then
  sed "s/@PC_IP@/$PC_IP/g" "$cfg/scud.toml" > "$T/scud.toml" && ftp put "$T/scud.toml" /switch/${SD_DIR:-scudrace-nx}/scud.toml
fi
before=$(newest_net)
# From the home menu (or a running title, closed first): hbmenu by a title takeover, its netloader, the NRO.
tid=$(python3 "$R/sb.py" title 2>/dev/null)  # empty: the home menu (sys-botbase answers nothing there)
if [ -n "$tid" ] && [ "$tid" != "0100000000001000" ]; then close_title; fi
sb "press R" "sleep 0.5" "click A" "sleep 2.5" "click A" "sleep 5" "release R" "sleep 2" "click Y" "sleep 3"
"$NXL" -a "$SWITCH_IP" -p "${NXPATH:-${SD_DIR:-scudrace-nx}/scudrace-nx-test.nro}" "$P/out/$nro" > /dev/null 2>&1
sleep 35                                              # boot, splash
sb "click A" "sleep 4"                                # splash -> attract
sb "click PLUS" "sleep 2" "click B" "sleep 2"         # main menu -> Single Player
i=0; while [ "$i" -lt "${MENU_DOWNS:-5}" ]; do sb "click DDOWN" "sleep 1"; i=$((i + 1)); done  # 0.0.17: SELECT TIMER above START (5; 0.0.14: 4)
sb "click B" "sleep 8"                                # START -> course select
i=0; while [ "$i" -lt "${COURSE:-0}" ]; do sb "setStick LEFT 0x7FFF 0" "sleep 0.4" "setStick LEFT 0 0" "sleep 1.2"; i=$((i + 1)); done  # COURSE=n: steer right n times
[ -n "${COURSE:-}" ] && sb shot "$out/course.jpg"
if [ -n "${JOYCODE:-}" ]; then python3 "$R/joycode.py"; sleep 2; sb shot "$out/joycode.jpg"; fi  # JOYCODE=1 with COURSE unset or 0 (the cursor starts on MEDIUM): JOYPOLIS
sb "press ZR" "sleep 1" "release ZR" "sleep 5"        # course
sb "press ZR" "sleep 1" "release ZR" "sleep 5"        # car
if [ -n "${INTRO_SHOTS:-}" ]; then                    # transmission, the start: the course intro's 2D layers, every 2 s
  sb "press ZR" "sleep 1" "release ZR"
  for k in 2 4 6 8; do sleep 1.6; sb shot "$out/intro_${k}s.jpg"; done
else
  sb "press ZR" "sleep 1" "release ZR" "sleep 8"
fi
start=$(date +%s)
sb "press ZR"
n=0
while [ $(( $(date +%s) - start )) -lt "$secs" ]; do
  sleep 10; n=$((n + 10)); sb shot "$out/race_${n}s.jpg"
done
sb "release ZR"
log=$(newest_net)
[ "$log" != "$before" ] && cp "$NET/$log" "$out/net.log"
echo "== $label ($nro${cfg:+, $cfg}): last [fps] lines"
grep -o '\[fps\].*' "$out/net.log" | tail -${5:-6}
grep -o '\[renut\] 10.0 s: [^;]*;' "$out/net.log" | tail -3
