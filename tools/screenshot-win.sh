#!/usr/bin/env bash
# Screenshot the running Windows build into the deploy folder: C:\Users\Dustin\worms3d-recomp\screenshots\
#   tools/screenshot-win.sh [name]   -> prints the Linux path of the PNG
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
deploy=/mnt/c/Users/Dustin/worms3d-recomp
mkdir -p "$deploy/screenshots" "$deploy/tools"
cp "$repo/tools/screenshot-win.ps1" "$deploy/tools/screenshot-win.ps1"

name="${1:-$(date +%Y%m%d-%H%M%S)}"
out="$deploy/screenshots/$name.png"
cd "$deploy"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w "$deploy/tools/screenshot-win.ps1")" \
    -Out "$(wslpath -w "$out")" | tr -d '\r' >&2
echo "$out"
