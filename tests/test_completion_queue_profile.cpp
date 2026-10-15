// Actual coherency/queue/profile integration; no game assets or GPU needed.
// Compile with HAZE_JOB_PROFILE, HAZE_JOB_TRACE, and small diagnostic capacities.
// clang-cl /EHsc /O2 /std:c++20 /DHAZE_JOB_PROFILE /DHAZE_JOB_TRACE
//   /DHAZE_JOB_TRACE_TEST_CAPACITY=16 /DSPU_QUEUE_DIAG_CAPACITY=16 /clang:-msse4.1
//   /Iexternal/ps3recomp/include /Iexternal/ps3recomp/runtime/spu /Iport
//   tests/test_completion_queue_profile.cpp port/haze_job_profile.cpp
//   port/haze_job_trace.cpp external/ps3recomp/runtime/spu/spu_coherency.c
//   external/ps3recomp/libs/spurs/cellSpursQueue.c
// Use test_completion_queue_profile.py for pending-clock cases: it compiles
// coherency through a generated wrapper substituting only its QPC query.
// Production source has no test clock switch or runtime policy changes.
// Run as: test_completion_queue_profile.exe <mode> output.csv
#include <windows.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "haze_job_profile.h"
#include "haze_job_trace.h"
#include "spu_coherency.h"
#include "spu_queue_diagnostic.h"

static bool fixture_fail_queue_qpc;
static bool fixture_script_queue_qpc;
static uint64_t fixture_queue_qpc;
static uint64_t fixture_last_queue_qpc;
extern "C" {
BOOL WINAPI completion_fixture_qpc(LARGE_INTEGER* value) {
    if(fixture_fail_queue_qpc) {value->QuadPart=0;return FALSE;}
    if(fixture_script_queue_qpc) {
        value->QuadPart=++fixture_queue_qpc;
        fixture_last_queue_qpc=fixture_queue_qpc;
        return TRUE;
    }
    const BOOL result=QueryPerformanceCounter(value);
    if(result)fixture_last_queue_qpc=uint64_t(value->QuadPart);
    return result;
}
uint8_t* vm_base;
int g_resv_store_active;
uint32_t g_ww_lo,g_ww_hi;
void ppu_resv_break_store(uint64_t) {}
void ps3_ww_report_inline(uint32_t,uint64_t,int) {}
void spu_taskset_signal_task(uint32_t,uint32_t) {}
void spu_ch_wake(spu_context*) {}
int ppu_native_memory_available() { return 1; }
int ppu_native_memory_range(uint32_t address,uint32_t size) {
    return uint64_t(address)+size<=16u*1024u*1024u;
}
int32_t _cellSpursQueueInitialize(uint64_t,uint64_t,uint64_t,uint64_t,uint32_t,uint32_t,uint32_t);
int32_t cellSpursQueuePopBody(uint64_t,uint64_t,uint32_t,uint32_t);
}
static constexpr uint32_t queue=0x1000,buffer=0x4000,destination=0x3000,depth=8;
static uint32_t read32(uint32_t ea) {
    const uint8_t* p=vm_base+ea;
    return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
}
static void write32(uint32_t ea,uint32_t value) {
    for(unsigned i=0;i<4;++i)vm_base[ea+i]=uint8_t(value>>(24-i*8));
}
static void submit(uint32_t slot,bool duplicate_call=false,uint32_t route=2) {
    // No HAZE_JOB_TRACE_CSV and no HAZE_NATIVE_JOB_WAIT: the diagnostic must
    // still observe this real guarded Submit hook before producer work starts.
    HazeJobTraceScope scope(unsigned(HazeJobTraceKind::Submit),1,0xE94EE0,0,nullptr,0);
    scope.set_slot(slot);
    scope.set_route(route);
    if(duplicate_call)scope.set_slot(slot);
    if(duplicate_call)scope.set_route(route);
}
static bool publish(uint32_t slot,bool claim=false) {
    spu_lockline_lock();
    const uint32_t head=read32(queue),tail=read32(queue+4);
    if((tail+depth*2-head)%(depth*2)==depth) {spu_lockline_unlock();return false;}
    if(claim) {
        write32(queue+4,~tail);spu_coh_notify_write(queue); // not a result
    }
    write32(buffer+(tail%depth)*16,slot);
    write32(buffer+(tail%depth)*16+4,slot^0xAA55AA55u);
    write32(queue+4,(tail+1)%(depth*2));
    spu_coh_notify_write_except(queue,nullptr);
    spu_lockline_unlock();return true;
}
static void pop(uint32_t expected) {
    assert(cellSpursQueuePopBody(queue,destination,0,0)==0);
    assert(read32(destination)==expected);
    assert(read32(destination+4)==(expected^0xAA55AA55u));
}
static void reset() {
    assert(_cellSpursQueueInitialize(0x800,0x900,queue,buffer,16,depth,1)==0);
}
static DWORD WINAPI producer(void*) {
    const ULONGLONG deadline=GetTickCount64()+5000;
    for(uint32_t slot=0;slot<128;++slot) {
        while(!publish(slot,slot%2==0)) {
            assert(GetTickCount64()<deadline);SwitchToThread();
        }
    }
    return 0;
}
int main(int argc,char** argv) {
    assert(argc==3 && GetFileAttributesA(argv[2])==INVALID_FILE_ATTRIBUTES);
    _putenv_s("HAZE_JOB_TRACE_CSV","");_putenv_s("HAZE_JOB_PROFILE_CSV","");
    _putenv_s("HAZE_NATIVE_JOB_WAIT","0");_putenv_s("SPURS_QUEUE_WAIT_NATIVE","0");
    _putenv_s("HAZE_COMPLETION_QUEUE_CSV",argv[2]);
    vm_base=static_cast<uint8_t*>(VirtualAlloc(nullptr,16u*1024u*1024u,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    assert(vm_base);
    // The source-audited TOC -> root -> table -> completion queue chain.
    write32(0xE94EE0-0x7C1C,0x10000);write32(0x10000-0x7FD8,0x2000);write32(0x2000,queue);
    reset();
    const bool pending=std::strncmp(argv[1],"pending",7)==0;
    if(pending) {
        LARGE_INTEGER base;assert(QueryPerformanceCounter(&base));
        fixture_queue_qpc=uint64_t(base.QuadPart);fixture_script_queue_qpc=true;
    }
    uint64_t expected_pending_publication=0,expected_pending_submit=0;
    if(std::strcmp(argv[1],"contracts")==0) {
        uint8_t before[128];std::memcpy(before,vm_base+queue,sizeof before);
        submit(3,true);assert(std::memcmp(before,vm_base+queue,sizeof before)==0);
        assert(publish(3,true));pop(3);
        submit(3);assert(publish(3));
        // Unrelated/same-value notifications and peek never add publications
        // or consume the tracked record.
        spu_lockline_lock();spu_coh_notify_write(queue);spu_coh_notify_write(queue+128);spu_lockline_unlock();
        assert(cellSpursQueuePopBody(queue,destination,1,0)==0);pop(3);
        assert(publish(7));pop(7); // no matching submit: explicit unknown
        submit(9);submit(9);assert(publish(9));pop(9); // reused before consumption
        reset(); // same EA/config, new epoch; old generations must not leak
        assert(publish(11)); // predates diagnostic re-registration
        submit(11);pop(11);
        submit(12);assert(publish(12));pop(12);
        reset();submit(3);assert(publish(3));pop(3);
        // A corrupted/replaced payload cannot inherit another record's match.
        submit(13);assert(publish(13));
        spu_lockline_lock();const uint32_t at=read32(queue)%depth;
        write32(buffer+at*16,14);write32(buffer+at*16+4,14^0xAA55AA55u);spu_lockline_unlock();pop(14);
        // Nonqueue routes must not leave a generation that poisons reuse.
        submit(15,false,0);submit(15);assert(publish(15));pop(15);
    } else if(std::strcmp(argv[1],"wrap")==0) {
        for(unsigned i=0;i<SPU_QUEUE_DIAG_CAPACITY+7;++i) {
            submit(3);assert(publish(3,i%2==0));pop(3);
        }
    } else if(pending) {
        submit(1);assert(publish(1));pop(1);
        if(std::strcmp(argv[1],"pending")==0) {
            submit(2);expected_pending_submit=fixture_last_queue_qpc;
            assert(publish(2));expected_pending_publication=fixture_last_queue_qpc;
            submit(3);assert(publish(3));
        } else if(std::strcmp(argv[1],"pending_unknown")==0) {
            assert(publish(7));expected_pending_publication=fixture_last_queue_qpc;
            submit(2);expected_pending_submit=fixture_last_queue_qpc;
            fixture_fail_queue_qpc=true;assert(publish(2));fixture_fail_queue_qpc=false;
        } else {
            assert(std::strcmp(argv[1],"pending_all_unknown")==0);
            fixture_fail_queue_qpc=true;assert(publish(7));fixture_fail_queue_qpc=false;
        }
    } else {
        assert(std::strcmp(argv[1],"concurrent")==0);
        for(unsigned i=0;i<128;++i)submit(i);
        HANDLE worker=CreateThread(nullptr,0,producer,nullptr,0,nullptr);assert(worker);
        const ULONGLONG deadline=GetTickCount64()+5000;
        for(unsigned i=0;i<128;++i) {
            while(cellSpursQueuePopBody(queue,destination,0,0)!=0) {
                assert(GetTickCount64()<deadline);SwitchToThread();
            }
            assert(read32(destination)==i && read32(destination+4)==(i^0xAA55AA55u));
        }
        assert(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0);CloseHandle(worker);
    }
    spu_queue_diag_snapshot snapshot{};spu_queue_diag_stop(&snapshot);
    if(std::strcmp(argv[1],"contracts")==0) {
        assert(snapshot.recorded==9 && snapshot.publications==8 && snapshot.epochs==3);
        const auto* r=snapshot.rows;
        assert(r[0].flags==0 && r[0].generation==1 && r[0].ordinal==1);
        assert(r[1].flags==0 && r[1].generation==2 && r[1].ordinal==2);
        assert(r[2].flags==SPU_QUEUE_DIAG_NO_GENERATION);
        assert(r[3].flags==(SPU_QUEUE_DIAG_NO_GENERATION|SPU_QUEUE_DIAG_AMBIGUOUS_GENERATION));
        assert(r[4].flags==(SPU_QUEUE_DIAG_NO_PUBLICATION|SPU_QUEUE_DIAG_NO_GENERATION));
        assert(r[5].flags==0 && r[5].epoch==2 && r[5].generation==1);
        assert(r[6].flags==0 && r[6].epoch==3 && r[6].generation==1);
        assert(r[7].flags==SPU_QUEUE_DIAG_RECORD_MISMATCH);
        assert(r[8].flags==0 && r[8].generation==1);
        assert(snapshot.ambiguous_submits==1 && snapshot.invalidations==2);
        assert(!snapshot.pending_at_stop && !snapshot.unsupported_tail);
    } else if(pending) {
        assert(snapshot.recorded==1 && snapshot.rows[0].flags==0);
        assert(snapshot.pending_at_stop==(std::strcmp(argv[1],"pending_all_unknown")==0?1:2));
        assert(snapshot.min_pending_publication_qpc==expected_pending_publication);
        assert(snapshot.min_pending_submit_qpc==expected_pending_submit);
        const unsigned unknown=std::strcmp(argv[1],"pending")==0?0:1;
        assert(snapshot.pending_unknown_publication_qpc==unknown);
        assert(snapshot.pending_unknown_submit_qpc==unknown);
    } else {
        const uint64_t first=snapshot.recorded>SPU_QUEUE_DIAG_CAPACITY?snapshot.recorded-SPU_QUEUE_DIAG_CAPACITY:0;
        assert(snapshot.max_overwritten_pop_qpc && !snapshot.pending_at_stop);
        for(uint64_t i=first;i<snapshot.recorded;++i) {
            const auto& r=snapshot.rows[i%SPU_QUEUE_DIAG_CAPACITY];
            assert(r.sequence==i && !r.flags && r.generation);
            assert(r.submit_qpc<=r.publication_qpc && r.publication_qpc<=r.pop_qpc);
        }
    }
    const uint64_t stopped_at=snapshot.end_qpc;
    submit(21);spu_lockline_lock();spu_coh_notify_write(queue);spu_lockline_unlock();
    spu_queue_diag_stop(&snapshot);assert(snapshot.end_qpc==stopped_at);
    if(pending) {
        assert(snapshot.min_pending_publication_qpc==expected_pending_publication);
        assert(snapshot.min_pending_submit_qpc==expected_pending_submit);
    }
    haze_job_profile_dump(); // independent CSV works without old profile CSV
    assert(GetFileAttributesA(argv[2])!=INVALID_FILE_ATTRIBUTES);
    const std::string metadata=std::string(argv[2])+".meta.json";
    assert(GetFileAttributesA(metadata.c_str())!=INVALID_FILE_ATTRIBUTES);
    DWORD old;
    assert(VirtualProtect(vm_base,16u*1024u*1024u,PAGE_NOACCESS,&old));
    // Frozen diagnostics retain no queue mapping in their notification path.
    spu_lockline_lock();spu_coh_notify_write(queue);spu_lockline_unlock();
    VirtualFree(vm_base,0,MEM_RELEASE);
    std::printf("PASS completion queue diagnostic %s: recorded=%llu, independent submit hook, no wait changes\n",argv[1],snapshot.recorded);
}
