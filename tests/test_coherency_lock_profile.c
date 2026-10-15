/* Actual linked coherence lock, with clock substitution only in the generated
 * test wrapper. Run test_coherency_lock_profile.py in a VS x64 environment.
 * No CPU-use/fairness threshold: held owners and explicit events gate writers. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <windows.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "spu_coherency.h"

enum { DEADLINE = 10000, THREADS = 8 };
typedef struct worker_args {
    HANDLE approaching, timing, resume;
    unsigned mode, pause;
    DWORD tid;
} worker_args;
static _Thread_local worker_args* current_worker;
static _Thread_local unsigned query_count;
static _Thread_local LONGLONG first_query;
static volatile LONG clock_calls, owners, published;
static uint64_t payload, complement = ~UINT64_C(0);
void spu_ch_wake(spu_context* ctx) { (void)ctx; }

BOOL WINAPI coherency_profile_fixture_qpc(LARGE_INTEGER* value)
{
    InterlockedIncrement(&clock_calls);
    BOOL result = QueryPerformanceCounter(value);
    worker_args* worker = current_worker;
    if (!worker) return result;
    ++query_count;
    if (query_count == 1) {
        first_query = value->QuadPart;
        assert(SetEvent(worker->timing));
        if (worker->pause) assert(WaitForSingleObject(worker->resume, DEADLINE) == WAIT_OBJECT_0);
    }
    if ((worker->mode == 1 && query_count == 1) || (worker->mode == 2 && query_count == 2)) {
        value->QuadPart = 0; return FALSE;
    }
    if (worker->mode == 3 && query_count == 2) value->QuadPart = first_query;
    if (worker->mode == 4 && query_count == 2) value->QuadPart = first_query - 1;
    return result;
}
static void enter(void)
{
    assert(InterlockedIncrement(&owners) == 1);
    assert(complement == ~payload);
}
static void leave(void) { assert(InterlockedDecrement(&owners) == 0); }
static DWORD WINAPI worker_main(void* context)
{
    worker_args* args = (worker_args*)context;
    current_worker = args; args->tid = GetCurrentThreadId();
    assert(SetEvent(args->approaching));
    spu_lockline_lock(); enter();
    ++payload; complement = ~payload;
    InterlockedIncrement(&published);
    leave(); spu_lockline_unlock();
    return 0;
}
static HANDLE create_worker(worker_args* args, unsigned mode, unsigned pause)
{
    memset(args, 0, sizeof(*args)); args->mode = mode; args->pause = pause;
    args->approaching = CreateEventW(NULL, TRUE, FALSE, NULL);
    args->timing = CreateEventW(NULL, TRUE, FALSE, NULL);
    args->resume = CreateEventW(NULL, TRUE, FALSE, NULL);
    assert(args->approaching && args->timing && args->resume);
    HANDLE thread = CreateThread(NULL, 0, worker_main, args, 0, NULL); assert(thread);
    return thread;
}
static void finish_worker(HANDLE thread, worker_args* args)
{
    assert(WaitForSingleObject(thread, DEADLINE) == WAIT_OBJECT_0);
    DWORD code; assert(GetExitCodeThread(thread, &code) && code == 0);
    CloseHandle(thread); CloseHandle(args->approaching); CloseHandle(args->timing); CloseHandle(args->resume);
}
static void one_contended(unsigned mode)
{
    worker_args args;
    spu_lockline_lock(); enter();
    HANDLE thread = create_worker(&args, mode, 0);
    assert(WaitForSingleObject(args.timing, DEADLINE) == WAIT_OBJECT_0);
    const LONG before = InterlockedCompareExchange(&published, 0, 0);
    Sleep(3); /* Actual owner remains held beyond the exported 1ms threshold. */
    assert(InterlockedCompareExchange(&published, 0, 0) == before);
    leave(); spu_lockline_unlock(); finish_worker(thread, &args);
}
int main(int argc, char** argv)
{
    assert(argc == 3 && GetFileAttributesA(argv[2]) == INVALID_FILE_ATTRIBUTES);
    const char* mode = argv[1];
    if (!strcmp(mode, "disabled")) {
        worker_args args;
        spu_lockline_lock(); enter();
        HANDLE thread = create_worker(&args, 0, 0);
        assert(WaitForSingleObject(args.approaching, DEADLINE) == WAIT_OBJECT_0);
        assert(WaitForSingleObject(thread, 10) == WAIT_TIMEOUT);
        assert(!published && clock_calls == 0);
        leave(); spu_lockline_unlock(); finish_worker(thread, &args);
        assert(clock_calls == 0);
    } else {
        assert(spu_coh_lock_profile_start());
        assert(spu_coh_lock_profile_start()); /* idempotent, no second clock */
        assert(clock_calls == 1);
        for (unsigned i = 0; i < 32; ++i) { spu_lockline_lock(); enter(); leave(); spu_lockline_unlock(); }
        assert(clock_calls == 1); /* Enabled uncontended fast path has no QPC. */
        if (!strcmp(mode, "contracts")) {
            worker_args args[THREADS]; HANDLE threads[THREADS], timing[THREADS];
            spu_lockline_lock(); enter();
            for (unsigned i = 0; i < THREADS; ++i) {
                threads[i] = create_worker(&args[i], 0, 0); timing[i] = args[i].timing;
            }
            assert(WaitForMultipleObjects(THREADS, timing, TRUE, DEADLINE) == WAIT_OBJECT_0);
            Sleep(3); assert(published == 0 && owners == 1);
            leave(); spu_lockline_unlock();
            for (unsigned i = 0; i < THREADS; ++i) finish_worker(threads[i], &args[i]);
        } else if (!strcmp(mode, "wrap")) {
            for (unsigned i = 0; i < SPU_COH_LOCK_PROFILE_CAPACITY + 7; ++i) one_contended(0);
        } else if (!strcmp(mode, "clocks")) {
            for (unsigned i = 1; i <= 4; ++i) one_contended(i);
        } else {
            assert(!strcmp(mode, "freeze"));
            worker_args args;
            spu_lockline_lock(); enter();
            HANDLE thread = create_worker(&args, 0, 1);
            assert(WaitForSingleObject(args.timing, DEADLINE) == WAIT_OBJECT_0);
            leave(); spu_lockline_unlock();
            spu_coh_lock_profile_snapshot pending;
            spu_coh_lock_profile_stop(&pending);
            assert(pending.inflight_at_stop == 1 && pending.admitted_contentions == 1);
            assert(pending.completed_contentions == 0 && pending.recorded == 0);
            assert(SetEvent(args.resume)); finish_worker(thread, &args);
        }
    }
    spu_coh_lock_profile_snapshot snapshot, repeated;
    spu_coh_lock_profile_stop(&snapshot);
    const LONG frozen_clock_calls = clock_calls;
    spu_lockline_lock(); enter(); leave(); spu_lockline_unlock();
    spu_coh_lock_profile_stop(&repeated);
    assert(snapshot.end_qpc == repeated.end_qpc && snapshot.recorded == repeated.recorded);
    assert(snapshot.inflight_at_stop == repeated.inflight_at_stop && clock_calls == frozen_clock_calls);
    assert(!spu_coh_lock_profile_start()); /* frozen capture cannot restart */
    assert(snapshot.completed_contentions == snapshot.recorded + snapshot.below_threshold + snapshot.invalid_clocks);
    assert(snapshot.admitted_contentions == snapshot.completed_contentions + snapshot.inflight_at_stop);
    if (!strcmp(mode, "disabled")) assert(!snapshot.started && snapshot.recorded == 0 && clock_calls == 0);
    else {
        assert(snapshot.started && snapshot.clock_valid && snapshot.image_base == (uint64_t)(uintptr_t)GetModuleHandleW(NULL));
        assert(!snapshot.admission_overflow);
        if (!strcmp(mode, "contracts")) assert(snapshot.recorded == THREADS && published == THREADS);
        if (!strcmp(mode, "wrap")) assert(snapshot.recorded == SPU_COH_LOCK_PROFILE_CAPACITY + 7 && snapshot.max_overwritten_end_qpc);
        if (!strcmp(mode, "clocks")) assert(snapshot.completed_contentions == 4 && snapshot.recorded == 0 && snapshot.invalid_clocks == 3 && snapshot.below_threshold == 1);
        if (!strcmp(mode, "freeze")) assert(snapshot.inflight_at_stop == 1 && snapshot.recorded == 0);
        const uint64_t first = snapshot.recorded > SPU_COH_LOCK_PROFILE_CAPACITY ? snapshot.recorded - SPU_COH_LOCK_PROFILE_CAPACITY : 0;
        for (uint64_t i = first; i < snapshot.recorded; ++i) {
            const spu_coh_lock_profile_row* row = &snapshot.rows[i % SPU_COH_LOCK_PROFILE_CAPACITY];
            assert(row->sequence == i && row->host_tid && row->caller >= snapshot.image_base);
            assert(row->begin_qpc >= snapshot.begin_qpc && row->end_qpc <= snapshot.end_qpc);
            assert(row->end_qpc - row->begin_qpc >= snapshot.threshold_ticks);
        }
    }
    assert(spu_coh_lock_profile_dump(argv[2]));
    assert(spu_coh_lock_profile_dump(argv[2]));
    assert(clock_calls == frozen_clock_calls);
    char blocked_path[4096+32];
    snprintf(blocked_path, sizeof blocked_path, "%s/nonexistent.csv", argv[2]);
    assert(!spu_coh_lock_profile_dump(blocked_path)); /* existing file blocks parent */
    assert(clock_calls == frozen_clock_calls);
    printf("PASS coherency lock profile %s: completed=%llu retained-events=%llu inflight=%llu\n",
        mode, snapshot.completed_contentions, snapshot.recorded, snapshot.inflight_at_stop);
    return 0;
}
