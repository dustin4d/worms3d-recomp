# Bindings and names (SLUS_208.94 only)

Names applied in Ghidra. "Binds" = name exists in PS2Recomp `ps2_call_list.h`, so the runtime
handler replaces the guest code after re-export. Library versions in the ELF: libkernl 2410,
libpad 2410, libcdvd/libmc/libsdr/libgraph/libdma/libipu/libscf/libmtap 2400, libmpeg 2300.

| Address | Name | Binds | Evidence |
|---|---|---|---|
| 0x004ef850 | sceSifInitRpc | yes | Init-once flag 0x5fdb80; registers SIF cmd handlers 0x80000008/9/A/C; checks sceSifGetReg(0x80000002) then sets it to 1. Called before reboot in main. |
| 0x004f5048 | sceSifRebootIop | yes | Builds `"rom0:UDNL " + path`; main loops on it with `cdrom0:\MODULES\IOPRP241.IMG;1` (0x653338) after printing "Rebooting IOP..". |
| 0x004f5010 | sceSifSyncIop | yes | Returns `sceSifGetReg(4) & 0x40000` (SIF_STAT_BOOTEND); main spins on it after the reboot. |
| 0x004ef138 | sceSifGetSreg | yes | Returns `sreg_table[idx]` (0x793440); called with 0 in sceSifInitRpc's wait loop and guess_InputDeviceMgr_ctor's SIF check ("Failed to init SIF!"). |
| 0x004f0090 | sceSifCallRpc | yes | 9-arg (client, fno, mode, send, ssize, recv, rsize, end, endparam); sends SIF cmd 0x8000000A; mode&1 = NOWAIT, mode&2 skips D-cache writeback; sema wait. |
| 0x004efec0 | sceSifBindRpc | yes | `(client 0x780700, 0x12345, 0)` loop in FUN_0034b580 until client+0x24 (server) is set; failure prints "error: sceSifBindRpc" (0x664cc0). |
| 0x004ef468 | sceSifAddCmdHandler | yes | `(cid, handler, data)` x4 inside sceSifInitRpc. |
| 0x004ef5f8 | sceSifSendCmd | yes | `(cid, pkt, size, src, dst, len)`; used by sceSifInitRpc and sceSifCallRpc. |
| 0x004f4b48 | sceSifLoadModule | yes | Called `(path, 0, 0)` with IRX paths, e.g. `cdrom0:\MODULES\SIO2MAN.IRX;1`; `< 0` triggers the cdrom0 fallback path. Tail-calls guess__sceSifLoadModuleRpc. |
| 0x00515318 | printf | yes | newlib shape: vfprintf(_impure_ptr->stdout, fmt, va); callers pass format strings ("**main** - sbrk(0) = %08x\n"). |
| 0x004ed7e0 | sbrk | no | Advances heap break 0x5fd3c4, compares to EndOfHeap(), sets errno=12 (ENOMEM) on failure; main prints its result as "sbrk(0)". |
| 0x004f4920 | guess__sceSifLoadModuleRpc | no | Copies path (0xfc) and args into a 0x200 buffer, sceSifCallRpc on loadfile client 0x796480. |
| 0x004ef0a8 | guess_scePrintf | no | Varargs printer that swaps the putchar hook 0x5fdb78; used only from libkernl code (sceSifRebootIop error, BindRpc error). |
| 0x0027fe90 | main | no | Prints "**main** - sbrk(0)", reads PS2.RunFromCdRom / PS2.RebootIOP config, reboots the IOP. `entry` calls it as main(argc @0x6b2900, argv 0x6b2904) after BSS clear, syscalls 0x3C/0x3D and FlushCache(0). |
| 0x0036dd28 | guess_InputDeviceMgr_ctor | no | Sets vtable 0x60a980, loads sio2man.irx and usbd.irx (host0, then cdrom0 fallback), enumerates 8 devices. |
| 0x004ebce0 | sceCdInit | yes | Binds RPC 0x80000592 (CD init server); bind failure prints "Libcdvd bind err %d CD_Init %d" (0x6a1628); mode 5 path prints "Libcdvd Exit". Called (0) in main after IOP reboot. |
| 0x004ebfc0 | sceCdDiskReady | yes | Prints "DiskReady 0"/"DiskReady ended"; binds RPC 0x8000059A, failure prints "Libcdvd bind err CdDiskReady" (0x6a1668). |
| 0x004ec1b8 | sceCdMmode | yes | S-command RPC 0x22 (SCECdMmode) on client 0x5fd308 with the media arg; main passes the PS2.RunFromCdRom value. |
| 0x004f1070 | guess_sceFsReset | no | Clears fio flag 0x5fdc08 and 4 bytes at 0x796028; sits in main's post-reboot sequence where SCE samples call sceFsReset. Body too generic to confirm. |
| 0x00514b50 | memset | yes | (dst, byte, n) returns dst; 16-byte-aligned fast path replicates the byte with PCPYH/PCPYLD, byte tail loop. |
| 0x0051499c | memcpy | yes | (dst, src, n) returns dst; 32-byte aligned block copy, 8-byte loop, byte tail; no overlap handling. |
| 0x00514a4c | memmove | yes | Same as memcpy but copies backwards when src < dst < src+n. |
| 0x00517f98 | strncpy | yes | (dst, src, n) returns dst; word-at-a-time NUL scan (0x0101.../0x8080... trick), zero-pads the rest of n. |
| 0x004f5528 | guess_exit | no | `entry` passes main's return value to it as the final call. Body not yet checked. |

## libpad 2410 (0x004e9bd0-0x004eaf00)
All public ones bind. Padman RPC client 0x792080, request buffer 0x7922c0, cmd word first.
Per-port state at 0x7920d0 + port*0x70 + slot*0x1c; DMA frame struct fields +0x60 len, +0x70 state, +0x71 reqState.

| Address | Name | Binds | Evidence |
|---|---|---|---|
| 0x004e9ce8 | scePadInit | yes | Binds RPC 0x80000100 and 0x80000101 (padman); checks scePadGetModVersion()>>8 == 4, else prints "libpad: Module version mismatch" + "[libpad.a = %d.%d, padman.irx = %d.%d]". |
| 0x004e9f48 | scePadPortOpen | yes | (port, slot, buf): "buffer addr is not 64 byte align" if buf&0x3f, "pad port is already open [%d][%d]"; RPC cmd 0x01. Called from FUN_0036f518. |
| 0x004e9ec8 | scePadEnd | yes | RPC cmd 0x0F; clears init flag 0x5fb880 on success. |
| 0x004ea1e8 | scePadGetDmaStr | yes | (port, slot) returns the newer of two 0x80 frames by frame counter (+0x58 vs +0xd8); used by every state accessor. |
| 0x004ea298 | scePadRead | yes | (port, slot, data) memcpy(data, frame, frame+0x60) and returns the length; 0 if port not open. |
| 0x004ea318 | scePadGetState | yes | Returns frame+0x70; maps state 6 with reqState 2 to 5; 'c' (0x63) when port closed. |
| 0x004ea3c8 | scePadSetReqState | yes | Writes frame+0x71; called with 2 (BUSY) after every successful mode RPC. |
| 0x004ea430 | scePadGetReqState | yes | Returns frame+0x71. |
| 0x004ea4b8 | scePadInfoAct | yes | (port, slot, actno, term): actuator table at +0x30 + 4*n, count at +0x6a, actno -1 returns count. |
| 0x004ea6f8 | scePadInfoMode | yes | (port, slot, term, offs): term 1 ID (+0x65>>4, 0xF3 special), 2 EXID, 3 CURINDEX (+0x69), 4 TABLE (+0x68, -1 = count). |
| 0x004ea830 | scePadSetMainMode | yes | RPC cmd 0x06 (SETMMODE) with (port, slot, mode, lock). |
| 0x004ea8e8 | scePadSetActDirect | yes | Copies 6 actuator bytes into the DMA struct, sends via guess__scePadSendDma. |
| 0x004ea9a8 | scePadSetActAlign | yes | RPC cmd 0x08 with 6 alignment bytes. |
| 0x004eaa80 | scePadGetButtonMask | yes | Assembles 32-bit mask from frame bytes +0x79..+0x7c. |
| 0x004eab38 | scePadSetButtonInfo | yes | RPC cmd 0x0A (SETBTNINFO). |
| 0x004eabe8 | scePadInfoPressMode | yes | scePadGetButtonMask() == 0x3ffff. |
| 0x004eac48 | scePadEnterPressMode | yes | scePadSetButtonInfo(port, slot, 0xfff). |
| 0x004eacf8 | scePadSetVrefParam | yes | RPC cmd 0x0B (SETVREF), 12-byte param. |
| 0x004eadc8 | scePadGetPortMax | yes | RPC cmd 0x0C. |
| 0x004eae30 | scePadGetSlotMax | yes | RPC cmd 0x0D (port). |
| 0x004eae98 | scePadGetModVersion | yes | RPC cmd 0x12; scePadInit compares its result. |
| 0x004e9e28 | guess__scePadRpcInit | no | RPC cmd 0x10 (INIT) after clearing 4 port records; only called by scePadInit. |
| 0x004e9bd0 | guess__scePadSendDma | no | sceSifSetDma of a 0x20-byte block into the double buffer; prints "libpad: sceSifSetDma faild". |
| 0x004eaf00 | (unnamed) | - | RPC cmd 0x14, one arg. Unidentified. |

Undefined code (no function in Ghidra, no xrefs, so likely unused): 0x004ea130 (prologue + port/slot indexing; probably scePadPortClose, 188 bytes), 0x004eaca0 (`jal scePadSetButtonInfo` with a2=0, so scePadExitPressMode). Needs a function created in Ghidra if anything ever calls them.

## libcdvd (starts ~0x004eaf68)
| 0x004eaff8 | guess_sceCdCallback | no | Swaps callback pointer 0x792340 under DI/EI and returns the old one; FUN_004eb0f0's callback thread calls it with the CD status. |

## Overrides (game/overrides/worms3d_overrides.cpp)
| Address | Name | Override | Evidence |
|---|---|---|---|
| 0x004ed6a8 | guess__consoleWrite | host fwrite to stdout/stderr | (fd, buf, len); only fd 1/2 accepted, else -1; opens the DECI2 TTY once (FUN_004ee6d0) then calls guess__deci2TtyWrite. Only caller of the TTY path. Without the override the first printf hangs (see below). |
| 0x004ee4b0 | guess__deci2TtyWrite | - (unreachable now) | Copies buf into a packet at 0x20792f4c with \n -> \r\n, Deci2Call(3) send (FUN_004f55f8), then loops Deci2Call(4) poll (FUN_004f5628) until the DECI2 handler clears 0x792f1c. Runtime returns KE_OK for 3/4 and never runs handlers, so this spins forever. |

## Boot log
- Run 1 (generated from first export, no overrides): no guest output; GameThread spinning in guess__deci2TtyWrite -> Deci2Call(4).
- Run 2 (+ consoleWrite override): prints "XomHelp::Crc::sCrc ==", "Rebooting IOP..", then hangs in main's sceSifRebootIop/sceSifSyncIop loops (guest SIF code; runtime HLE handlers not yet bound because the names are not exported yet).

## Boundary fixes
- 2026-10-23: created 3,186 functions headless (tools/ghidra/CreateFunctionsFromList.java over
  tools/ghidra/missing_vtable_targets.txt). Targets are code addresses referenced from .rodata/.data/.sdata
  (vtables, function pointers) that started no function and sit right after `jr ra`/nop padding. Found
  after boot run 3 died on JALR 0x370514 -> 0x37f538 (vtable 0x60b9d8 entries [3] AddRef 0x37f538,
  [4] Release 0x37f520). 2 skipped (inside existing functions: 0x00332e6c in FUN_00332e18, plus one
  more). Project backup before the change: C:\Users\Dustin\test-ghidra-backup-20261023.
- Side effect: FUN_00410748 now ends at 0x4107ac (was 0x41084c); 0x4107ac is a vtable target and its
  own function. FUN_00410748 still branches to 0x410808 (inside FUN_004107ac). Watch this if it misbehaves.

## libcdvd, round 2 (applied headless via tools/ghidra/renames_20261023_libcdvd.txt)
RPC servers: 0x80000592 init, 0x80000593 S-cmd (client 0x5fd308), 0x80000595 N-cmd (client 0x5fca90),
0x80000597 search file, 0x8000059A disk ready. Found after boot run 5 hung in FUN_004eb858's
bind loop on 0x80000595 (runtime has no IOP server for the raw N-cmd bind).

| Address | Name | Binds | Evidence |
|---|---|---|---|
| 0x004eb558 | sceCdSearchFile | yes | Binds 0x80000597; "Libcdvd bind err CdSearchFile" (0x6a1518), "ee call cmd search %s", "search name/size/loc lbn". |
| 0x004ec280 | sceCdRead | yes | N-cmd 0x01 via guess__CdCheckNCmd; "call cdread cmd" (0x6a16a0) / "cdread end" (0x6a16b8). |
| 0x004eba60 | sceCdSync | yes | "N cmd wait" (0x6a15c8); every N-cmd path and sceCdCallback call it with 1 as a busy check. |
| 0x004ebb00 | sceCdSyncS | yes | "S cmd wait" (0x6a15d8); same role for S-cmds (sceCdInit, sceCdDiskReady, guess__CdCheckSCmd). |
| 0x004ec5b0 | sceCdReadClock | yes | S-cmd 0x01; "Libcdvd call Clock read 1/2" (0x6a16c8/0x6a16e8). |
| 0x004ec460 | sceCdGetError | yes | S-cmd 0x04. S-cmd numbering matches the libcdvd table where 0x01 (ReadClock, string-confirmed) and 0x22 (sceCdMmode) line up. |
| 0x004ec4f8 | sceCdBreak | yes | S-cmd 0x16, same table. |
| 0x004eaff8 | sceCdCallback | yes | Upgraded from guess_: swaps callback 0x792340; callback thread FUN_004eb0f0 calls it and prints "sceCdCbfunc= %d sceCdCbfunc_num= %d". |
| 0x004eb858 | guess__CdCheckNCmd | no | Binds 0x80000595 in a delay loop; "Ncmd fail sema cur_cmd:%d keep_cmd:%d", "Libcdvd bind err N CMD". Callers: sceCdRead, guess_sceCdNcmdDiskReady. |
| 0x004ebb70 | guess__CdCheckSCmd | no | Binds 0x80000593; "Scmd fail sema", "Libcdvd bind err S cmd". Callers: sceCdMmode, sceCdGetError, sceCdBreak, sceCdReadClock. |
| 0x004eb9c8 | guess_sceCdNcmdDiskReady | no | N-cmd 0x0E, called first by sceCdRead. Dead once sceCdRead binds. |

Still unnamed in libcdvd: 0x4eb3c0/0x4eb4f0 (remove/add SIF cmd handler 0x80000012, CD callback
plumbing), 0x4ec6a8-0x4eca80 (no RPC; likely sceCdIntToPos/sceCdPosToInt-style helpers).

## IOP heap (applied headless via tools/ghidra/renames_20261023_iopheap.txt)
Found after boot run 8 spun in FUN_004f3e48's bind loop on 0x80000003. Heap client 0x7960c0, init flag 0x5fdc1c.

| Address | Name | Binds | Evidence |
|---|---|---|---|
| 0x004f3e48 | sceSifInitIopHeap | yes | Binds 0x80000003 (IOP heap server) in a delay loop until client+0x24 is set; clears 0x5fdc1c on success, which the other heap calls check. Game string "Error init \"sceSifInitIopHeap (IOP)\" in XSamplePlayerPS2!". |
| 0x004f3ed0 | sceSifAllocIopHeap | yes | Heap RPC 1 with the size argument. |
| 0x004f3fc0 | sceSifFreeIopHeap | yes | Heap RPC 2. |
| 0x004f4058 | sceSifLoadIopHeap | yes | Heap RPC 3; copies a path into 0x796180. |
| 0x004f3f40 | sceSifAllocSysMemory | yes | Heap RPC 4 with three arguments packed into 0x796140. |
| 0x004f4038 | sceSifFreeSysMemory | yes | Wrapper that only calls sceSifFreeIopHeap. |
| 0x004f4148 | guess__sceSifLoadFileInit | no | Binds 0x80000006 (loadfile server) for the client 0x796480 used by guess__sceSifLoadModuleRpc. |

## Runtime I/O setup (game/overrides/worms3d_overrides.cpp)
- CD image: the game reads ISO9660 itself (first read: sceCdRead LBN 16 = PVD), so the runtime needs the raw
  image. IoPaths::cdImage is set from $WORMS3D_CD_IMAGE or the single *.iso next to the ELF (iso/).
- host0: the game loads IRXs from host0:modules/<lower>.irx before cdrom0:\MODULES\<UPPER>.IRX. On Linux
  the uppercase disc names never matched, so libsd fell back to the runtime's HLE stub, which lacks the IOP
  exports SDRDRV/STREAM import ("unhandled import libsd:4"). game/host0/modules has lowercase symlinks to
  iso/MODULES; hostRoot points there ($WORMS3D_HOST0 overrides). Real LIBSD/SDRDRV/STREAM now load.
