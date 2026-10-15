"""Compile and exercise the actual bounded job13 capture source."""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=Path(os.environ.get("CLANG_CL", "clang-cl")))
    parser.add_argument("--sdk", type=Path,
                        default=Path(__file__).resolve().parents[1].parent /
                        "native-render-submission" / "external" / "ps3recomp")
    parser.add_argument("--source", type=Path,
                        default=Path(__file__).resolve().parents[1] / "port" / "haze_job13_capture.cpp")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    compiler = str(args.compiler)
    if shutil.which(compiler) is None and not Path(compiler).is_file():
        raise SystemExit(f"compiler not found: {compiler}")
    sdk = args.sdk.resolve()
    sdk_spu = sdk / "runtime" / "spu"
    sdk_include = sdk / "include"
    if not (sdk_spu / "spu_context.h").is_file():
        raise SystemExit(f"SDK context header not found under {sdk_spu}")

    with tempfile.TemporaryDirectory(prefix="haze-job13-") as temp_name:
        temp = Path(temp_name)
        exe = temp / "test_job13_capture.exe"
        c_caller = repo / "tests" / "test_job13_capture_caller.c"
        c_obj = temp / "test_job13_capture_caller.obj"
        c_cmd = [compiler, "/nologo", "/TC", "/W4", "/WX",
                 "/I", str(repo / "port"), "/I", str(sdk_spu), "/I", str(sdk_include),
                 "/c", str(c_caller), "/Fo" + str(c_obj)]
        subprocess.run(c_cmd, cwd=repo, check=True)
        cmd = [compiler, "/nologo", "/std:c++17", "/EHsc", "/W4", "/WX",
               "/I", str(repo / "port"), "/I", str(sdk_spu), "/I", str(sdk_include),
               str(c_obj),
               str(args.source.resolve()), str(repo / "tests" / "test_job13_capture.cpp"),
               "/Fe:" + str(exe), "/Fo" + str(temp) + os.sep]
        subprocess.run(cmd, cwd=repo, check=True)
        for mode in ("off", "valid", "atomic", "invalid", "payload", "nested", "limit"):
            out_dir = temp / mode
            subprocess.run([str(exe), mode, str(out_dir)], cwd=repo, check=True)
    print("PASS compiled actual job13 capture source: disabled, valid DMA/atomics, invalid calls, event/payload bounds")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
