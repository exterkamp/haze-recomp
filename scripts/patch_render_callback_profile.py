"""Profile Haze's two render callback branches without changing guest calls.

Nested call scopes attach only to an already active producer TLS scope. With
no active parent they do not initialize profiling or read clocks. The actual
CTR target is captured after the guest loads its descriptor/TOC, and the scope
includes the original indirect call and trampoline drain.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import re

from patch_producer_profile import _indirect_scopes, function_body

FUNCTION = "void func_004D2828(ppu_context* ctx) {"
ORIGINAL_SHA256 = "bdf2ebfc800f352659233feb093d1375b24da3b5003d69c4972e77339b9f8f42"
INCLUDE = '#include "haze_producer_profile.h"\n'
# EBOOT.ELF contains bctrl (4e800421) at 004D2884 and 004D28C0.
# These are architectural return addresses, not the caller's inherited ctx->lr.
# First branch: selector r3 == 0; second branch: selector r3 != 0.
INDIRECT_RETURNS = ("004D2888", "004D28C4")


def _digest(body):
    return hashlib.sha256((body+"\n").encode("utf-8")).hexdigest()


def patch(source: str) -> str:
    body = function_body(source, FUNCTION)
    if _digest(body) != ORIGINAL_SHA256:
        raise ValueError("Haze render callback 004D2828 changed; audit before profiling")
    if INCLUDE in source:
        raise ValueError("unexpected producer-profile include in callback source; audit composition")
    changed = _indirect_scopes(body, INDIRECT_RETURNS, "")
    return INCLUDE+source.replace(body, changed, 1)


def strip_profile(source: str) -> str:
    """Strip only this transform, retaining title hooks and all guest code."""
    if source.count(INCLUDE) != 1:
        raise ValueError("expected one callback producer-profile include")
    body = function_body(source, FUNCTION)
    wrapper = re.compile(
        r"(?m)^(?P<indent>[ \t]*)\{\n"
        r"(?P=indent)    haze_producer_profile::call_scope haze_producer_indirect_(?P<lr>004D2888|004D28C4)\("
        r"0x(?P=lr)u, static_cast<std::uint32_t>\(ctx->ctr\)\);\n"
        r"(?P=indent)    ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
        r"(?P=indent)\}"
    )
    changed, count = wrapper.subn(
        lambda match: match.group("indent")+"ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);", body)
    if count != 2 or _digest(changed) != ORIGINAL_SHA256:
        raise ValueError("callback instrumentation or guest body changed")
    return source.replace(body, changed, 1).replace(INCLUDE, "", 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = patch(args.source.read_text(encoding="cp1252"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result, encoding="cp1252")
    print("Render callback profile: 2 indirect CTR targets, returns 004D2888/004D28C4; inactive TLS no-op")


if __name__ == "__main__":
    main()
