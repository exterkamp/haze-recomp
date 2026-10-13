#pragma once
#include "haze_pump_intervals.h"
#ifdef _WIN32
struct HazePumpProfile {
    const char* csv_path=getenv("HAZE_PUMP_CSV");
    bool enabled = getenv("HAZE_PUMP_PROFILE") != nullptr || csv_path!=nullptr;
    LONGLONG frequency=0, epoch=0, wait=0, fifo=0, present=0, tick=0;
    unsigned drains=0, presents=0;
    unsigned waits=0, queued_waits=0, wait_ms=1;
    const char* wait_control=getenv("HAZE_FIFO_WAIT_FILE");
    HazePumpIntervals intervals;
    static constexpr unsigned capacity=16384;
    HazePumpInterval rows[capacity]{};
    uint64_t recorded=0;
    HazePumpProfile() {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f); frequency=f.QuadPart;
        epoch=now();
    }
    LONGLONG now() const {
        if (!enabled) return 0;
        LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart;
    }
    void add_wait(LONGLONG elapsed) {
        wait+=elapsed; if(csv_path) intervals.pending.wait+=elapsed;
    }
    void add_fifo(LONGLONG elapsed) {
        fifo+=elapsed; ++drains;
        if(csv_path) { intervals.pending.fifo+=elapsed; ++intervals.pending.drains; }
    }
    void add_tick(LONGLONG elapsed) {
        tick+=elapsed; if(csv_path) intervals.pending.tick+=elapsed;
    }
    void note_wait(bool queued) {
        ++waits; queued_waits+=queued;
        if(csv_path) { ++intervals.pending.waits; intervals.pending.queued_waits+=queued; }
    }
    void finish_present(LONGLONG begin, unsigned frame, unsigned flips) {
        const LONGLONG end=now();
        present+=end-begin; ++presents;
        if(!csv_path) return;
        intervals.pending.present+=end-begin;
        FILETIME creation,exit,kernel,user;
        const bool cpu_valid=GetThreadTimes(GetCurrentThread(),&creation,&exit,&kernel,&user)!=0;
        const uint64_t cpu=cpu_valid ?
            ((uint64_t)kernel.dwHighDateTime<<32)+kernel.dwLowDateTime+
            ((uint64_t)user.dwHighDateTime<<32)+user.dwLowDateTime : 0;
        HazePumpInterval row;
        if(intervals.finish(end,cpu,cpu_valid,frame,flips,row))
            rows[recorded++%capacity]=row;
    }
    void dump() const {
        if(!csv_path) return;
        FILE* csv=fopen(csv_path,"wb");
        if(!csv) { fprintf(stderr,"[pump-csv] cannot open output\n"); return; }
        fprintf(csv,"qpc_frequency,frame,flips,begin_qpc,end_qpc,dt_ms,wait_ms,fifo_ms,present_ms,tick_ms,other_ms,thread_cpu_ms,cpu_valid,waits,queued_waits,drains\n");
        const double scale=1000.0/frequency;
        const uint64_t first=recorded>capacity ? recorded-capacity : 0;
        for(uint64_t i=first;i<recorded;++i) {
            const auto& r=rows[i%capacity];
            const auto other=r.end-r.begin-r.wait-r.fifo-r.present-r.tick;
            fprintf(csv,"%lld,%u,%u,%lld,%lld,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%u,%u,%u\n",
                frequency,r.frame,r.flips,r.begin,r.end,(r.end-r.begin)*scale,
                r.wait*scale,r.fifo*scale,r.present*scale,r.tick*scale,other*scale,
                r.cpu_100ns/10000.0,(unsigned)r.cpu_valid,r.waits,r.queued_waits,r.drains);
        }
        fclose(csv);
        fprintf(stderr,"[pump-csv] recorded=%llu retained=%llu\n",
            (unsigned long long)recorded,(unsigned long long)(recorded-first));
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
    void add_wait(int64_t) {}
    void add_fifo(int64_t) {}
    void add_tick(int64_t) {}
    void note_wait(bool) {}
    void finish_present(int64_t,unsigned,unsigned) {}
    void dump() const {}
};
static HazePumpProfile g_pump_profile;
#endif
