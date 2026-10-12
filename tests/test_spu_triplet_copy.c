/* Compared with the actual locally generated loop, not a rewritten oracle. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include "haze_spu_triplet_copy.h"

void haze_triplet_reference(spu_context* ctx);
void haze_triplet_candidate(spu_context* ctx);
int g_spu_ls_watch_n;
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
static unsigned enabled = 1, hits;
int haze_spu_triplet_copy_enabled(void) { return enabled; }
void haze_spu_triplet_copy_note(unsigned items) { assert(items); ++hits; }
void spu_ls_watch_slow(uint32_t a, int b, const uint8_t* p, uint32_t pc, uint32_t lr)
{ (void)a; (void)b; (void)p; (void)pc; (void)lr; }

static spu_context original, native;
static uint32_t seed = 0x6342F1A5;
static uint32_t random_word(void)
{ seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static void reset(uint32_t first, unsigned count, uint32_t source, uint32_t target,
                  uint32_t ring_start)
{
    memset(&original, 0, sizeof(original));
    original.ls = original.ls_store;
    for (unsigned i = 0; i != sizeof(original.gpr)/4; ++i)
        ((uint32_t*)original.gpr)[i] = random_word();
    for (unsigned i = 0; i != SPU_LS_SIZE/4; ++i) {
        uint32_t bits = random_word();
        memcpy(original.ls + i*4, &bits, 4);
    }
    original.image_id = 42; original.pc = 0x35F8;
    original.gpr[15]._u32[0] = first;
    original.gpr[16]._u32[0] = first + count;
    original.gpr[17]._u32[0] = ring_start;
    original.gpr[18]._u32[0] = source;
    original.gpr[14]._u32[0] = target;
    original.gpr[20]._u32[0] = 0xFFFF;
    original.gpr[21]._u32[0] = 2047;
    memcpy(&native, &original, sizeof(native)); native.ls = native.ls_store;
}

static void compare(int expect_native)
{
    const unsigned before = hits;
    haze_triplet_reference(&original);
    void (*exit_fn)(spu_context*) = g_spu_trampoline_fn;
    haze_triplet_candidate(&native);
    assert(exit_fn == g_spu_trampoline_fn && native.pc == 0x3690);
    assert(hits - before == (unsigned)expect_native);
    // Normalize only the host pointer. Every guest register and LS byte counts.
    original.ls = native.ls;
    if (memcmp(&original, &native, sizeof(native))) {
        for (unsigned i = 0; i != sizeof(native); ++i)
            if (((unsigned char*)&original)[i] != ((unsigned char*)&native)[i]) {
                fprintf(stderr, "Full SPU state differs at byte %u\n", i); break;
            }
        abort();
    }
}

static void unchanged_guard(void)
{
    memcpy(&original, &native, sizeof(native));
    assert(haze_spu_triplet_copy(&native) == 0);
    assert(!memcmp(&original, &native, sizeof(native)));
}

static double clock_seconds(void)
{
    LARGE_INTEGER now, frequency;
    assert(QueryPerformanceCounter(&now) && QueryPerformanceFrequency(&frequency));
    return (double)now.QuadPart / frequency.QuadPart;
}

int main(void)
{
    g_spu_ls_probe = g_spu_smc_watch = g_spu_ls_watch_n = 0;
    const uint32_t firsts[] = {0, 1, 2047, 2048, 65535, 0xFFFFFFFE};
    const unsigned counts[] = {1, 2, 3, 17, 255, 2048};
    unsigned comparisons = 0;
    for (unsigned f = 0; f != 6; ++f)
        for (unsigned n = 0; n != 6; ++n)
            for (unsigned alignment = 0; alignment != 4; ++alignment)
                for (unsigned alias = 0; alias != 5; ++alias) {
                    uint32_t source = 0x3FFF0 + alignment*4;
                    uint32_t target = alias == 0 ? 0x18000 : source + (alias-1)*4;
                    reset(firsts[f], counts[n], source, target, 2047);
                    compare(counts[n] > 1); ++comparisons;
                }
    for (unsigned i = 0; i != 2048; ++i) {
        const uint32_t source = random_word() & ~3u;
        const uint32_t target = i%3 ? source + (i%11)*4 : random_word() & ~3u;
        const uint32_t first = random_word(), ring = random_word();
        reset(first, 2 + i%255, source, target, ring);
        compare(1); ++comparisons;
    }
    // Unsupported alignment, masks, image, PC and active diagnostics fall back.
    for (unsigned guard = 0; guard != 9; ++guard) {
        reset(0, 31, 0x1000, 0x2000, 2040);
        switch (guard) {
        case 0: native.gpr[18]._u32[0]++; original.gpr[18]._u32[0]++; break;
        case 1: native.gpr[14]._u32[0]++; original.gpr[14]._u32[0]++; break;
        case 2: native.gpr[20]._u32[0]--; original.gpr[20]._u32[0]--; break;
        case 3: native.gpr[21]._u32[0]--; original.gpr[21]._u32[0]--; break;
        case 4: native.image_id = original.image_id = 41; break;
        case 5: native.pc = original.pc = 0x35FC; break;
        case 6: native.policy_mode = original.policy_mode = 1; break;
        case 7: g_spu_ls_watch_n = 1; break;
        case 8: enabled = 0; break;
        }
        compare(0); ++comparisons;
        enabled = 1; g_spu_ls_watch_n = 0;
    }
    reset(0, 31, 0x1000, 0x2000, 2040);
    g_spu_ls_probe = -1; unchanged_guard(); g_spu_ls_probe = 0;
    g_spu_smc_watch = 1; unchanged_guard(); g_spu_smc_watch = 0;
    native.gpr[16]._u32[0] = 0; unchanged_guard();
    native.gpr[16]._u32[0] = 2049; unchanged_guard();
    native.ls = original.ls_store; unchanged_guard();
    printf("Triplet copy: %u complete-state comparisons passed (ring/LS/integer wrap, overlap, lanes, guards)\n", comparisons);

    reset(0, 1024, 0x10000, 0x20000, 0);
    const unsigned repeats = 2000;
    double begin = clock_seconds();
    for (unsigned i = 0; i != repeats; ++i) {
        original.pc = 0x35F8; original.gpr[15]._u32[0] = 0;
        haze_triplet_reference(&original);
    }
    const double reference_s = clock_seconds() - begin;
    begin = clock_seconds();
    for (unsigned i = 0; i != repeats; ++i) {
        native.pc = 0x35F8; native.gpr[15]._u32[0] = 0;
        haze_triplet_candidate(&native);
    }
    const double native_s = clock_seconds() - begin;
    printf("Loop microbenchmark only: reference %.3f us/call; native %.3f us/call; ratio %.3f\n",
        reference_s*1e6/repeats, native_s*1e6/repeats, reference_s/native_s);
    return 0;
}
