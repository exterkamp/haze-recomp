#define _CRT_SECURE_NO_WARNINGS
#include "haze_job13_capture.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

static uint32_t g_wrch_calls;
static uint32_t g_rdch_calls;
static uint32_t g_rchcnt_calls;
static std::vector<uint8_t> g_put_bytes;
static uint32_t g_atomic_get_calls;
static uint32_t g_putllc_status;

extern "C" bool job13_c_begin(spu_context* ctx);
extern "C" void job13_c_end(spu_context* ctx);
extern "C" void job13_c_wrch_word(spu_context* ctx, uint32_t channel, uint32_t word);
extern "C" uint32_t job13_c_rdch_word(spu_context* ctx, uint32_t channel);
extern "C" uint32_t job13_c_rchcnt(spu_context* ctx, uint32_t channel);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type-c-linkage"
extern "C" void spu_wrch(spu_context* ctx, uint32_t channel, u128 value) {
    ++g_wrch_calls;
    const uint32_t v = value._u32[0];
    switch (channel) {
    case MFC_LSA: ctx->mfc_lsa = v; break;
    case MFC_EAH: ctx->mfc_eah = v; break;
    case MFC_EAL: ctx->mfc_eal = v; break;
    case MFC_Size: ctx->mfc_size = v; break;
    case MFC_TagID: ctx->mfc_tag = v; break;
    case MFC_WrTagMask: ctx->mfc_tag_mask = v; break;
    case MFC_Cmd:
        if (v == MFC_PUT_CMD || v == MFC_PUTB_CMD || v == MFC_PUTF_CMD)
            g_put_bytes.assign(ctx->ls + ctx->mfc_lsa,
                               ctx->ls + ctx->mfc_lsa + ctx->mfc_size);
        else if (v == MFC_GET_CMD || v == MFC_GETB_CMD || v == MFC_GETF_CMD)
            for (uint32_t i = 0; i < ctx->mfc_size; ++i)
                ctx->ls[ctx->mfc_lsa + i] = (uint8_t)(0xA0u + (i & 0x3Fu));
        else if (v == MFC_GETLLAR_CMD) {
            const uint32_t lsa = ctx->mfc_lsa & SPU_LS_MASK;
            const uint8_t base = (uint8_t)(g_atomic_get_calls++ ? 0x80u : 0x40u);
            for (uint32_t i = 0; i < MFC_ATOMIC_LINE; ++i)
                ctx->ls[lsa + i] = (uint8_t)(base + i);
            ctx->atomic_stat = 4;
            ctx->resv_ea = ctx->mfc_eal & ~(MFC_ATOMIC_LINE - 1u);
            ctx->resv_valid = 1;
        } else if (v == MFC_PUTLLC_CMD) {
            ctx->atomic_stat = g_putllc_status;
            ctx->resv_valid = 0;
        }
        break;
    default: break;
    }
}

extern "C" u128 spu_rdch(spu_context* ctx, uint32_t channel) {
    ++g_rdch_calls;
    u128 value{};
    if (channel == MFC_RdAtomicStat) value._u32[0] = ctx->atomic_stat;
    else for (unsigned i = 0; i < 16; ++i) value._u8[i] = (uint8_t)(0x30u + i);
    return value;
}

extern "C" uint32_t spu_rchcnt(spu_context*, uint32_t channel) {
    ++g_rchcnt_calls;
    if (channel == MFC_RdAtomicStat) return 1;
    return 0xAABBCCDDu;
}
#pragma clang diagnostic pop

static std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    assert(in.good());
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    assert(size >= (std::streamoff)sizeof(haze_job13_capture_header));
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes((size_t)size);
    in.read((char*)bytes.data(), (std::streamsize)bytes.size());
    assert(in.good() || in.eof());
    assert((size_t)in.gcount() == bytes.size());
    return bytes;
}

static std::filesystem::path only_capture(const std::filesystem::path& dir) {
    std::filesystem::path found;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() == ".bin") {
            assert(found.empty());
            found = entry.path();
        }
    }
    assert(!found.empty());
    return found;
}

static void init_context(spu_context& ctx) {
    ctx.ls = ctx.ls_store;
    ctx.image_id = 32;
    ctx.pc = 0x3118;
    ctx.status = SPU_STATUS_RUNNING;
    ctx.mfc_lsa = 0x40;
    ctx.mfc_eah = 0x11223344;
    ctx.mfc_eal = 0x55667788;
    ctx.mfc_size = 8;
    ctx.mfc_tag = 7;
    ctx.mfc_tag_mask = 0x80;
    ctx.mfc_tag_status = 0x11;
    for (unsigned i = 0; i < 128; ++i)
        for (unsigned j = 0; j < 16; ++j) ctx.gpr[i]._u8[j] = (uint8_t)(i + j);
    for (unsigned i = 0; i < SPU_LS_SIZE; ++i) ctx.ls[i] = (uint8_t)(i * 13u);
}

static void stage(spu_context* ctx, uint32_t channel, uint32_t value) {
    u128 v{};
    v._u32[0] = value;
    haze_job13_capture_wrch(ctx, channel, v);
}

static void test_off(const std::filesystem::path& dir) {
    auto ctx = std::make_unique<spu_context>(); init_context(*ctx);
    assert(!haze_job13_capture_begin(ctx.get()));
    stage(ctx.get(), MFC_LSA, 64);
    const u128 rd = haze_job13_capture_rdch(ctx.get(), MFC_RdTagStat);
    const uint32_t count = haze_job13_capture_rchcnt(ctx.get(), MFC_RdTagStat);
    haze_job13_capture_end(ctx.get());
    assert(rd._u8[0] == 0x30 && count == 0xAABBCCDDu);
    assert(g_wrch_calls == 1 && g_rdch_calls == 1 && g_rchcnt_calls == 1);
    assert(std::filesystem::is_empty(dir));
}

static void test_valid(const std::filesystem::path& dir) {
    auto ctx = std::make_unique<spu_context>(); init_context(*ctx);
    assert(job13_c_begin(ctx.get()));
    job13_c_wrch_word(ctx.get(), MFC_LSA, 64);
    job13_c_wrch_word(ctx.get(), MFC_EAH, 0x11223344);
    job13_c_wrch_word(ctx.get(), MFC_EAL, 0x55667788);
    job13_c_wrch_word(ctx.get(), MFC_Size, 8);
    job13_c_wrch_word(ctx.get(), MFC_TagID, 7);
    job13_c_wrch_word(ctx.get(), MFC_Cmd, MFC_PUT_CMD);
    job13_c_wrch_word(ctx.get(), MFC_LSA, 128);
    job13_c_wrch_word(ctx.get(), MFC_Size, 8);
    job13_c_wrch_word(ctx.get(), MFC_Cmd, MFC_GETB_CMD);
    const uint32_t rd_word = job13_c_rdch_word(ctx.get(), MFC_RdTagStat);
    const uint32_t count = job13_c_rchcnt(ctx.get(), MFC_RdTagStat);
    ctx->pc = 0x3F48;
    job13_c_end(ctx.get());
    assert(g_wrch_calls == 9 && g_rdch_calls == 1 && g_rchcnt_calls == 1);
    assert(g_put_bytes.size() == 8);
    assert(rd_word == 0x33323130u && count == 0xAABBCCDDu);

    const auto bytes = read_file(only_capture(dir));
    const auto* h = (const haze_job13_capture_header*)bytes.data();
    assert(std::memcmp(h->magic, "HZJOB13", 7) == 0);
    assert(h->version == HAZE_JOB13_CAPTURE_VERSION && h->valid == 1);
    assert(h->invalid_reason == HAZE_JOB13_VALID && h->image_id == 32);
    assert(h->qpc_frequency > 0);
    assert(h->initial_pc == 0x3118 && h->final_pc == 0x3F48);
    assert(h->event_count == 11 && h->event_calls == 11);
    assert(h->payload_size == 16);
    size_t at = sizeof(*h);
    const uint8_t* initial_gpr = bytes.data() + at;
    at += HAZE_JOB13_CAPTURE_GPR_BYTES;
    const uint8_t* final_gpr = bytes.data() + at;
    at += HAZE_JOB13_CAPTURE_GPR_BYTES;
    const uint8_t* initial_ls = bytes.data() + at;
    at += HAZE_JOB13_CAPTURE_LS_SIZE;
    const uint8_t* final_ls = bytes.data() + at;
    at += HAZE_JOB13_CAPTURE_LS_SIZE;
    const auto* events = (const haze_job13_capture_event*)(bytes.data() + at);
    at += (size_t)h->event_count * sizeof(*events);
    const uint8_t* payload = bytes.data() + at;
    assert(bytes.size() == at + h->payload_size);
    assert(std::memcmp(initial_gpr, ctx->gpr, HAZE_JOB13_CAPTURE_GPR_BYTES) == 0);
    assert(std::memcmp(final_gpr, ctx->gpr, HAZE_JOB13_CAPTURE_GPR_BYTES) == 0);
    assert(initial_ls[0] == (uint8_t)0 && initial_ls[64] == (uint8_t)(64u * 13u));
    assert(final_ls[128] == 0xA0 && final_ls[135] == 0xA7);
    assert(events[5].kind == HAZE_JOB13_EVENT_WRCH && events[5].channel == MFC_Cmd);
    assert(events[5].command == MFC_PUT_CMD && events[5].lsa == 64 && events[5].size == 8);
    assert(events[5].payload_offset == 0 && events[5].payload_length == 8);
    assert(std::memcmp(payload, g_put_bytes.data(), 8) == 0);
    assert(events[8].command == MFC_GETB_CMD && events[8].lsa == 128);
    assert(events[8].payload_offset == 8 && events[8].payload_length == 8);
    for (unsigned i = 0; i < 8; ++i) assert(payload[8 + i] == (uint8_t)(0xA0u + i));
    assert(events[9].kind == HAZE_JOB13_EVENT_RDCH && events[9].value[0] == 0x30);
    assert(events[10].kind == HAZE_JOB13_EVENT_RCHCNT);
    uint32_t returned_count = 0;
    std::memcpy(&returned_count, events[10].value, sizeof(returned_count));
    assert(returned_count == 0xAABBCCDDu);
}

static void test_atomic_retry(const std::filesystem::path& dir) {
    auto ctx = std::make_unique<spu_context>(); init_context(*ctx);
    ctx->atomic_stat = 2; /* initial state is retained in the v2 header */
    g_atomic_get_calls = 0;
    g_putllc_status = 1;
    assert(haze_job13_capture_begin(ctx.get()));
    stage(ctx.get(), MFC_LSA, 0x101);       /* SDK masks, but does not 128-align, LSA */
    stage(ctx.get(), MFC_EAH, 0x12345678); /* preserved, though SDK atomics use EAL only */
    stage(ctx.get(), MFC_EAL, 0x1007F);     /* SDK aligns EA down to 128 bytes */
    stage(ctx.get(), MFC_Size, 4);         /* preserved; atomics always transfer 128 bytes */
    stage(ctx.get(), MFC_TagID, 9);
    stage(ctx.get(), MFC_Cmd, MFC_GETLLAR_CMD);
    stage(ctx.get(), MFC_Cmd, MFC_PUTLLC_CMD); /* failed compare: still records attempted line */
    g_putllc_status = 0;
    stage(ctx.get(), MFC_Cmd, MFC_GETLLAR_CMD);
    stage(ctx.get(), MFC_Cmd, MFC_PUTLLC_CMD);
    const u128 status = haze_job13_capture_rdch(ctx.get(), MFC_RdAtomicStat);
    const uint32_t ready = haze_job13_capture_rchcnt(ctx.get(), MFC_RdAtomicStat);
    haze_job13_capture_end(ctx.get());

    assert(status._u32[0] == 0 && ready == 1);
    assert(g_wrch_calls == 9 && g_rdch_calls == 1 && g_rchcnt_calls == 1);
    assert(!ctx->resv_valid);
    const auto bytes = read_file(only_capture(dir));
    const auto* h = (const haze_job13_capture_header*)bytes.data();
    assert(h->version == 2 && h->valid == 1);
    assert(h->initial_atomic_stat == 2);
    assert(h->event_count == 11 && h->payload_size == 4 * MFC_ATOMIC_LINE);
    size_t at = sizeof(*h) + 2u * HAZE_JOB13_CAPTURE_GPR_BYTES +
                2u * HAZE_JOB13_CAPTURE_LS_SIZE;
    const auto* events = (const haze_job13_capture_event*)(bytes.data() + at);
    at += (size_t)h->event_count * sizeof(*events);
    const uint8_t* payload = bytes.data() + at;
    assert(events[5].command == MFC_GETLLAR_CMD && events[5].atomic_status == 4);
    assert(events[5].lsa == 0x101 && events[5].eah == 0x12345678 &&
           events[5].eal == 0x1007F && events[5].size == 4 && events[5].tag == 9);
    assert(events[5].payload_length == MFC_ATOMIC_LINE && events[5].payload_offset == 0);
    assert(payload[0] == 0x40 && payload[127] == (uint8_t)(0x40u + 127));
    assert(events[6].command == MFC_PUTLLC_CMD && events[6].atomic_status == 1);
    assert(events[6].lsa == 0x101 && events[6].payload_length == MFC_ATOMIC_LINE &&
           events[6].payload_offset == MFC_ATOMIC_LINE);
    assert(std::memcmp(payload + events[6].payload_offset, payload, MFC_ATOMIC_LINE) == 0);
    assert(events[7].command == MFC_GETLLAR_CMD && events[7].atomic_status == 4);
    assert(events[7].payload_length == MFC_ATOMIC_LINE &&
           events[7].payload_offset == 2 * MFC_ATOMIC_LINE);
    assert(payload[events[7].payload_offset] == 0x80);
    assert(events[8].command == MFC_PUTLLC_CMD && events[8].atomic_status == 0);
    assert(events[8].payload_length == MFC_ATOMIC_LINE &&
           events[8].payload_offset == 3 * MFC_ATOMIC_LINE);
    assert(std::memcmp(payload + events[8].payload_offset,
                       payload + events[7].payload_offset, MFC_ATOMIC_LINE) == 0);
    assert(events[9].kind == HAZE_JOB13_EVENT_RDCH && events[9].channel == MFC_RdAtomicStat &&
           events[9].value[0] == 0);
    assert(events[10].kind == HAZE_JOB13_EVENT_RCHCNT &&
           events[10].channel == MFC_RdAtomicStat && events[10].value[0] == 1);
}

static void test_invalid_and_event_overflow(const std::filesystem::path& dir) {
    {
        auto ctx = std::make_unique<spu_context>(); init_context(*ctx);
        assert(haze_job13_capture_begin(ctx.get()));
        stage(ctx.get(), MFC_Cmd, MFC_PUTLLUC_CMD);
        haze_job13_capture_end(ctx.get());
        const auto bytes = read_file(only_capture(dir));
        const auto* h = (const haze_job13_capture_header*)bytes.data();
        assert(h->valid == 0 && h->invalid_reason == HAZE_JOB13_INVALID_UNSUPPORTED_COMMAND);
        assert(g_wrch_calls == 1);
        std::filesystem::remove(only_capture(dir));
    }
    {
        auto ctx = std::make_unique<spu_context>(); init_context(*ctx);
        assert(haze_job13_capture_begin(ctx.get()));
        stage(ctx.get(), SPU_WrOutMbox, 0x1234);
        haze_job13_capture_end(ctx.get());
        const auto bytes = read_file(only_capture(dir));
        const auto* h = (const haze_job13_capture_header*)bytes.data();
        assert(h->valid == 0 && h->invalid_reason == HAZE_JOB13_INVALID_UNSUPPORTED_CHANNEL);
        assert(g_wrch_calls == 2);
        std::filesystem::remove(only_capture(dir));
    }
    {
        auto ctx = std::make_unique<spu_context>(); init_context(*ctx);
        ctx->int_enable = 1;
        assert(haze_job13_capture_begin(ctx.get()));
        haze_job13_capture_end(ctx.get());
        const auto bytes = read_file(only_capture(dir));
        const auto* h = (const haze_job13_capture_header*)bytes.data();
        assert(h->valid == 0 && h->invalid_reason == HAZE_JOB13_INVALID_INTERRUPT_STATE);
        std::filesystem::remove(only_capture(dir));
    }
    {
        auto ctx = std::make_unique<spu_context>(); init_context(*ctx);
        ctx->resv_valid = 1; ctx->resv_ea = 0x1000;
        assert(haze_job13_capture_begin(ctx.get()));
        haze_job13_capture_end(ctx.get());
        const auto bytes = read_file(only_capture(dir));
        const auto* h = (const haze_job13_capture_header*)bytes.data();
        assert(h->valid == 0 && h->invalid_reason == HAZE_JOB13_INVALID_RESERVATION_STATE);
        std::filesystem::remove(only_capture(dir));
    }
    {
        auto ctx = std::make_unique<spu_context>(); init_context(*ctx);
        assert(haze_job13_capture_begin(ctx.get()));
        stage(ctx.get(), MFC_Cmd, MFC_GETLLAR_CMD);
        haze_job13_capture_end(ctx.get()); /* live reservation cannot escape as a valid batch */
        const auto bytes = read_file(only_capture(dir));
        const auto* h = (const haze_job13_capture_header*)bytes.data();
        assert(h->valid == 0 && h->invalid_reason == HAZE_JOB13_INVALID_RESERVATION_STATE);
        std::filesystem::remove(only_capture(dir));
    }
    {
        auto ctx = std::make_unique<spu_context>(); init_context(*ctx);
        assert(haze_job13_capture_begin(ctx.get()));
        for (uint32_t i = 0; i < HAZE_JOB13_CAPTURE_MAX_EVENTS + 1; ++i)
            (void)haze_job13_capture_rchcnt(ctx.get(), MFC_RdTagStat);
        haze_job13_capture_end(ctx.get());
        const auto bytes = read_file(only_capture(dir));
        const auto* h = (const haze_job13_capture_header*)bytes.data();
        assert(h->valid == 0 && h->invalid_reason == HAZE_JOB13_INVALID_EVENT_OVERFLOW);
        assert(h->event_count == HAZE_JOB13_CAPTURE_MAX_EVENTS);
        assert(h->event_calls == HAZE_JOB13_CAPTURE_MAX_EVENTS + 1);
        assert(g_rchcnt_calls == HAZE_JOB13_CAPTURE_MAX_EVENTS + 1);
    }
}

static void test_payload_overflow(const std::filesystem::path& dir) {
    auto ctx = std::make_unique<spu_context>(); init_context(*ctx);
    assert(haze_job13_capture_begin(ctx.get()));
    ctx->mfc_lsa = 0;
    ctx->mfc_size = SPU_LS_SIZE;
    ctx->mfc_tag = 0;
    u128 cmd{};
    cmd._u32[0] = MFC_PUT_CMD;
    for (unsigned i = 0; i < HAZE_JOB13_CAPTURE_MAX_PAYLOAD / SPU_LS_SIZE + 1; ++i)
        haze_job13_capture_wrch(ctx.get(), MFC_Cmd, cmd);
    haze_job13_capture_end(ctx.get());
    const auto bytes = read_file(only_capture(dir));
    const auto* h = (const haze_job13_capture_header*)bytes.data();
    assert(h->valid == 0 && h->invalid_reason == HAZE_JOB13_INVALID_PAYLOAD_OVERFLOW);
    assert(h->payload_size == HAZE_JOB13_CAPTURE_MAX_PAYLOAD);
    assert(g_wrch_calls == HAZE_JOB13_CAPTURE_MAX_PAYLOAD / SPU_LS_SIZE + 1);
}

static void test_nested_and_context(const std::filesystem::path& dir) {
    auto ctx = std::make_unique<spu_context>(); init_context(*ctx);
    auto other = std::make_unique<spu_context>(); init_context(*other);
    assert(haze_job13_capture_begin(ctx.get()));
    assert(!haze_job13_capture_begin(ctx.get()));
    haze_job13_capture_end(ctx.get()); /* balances the suppressed nested begin */
    (void)haze_job13_capture_rdch(other.get(), MFC_RdTagStat);
    haze_job13_capture_end(ctx.get());
    assert(g_rdch_calls == 1);
    const auto bytes = read_file(only_capture(dir));
    const auto* h = (const haze_job13_capture_header*)bytes.data();
    assert(h->valid == 0 && h->invalid_reason == HAZE_JOB13_INVALID_CONTEXT);
    assert(h->event_count == 0); /* foreign-context read is never joined to the batch */
}

static void test_batch_limit(const std::filesystem::path& dir) {
    for (unsigned i = 0; i < HAZE_JOB13_CAPTURE_MAX_BATCHES; ++i) {
        auto ctx = std::make_unique<spu_context>(); init_context(*ctx);
        assert(haze_job13_capture_begin(ctx.get()));
        haze_job13_capture_end(ctx.get());
    }
    auto extra = std::make_unique<spu_context>(); init_context(*extra);
    assert(!haze_job13_capture_begin(extra.get()));
    haze_job13_capture_end(extra.get());
    unsigned files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
        if (entry.path().extension() == ".bin") ++files;
    assert(files == HAZE_JOB13_CAPTURE_MAX_BATCHES);
}

static void prepare_dir(const std::filesystem::path& dir, bool gate_open) {
    assert(!std::filesystem::exists(dir));
    std::filesystem::create_directories(dir);
    const auto gate = dir / "capture.gate";
    if (gate_open) std::ofstream(gate).put('1');
    _putenv_s("HAZE_JOB13_CAPTURE_DIR", dir.string().c_str());
    _putenv_s("HAZE_JOB13_CAPTURE_GATE", gate.string().c_str());
}

int main(int argc, char** argv) {
    assert(argc == 3);
    const std::filesystem::path dir(argv[2]);
    const std::string mode(argv[1]);
    prepare_dir(dir, mode != "off");
    if (mode == "off") test_off(dir);
    else if (mode == "valid") test_valid(dir);
    else if (mode == "atomic") test_atomic_retry(dir);
    else if (mode == "invalid") test_invalid_and_event_overflow(dir);
    else if (mode == "payload") test_payload_overflow(dir);
    else if (mode == "nested") test_nested_and_context(dir);
    else if (mode == "limit") test_batch_limit(dir);
    else assert(false && "unknown mode");
    std::cout << "PASS job13 capture " << mode << '\n';
    return 0;
}
