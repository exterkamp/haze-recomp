#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
void report_mode(int enabled)
{
#ifdef _WIN32
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    std::fprintf(stderr, "[haze-spu-triplet-mode] enabled=%d qpc=%lld\n", enabled, now.QuadPart);
#else
    (void)enabled;
#endif
}
struct settings {
    const char* control = std::getenv("HAZE_TRIPLET_COPY_FILE");
    std::atomic<int> enabled{0};
#ifdef _WIN32
    SRWLOCK lock = SRWLOCK_INIT;
    std::atomic<ULONGLONG> next_check{0};
#endif
    settings() {
        const char* value = std::getenv("HAZE_NATIVE_TRIPLET_COPY");
        enabled.store(value && std::strcmp(value, "1") == 0);
        if (control || value) report_mode(enabled.load());
    }
};
}

// Default off until ordinary gameplay and critical-path timing are compared.
extern "C" int haze_spu_triplet_copy_enabled(void)
{
    static settings mode;
#ifdef _WIN32
    if (mode.control && GetTickCount64() >= mode.next_check.load(std::memory_order_relaxed) &&
        TryAcquireSRWLockExclusive(&mode.lock)) {
        const ULONGLONG now = GetTickCount64();
        if (now >= mode.next_check.load(std::memory_order_relaxed)) {
            mode.next_check.store(now + 100, std::memory_order_relaxed);
            FILE* file = std::fopen(mode.control, "r");
            unsigned value;
            if (file) {
                if (std::fscanf(file, "%u", &value) == 1 && value <= 1 &&
                    mode.enabled.exchange((int)value) != (int)value)
                    report_mode((int)value);
                std::fclose(file);
            }
        }
        ReleaseSRWLockExclusive(&mode.lock);
    }
#endif
    return mode.enabled.load();
}

extern "C" void haze_spu_triplet_copy_note(unsigned items)
{
    static std::atomic<bool> reported{false};
    if (!reported.exchange(true, std::memory_order_relaxed))
        std::fprintf(stderr, "[haze-spu-triplet-copy] image=42 pc=000035F8 native_items=%u\n", items);
}
