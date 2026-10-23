#include "game_overrides.h"
#include "ps2_runtime.h"
#include "runtime/ps2_memory.h"

#include <cstdio>

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

    void applyWorms3dOverrides(PS2Runtime &runtime)
    {
        runtime.replaceFunction(0x004ed6a8u, consoleWrite);
    }
}

PS2_REGISTER_GAME_OVERRIDE("Worms 3D (SLUS_208.94)", "SLUS_208.94", 0x00100008u, 0u, applyWorms3dOverrides)
