#!/usr/bin/env bash
# Populate game/host0/modules with lowercase copies of the disc's IOP modules (iso/MODULES).
# The game loads host0:modules/<lower>.irx first; see notes/bindings.md (Runtime I/O setup).
# Copies, not symlinks: Windows cannot follow WSL symlinks over \\wsl.localhost. Not committed
# (disc content); rerun after replacing iso/.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"
src="$repo/iso/MODULES"
dst="$repo/game/host0/modules"

[[ -d "$src" ]] || { echo "missing $src (extract the disc into iso/ first)" >&2; exit 1; }
mkdir -p "$dst"
for f in "$src"/*; do
    lower="$(basename "$f" | tr 'A-Z' 'a-z')"
    rm -f "$dst/$lower"
    cp "$f" "$dst/$lower"
done
ls "$dst"
