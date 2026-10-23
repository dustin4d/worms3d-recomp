#include "game_overrides.h"
#include "ps2_runtime.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <sstream>
#include <thread>
#include <vector>

#include "runtime/ee_scheduler.h"
#include "runtime/gs/gs_frontend.h"

#include "raylib.h"

namespace
{
    // 0x004ed6a8 guess__consoleWrite(fd, buf, len): libc console write for fd 1/2. The original
    // sends through the DECI2 TTY and spins on sceDeci2Poll until the protocol handler clears a
    // busy flag; the runtime never invokes DECI2 handlers, so the first printf hangs forever.
    // Write straight to the host instead. See notes/bindings.md.
    void consoleWrite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
        const uint32_t bufAddr = getRegU32(ctx, 5);
        const int32_t len = static_cast<int32_t>(getRegU32(ctx, 6));

        const uint8_t *buf = getConstMemPtr(rdram, bufAddr);
        if ((fd != 1 && fd != 2) || !buf || len < 0)
        {
            setReturnS32(ctx, -1);
            return;
        }

        std::FILE *out = (fd == 2) ? stderr : stdout;
        std::fwrite(buf, 1, static_cast<size_t>(len), out);
        std::fflush(out);
        setReturnS32(ctx, len);
    }

    // The game parses ISO9660 itself (first read: sceCdRead LBN 16, the PVD) and reads files by LBN,
    // so extracted files are not enough; the runtime needs a raw 2048-byte-sector image. main.cpp
    // never sets IoPaths::cdImage, so set it here: $WORMS3D_CD_IMAGE, else the one *.iso next to the ELF.
    void configureCdImage()
    {
        PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();

        std::filesystem::path image;
        if (const char *env = std::getenv("WORMS3D_CD_IMAGE"); env && env[0] != '\0')
        {
            image = env;
        }
        else
        {
            std::error_code ec;
            for (const auto &entry : std::filesystem::directory_iterator(paths.elfDirectory, ec))
            {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
                if (entry.is_regular_file() && ext == ".iso")
                {
                    if (!image.empty())
                    {
                        std::cerr << "[worms3d] several .iso files next to the ELF; set WORMS3D_CD_IMAGE" << std::endl;
                        return;
                    }
                    image = entry.path();
                }
            }
        }

        if (image.empty())
        {
            std::cerr << "[worms3d] no CD image found; disc reads will fail" << std::endl;
            return;
        }

        paths.cdImage = image;
        PS2Runtime::setIoPaths(paths);
        std::cerr << "[worms3d] CD image: " << image.string() << std::endl;
    }

    // The game loads each IRX from host0:modules/<lowercase>.irx first, then cdrom0:\MODULES\<UPPER>.IRX.
    // host0: maps to the ELF directory, where the disc's uppercase names never match on a case-sensitive
    // host, so libsd fell back to the runtime's HLE stub, which lacks the IOP exports the real SDRDRV/STREAM
    // import (first symptom: "unhandled import libsd:4"). A host0/modules directory holds lowercase copies of
    // the disc modules so the real LIBSD.IRX loads: <elf dir>/../host0 in the Windows deploy folder, or
    // game/host0 in the repo. $WORMS3D_HOST0 overrides both.
    void configureHost0()
    {
        PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();

        std::vector<std::filesystem::path> candidates;
        if (const char *env = std::getenv("WORMS3D_HOST0"); env && env[0] != '\0')
        {
            candidates.emplace_back(env);
        }
        else
        {
            candidates.push_back(paths.elfDirectory / ".." / "host0");
            candidates.push_back(paths.elfDirectory / ".." / "game" / "host0");
        }

        std::error_code ec;
        std::filesystem::path host0;
        for (const auto &candidate : candidates)
        {
            if (std::filesystem::is_directory(candidate, ec))
            {
                host0 = candidate;
                break;
            }
        }
        if (host0.empty())
        {
            std::cerr << "[worms3d] host0 directory missing: " << candidates.front().string() << std::endl;
            return;
        }

        paths.hostRoot = std::filesystem::weakly_canonical(host0, ec);
        PS2Runtime::setIoPaths(paths);
        std::cerr << "[worms3d] host0: " << paths.hostRoot.string() << std::endl;
    }

    // crt0 calls SetupThread(gp, stack=0x01ff0000, size=0x10000, ...), so the main thread's stack is the top
    // 64 KB of RAM (sp starts at 0x02000000). The runtime carves interrupt/callback handler stacks downward
    // from the top of RAM too, so the first handler ran on main's frame and overwrote it (first symptom: the
    // argument list in main's frame at 0x1ffff70 replaced by a handler sp, then null-string frees and
    // memset(ptr, 0xDD, -4)). Reserve the main stack region first so handler stacks start below 0x01ff0000.
    void reserveMainThreadStack(PS2Runtime &runtime)
    {
        constexpr uint32_t kMainStackBase = 0x01ff0000u;
        // Returns the reserved block's stack top, or 0 on failure; later reservations start below kMainStackBase.
        if (runtime.reserveAsyncCallbackStack(PS2_RAM_SIZE - kMainStackBase, 16u) == 0u)
        {
            std::cerr << "[worms3d] could not reserve the main thread stack from handler stacks" << std::endl;
        }
    }

    // Debug sampler: with $WORMS3D_SAMPLE=<seconds>, a host thread prints the EE scheduler snapshot (every
    // guest thread's state, wait reason and pc/ra/sp) at that interval. There is no debugger for the Windows
    // build, so this is how we see where the game sits. An unchanged snapshot sequence means no scheduler event
    // happened in between (a guest busy loop).
    const char *threadStatusName(EeThreadStatus status)
    {
        switch (status)
        {
        case EeThreadStatus::Running: return "run";
        case EeThreadStatus::Ready: return "ready";
        case EeThreadStatus::Waiting: return "wait";
        case EeThreadStatus::WaitingSuspended: return "wait+susp";
        case EeThreadStatus::Suspended: return "susp";
        case EeThreadStatus::Dormant: return "dormant";
        }
        return "?";
    }

    const char *waitReasonName(EeWaitReason reason)
    {
        switch (reason)
        {
        case EeWaitReason::None: return "";
        case EeWaitReason::Sleep: return " sleep";
        case EeWaitReason::Semaphore: return " sema";
        case EeWaitReason::EventFlag: return " evf";
        case EeWaitReason::VSync: return " vsync";
        case EeWaitReason::External: return " ext";
        case EeWaitReason::Mpeg: return " mpeg";
        }
        return " ?";
    }

    void startSampler(PS2Runtime &runtime)
    {
        const char *env = std::getenv("WORMS3D_SAMPLE");
        const double interval = env ? std::atof(env) : 0.0;
        if (interval <= 0.0)
        {
            return;
        }

        std::thread([&runtime, interval]() {
            uint64_t lastSequence = 0;
            for (int n = 1;; ++n)
            {
                std::this_thread::sleep_for(std::chrono::duration<double>(interval));
                const EeKernelSnapshot snap = runtime.eeScheduler().snapshot();

                std::ostringstream out;
                out << "[sample " << n << "] seq=" << snap.sequence << (snap.sequence == lastSequence ? " (unchanged)" : "")
                    << " eeCycle=" << snap.eeCycle << " running=" << snap.runningThreadId << std::hex;
                for (const EeThreadSnapshot &t : snap.threads)
                {
                    out << "\n  thread " << std::dec << t.id << std::hex << " " << threadStatusName(t.status)
                        << waitReasonName(t.waitReason);
                    if (t.waitReason != EeWaitReason::None)
                    {
                        out << " id=" << std::dec << t.waitId << std::hex;
                    }
                    out << " pc=0x" << t.pc << " ra=0x" << t.ra << " sp=0x" << t.sp << " entry=0x" << t.entry
                        << " prio=" << std::dec << t.currentPriority << " inv=" << t.invocationDepth << std::hex;
                    if (t.invocationDepth != 0)
                    {
                        out << " | base pc=0x" << t.basePc << " ra=0x" << t.baseRa << " sp=0x" << t.baseSp;
                    }
                }
                // GS privileged registers (unsynchronized reads; fine for a debug print).
                const GSRegisters &gs = runtime.memory().gs();
                out << "\n  gs pmode=0x" << gs.pmode << " dispfb1=0x" << gs.dispfb1 << " display1=0x" << gs.display1
                    << " dispfb2=0x" << gs.dispfb2 << " display2=0x" << gs.display2 << " bgcolor=0x" << gs.bgcolor
                    << " csr=0x" << gs.csr.load(std::memory_order_relaxed) << " vsync=" << std::dec
                    << gs.vsyncTick.load(std::memory_order_relaxed) << std::hex;
                // GS drawing state: where the game draws vs. what gets presented.
                const GSDebugSnapshot draw = runtime.gs().getDebugSnapshot();
                out << "\n  draw ctx1 fbp=0x" << draw.ctx[0].frame.fbp << " fbw=" << std::dec << draw.ctx[0].frame.fbw
                    << " psm=0x" << std::hex << uint32_t(draw.ctx[0].frame.psm) << " | ctx2 fbp=0x" << draw.ctx[1].frame.fbp
                    << " psm=0x" << uint32_t(draw.ctx[1].frame.psm) << " | prim=" << std::dec << uint32_t(draw.prim.type)
                    << " | bitblt dbp=0x" << std::hex << draw.bitbltbuf.dbp << " | present fbp=0x"
                    << draw.hostPresentationDisplayFbp << " src=0x" << draw.hostPresentationSourceFbp << " "
                    << std::dec << draw.hostPresentationWidth << "x" << draw.hostPresentationHeight
                    << (draw.hasHostPresentationFrame ? "" : " (no frame)") << std::hex;
                // IOP emulator state: is it still executing, and which modules/RPC servers/threads exist?
                const ps2x::iop::DebugSnapshot iop = runtime.iopDebugSnapshot();
                out << "\n  iop cycles=" << std::dec << iop.emulatorCycles << " insns=" << iop.emulatorInstructions
                    << " modules=" << iop.emulatorLoadedModules << " threads=" << iop.emulatorThreads
                    << " rpcServers=" << iop.emulatorRpcServers << " services=" << iop.services.size();
                for (size_t d = iop.diagnostics.size() > 4 ? iop.diagnostics.size() - 4 : 0; d < iop.diagnostics.size(); ++d)
                {
                    out << "\n  iop diag: " << iop.diagnostics[d];
                }
                out << std::hex;
                for (const EeSemaphoreSnapshot &s : snap.semaphores)
                {
                    if (s.waiters != 0)
                    {
                        out << "\n  sema " << std::dec << s.id << " count=" << s.count << "/" << s.maxCount
                            << " waiters=" << s.waiters << std::hex;
                    }
                }
                std::cerr << out.str() << std::endl;
                lastSequence = snap.sequence;
            }
        }).detach();
        std::cerr << "[worms3d] sampler every " << interval << " s" << std::endl;
    }

    // GS state watcher: with $WORMS3D_GSWATCH=<seconds>, poll the GS debug snapshot as fast as the host allows
    // and count each distinct draw/transfer state. States that touch the display buffer (DISPFB FBP 0x140 =
    // block 0x2800) or read the 32-bit draw buffer (FRAME FBP 0xa0 = block 0x1400) are printed when first seen;
    // a summary of the most common states is printed when the window ends. Used to find the game's
    // 32-bit -> 16-bit display conversion pass.
    void startGsWatch(PS2Runtime &runtime)
    {
        const char *env = std::getenv("WORMS3D_GSWATCH");
        const double seconds = env ? std::atof(env) : 0.0;
        if (seconds <= 0.0)
        {
            return;
        }

        std::thread([&runtime, seconds]() {
            constexpr uint32_t kDisplayFbp = 0x140, kDisplayBlock = 0x140 * 32, kDrawBlock = 0xa0 * 32;
            std::map<std::string, uint64_t> counts;
            uint64_t polls = 0;
            const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
            while (std::chrono::steady_clock::now() < end)
            {
                const GSDebugSnapshot s = runtime.gs().getDebugSnapshot();
                const GSContext &c = s.ctx[s.prim.ctxt ? 1 : 0];
                std::ostringstream key;
                key << std::hex << "ctx" << (s.prim.ctxt ? 2 : 1) << " frame=0x" << c.frame.fbp << "/w" << std::dec
                    << c.frame.fbw << "/psm" << std::hex << uint32_t(c.frame.psm) << " msk=0x" << c.frame.fbmsk
                    << " prim=" << std::dec << uint32_t(s.prim.type) << (s.prim.tme ? " tme" : "")
                    << (s.prim.abe ? " abe" : "") << (s.prim.fst ? " fst" : "") << (s.prim.iip ? " iip" : "")
                    << std::hex << " tex0=0x" << c.tex0.tbp0 << "/w" << std::dec << uint32_t(c.tex0.tbw) << "/psm"
                    << std::hex << uint32_t(c.tex0.psm) << " tfx=" << uint32_t(c.tex0.tfx) << " alpha=0x" << c.alpha
                    << " test=0x" << c.test << " ofs=" << std::dec << (c.xyoffset.ofx >> 4) << ","
                    << (c.xyoffset.ofy >> 4) << " sc=" << c.scissor.x0 << "-" << c.scissor.x1 << ","
                    << c.scissor.y0 << "-" << c.scissor.y1 << std::hex << " | blt 0x" << s.bitbltbuf.sbp << "/psm"
                    << uint32_t(s.bitbltbuf.spsm) << "->0x" << s.bitbltbuf.dbp << "/psm" << uint32_t(s.bitbltbuf.dpsm)
                    << " dir=" << s.trxdir;
                const std::string k = key.str();
                const bool touchesDisplay = c.frame.fbp == kDisplayFbp || c.tex0.tbp0 == kDisplayBlock ||
                                            c.tex0.tbp0 == kDrawBlock || s.bitbltbuf.dbp == kDisplayBlock ||
                                            s.bitbltbuf.sbp == kDrawBlock;
                if (counts[k]++ == 0 && touchesDisplay)
                {
                    std::cerr << "[gswatch] display-related state: " << k << std::endl;
                }
                ++polls;
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }

            std::vector<std::pair<uint64_t, std::string>> sorted;
            for (const auto &[k, n] : counts)
            {
                sorted.emplace_back(n, k);
            }
            std::sort(sorted.rbegin(), sorted.rend());
            std::cerr << "[gswatch] " << polls << " polls, " << counts.size() << " distinct states; most common:" << std::endl;
            for (size_t i = 0; i < sorted.size() && i < 25; ++i)
            {
                std::cerr << "  " << sorted[i].first << "x  " << sorted[i].second << std::endl;
            }
        }).detach();
        std::cerr << "[worms3d] GS watch for " << seconds << " s" << std::endl;
    }

    // GS dump: with $WORMS3D_GSDUMP=<seconds>, once, after that delay: print the GS debug history (last 512
    // events) for draws/transfers involving the display or draw buffer, then save both buffers as PNGs in
    // <elf dir>/../screenshots (the deploy folder): FBP 0xa0 PSMCT32 (where the game draws) and FBP 0x140
    // PSMCT16S (what is displayed).
    void savePsmImage(PS2Runtime &runtime, uint32_t psm, uint32_t fbp, const std::filesystem::path &path)
    {
        constexpr int kWidth = 640, kHeight = 448;
        std::vector<uint8_t> rgba(kWidth * kHeight * 4);
        uint32_t nonBlack = 0;
        for (int y = 0; y < kHeight; ++y)
        {
            for (int x = 0; x < kWidth; ++x)
            {
                const uint32_t v = runtime.gs().ReadVram(psm, fbp * 32, kWidth / 64, x, y);
                uint8_t *px = &rgba[(y * kWidth + x) * 4];
                if (psm == 0x00)
                {
                    px[0] = v & 0xff; px[1] = (v >> 8) & 0xff; px[2] = (v >> 16) & 0xff;
                }
                else
                {
                    px[0] = (v & 0x1f) << 3; px[1] = ((v >> 5) & 0x1f) << 3; px[2] = ((v >> 10) & 0x1f) << 3;
                }
                px[3] = 0xff;
                nonBlack += (px[0] | px[1] | px[2]) != 0;
            }
        }
        Image img{rgba.data(), kWidth, kHeight, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        ExportImage(img, path.string().c_str());
        std::cerr << std::dec << "[gsdump] fbp=0x" << std::hex << fbp << " psm=0x" << psm << std::dec << ": "
                  << nonBlack << "/" << kWidth * kHeight << " non-black pixels -> " << path.string() << std::endl;
    }

    // 8-bit indexed texture (PSMT8) as greyscale indices, to see whether texture data arrived in VRAM.
    void saveT8Indices(PS2Runtime &runtime, uint32_t block, uint32_t bw, int w, int h, const std::filesystem::path &path)
    {
        std::vector<uint8_t> rgba(w * h * 4);
        uint32_t nonZero = 0;
        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
                const uint8_t v = static_cast<uint8_t>(runtime.gs().ReadVram(0x13, block, bw, x, y));
                uint8_t *px = &rgba[(y * w + x) * 4];
                px[0] = px[1] = px[2] = v;
                px[3] = 0xff;
                nonZero += v != 0;
            }
        }
        Image img{rgba.data(), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        ExportImage(img, path.string().c_str());
        std::cerr << "[gsdump] T8 block=0x" << std::hex << block << std::dec << ": " << nonZero << "/" << w * h
                  << " non-zero indices -> " << path.string() << std::endl;
    }

    void startGsDump(PS2Runtime &runtime)
    {
        const char *env = std::getenv("WORMS3D_GSDUMP");
        const double delay = env ? std::atof(env) : 0.0;
        if (delay <= 0.0)
        {
            return;
        }
        // History recording starts paused (only the debug panel enables it).
        runtime.gs().setDebugHistoryPaused(false);

        std::thread([&runtime, delay]() {
            // Until the dump, poll the 512-entry history every 2 ms and log each distinct transfer destination
            // (first seen + count), so uploads made once at startup (e.g. CLUTs) are not lost.
            std::map<std::string, std::pair<uint64_t, uint64_t>> transfers; // key -> (first seq, count)
            uint64_t lastSeq = 0;
            const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(delay);
            while (std::chrono::steady_clock::now() < end)
            {
                for (const GSDebugHistoryEntry &e : runtime.gs().getDebugHistory())
                {
                    if (e.seq <= lastSeq)
                    {
                        continue;
                    }
                    lastSeq = e.seq;
                    if (e.kind != GSDebugEventKind::Transfer)
                    {
                        continue;
                    }
                    std::ostringstream key;
                    key << std::hex << "0x" << e.bitbltbuf.sbp << "/psm" << uint32_t(e.bitbltbuf.spsm) << " -> 0x"
                        << e.bitbltbuf.dbp << "/w" << std::dec << uint32_t(e.bitbltbuf.dbw) << "/psm" << std::hex
                        << uint32_t(e.bitbltbuf.dpsm) << " dir=" << e.trxdir << std::dec << " pixels=" << e.transferPixels
                        << " pos=" << e.trxpos.dsax << "," << e.trxpos.dsay << " size=" << e.trxreg.rrw << "x"
                        << e.trxreg.rrh;
                    auto [it, inserted] = transfers.try_emplace(key.str(), e.seq, 0);
                    ++it->second.second;
                    if (inserted)
                    {
                        std::cerr << "[gsdump] new transfer #" << e.seq << " vsync=" << e.vsyncTick << ": " << key.str() << std::endl;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }

            runtime.gs().setDebugHistoryPaused(true);
            const std::vector<GSDebugHistoryEntry> history = runtime.gs().getDebugHistory();
            runtime.gs().setDebugHistoryPaused(false);

            std::cerr << "[gsdump] " << history.size() << " history events; draws/transfers touching 0xa0/0x140:" << std::endl;
            for (const GSDebugHistoryEntry &e : history)
            {
                const bool draw = e.kind == GSDebugEventKind::Draw;
                const bool transfer = e.kind == GSDebugEventKind::Transfer;
                const bool relevant = draw || transfer;
                if (!relevant)
                {
                    continue;
                }
                std::ostringstream out;
                out << std::hex << "  #" << std::dec << e.seq << " vsync=" << e.vsyncTick << std::hex;
                if (draw)
                {
                    out << " DRAW frame=0x" << e.frame.fbp << "/psm" << uint32_t(e.frame.psm) << " msk=0x" << e.frame.fbmsk
                        << " prim=" << std::dec << uint32_t(e.prim.type) << (e.prim.tme ? " tme" : "")
                        << (e.prim.abe ? " abe" : "") << (e.prim.fst ? " fst" : "") << (e.prim.ctxt ? " ctx2" : "")
                        << std::hex << " tex0=0x" << e.tex0.tbp0 << "/psm" << uint32_t(e.tex0.psm) << " tfx="
                        << uint32_t(e.tex0.tfx) << " tcc=" << uint32_t(e.tex0.tcc) << " cbp=0x" << e.tex0.cbp << "/cpsm"
                        << uint32_t(e.tex0.cpsm) << " alpha=0x" << e.alpha
                        << " test=0x" << e.test << " zbuf=0x" << e.zbuf.zbp << "/psm" << uint32_t(e.zbuf.psm)
                        << (e.zbuf.zmask ? "/zmsk" : "") << std::dec << " z=" << e.zMin << ".." << e.zMax
                        << " verts=" << e.vertexCount << " x=" << e.xMin << ".."
                        << e.xMax << " y=" << e.yMin << ".." << e.yMax << " a=" << uint32_t(e.aMin) << ".."
                        << uint32_t(e.aMax);
                }
                else
                {
                    out << " XFER 0x" << e.bitbltbuf.sbp << "/psm" << uint32_t(e.bitbltbuf.spsm) << " -> 0x"
                        << e.bitbltbuf.dbp << "/psm" << uint32_t(e.bitbltbuf.dpsm) << " dir=" << e.trxdir << std::dec
                        << " pixels=" << e.transferPixels;
                }
                std::cerr << out.str() << std::endl;
            }

            const std::filesystem::path dir = PS2Runtime::getIoPaths().elfDirectory / ".." / "screenshots";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            savePsmImage(runtime, 0x00, 0xa0, dir / "gs-fbp0a0-ct32.png");
            savePsmImage(runtime, 0x0A, 0x140, dir / "gs-fbp140-ct16s.png");
            saveT8Indices(runtime, 0x3880, 2, 128, 128, dir / "gs-tex3880-t8.png");
            // CLUTs used by the loading sprites (CSM1 CT32 layout: 16x16 entries, block width 1).
            for (const uint32_t cbp : {0x3600u, 0x3700u})
            {
                uint32_t nonZero = 0;
                std::ostringstream first;
                for (int i = 0; i < 256; ++i)
                {
                    const uint32_t v = runtime.gs().ReadVram(0x00, cbp, 1, i % 16, i / 16);
                    nonZero += v != 0;
                    if (i < 6)
                    {
                        first << " 0x" << std::hex << v;
                    }
                }
                std::cerr << "[gsdump] CLUT block=0x" << std::hex << cbp << std::dec << ": " << nonZero
                          << "/256 non-zero entries; first:" << first.str() << std::endl;
            }
            saveT8Indices(runtime, 0x3800, 2, 128, 128, dir / "gs-tex3800-t8.png");
        }).detach();
        std::cerr << "[worms3d] GS dump after " << delay << " s" << std::endl;
    }

    // Handler trace: with $WORMS3D_HANDLERS=1, log every AddIntcHandler/AddDmacHandler/Enable*/SetAlarm call
    // (cause/channel, handler, caller) and count calls into FUN_004ee0c0 (the wakeup helper the vsync handler
    // uses) per caller. Used to find what keeps the main thread's invocation queue full.
    struct TracedSyscall
    {
        uint32_t address;
        const char *name;
        PS2Runtime::RecompiledFunction original;
    };
    std::array<TracedSyscall, 6> g_tracedSyscalls{{
        {0x004ecdc0u, "AddIntcHandler", nullptr},
        {0x004ecdf0u, "AddDmacHandler", nullptr},
        {0x004ece20u, "EnableIntc", nullptr},
        {0x004ece40u, "EnableDmac", nullptr},
        {0x004ece60u, "SetAlarm", nullptr},
        {0x004ece80u, "iEnableIntc", nullptr},
    }};
    PS2Runtime::RecompiledFunction g_origWakeHelper = nullptr;
    std::mutex g_wakeCountMutex;
    std::map<uint32_t, uint64_t> g_wakeCountByCaller;
    std::atomic<uint64_t> g_setAlarmCount{0};

    template <size_t Index>
    void tracedSyscallWrapper(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const TracedSyscall &sc = g_tracedSyscalls[Index];
        const uint32_t a0 = getRegU32(ctx, 4), a1 = getRegU32(ctx, 5), a2 = getRegU32(ctx, 6);
        const uint32_t ra = getRegU32(ctx, 31);
        sc.original(rdram, ctx, runtime);
        const bool isAlarm = std::string_view(sc.name) == "SetAlarm";
        const uint64_t n = isAlarm ? ++g_setAlarmCount : 0;
        if (!isAlarm || n <= 8 || (n & (n - 1)) == 0)
        {
            std::cerr << std::hex << "[handlers] " << sc.name << "(0x" << a0 << ", 0x" << a1 << ", 0x" << a2
                      << ") = 0x" << getRegU32(ctx, 2) << " ra=0x" << ra << std::dec
                      << (isAlarm ? " #" + std::to_string(n) : std::string()) << std::endl;
        }
    }

    void wakeHelperWrapper(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        {
            std::lock_guard<std::mutex> lock(g_wakeCountMutex);
            ++g_wakeCountByCaller[getRegU32(ctx, 31)];
        }
        g_origWakeHelper(rdram, ctx, runtime);
    }

    void startHandlerTrace(PS2Runtime &runtime)
    {
        const char *env = std::getenv("WORMS3D_HANDLERS");
        if (!env || env[0] == '\0' || env[0] == '0')
        {
            return;
        }

        auto wrap = [&runtime](auto indexConstant) {
            constexpr size_t index = decltype(indexConstant)::value;
            TracedSyscall &sc = g_tracedSyscalls[index];
            sc.original = runtime.lookupFunction(sc.address);
            if (sc.original)
            {
                runtime.replaceFunction(sc.address, tracedSyscallWrapper<index>);
            }
        };
        wrap(std::integral_constant<size_t, 0>{});
        wrap(std::integral_constant<size_t, 1>{});
        wrap(std::integral_constant<size_t, 2>{});
        wrap(std::integral_constant<size_t, 3>{});
        wrap(std::integral_constant<size_t, 4>{});
        wrap(std::integral_constant<size_t, 5>{});

        g_origWakeHelper = runtime.lookupFunction(0x004ee0c0u);
        if (g_origWakeHelper)
        {
            runtime.replaceFunction(0x004ee0c0u, wakeHelperWrapper);
        }

        std::thread([]() {
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                std::ostringstream out;
                out << "[handlers] 5s: SetAlarm total=" << g_setAlarmCount.load() << "; FUN_004ee0c0 calls by caller:";
                {
                    std::lock_guard<std::mutex> lock(g_wakeCountMutex);
                    for (const auto &[ra, n] : g_wakeCountByCaller)
                    {
                        out << std::hex << " 0x" << ra << std::dec << "=" << n;
                    }
                    g_wakeCountByCaller.clear();
                }
                std::cerr << out.str() << std::endl;
            }
        }).detach();
        std::cerr << "[worms3d] handler trace on" << std::endl;
    }

    void applyWorms3dOverrides(PS2Runtime &runtime)
    {
        startHandlerTrace(runtime);
        startGsDump(runtime);
        startGsWatch(runtime);
        startSampler(runtime);
        reserveMainThreadStack(runtime);
        runtime.replaceFunction(0x004ed6a8u, consoleWrite);
        configureCdImage();
        configureHost0();
    }
}

PS2_REGISTER_GAME_OVERRIDE("Worms 3D (SLUS_208.94)", "SLUS_208.94", 0x00100008u, 0u, applyWorms3dOverrides)
