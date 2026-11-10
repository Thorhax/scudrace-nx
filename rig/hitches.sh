#!/bin/sh
# hitches.sh <label>...: the [hitch] frames after boot (past frame 1000) of each run, and the intro / race fps.
R=$(cd "$(dirname "$0")" && pwd); . "$R/common.sh"
for label in "$@"; do
  f="$L/$label/net.log"
  grep -o '\[hitch\] frame [0-9]*: [0-9.]*' "$f" | awk -v l="$label" '$3+0 > 1000 {n++; s=s sprintf(" %d:%s", $3+0, $4); if ($4 > 60) big++}
    END {printf "%-16s %2d frames over 45 ms (%d over 60):%s\n", l, n, big, s}'
done
