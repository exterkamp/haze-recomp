"""Replace the audited image32 division prefix in a private generated copy."""
from patch_job13_capture import validate, PREFIX


def patch(source,registration):
    validate(source,registration)
    include='#include "spu_helpers.h"'
    start=source.index('void '+PREFIX+'0000EB10(spu_context* ctx) {')
    end=source.index('\n}\n',start)
    body=source[start:end]
    anchor='        { ctx->pc = 0xEB38; g_spu_trampoline_fn = '+PREFIX+'0000EB38; return; }'
    if body.count(anchor)!=1:raise ValueError('Division entry changed')
    replacement=('        if (haze_job13_native_divide_prefix(ctx)) { ctx->pc = 0xEB24; '
                 'g_spu_trampoline_fn = '+PREFIX+'0000EB24; return; }\n'+anchor)
    return (source[:start]+body.replace(anchor,replacement)+source[end:]).replace(
        include,include+'\n#include "haze_job13_native_divide.h"',1)
