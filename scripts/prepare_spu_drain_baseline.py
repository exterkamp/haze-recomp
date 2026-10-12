"""Extract an unchanged historical SPU launch hook for the local cost benchmark.

No game assets are read. Compile the output alongside tests/benchmark_spu_drain.c
with SDK include and runtime/spu paths; see the performance report in docs.
"""
import argparse
from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=root / 'external/ps3recomp')
    parser.add_argument('--revision', default='b582819')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = subprocess.run(
        ['git', '-C', str(args.sdk), 'show',
         f'{args.revision}:runtime/spu/spu_drain.c'],
        check=True, capture_output=True, text=True,
    ).stdout

    def function(signature):
        begin = source.index(signature)
        end = source.index('\n}\n', begin) + 3
        return source[begin:end]

    prefix = '''#include "spu_context.h"
#include <stdio.h>
#include <stdlib.h>
extern uint32_t g_pm_flow_buf[8192];
extern volatile unsigned g_pm_flow_n;
extern void* volatile g_pm_flow_ctx;
void spu_halt(spu_context*);
static _Thread_local uint32_t t_recent[32],t_recent_n;
'''
    body = function('unsigned spu_recent_pcs(').replace(
        'spu_recent_pcs', 'baseline_spu_recent_pcs')
    body += '\n' + function('void spu_task_launch_check(').replace(
        'spu_task_launch_check', 'baseline_spu_task_launch_check')
    if 'spu_task_launch_check_diagnostic' in body:
        raise ValueError('Baseline must predate diagnostic outlining')
    args.output.write_text(prefix + body, encoding='ascii')


if __name__ == '__main__':
    main()
