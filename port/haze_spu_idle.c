/* Haze lift 0028 (runtime image 29), LS 0x3378: failed pop -> retry SAME queue.
 * The real translated pop still runs and establishes its reservation/state.
 * Only the retry after AGAIN may park; no guest data or registers are changed.
 */
#include "spu_coherency.h"
#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "synchronization.lib")
extern unsigned char* vm_base;

static int idle_wait_enabled(void)
{
    static _Thread_local int initialized, enabled, reported = -1;
    static _Thread_local const char* control;
    static _Thread_local ULONGLONG next_check;
    if (!initialized) {
        const char* setting = getenv("HAZE_SPU_IDLE_WAIT");
        enabled = setting && atoi(setting) != 0;
        control = getenv("HAZE_SPU_IDLE_CONTROL");
        initialized = 1;
    }
    if (control && GetTickCount64() >= next_check) {
        next_check = GetTickCount64() + 100;
        FILE* file = fopen(control, "r");
        unsigned value;
        if (file) {
            if (fscanf(file, "%u", &value) == 1 && value <= 1) enabled = value;
            fclose(file);
        }
    }
    if (reported != enabled) {
        if (control || enabled) {
            LARGE_INTEGER now; QueryPerformanceCounter(&now);
            fprintf(stderr, "[haze-spu-idle-mode] tid=%lu enabled=%d qpc=%lld\n",
                GetCurrentThreadId(), enabled, now.QuadPart);
        }
        reported = enabled;
    }
    return enabled;
}

static uint32_t read_be32(const unsigned char* p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

#ifndef HAZE_IDLE_BEFORE_WAIT
#define HAZE_IDLE_BEFORE_WAIT() ((void)0)
#endif

/* Kept separate for predicate/race tests, which use actual coherence wakes. */
static int idle_wait_empty_queue(spu_context* ctx)
{
    if (!vm_base || !ctx->ls || ctx->image_id != 29 || ctx->pc != 0x33A4 ||
        ctx->gpr[3]._u32[0] != 0x80410901u ||
        (ctx->int_enable && (ctx->event_mask & ctx->event_status))) return 0;
    uint32_t base = ctx->gpr[83]._u32[0];
    if (ctx->gpr[84]._u32[0] != 4 || ctx->gpr[82]._u32[0] != base + 4 ||
        (base & 127u) || base > SPU_LS_SIZE - 128) return 0;
    /* The audited caller rotates descriptor word +4 into EA's low word;
     * rotqmbyi -4 supplies its zero high word. Callee saves r80..r87. */
    uint32_t queue = read_be32(ctx->ls + base + 4);
    if (!queue || (queue & 127u) || queue > UINT32_MAX - 127) return 0;
    spu_lockline_lock();
    unsigned char* header = vm_base + queue;
    uint32_t head = read_be32(header), tail = read_be32(header + 4);
    uint32_t size = read_be32(header + 8), depth = read_be32(header + 12);
    int cookie = -1;
    uint64_t snapshot = 0;
    /* No inference from normalized/claimed indices: only a genuinely empty,
     * unclaimed PPU->SPU ring can park. A ready queue always retries at once. */
    if (head == tail && !(head & 0x80000000u) && size && !(size & 15u) &&
        depth && depth <= 0x3FFFFFFFu && head < depth * 2u &&
        read_be32(header + 28) == 2) {
        memcpy(&snapshot, header, sizeof(snapshot));
        cookie = spu_coh_watch_queue(queue, header);
    }
    spu_lockline_unlock();
    if (cookie < 0) return 0; /* Collision: retain the original retry loop. */
    static _Thread_local int first_park;
    if (!first_park) {
        first_park = 1;
        fprintf(stderr, "[haze-spu-idle-park] tid=%lu queue=%08X depth=%u\n",
            GetCurrentThreadId(), queue, depth);
    }
    HAZE_IDLE_BEFORE_WAIT();
    /* Compare covers publication between unlock and sleep. Notifications wake
     * immediately; a 1ms timeout bounds missing writers and pending interrupts.
     * The next original pop checks the predicate again, including spurious wakes. */
    WaitOnAddress(header, &snapshot, sizeof(snapshot), 1);
    spu_lockline_lock();
    spu_coh_unwatch_queue(cookie);
    spu_lockline_unlock();
    return 1;
}

void haze_spu_idle_wait(spu_context* ctx)
{
    if (idle_wait_enabled()) idle_wait_empty_queue(ctx);
}
#else
void haze_spu_idle_wait(spu_context* ctx) { (void)ctx; }
#endif
