#include "ppu_recomp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" uint8_t* vm_base;

// BLUS30094's libc memmove. Keep its overlap semantics while letting Wasm's
// bulk-memory instruction replace thousands of lifted loads and stores.
extern "C" void __wrap__Z13func_00C07818P11ppu_context(ppu_context* ctx)
{
    const uint32_t destination = (uint32_t)ctx->gpr[3];
    const uint32_t source = (uint32_t)ctx->gpr[4];
    const uint32_t size = (uint32_t)ctx->gpr[5];
    if ((uint64_t)destination + size > 0x100000000ull ||
        (uint64_t)source + size > 0x100000000ull) {
        fprintf(stderr, "[wasm-memmove] invalid guest range: %08X <- %08X size=%08X\n",
                destination, source, size);
        abort();
    }
    memmove(vm_base + destination, vm_base + source, size);
    static thread_local unsigned calls;
    if (++calls <= 3) fprintf(stderr, "[wasm-memmove] bulk copy %u bytes\n", size);
}
