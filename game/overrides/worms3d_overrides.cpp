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
#include <sstream>
#include <thread>
#include <vector>

#include "runtime/ee_scheduler.h"
#include "runtime/gs/gs_frontend.h"

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
                        << " prio=" << std::dec << t.currentPriority << std::hex;
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

    void applyWorms3dOverrides(PS2Runtime &runtime)
    {
        startSampler(runtime);
        reserveMainThreadStack(runtime);
        runtime.replaceFunction(0x004ed6a8u, consoleWrite);
        configureCdImage();
        configureHost0();
    }
}

PS2_REGISTER_GAME_OVERRIDE("Worms 3D (SLUS_208.94)", "SLUS_208.94", 0x00100008u, 0u, applyWorms3dOverrides)
