#pragma once
#ifdef _WIN32
struct HazePumpProfile {
    bool enabled = getenv("HAZE_PUMP_PROFILE") != nullptr;
    LONGLONG frequency=0, epoch=0, wait=0, fifo=0, present=0, tick=0;
    unsigned drains=0, presents=0;
    unsigned waits=0, queued_waits=0, wait_ms=1;
    const char* wait_control=getenv("HAZE_FIFO_WAIT_FILE");
    HazePumpProfile() {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f); frequency=f.QuadPart;
        epoch=now();
    }
    LONGLONG now() const {
        if (!enabled) return 0;
        LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart;
    }
    void report() {
        const LONGLONG t=now();
        if (!enabled || t-epoch<frequency*2) return;
        const double scale=1000.0/frequency;
        fprintf(stderr,"[pump-perf] elapsed_ms=%.3f drains=%u presents=%u wait_ms=%.3f fifo_ms=%.3f present_ms=%.3f tick_ms=%.3f waits=%u queued_waits=%u timeout_ms=%u\n",
            (t-epoch)*scale,drains,presents,wait*scale,fifo*scale,present*scale,tick*scale,waits,queued_waits,wait_ms);
        epoch=t; wait=fifo=present=tick=0; drains=presents=0;
        waits=queued_waits=0;
        /* Optional live A/B control: the timeout is otherwise always 1 ms.
         * Poll once per report, never per draw or command. */
        if (wait_control) {
            FILE* file=fopen(wait_control,"r"); unsigned value;
            if (file) {
                if (fscanf(file,"%u",&value)==1 && value<=16 && value!=wait_ms) {
                    wait_ms=value;
                    fprintf(stderr,"[pump-perf] timeout changed to %u ms\n",wait_ms);
                }
                fclose(file);
            }
        }
    }
};
static HazePumpProfile g_pump_profile;
#else
struct HazePumpProfile {
    bool enabled=false;
    unsigned waits=0, queued_waits=0, wait_ms=1;
    int64_t wait=0, fifo=0, present=0, tick=0;
    unsigned drains=0, presents=0;
    int64_t now() const { return 0; }
    void report() {}
};
static HazePumpProfile g_pump_profile;
#endif
