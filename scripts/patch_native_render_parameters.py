"""Replace one audited rendering parameter traversal with native loop state."""
import argparse
import hashlib
from pathlib import Path
from patch_producer_profile import function_body

DIGESTS = {
    '00673750': '51ae9b4fce79163e1dfe6909ba83484043a52042c71ab9551002d5dcb96f6e98',
    '006740E0': 'e0d4b24718c48f15fdd98cdd8920ab2fef6ad9fee9412a26b3f8969485fa93ed',
}
# The producer transform now scopes this caller's five direct calls. Its
# stripped body is unchanged; accept only this audited instrumentation variant.
PROFILED_CALLER_DIGEST = '1b9f7fee6ee1c1525f3b0b9ae04e5c1483036485b5f200ca54eaaf4ea84239f2'
CALL = 'ctx->lr = 0x006741AC; func_00673750(ctx); DRAIN_TRAMPOLINE(ctx);'
REPLACEMENT = ('ctx->lr = 0x006741AC; if (!haze_native_render_parameters(ctx)) '
               'func_00673750(ctx); DRAIN_TRAMPOLINE(ctx);')


def audited_body(source, address):
    body = function_body(source, f'void func_{address}(ppu_context* ctx) {{')
    accepted = {DIGESTS[address]}
    if address == '006740E0':
        accepted.add(PROFILED_CALLER_DIGEST)
    if hashlib.sha256(body.encode()).hexdigest() not in accepted:
        raise ValueError(f'{address} changed; re-audit native rendering boundary')
    return body


def patch(source):
    audited_body(source, '00673750')
    caller = audited_body(source, '006740E0')
    if caller.count(CALL) != 1:
        raise ValueError('parameter traversal callsite changed')
    return source.replace(caller, '#include "haze_native_render_parameters.h"\n' +
                          caller.replace(CALL, REPLACEMENT), 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--oracle', action='store_true')
    args = parser.parse_args()
    source = args.source.read_text(encoding='cp1252')
    result = (audited_body(source, '00673750').replace('func_00673750(',
              'reference_render_parameters(', 1) if args.oracle else patch(source))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result, encoding='cp1252')
