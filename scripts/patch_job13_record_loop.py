"""Fuse the complete audited image32 record loop in a private generated copy.

The 59-block operation has one external tail continuation, 9BC4. Keep its five
subroutine calls and all state writes intact, but let the host compiler see the
whole loop instead of returning to the SPU dispatcher between basic blocks.
This is a control-flow prototype, not a typed native effects implementation.
Generated game operations are never distributed by this script.
"""
import re
from patch_job13_capture import validate, PREFIX

ENTRY = 0x9A00
EXIT = 0x9BC4
FUNCTION = re.compile(r'void ' + PREFIX + r'([0-9A-F]{8})\(spu_context\* ctx\) \{(.*?)\n\}', re.S)
TAIL = re.compile(r'\{ ctx->pc = 0x([0-9A-F]+); g_spu_trampoline_fn = ' + PREFIX + r'([0-9A-F]{8}); return; \}')


def patch(source, registration, native_divide=False):
    validate(source, registration)
    if native_divide:
        from patch_job13_native_divide import patch as divide
        source = divide(source, registration)
    functions = {int(m[1], 16): m for m in FUNCTION.finditer(source)}
    pending, selected = [ENTRY], set()
    while pending:
        address = pending.pop()
        if address in selected or address == EXIT:
            continue
        if address not in functions:
            raise ValueError('Unresolved record-loop target')
        selected.add(address)
        body = functions[address][2]
        if 'spu_indirect_branch' in body or 'spu_wrch' in body or 'spu_rdch' in body:
            raise ValueError('Unexpected indirect transfer or channel operation')
        tails = list(TAIL.finditer(body))
        if not tails or body.count('g_spu_trampoline_fn') != len(tails):
            raise ValueError('Unparsed record-loop transfer')
        for tail in tails:
            if int(tail[1], 16) != int(tail[2], 16):
                raise ValueError('PC and direct target disagree')
            pending.append(int(tail[1], 16))
    if len(selected) != 59:
        raise ValueError('Record-loop closure changed; re-audit')
    lines = ['static void haze_job13_fused_records(spu_context* ctx) {',
             '    goto record_9A00;']
    for address in sorted(selected):
        def transfer(m):
            target = int(m[1], 16)
            if target == EXIT:
                return m[0]
            if target not in selected:
                raise ValueError('Incomplete record loop')
            return '{ ctx->pc = 0x%X; goto record_%04X; }' % (target, target)
        body = TAIL.sub(transfer, functions[address][2])
        lines += ['record_%04X: {' % address, body, '}']
    lines += ['}', '']
    entry = functions[ENTRY]
    source = source[:entry.start(2)] + '\n    haze_job13_fused_records(ctx);' + source[entry.end(2):]
    include = '#include "spu_helpers.h"'
    if source.count(include) != 1:
        raise ValueError('Unexpected generated includes')
    return source.replace(include, include + '\n' + '\n'.join(lines), 1)
