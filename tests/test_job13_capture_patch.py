"""Validate the actual private image and reject changed extraction boundaries."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from patch_job13_capture import patch, PREFIX, REGISTRATION


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--registration', type=Path, required=True)
    args = parser.parse_args()
    source, registration = args.source.read_text(), args.registration.read_text()
    result = patch(source, registration)
    assert result.count('haze_job13_capture_begin(ctx);') == 1
    assert result.count('haze_job13_capture_end(ctx);') == 1
    begin, call, drain, end = (result.index(s) for s in (
        'haze_job13_capture_begin(ctx);', PREFIX+'00008AD8(ctx);',
        'spu_drain_call(ctx, 0xCE04);', 'haze_job13_capture_end(ctx);'))
    assert begin < call < drain < end
    assert result.count('void '+PREFIX) == source.count('void '+PREFIX)
    for changed, reg in ((source+'\n', registration),
                         (source, registration.replace(REGISTRATION, '')),
                         (source, registration+'\n'+REGISTRATION),
                         (result, registration)):
        try:
            patch(changed, reg)
        except ValueError:
            pass
        else:
            raise AssertionError('Changed source/registration or duplicate instrumentation accepted')
    print('PASS actual-image boundary, channel wrappers, completion ordering and four fail-closed guards')


if __name__ == '__main__':
    main()
