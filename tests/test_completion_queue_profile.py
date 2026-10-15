"""Compile the real queue/coherency/profile boundary and validate its exports."""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sdk = args.sdk.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    # Preserve earlier artifacts; never recursively remove/reuse another run.
    out = Path(tempfile.mkdtemp(prefix="completion-queue-", dir=args.output_dir.resolve()))
    exe = out / "test_completion_queue_profile.exe"
    # Compile the actual production coherency source; only the OS clock call
    # is substituted so zero-clock pending records are exercised deterministically.
    coherency_wrapper = out / "coherency_fixture.c"
    coherency_wrapper.write_text(
        '#include <windows.h>\nBOOL WINAPI completion_fixture_qpc(LARGE_INTEGER*);\n'
        '#define QueryPerformanceCounter completion_fixture_qpc\n'
        f'#include "{(sdk / "runtime/spu/spu_coherency.c").as_posix()}"\n')
    subprocess.run([
        args.compiler, "/nologo", "/EHsc", "/W0", "/O2", "/std:c++20",
        "/DHAZE_JOB_PROFILE", "/DHAZE_JOB_TRACE", "/D_CRT_SECURE_NO_WARNINGS",
        "/DHAZE_JOB_TRACE_TEST_CAPACITY=16", "/DSPU_QUEUE_DIAG_CAPACITY=16",
        "/clang:-msse4.1", f"/I{sdk / 'include'}",
        f"/I{sdk / 'runtime/spu'}", f"/I{root / 'port'}",
        f"/Fo{out}/", f"/Fe{exe}",
        str(root / "tests/test_completion_queue_profile.cpp"),
        str(root / "port/haze_job_profile.cpp"),
        str(root / "port/haze_job_trace.cpp"),
        str(coherency_wrapper),
        str(sdk / "libs/spurs/cellSpursQueue.c"),
    ], check=True, timeout=120)
    for mode, total in (("contracts", 9), ("wrap", 23), ("concurrent", 128),
                        ("pending", 1), ("pending_unknown", 1), ("pending_all_unknown", 1)):
        path = out / f"{mode}.csv"
        subprocess.run([str(exe), mode, str(path)], check=True, timeout=20)
        metadata = json.loads(Path(str(path) + ".meta.json").read_text())
        with path.open(newline="") as file:
            rows = [{key: int(value) for key, value in row.items()}
                    for row in csv.DictReader(file)]
        assert metadata["schema"] == 2
        assert metadata["recorded"] == total
        assert metadata["retained"] == len(rows) == min(total, 16)
        assert metadata["qpc_frequency"] > 0
        assert 0 < metadata["begin_qpc"] <= metadata["end_qpc"]
        assert metadata["publication_is_locked_notification"] is True
        assert metadata["service_and_callback_phases"] == "missing"
        assert metadata["flag_bits"] == {
            "no_publication": 1, "no_generation": 2,
            "ambiguous_generation": 4, "record_mismatch": 8, "bad_clock": 16,
        }
        assert [row["sequence"] for row in rows] == list(range(total-len(rows), total))
        for row in rows:
            assert row["qpc_frequency"] == metadata["qpc_frequency"]
            assert metadata["begin_qpc"] <= row["pop_qpc"] <= metadata["end_qpc"]
            if row["flags"] == 0:
                assert row["submit_generation"] and row["publication_ordinal"]
                assert 0 < row["submit_qpc"] <= row["publication_qpc"] <= row["pop_qpc"]
        if mode == "contracts":
            assert [row["flags"] for row in rows] == [0, 0, 2, 6, 3, 0, 0, 8, 0]
            assert metadata["epochs"] == 3 and metadata["invalidations"] == 2
        else:
            assert not any(row["flags"] for row in rows)
            # Distinct adjacent pops can share one QPC tick. Coverage joins
            # must exclude the overwrite boundary itself, rather than assume
            # a strictly later clock value for every retained row.
            assert metadata["max_overwritten_pop_qpc"] <= rows[0]["pop_qpc"]
        if mode.startswith("pending"):
            unknown = 0 if mode == "pending" else 1
            assert metadata["pending_unknown_publication_qpc"] == unknown
            assert metadata["pending_unknown_submit_qpc"] == unknown
            assert metadata["pending_at_stop"] == (1 if mode == "pending_all_unknown" else 2)
            if mode == "pending_all_unknown":
                assert metadata["min_pending_publication_qpc"] == metadata["min_pending_submit_qpc"] == 0
            else:
                assert metadata["min_pending_publication_qpc"] > 0
                assert metadata["min_pending_submit_qpc"] > 0
        else:
            assert metadata["pending_at_stop"] == 0
            assert not any(metadata[field] for field in (
                "min_pending_publication_qpc", "min_pending_submit_qpc",
                "pending_unknown_publication_qpc", "pending_unknown_submit_qpc"))
    print(f"PASS completion queue actual-source fixture and export metadata: {out}")


if __name__ == "__main__":
    main()
