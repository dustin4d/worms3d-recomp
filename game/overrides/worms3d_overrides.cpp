#include "game_overrides.h"
#include "ps2_runtime.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>

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
    // import (first symptom: "unhandled import libsd:4"). game/host0/modules holds lowercase links to the
    // disc modules so the real LIBSD.IRX loads. $WORMS3D_HOST0 overrides the directory.
    void configureHost0()
    {
        PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();

        std::filesystem::path host0;
        if (const char *env = std::getenv("WORMS3D_HOST0"); env && env[0] != '\0')
        {
            host0 = env;
        }
        else
        {
            host0 = paths.elfDirectory / ".." / "game" / "host0";
        }

        std::error_code ec;
        if (!std::filesystem::is_directory(host0, ec))
        {
            std::cerr << "[worms3d] host0 directory missing: " << host0.string() << std::endl;
            return;
        }

        paths.hostRoot = std::filesystem::weakly_canonical(host0, ec);
        PS2Runtime::setIoPaths(paths);
        std::cerr << "[worms3d] host0: " << paths.hostRoot.string() << std::endl;
    }

    void applyWorms3dOverrides(PS2Runtime &runtime)
    {
        runtime.replaceFunction(0x004ed6a8u, consoleWrite);
        configureCdImage();
        configureHost0();
    }
}

PS2_REGISTER_GAME_OVERRIDE("Worms 3D (SLUS_208.94)", "SLUS_208.94", 0x00100008u, 0u, applyWorms3dOverrides)
