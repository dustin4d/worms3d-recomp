#!/usr/bin/env bash
# Deploy the Windows build to the Windows run folder. This folder is the only place builds are pushed:
#   C:\Users\Dustin\worms3d-recomp\
#     worms3d.exe          <- game/build-win/worms3d.exe
#     host0\modules\*.irx  <- lowercase copies of iso\MODULES (the game loads host0:modules/<lower>.irx first)
#     iso\                 <- user-provided: extracted disc + SLUS_208.94 + the raw .iso (never written here)
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
deploy=/mnt/c/Users/Dustin/worms3d-recomp
exe="$repo/game/build-win/worms3d.exe"

[[ -f "$exe" ]] || { echo "missing $exe; build game/build-win first" >&2; exit 1; }
[[ -d "$deploy/iso/MODULES" ]] || { echo "missing $deploy/iso/MODULES (disc contents)" >&2; exit 1; }
ls "$deploy"/iso/*.iso > /dev/null 2>&1 || echo "warning: no raw .iso in $deploy/iso; disc reads will fail" >&2

# A running copy keeps the .exe locked.
taskkill.exe /IM worms3d.exe /F > /dev/null 2>&1 || true

cp "$exe" "$deploy/worms3d.exe.new"
mv -f "$deploy/worms3d.exe.new" "$deploy/worms3d.exe"

mkdir -p "$deploy/host0/modules"
for f in "$deploy"/iso/MODULES/*; do
    lower="$(basename "$f" | tr 'A-Z' 'a-z')"
    cmp -s "$f" "$deploy/host0/modules/$lower" || cp "$f" "$deploy/host0/modules/$lower"
done

echo "deployed $(stat -c %s "$deploy/worms3d.exe") bytes to $(wslpath -w "$deploy")\\worms3d.exe ($(git -C "$repo" rev-parse --short HEAD)$(git -C "$repo" diff --quiet || echo '+dirty'))"
