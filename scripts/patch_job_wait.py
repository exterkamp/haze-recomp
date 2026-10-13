"""Guard BLUS30094's completion-loop delay without changing its work checks."""
import argparse
import hashlib
from pathlib import Path

DIGESTS = {
    '00682538': '88f6044616bbee9074fc5e8e031bf469036bda62a22de0158105d0b9a654fd1a',
    '0068EFC0': '15262821281dbc06398a4c4bc8a34ee296fe272d2d73b336e1a7cd9e322817ef',
    '0071EFE8': 'daf5d491622c7593d09754d32873daa3ff25dcf9dbce56cb3bcdfb4d9ff56897',
}
CALL = 'ctx->lr = 0x00682574; func_0068EFC0(ctx); DRAIN_TRAMPOLINE(ctx);'

def patch(source):
    bodies = {}
    for address, digest in DIGESTS.items():
        entry = f'void func_{address}(ppu_context* ctx) {{'
        begin = source.index(entry)
        end = source.index('\nvoid ', begin + len(entry))
        body = source[begin:end]
        if hashlib.sha256(body.encode()).hexdigest() != digest:
            raise ValueError(f'Haze job-wait function {address} changed; re-audit the boundary')
        bodies[address] = body
    body = bodies['00682538']
    changed = body.replace('{\n', '{\n        extern void haze_job_wait_call(ppu_context*);\n', 1)
    assert body.count(CALL) == 1
    changed = changed.replace(CALL, CALL.replace('func_0068EFC0', 'haze_job_wait_call'), 1)
    source = source.replace(body, changed, 1)
    body = bodies['0071EFE8']
    changed = body.replace('{\n', '{\n        extern bool haze_job_wait_usleep(ppu_context*);\n', 1)
    assert body.count('        lv2_syscall(ctx);') == 1
    changed = changed.replace('        lv2_syscall(ctx);',
        '        if (!haze_job_wait_usleep(ctx)) lv2_syscall(ctx);', 1)
    return source.replace(body, changed, 1)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = patch(args.source.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)
