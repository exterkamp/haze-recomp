#define _CRT_SECURE_NO_WARNINGS
#include "haze_job13_capture.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <chrono>
#include <thread>
#endif
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
#include <xmmintrin.h>
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
extern "C" void spu_wrch(spu_context* ctx, uint32_t channel, u128 value);
extern "C" u128 spu_rdch(spu_context* ctx, uint32_t channel);
extern "C" uint32_t spu_rchcnt(spu_context* ctx, uint32_t channel);
#pragma clang diagnostic pop

namespace {

constexpr uint32_t kTargetImage = 32;
constexpr uint32_t kNoPayload = UINT32_MAX;
constexpr uint64_t kGatePollMs = 250;

struct CaptureSlot {
    haze_job13_capture_header header;
    alignas(16) uint8_t initial_gpr[HAZE_JOB13_CAPTURE_GPR_BYTES];
    alignas(16) uint8_t final_gpr[HAZE_JOB13_CAPTURE_GPR_BYTES];
    alignas(16) uint8_t initial_ls[HAZE_JOB13_CAPTURE_LS_SIZE];
    alignas(16) uint8_t final_ls[HAZE_JOB13_CAPTURE_LS_SIZE];
    haze_job13_capture_event events[HAZE_JOB13_CAPTURE_MAX_EVENTS];
    uint8_t payload[HAZE_JOB13_CAPTURE_MAX_PAYLOAD];
    uint32_t event_count;
    uint32_t event_calls;
    uint32_t payload_size;
    uint32_t initial_int_enable;
    uint32_t initial_event_mask;
    uint32_t initial_event_status;
};

/* Eight fixed slots bound storage to about 44 MiB. Slots are single-owner:
 * begin reserves one unique batch index, and only that thread mutates it. */
CaptureSlot g_slots[HAZE_JOB13_CAPTURE_MAX_BATCHES];
std::atomic<uint32_t> g_next_batch{0};
thread_local CaptureSlot* g_active = nullptr;
thread_local spu_context* g_active_ctx = nullptr;
thread_local uint32_t g_suppressed_nested = 0;

struct CaptureConfig {
    std::atomic<int> initialized{0};
    std::atomic<uint64_t> next_poll_ms{0};
    char output_dir[768]{};
    char gate_path[768]{};
    bool configured = false;
};
CaptureConfig g_config;

static uint64_t now_ms() {
#ifdef _WIN32
    return GetTickCount64();
#else
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

static uint64_t now_qpc() {
#ifdef _WIN32
    LARGE_INTEGER qpc{};
    return QueryPerformanceCounter(&qpc) ? (uint64_t)qpc.QuadPart : 0;
#else
    using namespace std::chrono;
    return (uint64_t)duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
#endif
}

static uint64_t qpc_frequency() {
#ifdef _WIN32
    LARGE_INTEGER frequency{};
    return QueryPerformanceFrequency(&frequency) ? (uint64_t)frequency.QuadPart : 0;
#else
    return 1000000000ull;
#endif
}

static uint32_t current_tid() {
#ifdef _WIN32
    return (uint32_t)GetCurrentThreadId();
#else
    return (uint32_t)std::hash<std::thread::id>{}(std::this_thread::get_id());
#endif
}

static uint32_t current_mxcsr() {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    return (uint32_t)_mm_getcsr();
#else
    return 0;
#endif
}

static void initialize_config() {
    int state = g_config.initialized.load(std::memory_order_acquire);
    if (state == 2) return;
    int expected = 0;
    if (g_config.initialized.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
        const char* output = std::getenv("HAZE_JOB13_CAPTURE_DIR");
        const char* gate = std::getenv("HAZE_JOB13_CAPTURE_GATE");
        if (output && *output && gate && *gate &&
            std::strlen(output) < sizeof(g_config.output_dir) &&
            std::strlen(gate) < sizeof(g_config.gate_path)) {
            std::memcpy(g_config.output_dir, output, std::strlen(output) + 1);
            std::memcpy(g_config.gate_path, gate, std::strlen(gate) + 1);
            g_config.configured = true;
        }
        g_config.initialized.store(2, std::memory_order_release);
    } else {
        while (g_config.initialized.load(std::memory_order_acquire) != 2) { }
    }
}

std::atomic<bool> g_gate_cached{false};

static bool poll_gate() {
#ifndef _WIN32
    return false;
#else
    initialize_config();
    if (!g_config.configured) return false;
    const uint64_t now = now_ms();
    uint64_t due = g_config.next_poll_ms.load(std::memory_order_relaxed);
    if (now < due) return g_gate_cached.load(std::memory_order_relaxed);
    if (!g_config.next_poll_ms.compare_exchange_strong(due, now + kGatePollMs,
                                                       std::memory_order_relaxed))
        return g_gate_cached.load(std::memory_order_relaxed);
    const DWORD attrs = GetFileAttributesA(g_config.gate_path);
    const bool open = attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    g_gate_cached.store(open, std::memory_order_relaxed);
    return open;
#endif
}

static void invalidate(CaptureSlot* slot, uint32_t reason) {
    if (slot && slot->header.valid) {
        slot->header.valid = 0;
        slot->header.invalid_reason = reason;
    }
}

static bool supported_wrch(uint32_t ch) {
    return ch == MFC_LSA || ch == MFC_EAH || ch == MFC_EAL || ch == MFC_Size ||
           ch == MFC_TagID || ch == MFC_Cmd || ch == MFC_WrTagMask ||
           ch == MFC_WrTagUpdate;
}

static bool supported_rdch(uint32_t ch) {
    return ch == MFC_RdTagMask || ch == MFC_RdTagStat || ch == MFC_RdAtomicStat;
}

static bool supported_rchcnt(uint32_t ch) {
    return ch == MFC_RdTagMask || ch == MFC_RdTagStat || ch == MFC_RdAtomicStat;
}

static haze_job13_capture_event* append_event(CaptureSlot* slot, uint32_t kind,
                                               uint32_t channel) {
    if (!slot) return nullptr;
    if (slot->event_calls != UINT32_MAX) ++slot->event_calls;
    else invalidate(slot, HAZE_JOB13_INVALID_EVENT_OVERFLOW);
    slot->header.event_calls = slot->event_calls;
    if (slot->event_count >= HAZE_JOB13_CAPTURE_MAX_EVENTS) {
        invalidate(slot, HAZE_JOB13_INVALID_EVENT_OVERFLOW);
        return nullptr;
    }
    haze_job13_capture_event* e = &slot->events[slot->event_count];
    std::memset(e, 0, sizeof(*e));
    e->sequence = slot->event_calls - 1;
    e->kind = kind;
    e->channel = channel;
    e->payload_offset = kNoPayload;
    e->lsa = e->eah = e->eal = e->size = e->tag = 0;
    ++slot->event_count;
    slot->header.event_count = slot->event_count;
    return e;
}

static void snapshot_staging(CaptureSlot* slot, haze_job13_capture_event* e,
                             const spu_context* ctx) {
    if (!slot || !e || !ctx) return;
    e->lsa = ctx->mfc_lsa;
    e->eah = ctx->mfc_eah;
    e->eal = ctx->mfc_eal;
    e->size = ctx->mfc_size;
    e->tag = ctx->mfc_tag;
}

static bool dma_kind(uint32_t cmd, bool* is_get) {
    switch (cmd) {
    case MFC_GET_CMD: case MFC_GETB_CMD: case MFC_GETF_CMD:
        *is_get = true; return true;
    case MFC_PUT_CMD: case MFC_PUTB_CMD: case MFC_PUTF_CMD:
        *is_get = false; return true;
    default:
        return false;
    }
}

static bool valid_ls_span(CaptureSlot* slot, const spu_context* ctx,
                          const haze_job13_capture_event* e) {
    if (!ctx || !ctx->ls || e->lsa > SPU_LS_SIZE || e->size > SPU_LS_SIZE - e->lsa) {
        invalidate(slot, HAZE_JOB13_INVALID_LOCAL_STORE_RANGE);
        return false;
    }
    return true;
}

static bool append_dma_payload(CaptureSlot* slot, const spu_context* ctx,
                               haze_job13_capture_event* e) {
    if (!slot || !ctx || !e || e->size == 0) return true;
    if (e->size > HAZE_JOB13_CAPTURE_MAX_PAYLOAD - slot->payload_size) {
        invalidate(slot, HAZE_JOB13_INVALID_PAYLOAD_OVERFLOW);
        return false;
    }
    e->payload_offset = slot->payload_size;
    e->payload_length = e->size;
    std::memcpy(slot->payload + slot->payload_size, ctx->ls + e->lsa, e->size);
    slot->payload_size += e->size;
    slot->header.payload_size = slot->payload_size;
    return true;
}

static bool append_payload(CaptureSlot* slot, const spu_context* ctx,
                           haze_job13_capture_event* e, uint32_t lsa, uint32_t size) {
    if (!slot || !ctx || !e || size == 0) return true;
    if (size > HAZE_JOB13_CAPTURE_MAX_PAYLOAD - slot->payload_size) {
        invalidate(slot, HAZE_JOB13_INVALID_PAYLOAD_OVERFLOW);
        return false;
    }
    e->payload_offset = slot->payload_size;
    e->payload_length = size;
    std::memcpy(slot->payload + slot->payload_size, ctx->ls + lsa, size);
    slot->payload_size += size;
    slot->header.payload_size = slot->payload_size;
    return true;
}

static bool valid_atomic_ls_span(CaptureSlot* slot, const spu_context* ctx) {
    const uint32_t lsa = ctx ? (ctx->mfc_lsa & SPU_LS_MASK) : SPU_LS_SIZE;
    if (!ctx || !ctx->ls || lsa > SPU_LS_SIZE - MFC_ATOMIC_LINE) {
        invalidate(slot, HAZE_JOB13_INVALID_LOCAL_STORE_RANGE);
        return false;
    }
    /* The SDK masks LSA with SPU_LS_MASK but does not align it to 128 bytes. */
    return true;
}

static bool write_all(HANDLE file, const void* data, uint64_t length) {
    const uint8_t* p = (const uint8_t*)data;
    while (length) {
        const DWORD chunk = length > 1024u * 1024u ? 1024u * 1024u : (DWORD)length;
        DWORD written = 0;
        if (!WriteFile(file, p, chunk, &written, nullptr) || written != chunk) return false;
        p += written;
        length -= written;
    }
    return true;
}

static void emit_file(CaptureSlot* slot) {
#ifdef _WIN32
    char path[MAX_PATH];
    const int n = std::snprintf(path, sizeof(path), "%s\\job13-%02u-tid%08X-qpc%016llX.bin",
                                g_config.output_dir, slot->header.batch_index,
                                slot->header.host_tid,
                                (unsigned long long)slot->header.begin_qpc);
    if (n <= 0 || (size_t)n >= sizeof(path)) return;
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    bool ok = write_all(file, &slot->header, sizeof(slot->header)) &&
        write_all(file, slot->initial_gpr, sizeof(slot->initial_gpr)) &&
        write_all(file, slot->final_gpr, sizeof(slot->final_gpr)) &&
        write_all(file, slot->initial_ls, sizeof(slot->initial_ls)) &&
        write_all(file, slot->final_ls, sizeof(slot->final_ls)) &&
        write_all(file, slot->events, (uint64_t)slot->event_count * sizeof(slot->events[0])) &&
        write_all(file, slot->payload, slot->payload_size) && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok) DeleteFileA(path);
#else
    (void)slot;
#endif
}

} // namespace

extern "C" bool haze_job13_capture_begin(spu_context* ctx) {
    if (g_active) {
        invalidate(g_active, HAZE_JOB13_INVALID_CONTEXT);
        ++g_suppressed_nested;
        return false;
    }
    if (!ctx || ctx->image_id != (int)kTargetImage || !ctx->ls || !poll_gate()) return false;
    const uint32_t batch = g_next_batch.fetch_add(1, std::memory_order_relaxed);
    if (batch >= HAZE_JOB13_CAPTURE_MAX_BATCHES) return false;

    CaptureSlot* slot = &g_slots[batch];
    std::memset(&slot->header, 0, sizeof(slot->header));
    slot->event_count = 0;
    slot->event_calls = 0;
    slot->payload_size = 0;
    slot->initial_int_enable = 0;
    slot->initial_event_mask = 0;
    slot->initial_event_status = 0;
    slot->header.magic[0] = 'H'; slot->header.magic[1] = 'Z';
    slot->header.magic[2] = 'J'; slot->header.magic[3] = 'O';
    slot->header.magic[4] = 'B'; slot->header.magic[5] = '1';
    slot->header.magic[6] = '3'; slot->header.magic[7] = 0;
    slot->header.version = HAZE_JOB13_CAPTURE_VERSION;
    slot->header.header_size = HAZE_JOB13_CAPTURE_HEADER_SIZE;
    slot->header.valid = 1;
    slot->header.image_id = (uint32_t)ctx->image_id;
    slot->header.initial_pc = ctx->pc;
    slot->header.initial_status = ctx->status;
    slot->header.initial_mxcsr = current_mxcsr();
    slot->header.host_tid = current_tid();
    slot->header.batch_index = batch;
    slot->header.qpc_frequency = qpc_frequency();
    slot->header.begin_qpc = now_qpc();
    if (!slot->header.qpc_frequency || !slot->header.begin_qpc)
        invalidate(slot, HAZE_JOB13_INVALID_CLOCK);
    slot->header.initial_lsa = ctx->mfc_lsa;
    slot->header.initial_eah = ctx->mfc_eah;
    slot->header.initial_eal = ctx->mfc_eal;
    slot->header.initial_size = ctx->mfc_size;
    slot->header.initial_tag = ctx->mfc_tag;
    slot->header.initial_tag_mask = ctx->mfc_tag_mask;
    slot->header.initial_tag_status = ctx->mfc_tag_status;
    slot->header.initial_atomic_stat = ctx->atomic_stat;
    slot->initial_int_enable = ctx->int_enable;
    slot->initial_event_mask = ctx->event_mask;
    slot->initial_event_status = ctx->event_status;
    if (ctx->int_enable || ctx->event_mask || ctx->event_status)
        invalidate(slot, HAZE_JOB13_INVALID_INTERRUPT_STATE);
    if (ctx->resv_valid)
        invalidate(slot, HAZE_JOB13_INVALID_RESERVATION_STATE);
    std::memcpy(slot->initial_gpr, ctx->gpr, sizeof(slot->initial_gpr));
    std::memcpy(slot->initial_ls, ctx->ls, sizeof(slot->initial_ls));
    g_active = slot;
    g_active_ctx = ctx;
    return true;
}

extern "C" void haze_job13_capture_end(spu_context* ctx) {
    if (g_suppressed_nested) {
        --g_suppressed_nested;
        return;
    }
    CaptureSlot* slot = g_active;
    if (!slot) return;
    if (!ctx || ctx != g_active_ctx || !ctx->ls) invalidate(slot, HAZE_JOB13_INVALID_CONTEXT);
    else {
        slot->header.final_pc = ctx->pc;
        slot->header.final_status = ctx->status;
        slot->header.final_mxcsr = current_mxcsr();
        std::memcpy(slot->final_gpr, ctx->gpr, sizeof(slot->final_gpr));
        std::memcpy(slot->final_ls, ctx->ls, sizeof(slot->final_ls));
        if (ctx->image_id != (int)kTargetImage)
            invalidate(slot, HAZE_JOB13_INVALID_CONTEXT);
        if (ctx->int_enable != slot->initial_int_enable || ctx->event_mask != slot->initial_event_mask ||
            ctx->event_status != slot->initial_event_status || ctx->int_enable ||
            ctx->event_mask || ctx->event_status)
            invalidate(slot, HAZE_JOB13_INVALID_INTERRUPT_STATE);
        if (ctx->resv_valid)
            invalidate(slot, HAZE_JOB13_INVALID_RESERVATION_STATE);
    }
    slot->header.end_qpc = now_qpc();
    if (!slot->header.end_qpc) invalidate(slot, HAZE_JOB13_INVALID_CLOCK);
    g_active = nullptr;
    g_active_ctx = nullptr;
    emit_file(slot);
}

extern "C" void haze_job13_capture_wrch(spu_context* ctx, uint32_t channel, u128 value) {
    CaptureSlot* slot = g_active;
    if (slot && ctx != g_active_ctx) {
        invalidate(slot, HAZE_JOB13_INVALID_CONTEXT);
        slot = nullptr;
    }
    haze_job13_capture_event* e = nullptr;
    bool is_get = false;
    bool is_atomic_get = false;
    bool is_atomic_put = false;
    bool atomic_span_valid = false;
    if (slot) {
        e = append_event(slot, HAZE_JOB13_EVENT_WRCH, channel);
        if (!supported_wrch(channel)) invalidate(slot, HAZE_JOB13_INVALID_UNSUPPORTED_CHANNEL);
        if (e) {
            std::memcpy(e->value, &value, sizeof(e->value));
            snapshot_staging(slot, e, ctx);
            if (channel == MFC_Cmd) {
                const uint32_t cmd = value._u32[0];
                e->command = cmd;
                if (cmd == MFC_GETLLAR_CMD || cmd == MFC_PUTLLC_CMD) {
                    is_atomic_get = cmd == MFC_GETLLAR_CMD;
                    is_atomic_put = cmd == MFC_PUTLLC_CMD;
                    atomic_span_valid = valid_atomic_ls_span(slot, ctx);
                    if (is_atomic_put && atomic_span_valid && slot->header.valid)
                        append_payload(slot, ctx, e, ctx->mfc_lsa & SPU_LS_MASK,
                                       MFC_ATOMIC_LINE);
                } else {
                    const bool supported = dma_kind(cmd, &is_get);
                    if (!supported) invalidate(slot, HAZE_JOB13_INVALID_UNSUPPORTED_COMMAND);
                    else if (is_get) {
                        if (valid_ls_span(slot, ctx, e)) e->payload_length = e->size;
                    } else if (valid_ls_span(slot, ctx, e)) {
                        append_dma_payload(slot, ctx, e);
                    }
                }
            }
        }
    }
    spu_wrch(ctx, channel, value);
    if (slot && e && (is_atomic_get || is_atomic_put)) {
        e->atomic_status = ctx ? ctx->atomic_stat : 0;
        if (is_atomic_get && atomic_span_valid && slot->header.valid)
            append_payload(slot, ctx, e, ctx->mfc_lsa & SPU_LS_MASK,
                           MFC_ATOMIC_LINE);
    }
    if (slot && e && is_get && slot->header.valid && valid_ls_span(slot, ctx, e))
        append_dma_payload(slot, ctx, e);
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
extern "C" u128 haze_job13_capture_rdch(spu_context* ctx, uint32_t channel) {
    CaptureSlot* slot = g_active;
    if (slot && ctx != g_active_ctx) {
        invalidate(slot, HAZE_JOB13_INVALID_CONTEXT);
        slot = nullptr;
    }
    haze_job13_capture_event* e = nullptr;
    if (slot) {
        e = append_event(slot, HAZE_JOB13_EVENT_RDCH, channel);
        if (!supported_rdch(channel)) invalidate(slot, HAZE_JOB13_INVALID_UNSUPPORTED_CHANNEL);
        if (e) snapshot_staging(slot, e, ctx);
    }
    const u128 value = spu_rdch(ctx, channel);
    if (slot && e) std::memcpy(e->value, &value, sizeof(e->value));
    return value;
}
#pragma clang diagnostic pop

extern "C" uint32_t haze_job13_capture_rchcnt(spu_context* ctx, uint32_t channel) {
    CaptureSlot* slot = g_active;
    if (slot && ctx != g_active_ctx) {
        invalidate(slot, HAZE_JOB13_INVALID_CONTEXT);
        slot = nullptr;
    }
    haze_job13_capture_event* e = nullptr;
    if (slot) {
        e = append_event(slot, HAZE_JOB13_EVENT_RCHCNT, channel);
        if (!supported_rchcnt(channel)) invalidate(slot, HAZE_JOB13_INVALID_UNSUPPORTED_CHANNEL);
        if (e) snapshot_staging(slot, e, ctx);
    }
    const uint32_t count = spu_rchcnt(ctx, channel);
    if (slot && e) std::memcpy(e->value, &count, sizeof(count));
    return count;
}
