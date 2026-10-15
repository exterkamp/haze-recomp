"""Instrument one complete image-32 batch for private, bounded replay fixtures.

No generated game operations are distributed. This diagnostic copy must never
be used as a frame-rate acceptance build: selected batches write their fixture
after completion. The ordinary generated source is left untouched.
"""
import argparse
import hashlib
from pathlib import Path

IMAGE_DIGEST = '98222b50bfa6fa2015ee9b9e1e592eae11c166b05f57c46136ce69d5c925a694'
PREFIX = 'spu_0031_at_00ECEC00_spu_func_'
REGISTRATION = 'spu_begin_image(32); spu_0031_at_00ECEC00_spu_recomp_register();'


def validate(source, registration):
    if hashlib.sha256(source.encode()).hexdigest() != IMAGE_DIGEST:
        raise ValueError('Image-32 source changed; re-audit the complete batch boundary')
    if registration.count(REGISTRATION) != 1:
        raise ValueError('Image-32 registration changed')


def patch(source, registration):
    validate(source, registration)
    include = '#include "spu_helpers.h"'
    anchor = ('ctx->gpr[0] = spu_link(0xCE04); { int32_t _si = (int32_t)ctx->image_id; '
              'ctx->host_depth++; ' + PREFIX + '00008AD8(ctx); '
              'spu_drain_call(ctx, 0xCE04); ctx->host_depth--; spu_img_restore(ctx, _si); }')
    if source.count(include) != 1 or source.count(anchor) != 1:
        raise ValueError('Complete batch call boundary is not unique')
    instrumented = anchor.replace('ctx->host_depth++;',
                                  'haze_job13_capture_begin(ctx); ctx->host_depth++;', 1)
    instrumented = instrumented.replace('spu_img_restore(ctx, _si); }',
                                        'spu_img_restore(ctx, _si); haze_job13_capture_end(ctx); }')
    return source.replace(include, include + '\n#include "haze_job13_capture.h"\n'
        '#define spu_wrch haze_job13_capture_wrch\n'
        '#define spu_rdch haze_job13_capture_rdch\n'
        '#define spu_rchcnt haze_job13_capture_rchcnt', 1).replace(anchor, instrumented, 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--registration', type=Path, required=True)
    args = parser.parse_args()
    result = patch(args.source.read_text(), args.registration.read_text())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('x') as stream:
        stream.write(result)
