"""Compile production coherence-lock profiling and run bounded threaded tests."""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--sdk", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    sdk = args.sdk.resolve()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix="coherency-lock-", dir=args.output_dir.resolve()))
    wrapper = out / "coherency_fixture.c"
    wrapper.write_text('#include <windows.h>\nBOOL WINAPI coherency_profile_fixture_qpc(LARGE_INTEGER*);\n'
                       '#define QueryPerformanceCounter coherency_profile_fixture_qpc\n'
                       f'#include "{(sdk / "runtime/spu/spu_coherency.c").as_posix()}"\n')
    exe = out / "test_coherency_lock_profile.exe"
    subprocess.run([args.compiler, "/nologo", "/W0", "/O2", "/std:c11",
                    "/D_CRT_SECURE_NO_WARNINGS", "/DSPU_COH_LOCK_PROFILE_CAPACITY=16",
                    "/clang:-msse4.1", f"/I{sdk / 'include'}", f"/I{sdk / 'runtime/spu'}",
                    f"/Fe{exe}", f"/Fo{out}/", str(root / "tests/test_coherency_lock_profile.c"),
                    str(wrapper)], check=True, timeout=120)
    for mode, completed, recorded, inflight in (("disabled", 0, 0, 0), ("contracts", 8, 8, 0),
                                              ("wrap", 23, 23, 0), ("freeze", 0, 0, 1),
                                              ("clocks", 4, 0, 0)):
        path = out / f"{mode}.csv"
        subprocess.run([str(exe), mode, str(path)], check=True, timeout=30)
        meta = json.loads(Path(str(path)+".meta.json").read_text())
        with path.open(newline="") as file:
            rows = [{key: int(value) for key, value in row.items()} for row in csv.DictReader(file)]
        assert meta["schema"] == 1 and meta["capacity"] == 16 and meta["threshold_us"] == 1000
        assert meta["completed_contentions"] == completed and meta["recorded"] == recorded
        assert meta["inflight_at_stop"] == inflight and meta["admitted_contentions"] == completed+inflight
        assert len(rows) == meta["retained"] == min(16, recorded)
        assert meta["overwritten"] == recorded-len(rows)
        assert [r["sequence"] for r in rows] == list(range(recorded-len(rows), recorded))
        assert meta["invalid_clock_discarded"] == (3 if mode == "clocks" else 0)
        assert meta["below_threshold"] == (1 if mode == "clocks" else 0)
        assert meta["started"] == meta["capture_clock_valid"] == (mode != "disabled")
        for row in rows:
            assert row["qpc_frequency"] == meta["qpc_frequency"]
            assert meta["begin_qpc"] <= row["begin_qpc"] <= row["end_qpc"] <= meta["end_qpc"]
            assert row["end_qpc"]-row["begin_qpc"] >= meta["threshold_ticks"]
            assert row["host_tid"] and row["caller"] >= meta["image_base"]
    print(f"PASS production coherency profile and export metadata: {out}")


if __name__ == "__main__":
    main()
