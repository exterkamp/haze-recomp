"""Check profiling flags on the actual Ninja compile rules before compilation.

Source properties in the SDK directory do not apply to PPU files compiled by
the game target. Inspect both targets so this silent instrumentation loss is
reported immediately, and ordinary builds retain no profiling definitions.
"""
import argparse
from pathlib import Path
import re


def check(build, enabled):
    ninja = (Path(build) / 'build.ninja').read_text()
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
    for source in ('main.cpp', 'haze_job_trace.cpp', 'ppu_recomp_003.cpp'):
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
    args = parser.parse_args()
    check(args.build, bool(args.enabled))
