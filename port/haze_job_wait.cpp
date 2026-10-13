#include "ppu_recomp.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
thread_local bool completion_delay = false;
struct CompletionScope {
    bool previous = completion_delay;
    explicit CompletionScope(ppu_context* ctx) {
        completion_delay = ctx->thread_id == 1 && ctx->gpr[2] == 0xE94EE0 &&
            ctx->lr == 0x00682574 && ctx->gpr[31] < 512;
    }
    ~CompletionScope() { completion_delay = previous; }
};
}

void haze_job_wait_call(ppu_context* ctx) {
    CompletionScope scope(ctx);
    // Retain the original duration conversion, conditional delay, guest ABI
    // and stack behavior. Only its guarded usleep may choose a native policy.
    func_0068EFC0(ctx);
}

bool haze_job_wait_usleep(ppu_context* ctx) {
#ifdef _WIN32
    if (!completion_delay || ctx->lr != 0x0068EFF0 ||
        ctx->gpr[11] != 0x8D || ctx->gpr[3] != 330) return false;
    static const char* control = std::getenv("HAZE_JOB_WAIT_FILE");
    static int enabled = [] {
        const char* value = std::getenv("HAZE_NATIVE_JOB_WAIT");
        return value && std::strcmp(value, "1") == 0 ? 1 : 0;
    }();
    if (!control && !enabled) return false;
    static ULONGLONG next_check = 0;
    static int reported = -1;
    static unsigned long long original_calls = 0, native_calls = 0, yields = 0;
    const ULONGLONG now = GetTickCount64();
    if (control && now >= next_check) {
        next_check = now + 100;
        if (FILE* file = std::fopen(control, "r")) {
            unsigned value;
            if (std::fscanf(file, "%u", &value) == 1 && value <= 1) enabled = (int)value;
            std::fclose(file);
        }
    }
    if (reported != enabled) {
        LARGE_INTEGER qpc; QueryPerformanceCounter(&qpc);
        std::fprintf(stderr, "[haze-job-wait-counts] qpc=%lld original=%llu native=%llu yields=%llu\n",
            qpc.QuadPart, original_calls, native_calls, yields);
        std::fprintf(stderr, "[haze-job-wait-mode] enabled=%d qpc=%lld\n", enabled, qpc.QuadPart);
        reported = enabled;
    }
    if (!enabled) { ++original_calls; return false; }
    ++native_calls;
    // Retry the genuine completion predicate and service pending work sooner.
    // Each retry is bounded, with a scheduler yield every 64 native delays.
    // No completion state, clocks, queue contents or renderer limits change.
    for (unsigned i = 0; i < 32; ++i) YieldProcessor();
    if ((native_calls & 63u) == 0) { SwitchToThread(); ++yields; }
    ctx->gpr[3] = 0; // sys_timer_usleep's successful result; other state is intact.
    return true;
#else
    (void)ctx;
    return false;
#endif
}
