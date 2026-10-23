#!/usr/bin/env bash
# Run the deployed Windows build natively on Windows (never under WSLg). Deploy first: tools/deploy-win.sh
#   tools/run-win.sh [seconds] [extra args...]   default 90 s; 0 = no time limit
# Runs from C:\Users\Dustin\worms3d-recomp, so the exe's default boot path iso\SLUS_208.94 resolves.
# Console output comes back on this script's stdout/stderr.
set -uo pipefail

deploy=/mnt/c/Users/Dustin/worms3d-recomp
limit="${1:-90}"
shift || true

[[ -f "$deploy/worms3d.exe" ]] || { echo "missing $deploy/worms3d.exe; run tools/deploy-win.sh" >&2; exit 1; }

cd "$deploy"
./worms3d.exe "$@" &
pid=$!

if [[ "$limit" != 0 ]]; then
    ( sleep "$limit"; taskkill.exe /IM worms3d.exe /F > /dev/null 2>&1 ) &
    watchdog=$!
fi

wait "$pid"
status=$?
[[ -n "${watchdog:-}" ]] && kill "$watchdog" 2> /dev/null
# Killing the WSL-side proxy does not always end the Windows process.
taskkill.exe /IM worms3d.exe /F > /dev/null 2>&1 || true
exit "$status"
