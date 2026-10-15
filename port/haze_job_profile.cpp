#include "haze_job_profile.h"
#if defined(HAZE_JOB_PROFILE) && defined(_WIN32)
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>
#include <atomic>
#include <cstring>
#include "spu_queue_diagnostic.h"
extern "C" uint8_t* vm_base;
extern "C" int ppu_native_memory_range(uint32_t address, uint32_t size);
extern "C" int ppu_native_memory_available(void);
extern "C" void spu_lockline_lock(void);
extern "C" void spu_lockline_unlock(void);

namespace {
struct Row {
    uint64_t begin,end,job,caller,ticks[4],calls[4];
    unsigned depth;
};
constexpr unsigned capacity=65536;
const char* path() { static const char* value=std::getenv("HAZE_JOB_PROFILE_CSV"); return value; }
std::mutex row_mutex;
std::vector<Row> rows;
uint64_t recorded=0;
thread_local unsigned nesting=0;
uint64_t clock_now() { LARGE_INTEGER value; QueryPerformanceCounter(&value); return value.QuadPart; }

char completion_path[4096];
std::atomic<unsigned> completion_initialized{0};
std::atomic<bool> completion_dumped{false};
DWORD completion_owner;
uint32_t completion_read32(uint32_t ea) {
    const uint8_t* p=vm_base+ea;
    return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
}
bool completion_prepare(unsigned thread,uint64_t toc) {
    if(thread!=1 || toc!=0xE94EE0 || completion_dumped.load(std::memory_order_acquire)) return false;
    unsigned uninitialized=0;
    if(completion_initialized.compare_exchange_strong(uninitialized,1,std::memory_order_acq_rel)) {
        // Guarded guest thread 1 is the sole configuration owner. Publish the
        // copied path before shutdown can inspect it. No producer does I/O.
        const char* setting=std::getenv("HAZE_COMPLETION_QUEUE_CSV");
        if(setting && *setting && std::strlen(setting)<sizeof completion_path)
            std::memcpy(completion_path,setting,std::strlen(setting)+1);
        completion_owner=GetCurrentThreadId();
        completion_initialized.store(2,std::memory_order_release);
    }
    if(completion_initialized.load(std::memory_order_acquire)!=2) return false;
    if(!completion_path[0] || completion_owner!=GetCurrentThreadId() ||
        !ppu_native_memory_available()) return false;
    const uint32_t root_ea=uint32_t(toc)-0x7C1Cu;
    if(!ppu_native_memory_range(root_ea,4)) return false;
    const uint32_t root=completion_read32(root_ea);
    if(root<0x7FD8u || (root&3u) || !ppu_native_memory_range(root-0x7FD8u,4)) return false;
    const uint32_t table=completion_read32(root-0x7FD8u);
    if(!table || (table&3u) || !ppu_native_memory_range(table,4)) return false;
    const uint32_t queue=completion_read32(table);
    if(!queue || (queue&127u) || !ppu_native_memory_range(queue,128)) return false;
    const uint32_t depth=completion_read32(queue+12),buffer=completion_read32(queue+20);
    if(!depth || depth>SPU_QUEUE_DIAG_DEPTH || !buffer || (buffer&15u) ||
        !ppu_native_memory_range(buffer,depth*16u)) return false;
    spu_lockline_lock();
    const bool configured=spu_queue_diag_configure_locked(queue,vm_base+queue,
        vm_base+buffer,buffer,depth)!=0;
    spu_lockline_unlock();
    return configured;
}
void completion_dump() {
    if(completion_dumped.exchange(true,std::memory_order_acq_rel)) return;
    spu_queue_diag_snapshot snapshot{};
    spu_queue_diag_stop(&snapshot); // freezes RAM before any file operations
    if(completion_initialized.load(std::memory_order_acquire)!=2 || !completion_path[0]) return;
    FILE* file=std::fopen(completion_path,"wb");
    if(!file) { std::fprintf(stderr,"[completion-queue-profile] cannot open output\n"); return; }
    std::fprintf(file,"qpc_frequency,sequence,queue_epoch,publication_ordinal,submit_generation,queue_ea,slot,index,flags,submit_qpc,publication_qpc,pop_qpc,producer_tid,consumer_tid\n");
    const uint64_t first=snapshot.recorded>SPU_QUEUE_DIAG_CAPACITY?snapshot.recorded-SPU_QUEUE_DIAG_CAPACITY:0;
    for(uint64_t i=first;i<snapshot.recorded;++i) {
        const auto& r=snapshot.rows[i%SPU_QUEUE_DIAG_CAPACITY];
        std::fprintf(file,"%llu,%llu,%llu,%llu,%llu,%u,%u,%u,%u,%llu,%llu,%llu,%u,%u\n",
            snapshot.frequency,r.sequence,r.epoch,r.ordinal,r.generation,
            r.queue,r.slot,r.index,r.flags,r.submit_qpc,r.publication_qpc,r.pop_qpc,
            r.producer_tid,r.consumer_tid);
    }
    std::fclose(file);
    char metadata_path[sizeof completion_path+16];
    std::snprintf(metadata_path,sizeof metadata_path,"%s.meta.json",completion_path);
    FILE* metadata=std::fopen(metadata_path,"wb");
    if(metadata) {
        std::fprintf(metadata,"{\n  \"schema\": 2,\n  \"qpc_frequency\": %llu,\n"
            "  \"begin_qpc\": %llu,\n  \"end_qpc\": %llu,\n"
            "  \"recorded\": %llu,\n  \"retained\": %llu,\n  \"epochs\": %llu,\n"
            "  \"publications\": %llu,\n  \"submits\": %llu,\n  \"invalidations\": %llu,\n"
            "  \"unsupported_tail\": %llu,\n  \"pending_overwritten\": %llu,\n"
            "  \"pending_at_stop\": %llu,\n  \"ambiguous_submits\": %llu,\n"
            "  \"max_overwritten_pop_qpc\": %llu,\n"
            "  \"min_pending_publication_qpc\": %llu,\n  \"min_pending_submit_qpc\": %llu,\n"
            "  \"pending_unknown_publication_qpc\": %llu,\n  \"pending_unknown_submit_qpc\": %llu,\n"
            "  \"publication_is_locked_notification\": true,\n"
            "  \"service_and_callback_phases\": \"missing\",\n"
            "  \"flag_bits\": {\"no_publication\": 1, \"no_generation\": 2, "
            "\"ambiguous_generation\": 4, \"record_mismatch\": 8, \"bad_clock\": 16}\n}\n",
            snapshot.frequency,snapshot.begin_qpc,snapshot.end_qpc,
            snapshot.recorded,snapshot.recorded-first,snapshot.epochs,
            snapshot.publications,snapshot.submits,snapshot.invalidations,
            snapshot.unsupported_tail,snapshot.pending_overwritten,
            snapshot.pending_at_stop,snapshot.ambiguous_submits,
            snapshot.max_overwritten_pop_qpc,snapshot.min_pending_publication_qpc,
            snapshot.min_pending_submit_qpc,snapshot.pending_unknown_publication_qpc,
            snapshot.pending_unknown_submit_qpc);
        std::fclose(metadata);
    } else std::fprintf(stderr,"[completion-queue-profile] cannot open metadata output\n");
    std::fprintf(stderr,"[completion-queue-profile] recorded=%llu retained=%llu epochs=%llu "
        "publications=%llu submits=%llu invalidations=%llu unsupported_tail=%llu "
        "pending_overwritten=%llu pending_at_stop=%llu ambiguous_submits=%llu "
        "max_overwritten_pop_qpc=%llu begin_qpc=%llu end_qpc=%llu "
        "min_pending_publication_qpc=%llu min_pending_submit_qpc=%llu "
        "pending_unknown_publication_qpc=%llu pending_unknown_submit_qpc=%llu "
        "publication_is_locked_notification=1 service_and_callback_phases=missing\n",snapshot.recorded,snapshot.recorded-first,
        snapshot.epochs,snapshot.publications,snapshot.submits,snapshot.invalidations,
        snapshot.unsupported_tail,snapshot.pending_overwritten,snapshot.pending_at_stop,
        snapshot.ambiguous_submits,snapshot.max_overwritten_pop_qpc,
        snapshot.begin_qpc,snapshot.end_qpc,snapshot.min_pending_publication_qpc,
        snapshot.min_pending_submit_qpc,snapshot.pending_unknown_publication_qpc,
        snapshot.pending_unknown_submit_qpc);
}
}
void haze_completion_queue_submit(unsigned thread,uint64_t toc,uint32_t slot) {
    if(slot>=SPU_QUEUE_DIAG_SLOTS || !completion_prepare(thread,toc)) return;
    spu_lockline_lock(); spu_queue_diag_submit_locked(slot); spu_lockline_unlock();
}
HazeJobProfileScope::HazeJobProfileScope(unsigned thread,uint64_t toc,uint64_t handle,uint64_t lr) {
    completion_prepare(thread,toc);
    if(thread!=1 || toc!=0xE94EE0 || handle>=512 || !path()) return;
    // Allocate before the first interval, never while measuring a later loop.
    { std::lock_guard<std::mutex> lock(row_mutex); if(rows.empty()) rows.resize(capacity); }
    active=true; job=handle; caller=lr; depth=nesting++; begin=clock_now();
}
void HazeJobProfileScope::start(unsigned phase) {
    if(active) { ++calls[phase]; phase_begin=clock_now(); }
}
void HazeJobProfileScope::stop(unsigned phase) {
    if(active) ticks[phase]+=clock_now()-phase_begin;
}
HazeJobProfileScope::~HazeJobProfileScope() {
    if(!active) return;
    const uint64_t end=clock_now(); --nesting;
    Row row{begin,end,job,caller,{ticks[0],ticks[1],ticks[2],ticks[3]},
        {calls[0],calls[1],calls[2],calls[3]},depth};
    std::lock_guard<std::mutex> lock(row_mutex);
    rows[recorded++%capacity]=row;
}
void haze_job_profile_dump() {
    completion_dump();
    if(!path()) return;
    // The guest may still be finishing a loop when the window closes.
    std::vector<Row> snapshot; uint64_t total;
    { std::lock_guard<std::mutex> lock(row_mutex);
      total=recorded;
      const uint64_t first=total>capacity?total-capacity:0;
      snapshot.reserve(total-first);
      for(uint64_t i=first;i<total;++i) snapshot.push_back(rows[i%capacity]); }
    FILE* file=std::fopen(path(),"wb");
    if(!file) { std::fprintf(stderr,"[job-profile] cannot open output\n"); return; }
    LARGE_INTEGER frequency; QueryPerformanceFrequency(&frequency);
    std::fprintf(file,"qpc_frequency,begin_qpc,end_qpc,job,caller,depth,predicate_ticks,delay_ticks,pending_ticks,service_ticks,predicate_calls,delay_calls,pending_calls,service_calls\n");
    for(const auto& r:snapshot)
        std::fprintf(file,"%llu,%llu,%llu,%llu,%llu,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
            (unsigned long long)frequency.QuadPart,r.begin,r.end,r.job,r.caller,r.depth,
            r.ticks[0],r.ticks[1],r.ticks[2],r.ticks[3],r.calls[0],r.calls[1],r.calls[2],r.calls[3]);
    std::fclose(file);
    std::fprintf(stderr,"[job-profile] completed=%llu retained=%zu; active loops excluded\n",total,snapshot.size());
}
#endif
