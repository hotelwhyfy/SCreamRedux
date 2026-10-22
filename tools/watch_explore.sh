#!/bin/bash
# Watch a `scream --explore` run: prints progress every 10 checks, a stall (no new action in
# 6 checks), a runaway clock (script global `time` past 24) and the end of the run.
# Usage: tools/watch_explore.sh LOG PGREP_PATTERN [INTERVAL_SECONDS]
# Give PGREP_PATTERN a bracket (e.g. "game sd[c] --explore") so it never matches this script.
f=$1; pat=$2; iv=${3:-30}; prev=-1; still=0; t=0; warned=0
while true; do
  n=$(grep -c '^\[' "$f" 2>/dev/null); n=${n:-0}
  end=$(grep -m1 '^explore: \(stuck\|stalled\|gave\|the game\)' "$f" 2>/dev/null)
  if [ -n "$end" ]; then echo "$end"; exit 0; fi
  if ! pgrep -f "$pat" >/dev/null; then echo "process exited without result (actions=$n)"; tail -3 "$f"; exit 1; fi
  if [ "$n" = "$prev" ]; then still=$((still+1)); else still=0; fi
  [ $still -eq 6 ] && echo "STALL: no new action for $((6*iv))s (actions=$n, last: $(grep '^\[' "$f" | tail -1))"
  tm=$(grep -o 'time=n [0-9]*' "$f" | tail -1 | awk '{print $2}')
  if [ -n "$tm" ] && [ "$tm" -gt 24 ] && [ $warned -eq 0 ]; then echo "CLOCK RUNAWAY: time=$tm (last: $(grep '^\[' "$f" | tail -1))"; warned=1; fi
  t=$((t+1))
  if [ $((t % 10)) -eq 0 ]; then
    echo "progress: $n actions, $(grep -c 'state #' "$f") states, $(grep -o 'phase=[^ ]* [0-9]*' "$f" | tail -1), time ${tm:-?}, items $(grep -o '~inv [a-z-]*' "$f" | sort -u | wc -l | tr -d ' ')"
  fi
  prev=$n
  sleep "$iv"
done
