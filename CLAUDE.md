# PS2 recomp: Worms 3D (SLUS_208.94, sha256 69b32de842fe66a048f6030a454c8d53ea6fd05ad05f7d8a74391597a99b4ef9)

## Environment
- You run in Arch on WSL2. Ghidra runs on Windows; reach it only through the ghidra MCP server (127.0.0.1:8080, mirrored networking).
- Project and tools live on the Linux filesystem. Never build under /mnt/c.
- Paths in config files are Linux paths, relative to config/.
- The game runs on Windows, never under WSLg. The only Windows-side location you write to is the deploy folder /mnt/c/Users/Dustin/worms3d-recomp (C:\Users\Dustin\worms3d-recomp).

## Layout
- elf/ = input ELF (read-only). config/ = game.toml + functions.csv exported from Ghidra.
- iso/ = extracted disc contents + raw .iso (WSL copy, for tooling and Linux debug runs). game/ = CMake project for the runner (worms3d target).
- generated/ = ps2_recomp output; never hand-edit, regenerate instead.
- game/overrides/ = per-game fixes via PS2_REGISTER_GAME_OVERRIDE.
- tools/ = deploy/run scripts; tools/ghidra/ = headless Ghidra scripts and rename/create lists.
- PS2Recomp source: ~/src/PS2Recomp (runtime handlers: PS2_SYSCALL_LIST / PS2_STUB_LIST).

## Deploy folder (C:\Users\Dustin\worms3d-recomp)
- worms3d.exe and host0\modules\*.irx (lowercase copies of iso\MODULES) are written by tools/deploy-win.sh.
- iso\ (extracted disc, SLUS_208.94, raw "Worms 3D (USA) (En,Fr).iso") is user-provided; never write to it.
- SLUS_208.94 and worms3d-config at the top level belong to Ghidra's import; leave them alone.
- tools\headless.bat (tools/ghidra/headless.sh), tools\screenshot-win.ps1 and screenshots\ (tools/screenshot-win.sh) are also ours.

## Iteration loop
1. Name/fix in Ghidra (MCP while it is open; tools/ghidra/headless.sh rename|create ... export while it is fully closed).
2. After an export, re-fix the Windows paths in config/game.toml.
3. Regenerate: (cd config && ps2_recomp game.toml). It resolves TOML paths against the cwd, so run it from config/. Clear generated/ first when names change.
4. Build for Windows: cmake --build game/build-win --target worms3d (mingw-w64 cross-compile; configure with -DCMAKE_TOOLCHAIN_FILE=$PWD/game/cmake/mingw-w64-x86_64.cmake, see that file).
5. Deploy: tools/deploy-win.sh. Run: tools/run-win.sh [seconds] (native Windows process; console output comes back to WSL). While it runs, tools/screenshot-win.sh [name] saves the game window to screenshots\ in the deploy folder.
- WORMS3D_SAMPLE=<seconds> tools/run-win.sh ... prints EE thread states (pc/ra/sp, waits), GS display registers and draw targets periodically.
- game/build (Linux) exists only for gdb debugging; it needs tools/setup-host0.sh and is never run under WSLg without asking.

## Tools
- Ghidra MCP (server "ghidra") is the source of truth for function boundaries and names.

## Rules
- Cite evidence (xrefs, strings, call sites) for every function you name.
- Prefix guessed names with guess_; confirmed names have no prefix.
- Addresses are for this ELF only. Log every new stub/binding in notes/bindings.md.
- Fix boundaries in Ghidra and re-export; don't patch generated/.
