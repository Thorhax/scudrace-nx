#!/bin/sh
# race_sd.sh <nro> <label> <config dir> [secs]: race.sh without the network log (its sends made the 10 s report's
# frames long): the SD log is fetched after the title is closed, into <MK9_LOGS>/<label>/net.log.
R=$(cd "$(dirname "$0")" && pwd); . "$R/common.sh"
sh "$R/race.sh" "$1" "$2" "$3" "${4:-90}" 1 > /dev/null 2>&1
close_title
n=$(ftp ls /switch/${SD_DIR:-scudrace-nx}/logs | awk '/scud_[0-9]+\.log/{print $NF}' | sort | tail -1)
mkdir -p "$L/$2"; ftp get "/switch/${SD_DIR:-scudrace-nx}/logs/$n" "$L/$2/net.log"
sh "$R/hitches.sh" "$2"
grep -o '\[fps\].*' "$L/$2/net.log" | awk 'NR>=16 && NR<=20{i+=$2;ni++} NR>=21{s+=$2;n++} END{printf "  intro fps %.1f, race fps %.2f\n", i/ni, s/n}'
