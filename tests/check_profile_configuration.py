"""Check profiling flags on the actual Ninja compile rules before compilation.

Source properties in the SDK directory do not apply to PPU files compiled by
the game target. Inspect both targets so this silent instrumentation loss is
reported immediately, and ordinary builds retain no profiling definitions.
"""
import argparse
from pathlib import Path
import re


def check(build, enabled, inline_reads=None):
    ninja = (Path(build) / 'build.ninja').read_text()
    if inline_reads is not None:
        generated = 0
        for block in ninja.split('\n\n'):
            if not block.startswith('build ') or '__HazeRecomp_' not in block.splitlines()[0]:
                continue
            first = block.splitlines()[0]
            is_generated = bool(re.search(r'[\\/]ppu_(?:recomp_\d+|hle_nids)\.cpp\.obj:', first))
            if is_generated:
                generated += 1
            if ('haze_ppu_inline_reads.h' in block) != (is_generated and inline_reads):
                raise ValueError(f'Generated-only inline-read compile gate mismatch: {first}')
        if generated != 8:
            raise ValueError(f'Expected 8 lifted Haze compile rules, got {generated}')
        print(f'Actual compile rules: inline reads={int(inline_reads)} on generated sources only')
    sources = {
        'ppu_loader.cpp': 'HazeRecomp',
        'ppu_hle.cpp': 'HazeRecomp',
        'spu_workload.c': 'ps3recomp_runtime',
        'cellSpursQueue.c': 'ps3recomp_runtime',
        'rsx_live_draw.c': 'ps3recomp_runtime',
        'cellGcmSys.c': 'ps3recomp_runtime',
    }
    for source, target in sources.items():
        blocks = [block for block in ninja.split('\n\n')
                  if block.startswith('build ')
                  and re.search(r'[\\/]' + re.escape(source) + r'\.obj:', block.splitlines()[0])
                  and f'__{target}_' in block.splitlines()[0]]
        if len(blocks) != 1:
            raise ValueError(f'Expected one {target} compile rule for {source}, found {len(blocks)}')
        defines = next((line for line in blocks[0].splitlines()
                        if line.startswith('  DEFINES = ')), '')
        profiled = bool(re.search(r'(?:^|\s)(?:-D|/D)YZ_PERF_PROFILE(?:=\S+)?(?:\s|$)', defines))
        if profiled != enabled:
            raise ValueError(f'{target}/{source}: profiling flag is {profiled}, expected {enabled}')
    print(f'Actual compile rules: profiling={int(enabled)} verified on all {len(sources)} PPU/SPU/RSX sources')
    for source in ('main.cpp', 'haze_job_profile.cpp', 'ppu_recomp_003.cpp'):
        blocks = [block for block in ninja.split('\n\n') if block.startswith('build ')
                  and re.search(r'[\\/]' + re.escape(source) + r'\.obj:', block.splitlines()[0])
                  and '__HazeRecomp_' in block.splitlines()[0]]
        if len(blocks) != 1:
            raise ValueError(f'Expected one completion-profile rule for {source}')
        defines = next((line for line in blocks[0].splitlines() if line.startswith('  DEFINES = ')), '')
        if bool(re.search(r'(?:-D|/D)HAZE_JOB_PROFILE(?:\s|$)', defines)) != enabled:
            raise ValueError(f'{source}: completion profiler compile gate mismatch')
    for source in ('main.cpp', 'haze_job_trace.cpp', 'haze_job0_capture.cpp',
                   r'haze_job0_capture\spu_recomp.c', 'ppu_recomp_003.cpp'):
        blocks = [block for block in ninja.split('\n\n') if block.startswith('build ')
                  and re.search(r'[\\/]' + re.escape(source) + r'\.obj:', block.splitlines()[0])
                  and '__HazeRecomp_' in block.splitlines()[0]]
        if len(blocks) != 1:
            raise ValueError(f'Expected one job-trace rule for {source}')
        defines = next((line for line in blocks[0].splitlines() if line.startswith('  DEFINES = ')), '')
        if bool(re.search(r'(?:-D|/D)HAZE_JOB_TRACE(?:\s|$)', defines)) != enabled:
            raise ValueError(f'{source}: job trace compile gate mismatch')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--enabled', type=int, choices=(0, 1), required=True)
    parser.add_argument('--inline-reads', type=int, choices=(0, 1))
    args = parser.parse_args()
    check(args.build, bool(args.enabled), None if args.inline_reads is None else bool(args.inline_reads))
