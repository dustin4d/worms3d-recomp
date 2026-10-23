# PS2 recomp: Worms 3D (<SLUS_208.94>, sha256 <hash>)

## Environment
- You run in Arch on WSL2. Ghidra runs on Windows; reach it only through the ghidra MCP server (127.0.0.1:8080, mirrored networking).
- Project and tools live on the Linux filesystem. Never build under /mnt/c.
- Paths in config files are Linux paths, relative to config/.

## Layout
- elf/ = input ELF (read-only). config/ = game.toml + functions.csv exported from Ghidra.
- generated/ = ps2_recomp output; never hand-edit, regenerate instead.
- game/overrides/ = per-game fixes via PS2_REGISTER_GAME_OVERRIDE.
- PS2Recomp source: ~/tools/PS2Recomp (runtime handlers: PS2_SYSCALL_LIST / PS2_STUB_LIST).

## Tools
- Ghidra MCP (server "ghidra") is the source of truth for function boundaries and names.
- Rebuild: ps2_recomp config/game.toml, then cmake --build game/build.
- After a re-export from Ghidra, re-fix any Windows paths in config/game.toml.

## Rules
- Cite evidence (xrefs, strings, call sites) for every function you name.
- Prefix guessed names with guess_; confirmed names have no prefix.
- Addresses are for this ELF only. Log every new stub/binding in notes/bindings.md.
- Fix boundaries in Ghidra and re-export; don't patch generated/.
