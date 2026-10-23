#!/usr/bin/env bash
# Run Windows headless Ghidra on the project from WSL. Ghidra must be fully closed (project lock).
#   tools/ghidra/headless.sh export                  -> read-only export to config/game.toml + functions.csv
#   tools/ghidra/headless.sh create <list> export    -> create functions from <list> (repo-relative), then export
#   tools/ghidra/headless.sh rename <list> export    -> rename functions from <list> ("addr name" lines), then export
# After an export, restore the Linux paths in config/game.toml.
set -euo pipefail

GHIDRA='C:\tools\ghidra_12.1.4_PUBLIC'
JDK='C:\Program Files\Microsoft\jdk-21.0.12.101-hotspot'
PROJECT_DIR='C:\Users\Dustin'
PROJECT=test-ghidra
REPO_UNC='\\wsl.localhost\archlinux\home\dustin\workspace\worms3d-recomp'
DEPLOY=/mnt/c/Users/Dustin/worms3d-recomp   # the only Windows-side location files are written to

if [[ -e /mnt/c/Users/Dustin/$PROJECT.lock ]]; then
    echo "Project is locked: close Ghidra (File > Exit in the Project window) first." >&2
    exit 1
fi

unc() { echo "${REPO_UNC}\\${1//\//\\}"; }

scripts=()
readonly_flag=-readOnly
while [[ $# -gt 0 ]]; do
    case "$1" in
        create) scripts+=(-postScript CreateFunctionsFromList.java "$(unc "$2")"); readonly_flag=; shift 2 ;;
        rename) scripts+=(-postScript RenameFromList.java "$(unc "$2")"); readonly_flag=; shift 2 ;;
        export) scripts+=(-postScript ExportPS2Functions.java "$(unc config/game.toml)" "$(unc config/functions.csv)"); shift ;;
        *) echo "unknown step: $1" >&2; exit 2 ;;
    esac
done
[[ ${#scripts[@]} -gt 0 ]] || { echo "usage: $0 [create <list>] [rename <list>] export" >&2; exit 2; }

# cmd.exe quoting from bash is fragile; go through a batch file in the deploy folder.
mkdir -p "$DEPLOY/tools"
bat="$DEPLOY/tools/headless.bat"
printf '%s\r\n' '@echo off' \
    "set \"JAVA_HOME=${JDK}\"" \
    "call ${GHIDRA}\\support\\analyzeHeadless.bat ${PROJECT_DIR} ${PROJECT} -process SLUS_208.94 -noanalysis ${readonly_flag} -scriptPath \"$(unc tools/ghidra);${PROJECT_DIR}\\ghidra_scripts\" ${scripts[*]}" \
    > "$bat"

cd /mnt/c/Users/Dustin
cmd.exe /c "$(wslpath -w "$bat")" < /dev/null 2>&1 | tr -d '\r' \
    | grep -E 'REPORT|RenameFromList|RENAME|CreateFunctionsFromList|ExportPS2Functions|SKIP|ERROR|Exception|Press any key'
