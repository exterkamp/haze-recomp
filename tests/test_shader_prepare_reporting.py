#!/usr/bin/env python3
"""Compile the actual preparer reporting helpers and check their output."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "port" / "haze_shader_prepare.cpp"
BEGIN = "/* BEGIN HAZE SHADER PREP REPORT */"
END = "/* END HAZE SHADER PREP REPORT */"


HARNESS = r'''#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <string>
using std::uint32_t;
using std::uint64_t;
'''


def extract_helpers(source: pathlib.Path) -> str:
    text = source.read_text(encoding="utf-8")
    assert text.count(BEGIN) == 1 and text.count(END) == 1
    start = text.index(BEGIN) + len(BEGIN)
    finish = text.index(END, start)
    return text[start:finish]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", required=True, help="clang-cl executable")
    parser.add_argument("--source", default=str(SOURCE), help="preparer source to validate")
    parser.add_argument("--output-dir", help="temporary build directory")
    args = parser.parse_args()
    source_path = pathlib.Path(args.source)
    source_text = source_path.read_text(encoding="utf-8")

    # These assertions protect the report's integration contract without
    # pretending that source-string checks exercise the generator or cache.
    assert "print_prepare_result(stdout, report, failed || sources.empty() ? \"failed\" : \"finished\")" in source_text
    assert "report.bundle_save = bundle.save(!failed && !sources.empty())" in source_text
    assert "if (sources.size() % 64 == 0)" in source_text
    assert "per_file_cache_write_failures" in source_text
    assert "report.pixels = pixels;\n        report.width = width;\n        report.height = height;" in source_text

    if args.output_dir:
        temp = pathlib.Path(args.output_dir)
        temp.mkdir(parents=True, exist_ok=True)
        run_fixture(temp, args.compiler, source_path)
    else:
        with tempfile.TemporaryDirectory() as temporary:
            run_fixture(pathlib.Path(temporary), args.compiler, source_path)
    print("shader prepare reporting: helper states and structured output passed")
    return 0


def run_fixture(temp: pathlib.Path, compiler: str, source_path: pathlib.Path) -> None:
        cpp = temp / "report_fixture.cpp"
        exe = temp / "report_fixture.exe"
        code = HARNESS + extract_helpers(source_path) + r'''
int main() {
    assert(choose_bundle_save_state(true, true, true, true, 0, true) == BundleSaveState::saved);
    assert(choose_bundle_save_state(true, true, true, true, 0, false) == BundleSaveState::failed);
    assert(choose_bundle_save_state(true, true, true, false, 4, false) == BundleSaveState::reused);
    assert(choose_bundle_save_state(true, true, true, false, 0, true) == BundleSaveState::skipped);
    assert(choose_bundle_save_state(false, true, true, true, 0, true) == BundleSaveState::skipped);
    assert(choose_bundle_save_state(true, false, true, true, 0, true) == BundleSaveState::skipped);
    assert(choose_bundle_save_state(true, true, false, true, 0, true) == BundleSaveState::skipped);

    PrepareReport report;
    report.pixels = true;
    report.width = 2560;
    report.height = 1440;
    report.vertex_archive_entries = 10;
    report.vertex_programs = 8;
    report.vertex_recipes = 7;
    report.source_variants = 11;
    report.compiled = 3;
    report.cached = 8;
    report.errors = 1;
    report.per_file_cache_write_attempts = 4;
    report.per_file_cache_write_failures = 1;
    report.skipped_large = 2;
    report.unsupported_decode = 1;
    report.unsupported_sampler_records = 5;
    report.bundle_save = BundleSaveState::failed;
    report.bundle_hits = 6;
    FILE* output = std::tmpfile();
    assert(output);
    print_prepare_progress(output, 'P', 64, 3, 8, 1);
    print_prepare_result(output, report, "finished");
    std::rewind(output);
    char buffer[4096]{};
    const size_t n = std::fread(buffer, 1, sizeof(buffer) - 1, output);
    std::fclose(output);
    const std::string text(buffer, n);
    assert(text.find("[shader-prepare-progress] schema=1 phase=shader_sources stage=P processed=64") != std::string::npos);
    assert(text.find("worker_result=finished counts_known=1 pixels=1 width=2560 height=1440") != std::string::npos);
    assert(text.find("compiled=3 cached=8 errors=1 per_file_cache_write_attempts=4 per_file_cache_write_failures=1") != std::string::npos);
    assert(text.find("skipped_large=2 unsupported_decode=1") != std::string::npos);
    assert(text.find("unsupported_sampler_records=5 bundle_save=failed") != std::string::npos);
    assert(text.find("coverage=archive_supported_sources_only pso_coverage=not_prepared") != std::string::npos);

    report.bundle_save = BundleSaveState::saved;
    report.bundle_entries = 17;
    report.bundle_bytes = 12345;
    report.counts_known = false;
    report.pixels = true;
    report.width = 2560;
    report.height = 1440;
    output = std::tmpfile();
    assert(output);
    print_prepare_result(output, report, "failed");
    std::rewind(output);
    std::memset(buffer, 0, sizeof(buffer));
    const size_t m = std::fread(buffer, 1, sizeof(buffer) - 1, output);
    std::fclose(output);
    const std::string unknown(buffer, m);
    assert(unknown.find("worker_result=failed counts_known=0 pixels=1 width=2560 height=1440") != std::string::npos);
    assert(unknown.find("bundle_save=saved bundle_entries=17 bundle_bytes=12345") != std::string::npos);
    return 0;
}
'''
        cpp.write_text(code, encoding="utf-8")
        subprocess.run([
            compiler, "/nologo", "/W0", "/O2", "/std:c++17", "/EHsc",
            "/UNDEBUG", str(cpp), f"/Fe{exe}", f"/Fo{temp / 'fixture.obj'}",
        ], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    raise SystemExit(main())
