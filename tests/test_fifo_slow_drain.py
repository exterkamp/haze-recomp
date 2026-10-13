"""Focused native regression fixture for the SDK slow FIFO drain diagnostic.

Extracts the live Windows diagnostic and wrapper from cellGcmSys.c, then links
them with a controllable walker and guest-memory stubs. It exercises real
Win32 locks, QPC, thread-cycle queries, and CSV output without game code.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
DIAG_BEGIN = "/* BEGIN GCM SLOW DRAIN DIAGNOSTIC */"
DIAG_END = "/* END GCM SLOW DRAIN DIAGNOSTIC */"
WRAPPER_BEGIN = "/* BEGIN GCM SLOW DRAIN WRAPPER */"
WRAPPER_END = "/* END GCM SLOW DRAIN WRAPPER */"


def extract_marked(source: str, begin: str, end: str) -> str:
    if source.count(begin) != 1 or source.count(end) != 1:
        raise RuntimeError(f"expected unique extraction markers {begin!r} / {end!r}")
    start = source.index(begin)
    finish = source.index(end, start + len(begin))
    if finish <= start:
        raise RuntimeError(f"invalid extraction marker order for {begin!r}")
    return source[start:finish]


def fixture_source(diagnostic: str, wrapper: str) -> str:
    return f'''#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef uint64_t u64;
#define GCM_CONTROL_GUEST_ADDR 0xC0002000u

static SRWLOCK s_gcm_fifo_lock = SRWLOCK_INIT;
static u32 s_gcm_context_ea;
static int s_gcm_syscall_mode;
static u32 s_fifo_getoff = 0x88u;
static volatile LONG guest_read_count;
static volatile LONG frame_query_count;
static volatile LONG walker_calls;
static volatile LONG walk_mode;

u32 vm_read32(u64 address)
{{
    if (address != GCM_CONTROL_GUEST_ADDR) abort();
    InterlockedIncrement(&guest_read_count);
    return 0x1234u;
}}

u32 rsx_live_draw_get_frames(void)
{{
    InterlockedIncrement(&frame_query_count);
    return 77u;
}}

static void gcm_rsx_process_fifo_unlocked(void)
{{
    InterlockedIncrement(&walker_calls);
    s_fifo_getoff += 4u;
    if (InterlockedCompareExchange(&walk_mode, 0, 0) == 1)
        Sleep(12);
}}

{diagnostic}

{wrapper}

static DWORD WINAPI hold_fifo_lock(LPVOID unused)
{{
    HANDLE ready = (HANDLE)unused;
    AcquireSRWLockExclusive(&s_gcm_fifo_lock);
    SetEvent(ready);
    Sleep(12);
    ReleaseSRWLockExclusive(&s_gcm_fifo_lock);
    return 0;
}}

int main(int argc, char** argv)
{{
    if (argc != 2) return 2;
    const char* mode = argv[1];
    const int enabled = strcmp(mode, "disabled") != 0;
    s_gcm_context_ea = (strcmp(mode, "invalid-context") == 0) ? 0u : 0x4000u;
    s_gcm_syscall_mode = 0;
    InterlockedExchange(&walk_mode, strcmp(mode, "body") == 0 ||
                                    strcmp(mode, "invalid-context") == 0 ? 1 : 0);

    if (strcmp(mode, "lock") == 0) {{
        HANDLE ready = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (!ready) return 3;
        HANDLE holder = CreateThread(NULL, 0, hold_fifo_lock, ready, 0, NULL);
        if (!holder) return 4;
        if (WaitForSingleObject(ready, 5000) != WAIT_OBJECT_0) return 5;
        /* The holder owns the SRW lock before the measured wrapper enters. */
        cellGcm_rsx_process_fifo();
        WaitForSingleObject(holder, 5000);
        CloseHandle(holder);
        CloseHandle(ready);
    }} else {{
        if (enabled) {{
            char path[MAX_PATH];
            if (!GetEnvironmentVariableA("GCM_SLOW_DRAIN_CSV", path, sizeof(path))) return 6;
        }} else if (getenv("GCM_SLOW_DRAIN_CSV")) return 7;
        cellGcm_rsx_process_fifo();
    }}

    if (walker_calls != 1 || s_fifo_getoff != 0x8Cu) return 8;
    if (strcmp(mode, "disabled") == 0) {{
        if (guest_read_count || frame_query_count) return 9;
        return 0;
    }}
    if (frame_query_count != 1) return 10;
    if (strcmp(mode, "invalid-context") == 0) {{
        if (guest_read_count != 0) return 11;
    }} else if (guest_read_count != 2) return 12;

    /* A second dump must not append or rewrite another copy of the events. */
    cellGcm_fifo_slow_drain_dump();
    const char* csv_path = getenv("GCM_SLOW_DRAIN_CSV");
    if (!csv_path) return 13;
    FILE* sentinel_file = fopen(csv_path, "ab");
    if (!sentinel_file) return 14;
    const int sentinel_written = fputs("# second-dump-sentinel\\n", sentinel_file) >= 0;
    const int sentinel_closed = fclose(sentinel_file) == 0;
    if (!sentinel_written || !sentinel_closed) return 15;
    cellGcm_fifo_slow_drain_dump();
    return 0;
}}
'''


def compiler_command(compiler: str, source: Path, exe: Path) -> list[str]:
    name = Path(compiler).name.lower()
    if name in ("clang-cl", "clang-cl.exe"):
        return [compiler, "/nologo", "/TC", "/std:c11", "/O2", "/W4",
                "/wd4100", "/D_WIN32", str(source), f"/Fe:{exe}",
                f"/Fo:{exe.with_suffix('.obj')}"]
    return [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-D_WIN32",
            str(source), "-o", str(exe)]


def read_csv(path: Path) -> tuple[dict[str, str], list[str], list[dict[str, str]]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines or lines[-1] != "# second-dump-sentinel":
        raise AssertionError(f"second dump rewrote or appended to the CSV: {path}")
    lines.pop()
    if len(lines) < 2 or not lines[0].startswith("#qpc_frequency,"):
        raise AssertionError(f"missing slow-drain metadata in {path}")
    meta_values = lines[0][1:].split(",")
    meta = dict(zip(meta_values[0::2], meta_values[1::2]))
    header = lines[1].split(",")
    rows = [dict(zip(header, line.split(","))) for line in lines[2:]]
    return meta, header, rows


def run_case(exe: Path, mode: str, csv_path: Path | None) -> None:
    env = os.environ.copy()
    env.pop("GCM_SLOW_DRAIN_CSV", None)
    if csv_path is not None:
        env["GCM_SLOW_DRAIN_CSV"] = str(csv_path)
    result = subprocess.run([str(exe), mode], env=env, capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(
            f"{mode} failed ({result.returncode})\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    if csv_path is None:
        return
    meta, header, rows = read_csv(csv_path)
    if int(meta.get("recorded", "-1")) != 1 or int(meta.get("overwritten", "-1")) != 0:
        raise AssertionError(f"unexpected event counts: {meta}")
    if len(rows) != 1:
        raise AssertionError(f"expected one retained event, got {len(rows)}")
    row = rows[0]
    if row["renderer_frame"] != "77" or row["get_before"] != "0x00000088" or row["get_after"] != "0x0000008C":
        raise AssertionError(f"bad FIFO/frame snapshots: {row}")
    if mode == "invalid-context":
        if row["context_valid"] != "0" or row["put_before"] != "0x00000000" or row["put_after"] != "0x00000000":
            raise AssertionError(f"invalid context should suppress PUT reads: {row}")
    else:
        if row["context_valid"] != "1" or row["put_before"] != "0x00001234" or row["put_after"] != "0x00001234":
            raise AssertionError(f"valid context PUT snapshots missing: {row}")
    lock_ms = float(row["lock_wait_ms"])
    body_ms = float(row["body_ms"])
    if mode == "lock" and not (lock_ms >= 5.0 and body_ms < 10.0):
        raise AssertionError(f"held lock not separated from body: lock={lock_ms} body={body_ms}")
    if mode in ("body", "invalid-context") and not (body_ms >= 5.0 and lock_ms < 10.0):
        raise AssertionError(f"sleeping walker not separated from lock wait: lock={lock_ms} body={body_ms}")
    if not row["thread_cycles_valid"] in ("0", "1"):
        raise AssertionError(f"invalid cycle-query validity field: {row}")
    if header[-1] != "put_after":
        raise AssertionError(f"unexpected CSV schema: {header}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, help="clang-cl executable")
    parser.add_argument("--sdk", type=Path, default=ROOT / "external/ps3recomp")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "local/test-fifo-slow-drain")
    args = parser.parse_args()
    source_path = args.sdk / "libs/video/cellGcmSys.c"
    source_bytes = source_path.read_bytes()
    source = source_bytes.decode("utf-8")
    diagnostic = extract_marked(source, DIAG_BEGIN, DIAG_END)
    wrapper = extract_marked(source, WRAPPER_BEGIN, WRAPPER_END)
    if "context_valid ? vm_read32(GCM_CONTROL_GUEST_ADDR) : 0" not in wrapper:
        raise RuntimeError("wrapper no longer guards guest PUT reads by context validity")
    if "AcquireSRWLockExclusive(&s_gcm_fifo_lock)" not in wrapper:
        raise RuntimeError("wrapper no longer acquires the production FIFO lock")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    fixture = args.output_dir / "fifo_slow_drain_fixture.c"
    fixture.write_text(fixture_source(diagnostic, wrapper), encoding="utf-8")
    exe = args.output_dir / "fifo_slow_drain_fixture.exe"
    completed = subprocess.run(compiler_command(args.compiler, fixture, exe), capture_output=True, text=True)
    if completed.returncode:
        raise RuntimeError(f"compile failed ({completed.returncode})\n{completed.stdout}\n{completed.stderr}")
    print(f"SDK source sha256: {hashlib.sha256(source_bytes).hexdigest()}")
    print(f"fixture: {fixture}")
    with tempfile.TemporaryDirectory(prefix="haze-fifo-slow-drain-") as temp:
        temp_path = Path(temp)
        run_case(exe, "disabled", None)
        print("PASS disabled diagnostic avoids guest/frame reads")
        for mode in ("invalid-context", "lock", "body"):
            csv_path = temp_path / f"{mode}.csv"
            run_case(exe, mode, csv_path)
            print(f"PASS {mode} attribution and retained/idempotent CSV")
    print("4 focused FIFO slow-drain cases passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
