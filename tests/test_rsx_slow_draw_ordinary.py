#!/usr/bin/env python3
"""Compile the exact slow-draw recorder and sink_end wrapper from the SDK."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path
import re
import subprocess


BEGIN = "/* BEGIN RSX_SLOW_DRAW_DIAGNOSTIC */"
END = "/* END RSX_SLOW_DRAW_DIAGNOSTIC */"


def extract_function(source: str, signature: str, last: bool = False) -> str:
    start = source.rfind(signature) if last else source.find(signature)
    if start < 0:
        raise ValueError(f"missing function signature: {signature}")
    brace = source.find("{", start)
    if brace < 0:
        raise ValueError(f"missing function body: {signature}")
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    raise ValueError(f"unterminated function body: {signature}")


def extract_block(source: str) -> str:
    start = source.find(BEGIN)
    end = source.find(END, start + len(BEGIN))
    if start < 0 or end < 0:
        raise ValueError("slow-draw diagnostic markers are missing")
    return source[start + len(BEGIN) : end]


PREFIX = r"""
#define _CRT_SECURE_NO_WARNINGS
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned long long u64;
typedef unsigned int u32;
typedef long long LONGLONG;
typedef long long LONG64;
typedef struct { long long QuadPart; } LARGE_INTEGER;
typedef int SRWLOCK;
#define SRWLOCK_INIT 0
static unsigned qpc_calls, qpf_calls, cycle_calls;
static unsigned long long fake_qpc = 10000;
static unsigned long long fake_cycles;
static int env_enabled;
static int fake_early_return;
static const char* output_path;
static int fake_getenv_calls;
static const char* fake_getenv(const char* name) {
    ++fake_getenv_calls;
    if (!env_enabled) return NULL;
    if (strcmp(name, "RSX_SLOW_DRAW_CSV") == 0) return output_path;
    if (strcmp(name, "RSX_SLOW_DRAW_THRESHOLD_US") == 0) return "250";
    return NULL;
}
#define getenv fake_getenv
static int QueryPerformanceFrequency(LARGE_INTEGER* out) {
    ++qpf_calls; out->QuadPart = 1000000; return 1;
}
static int QueryPerformanceCounter(LARGE_INTEGER* out) {
    ++qpc_calls; fake_qpc += 1000; out->QuadPart = (long long)fake_qpc; return 1;
}
static int QueryThreadCycleTime(void* thread, unsigned long long* out) {
    (void)thread; ++cycle_calls; fake_cycles += 111; *out = fake_cycles; return 1;
}
static void* GetCurrentThread(void) { return (void*)1; }
static unsigned GetCurrentThreadId(void) { return 77; }
static LONG64 InterlockedIncrement64(volatile LONG64* p) { return ++*p; }
static void AcquireSRWLockExclusive(SRWLOCK* lock) { (void)lock; }
static void ReleaseSRWLockExclusive(SRWLOCK* lock) { (void)lock; }
static u32 g_ld_frames = 9;
static struct { int ready; } g;
typedef struct { int unused; } rsx_dispatch;
#ifdef TEST_PROFILE
#define YZ_PERF_PROFILE 1
typedef struct {
    LONGLONG first_draw_qpc, last_draw_qpc;
    struct { u64 sink_end_qpc; } total;
} fake_profile_state;
static fake_profile_state g_ld_profile;
static LONGLONG ld_profile_qpc(void) { LARGE_INTEGER q; QueryPerformanceCounter(&q); return q.QuadPart; }
#endif
"""


EXIT_STUBS = r"""
static unsigned summaries_called;
static void ld_report_counters(void) { ++summaries_called; }
static void ld_slow_flush_dump(void) { ++summaries_called; }
static void ld_texture_content_summary(void) { ++summaries_called; }
static void ld_pull_summary(void) { ++summaries_called; }
static void ld_present_measure_dump(void) { ++summaries_called; }
static void ld_texture_watch_summary(void) { ++summaries_called; }
static void ld_geometry_cache_summary(void) { ++summaries_called; }
static void ld_vertex_worker_summary(void) { ++summaries_called; }
#ifdef YZ_PERF_PROFILE
static void ld_profile_buffers_dump(void) { ++summaries_called; }
#endif
"""


def fixture_source(diag: str, wrapper: str, exit_dump: str) -> str:
    return PREFIX + diag + r"""
""" + EXIT_STUBS + exit_dump + r"""
static unsigned stub_calls;
static void sink_end_impl(void*, const rsx_dispatch*, ld_slow_draw_scope* scope) {
    ++stub_calls;
    if (!scope || !scope->active) return;
    scope->event.primitive = 7;
    scope->event.packets = 3;
    scope->event.source_refs = 6;
    scope->event.vertices = 4;
    scope->event.topology_count = 2;
    ld_slow_draw_advance(scope, LD_SLOW_DRAW_PIPELINE_CONSTANTS);
    ld_slow_draw_advance(scope, LD_SLOW_DRAW_RESOURCE_RESOLUTION);
    scope->event.target = 5;
    scope->event.texture_mask = 0x21;
    scope->event.vertex_texture_mask = 0x02;
    ld_slow_draw_advance(scope, LD_SLOW_DRAW_COMMAND_RECORDING);
    if (fake_early_return) return;
}
""" + wrapper + r"""
int main(int argc, char** argv) {
    if (argc != 3) return 10;
    output_path = argv[1];
    const int enabled = atoi(argv[2]);
    env_enabled = enabled;
    rsx_dispatch dispatch = {};
    ld_slow_draw_configure();
    if (!enabled) {
        for (unsigned i = 0; i < 32; ++i) sink_end(NULL, &dispatch);
        rsx_live_draw_dump_present_samples();
#ifdef YZ_PERF_PROFILE
        if (qpc_calls != 64 || qpf_calls || cycle_calls || g_ld_slow_draw.enabled ||
#else
        if (qpc_calls || qpf_calls || cycle_calls || g_ld_slow_draw.enabled ||
#endif
            g_ld_slow_draw.events || g_ld_slow_draw.recorded ||
            g_ld_slow_draw.callbacks || stub_calls != 32) return 11;
        return 0;
    }
    if (!g_ld_slow_draw.enabled || qpf_calls != 1 || !g_ld_slow_draw.events)
        return 12;
    fake_early_return = 1;
    for (unsigned i = 0; i < LD_SLOW_DRAW_CAP + 4; ++i)
        sink_end(NULL, &dispatch);
    if (stub_calls != LD_SLOW_DRAW_CAP + 4 ||
        g_ld_slow_draw.callbacks != LD_SLOW_DRAW_CAP + 4 ||
        g_ld_slow_draw.recorded != LD_SLOW_DRAW_CAP ||
        g_ld_slow_draw.overwritten != 4 || cycle_calls != 2 * (LD_SLOW_DRAW_CAP + 4))
        return 13;
    for (unsigned i = 0; i < g_ld_slow_draw.recorded; ++i) {
        const ld_slow_draw_event* e = &g_ld_slow_draw.events[i];
        u64 stages = 0;
        for (unsigned j = 0; j < LD_SLOW_DRAW_STAGE_COUNT; ++j) stages += e->stage_ticks[j];
        if (!e->callback_id || e->qpc_end < e->qpc_begin ||
            stages != e->qpc_end - e->qpc_begin || e->target != 5 ||
            e->primitive != 7 || e->texture_mask != 0x21 ||
            e->stage != LD_SLOW_DRAW_COMMAND_RECORDING) return 15;
    }
    g.ready = 1;
    rsx_live_draw_dump_present_samples();
    if (!g_ld_slow_draw.dumped) return 16;
    FILE* first_dump = fopen(output_path, "rb");
    if (!first_dump || fseek(first_dump, 0, SEEK_END) != 0) return 17;
    const long first_size = ftell(first_dump);
    fclose(first_dump);
    rsx_live_draw_dump_present_samples();
    FILE* second_dump = fopen(output_path, "rb");
    if (!second_dump || fseek(second_dump, 0, SEEK_END) != 0) return 18;
    const long second_size = ftell(second_dump);
    fclose(second_dump);
    if (first_size <= 0 || second_size != first_size) return 19;
#ifdef YZ_PERF_PROFILE
    if (summaries_called != 18) return 20;
#else
    if (summaries_called != 16) return 21;
#endif
    free(g_ld_slow_draw.events);
    g_ld_slow_draw.events = NULL;
    return 0;
}
"""


def run_test(compiler: str, source: Path, output_dir: Path, profile: bool) -> None:
    text = source.read_text(encoding="utf-8")
    diag = extract_block(text)
    wrapper = extract_function(text, "static void sink_end(void* user, const rsx_dispatch* r)")
    exit_dump = extract_function(text, "void rsx_live_draw_dump_present_samples(void)", last=True)
    # Assert the checked-in phase boundaries remain in the actual draw path.
    body = extract_function(text, "static void sink_end_impl(void* user, const rsx_dispatch* r,")
    stages = re.findall(r"ld_slow_draw_advance\(slow_draw,\s*(LD_SLOW_DRAW_\w+)\)", body)
    if stages != ["LD_SLOW_DRAW_PIPELINE_CONSTANTS", "LD_SLOW_DRAW_RESOURCE_RESOLUTION",
                  "LD_SLOW_DRAW_COMMAND_RECORDING"]:
        raise AssertionError(f"unexpected stage boundary sequence: {stages}")
    output_dir.mkdir(parents=True, exist_ok=True)
    mode = "profile" if profile else "ordinary"
    fixture = output_dir / f"slow_draw_{mode}.cpp"
    exe = output_dir / f"slow_draw_{mode}.exe"
    csv_path = output_dir / f"slow_draw_{mode}.csv"
    fixture.write_text(fixture_source(diag, wrapper, exit_dump), encoding="utf-8")
    cmd = [compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX",
           "/Od", f"/Fo{output_dir / f'{mode}.obj'}", str(fixture), f"/Fe{exe}"]
    if profile:
        cmd.insert(1, "/DTEST_PROFILE")
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe), str(csv_path), "1"], check=True)
    with csv_path.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    if len(rows) != 1024:
        raise AssertionError(f"expected 1024 retained rows, found {len(rows)}")
    if int(rows[0]["callback_id"]) != 5 or int(rows[-1]["callback_id"]) != 1028:
        raise AssertionError("retained callback suffix is not oldest-to-newest")
    for row in rows:
        wall = float(row["wall_ms"])
        phases = sum(float(row[k]) for k in ("geometry_preparation_ms", "pipeline_constants_ms",
                                              "resource_resolution_ms", "command_recording_ms"))
        if abs(wall - phases) > 0.00001:
            raise AssertionError("phase wall-time conservation failed")
        if row["thread_cycles_valid"] != "1" or int(row["thread_cycles_delta_not_ms"]) <= 0:
            raise AssertionError("raw thread-cycle metadata missing")
    subprocess.run([str(exe), str(output_dir / f"disabled_{mode}.csv"), "0"], check=True)
    if (output_dir / f"disabled_{mode}.csv").exists():
        raise AssertionError("disabled recorder unexpectedly dumped a file")
    disabled_clock_note = "disabled diagnostic clocks=0" if not profile else "disabled diagnostic cycles=0 (profile QPC preserved)"
    print(f"PASS {mode}: {disabled_clock_note}; retained=1024 overwritten=4; early-return and phases conserved")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, help="clang-cl executable")
    parser.add_argument("--source", type=Path, required=True, help="SDK rsx_live_draw.c")
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    run_test(args.compiler, args.source, args.output_dir, profile=False)
    run_test(args.compiler, args.source, args.output_dir, profile=True)


if __name__ == "__main__":
    main()
