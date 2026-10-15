"""Add bounded RAII timing scopes to Haze's main producer update.

Accepts the pinned generated function either before guest-pacer patching or
after that deterministic patch. Direct call sites are wrapped as C++ blocks so
their scopes end immediately after DRAIN_TRAMPOLINE returns, including across
branches and loops. High-frequency timer helpers are explicit exclusions.
"""

from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path


FUNCTION = "void func_00680FA0(ppu_context* ctx) {"
DISPATCH_FUNCTION = "void func_006869F0(ppu_context* ctx) {"
DISPATCH_SHA256 = "1c64f130f99ef3f6817bbe2cd11e31c7616b89263bcc7e2e02a6ddfdc31499cd"
# Split the producer children implicated in hitches and its dominant steady
# rendering phase without changing guest operations or assuming that elapsed
# wall time is active CPU work. Unlisted indirect calls in the simulation
# callback remain in its unprofiled remainder.
PHASE_FUNCTIONS = (
    ("00686810", "34613d959c4c24225dabc03a9e194b5b4d9fd755bdc63ac495c4d1c00fd8e71f", 4),
    ("005FBB70", "626f4bc455092a532750dafeb2a196504f90785c496584f1c316a5fee945bc90", 58),
    # Dominant steady producer phase in the prepared-shader opening capture.
    ("00684ED8", "56c5420e2f8c0f9bf519790f3c65a18ce3e06dbd46982f591cc5d262551abb63", 12),
    # 006DEC78 tail-dispatches here inside the profiled caller's lifetime.
    ("006DE730", "c52f06ee56d847115caaeb6897b207dd38d92e1bf3d0ab979f787e27c107fd61", 42),
    # Accounts for most of that stage in the opening-deck capture.
    ("006AC9A8", "e83000f0f51401a94edfdd7dbb61ce72712a281a783b9dd727237b35f8be3e7b", 21),
    # Dominant record-handler path, with its nested object callback resolved.
    ("007EB5A8", "62e42c0f92175d27e237fe5eed51d57365ad5afa0c18efae6ff6736e1ed5162c", 23),
    ("006A8AE8", "f76929306e7433d6257061c57487f87f3a3f9809ccbd1d9dc1ac10d72b295184", 0),
    # Consolidated state/material update candidate; elapsed time is not proof
    # of draw submission. Cover its complete direct-call frontier together.
    ("00675800", "d1f6ac6d7ef22f17624713bcca17947f7945c4309ab5108cfd73f7bdd42b693a", 23),
    ("006740E0", "f60f944953d7c0e9bd5fb065ec8c7552164686f98e632fdd7ad9646df8e77d1f", 5),
)
# Architectural return addresses verified against bctrl instructions in the ELF.
MAIN_INDIRECT_RETURNS = ("00681184", "006811DC")
DISPATCH_INDIRECT_RETURNS = ("00686AC8", "00686BE8", "00686C5C", "00686CFC")
PHASE_INDIRECT_RETURNS = {
    "00684ED8": ("00685008", "00685058", "0068511C"),
    "006DE730": ("006DE8CC", "006DEBAC"),
    "006AC9A8": ("006ACBB4", "006ACC18"),
    "006A8AE8": ("006A8B68",),
    "00675800": ("00675D64",),
}
# Rendering-record callback probes. These indirect calls load the guest
# target into CTR from operation descriptors, so the call scope records the
# resolved target rather than an inferred operation type. The path scope at
# 007E4974 distinguishes 00676938 -> 006743F8 from the sibling
# 007E41E8 -> 00675800 callback in the scope tree.
RENDER_CALLBACK_FUNCTIONS = (
    ("006743F8", "3f02d2ce24ad741098a90a5dd43868c91b46f8ce72c96b8735f77cd3c873f7f7", ("00674688",)),
    ("00673AF0", "86ee53bf0baca0e8577769e2f01a9578ec74f01296698e013845edcc4f92aa88", ("00673B7C",)),
    ("0072BAD0", "d719dd7183a701228247ddf14b2fca1fb54cfb6ea1cd4bd8d8c42d8748a8e913",
     ("0072C0B4", "0072C18C", "0072C248", "0072C2C4", "0072C2F8", "0072C34C")),
    # All descriptor bctrl sites, preserving the separate switch-tail fallback.
    ("0072C368", "16c315e0e8c3e43767b330e6cde60871b1a2ab984b0e2fa7a6292d4b9f4733d1",
     ("0072C4A0", "0072C8DC", "0072C910", "0072CBD8", "0072CC38", "0072CCD8", "0072CD7C",
      "0072CE48", "0072CF2C", "0072D014", "0072D12C", "0072D230", "0072D2FC")),
)
RENDER_PATH_FUNCTION = "007E41E8"
RENDER_PATH_SHA256 = "7eff15979bf2cb9e5b8f13fb76b8b694b1bf9d0c62d10e101ce5c3445005ffe4"
RENDER_PATH_CALLS = (
    ("007E4520", "00675800"), ("007E4974", "00676938"),
    ("007E4980", "00675800"), ("007E4B1C", "00675800"),
)
# The build applies patch_vp_upload before profiling. Accept its exact pinned
# 0072C368 body too; no normalization may hide changes in that existing hook.
RENDER_CALLBACK_VARIANT_SHA256 = {
    "0072C368": "3d330aa8747b5f5680e37e1e6652a9101c4a9fd259b73d226698cf3037eb2240",
}
ORIGINAL_SHA256 = "0f1c08565667f88ffe25988059165385f35cdbee26b5c6880220c25d0685af52"
GUEST_PACER_SHA256 = "56f434e9a85787e2471f2569106c05ee280af91f84d1b37efeecf10ffbf0ae1e"
INCLUDE = '#include "haze_producer_profile.h"\n'
EXCLUDED_CALLEES = {"00680D18", "0068EC78"}
CALL_RE = re.compile(
    r"(?m)^(?P<indent>[ \t]*)ctx->lr = 0x(?P<lr>[0-9A-Fa-f]+); "
    r"func_(?P<callee>[0-9A-Fa-f]+)\(ctx\); DRAIN_TRAMPOLINE\(ctx\);$"
)
FRAME_LINE = (
    "        haze_producer_profile::frame_scope haze_producer_frame("
    "ctx->thread_id, ctx->gpr[2], 0x00680FA0u, 0);\n"
)


def function_body(source: str, function: str = FUNCTION) -> str:
    start = source.find(function)
    if start < 0 or source.find(function, start + 1) >= 0:
        raise ValueError(f"expected exactly one {function} definition")
    brace = source.index("{", start)
    depth = 0
    for end in range(brace, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start : end + 1]
    raise ValueError("unterminated func_00680FA0")


def call_sites(body: str) -> list[re.Match[str]]:
    return list(CALL_RE.finditer(body))


def _digest(body: str) -> str:
    return hashlib.sha256(body.encode("utf-8")).hexdigest()


def _call_scope(index: int, lr: str, callee: str, indent: str) -> str:
    inner = indent + "    "
    return (
        indent + "{\n"
        + inner + f"haze_producer_profile::call_scope haze_producer_call_{index}("
          f"haze_producer_frame, 0x{lr}u, 0x{callee}u);\n"
        + inner + f"ctx->lr = 0x{lr}; func_{callee}(ctx); DRAIN_TRAMPOLINE(ctx);\n"
        + indent + "}"
    )


def _indirect_scopes(body: str, returns: tuple[str, ...], parent: str) -> str:
    pattern = re.compile(r"(?m)^(?P<indent>[ \t]*)ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);$")
    matches = list(pattern.finditer(body))
    if len(matches) != len(returns):
        raise ValueError("indirect call inventory changed; re-audit branch return addresses")
    for match, lr in reversed(list(zip(matches, returns))):
        indent = match.group("indent")
        inner = indent + "    "
        block = (indent + "{\n" + inner +
                 f"haze_producer_profile::call_scope haze_producer_indirect_{lr}("
                 f"{parent}0x{lr}u, static_cast<std::uint32_t>(ctx->ctr));\n" +
                 inner + "ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n" + indent + "}")
        body = body[:match.start()] + block + body[match.end():]
    return body


def _dispatch_scopes(source: str) -> str:
    body = function_body(source, DISPATCH_FUNCTION)
    if _digest(body + "\n") != DISPATCH_SHA256:
        raise ValueError("Haze callback dispatcher changed; audit before profiling")
    matches = call_sites(body)
    if len(matches) != 14:
        raise ValueError("callback dispatcher direct-call inventory changed")
    changed = body
    for match in reversed(matches):
        indent = match.group("indent")
        inner = indent + "    "
        lr, callee = match.group("lr"), match.group("callee")
        block = (indent + "{\n" + inner +
                 f"haze_producer_profile::call_scope haze_producer_nested_{lr}(0x{lr}u, 0x{callee}u);\n" +
                 inner + f"ctx->lr = 0x{lr}; func_{callee}(ctx); DRAIN_TRAMPOLINE(ctx);\n" + indent + "}")
        changed = changed[:match.start()] + block + changed[match.end():]
    changed = _indirect_scopes(changed, DISPATCH_INDIRECT_RETURNS, "")
    return source.replace(body, changed, 1)


def _phase_scopes(source: str) -> str:
    for address, digest, count in PHASE_FUNCTIONS:
        body = function_body(source, f"void func_{address}(ppu_context* ctx) {{")
        if _digest(body + "\n") != digest:
            raise ValueError(f"Haze producer phase {address} changed; audit before profiling")
        matches = call_sites(body)
        if len(matches) != count:
            raise ValueError(f"producer phase {address} direct-call inventory changed")
        changed = body
        for match in reversed(matches):
            indent = match.group("indent")
            inner = indent + "    "
            lr, callee = match.group("lr"), match.group("callee")
            block = (indent + "{\n" + inner +
                     f"haze_producer_profile::call_scope haze_producer_nested_{lr}(0x{lr}u, 0x{callee}u);\n" +
                     inner + f"ctx->lr = 0x{lr}; func_{callee}(ctx); DRAIN_TRAMPOLINE(ctx);\n" + indent + "}")
            changed = changed[:match.start()] + block + changed[match.end():]
        if address in PHASE_INDIRECT_RETURNS:
            changed = _indirect_scopes(changed, PHASE_INDIRECT_RETURNS[address], "")
        source = source.replace(body, changed, 1)
    return source


def _render_callback_scopes(source: str) -> str:
    body = function_body(source, f"void func_{RENDER_PATH_FUNCTION}(ppu_context* ctx) {{")
    if _digest(body + "\n") != RENDER_PATH_SHA256:
        raise ValueError("render-record parent function changed; re-audit callback paths")
    matches = [m for m in call_sites(body)
               if (m.group("lr").upper(), m.group("callee").upper()) in RENDER_PATH_CALLS]
    if [(m.group("lr").upper(), m.group("callee").upper()) for m in matches] != list(RENDER_PATH_CALLS):
        raise ValueError("render-record parent path callsites changed")
    changed = body
    for match in reversed(matches):
        indent = match.group("indent")
        inner = indent + "    "
        lr, callee = match.group("lr"), match.group("callee")
        path_scope = (indent + "{\n" + inner +
                      f"haze_producer_profile::call_scope haze_producer_nested_{lr}("
                      f"0x{lr}u, 0x{callee}u);\n" + inner +
                      f"ctx->lr = 0x{lr}; func_{callee}(ctx); DRAIN_TRAMPOLINE(ctx);\n" + indent + "}")
        changed = changed[:match.start()] + path_scope + changed[match.end():]
    source = source.replace(body, changed, 1)

    for address, digest, returns in RENDER_CALLBACK_FUNCTIONS:
        body = function_body(source, f"void func_{address}(ppu_context* ctx) {{")
        if _digest(body + "\n") not in (digest, RENDER_CALLBACK_VARIANT_SHA256.get(address)):
            raise ValueError(f"render callback {address} changed; re-audit indirect target")
        source = source.replace(body, _indirect_scopes(body, returns, ""), 1)
    return source


def patch(source: str) -> str:
    body = function_body(source)
    # The original guard digest is defined over the source slice through the
    # newline before the next function, matching patch_guest_pacer.py.
    digest = _digest(body + "\n")
    if digest not in (ORIGINAL_SHA256, GUEST_PACER_SHA256):
        raise ValueError("Haze producer function changed; audit before profiling")
    sites = call_sites(body)
    if len(sites) != 74:
        raise ValueError(f"expected 74 direct call/trampoline sites, found {len(sites)}")
    excluded = [m for m in sites if m.group("callee").upper() in EXCLUDED_CALLEES]
    profiled = [m for m in sites if m.group("callee").upper() not in EXCLUDED_CALLEES]
    if len(excluded) != 15 or len(profiled) != 59:
        raise ValueError("main producer direct-call inventory changed; re-audit profile coverage")

    # Reverse-order substitutions keep match offsets stable.
    changed = body
    profile_index = len(profiled) - 1
    for match in reversed(profiled):
        index = profile_index
        profile_index -= 1
        replacement = _call_scope(index, match.group("lr"), match.group("callee"), match.group("indent"))
        changed = changed[:match.start()] + replacement + changed[match.end():]
    if profile_index != -1:
        raise AssertionError("profile call index accounting failed")
    changed = changed.replace(FUNCTION, FUNCTION + "\n" + FRAME_LINE.rstrip("\n"), 1)
    changed = _indirect_scopes(changed, MAIN_INDIRECT_RETURNS, "haze_producer_frame, ")

    if source.count(INCLUDE):
        if source.count(INCLUDE) != 1:
            raise ValueError("duplicate producer-profile header include")
        if source.index(INCLUDE) > source.index(FUNCTION):
            raise ValueError("producer-profile header appears after the target function")
        result = source.replace(INCLUDE, INCLUDE, 1)
    else:
        result = INCLUDE + source
    result = _phase_scopes(_dispatch_scopes(result.replace(body, changed, 1)))
    return _render_callback_scopes(result)


def strip_profile(source: str) -> str:
    """Remove only generated instrumentation, for preservation tests."""
    if source.count(INCLUDE) != 1:
        raise ValueError("expected one producer-profile include")
    result = source.replace(INCLUDE, "", 1)
    indirect = re.compile(
        r"(?m)^(?P<indent>[ \t]*)\{\n"
        r"(?P=indent)    haze_producer_profile::call_scope haze_producer_indirect_(?P<lr>[0-9A-F]+)\("
        r"(?:haze_producer_frame, )?0x(?P=lr)u, static_cast<std::uint32_t>\(ctx->ctr\)\);\n"
        r"(?P=indent)    ps3_indirect_call\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
        r"(?P=indent)\}"
    )
    result, count = indirect.subn(lambda m: m.group("indent") + "ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);", result)
    expected_indirect = len(MAIN_INDIRECT_RETURNS) + len(DISPATCH_INDIRECT_RETURNS) + sum(
        len(returns) for returns in PHASE_INDIRECT_RETURNS.values()) + sum(
            len(returns) for _, _, returns in RENDER_CALLBACK_FUNCTIONS)
    if count != expected_indirect:
        raise ValueError(f"expected {expected_indirect} indirect scopes, removed {count}")
    nested = re.compile(
        r"(?m)^(?P<indent>[ \t]*)\{\n"
        r"(?P=indent)    haze_producer_profile::call_scope haze_producer_nested_(?P<lr>[0-9A-F]+)\("
        r"0x(?P=lr)u, 0x(?P<callee>[0-9A-F]+)u\);\n"
        r"(?P=indent)    ctx->lr = 0x(?P=lr); func_(?P=callee)\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
        r"(?P=indent)\}"
    )
    result, count = nested.subn(lambda m: m.group("indent") + f"ctx->lr = 0x{m.group('lr')}; func_{m.group('callee')}(ctx); DRAIN_TRAMPOLINE(ctx);", result)
    expected_nested = 14 + sum(count for _, _, count in PHASE_FUNCTIONS) + len(RENDER_PATH_CALLS)
    if count != expected_nested:
        raise ValueError(f"expected {expected_nested} nested direct scopes, removed {count}")
    body = function_body(result)
    if body.count(FRAME_LINE) != 1:
        raise ValueError("missing or repeated producer frame scope")
    changed = body.replace(FRAME_LINE, "", 1)
    # Match each generated block, recover the guest line, and reject stray scopes.
    wrapper = re.compile(
        r"(?m)^(?P<indent>[ \t]*)\{\n"
        r"(?P=indent)    haze_producer_profile::call_scope haze_producer_call_\d+\("
        r"haze_producer_frame, 0x(?P<lr>[0-9A-Fa-f]+)u, 0x(?P<callee>[0-9A-Fa-f]+)u\);\n"
        r"(?P=indent)    ctx->lr = 0x(?P=lr); func_(?P=callee)\(ctx\); DRAIN_TRAMPOLINE\(ctx\);\n"
        r"(?P=indent)\}"
    )
    changed, count = wrapper.subn(
        lambda match: (match.group("indent") + f"ctx->lr = 0x{match.group('lr')}; "
                       f"func_{match.group('callee')}(ctx); DRAIN_TRAMPOLINE(ctx);"),
        changed,
    )
    if count != 59 or "haze_producer_call_" in changed:
        raise ValueError(f"expected 59 generated call scopes, removed {count}")
    result = result.replace(body, changed, 1)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="generated or build-local ppu_recomp_003.cpp")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = patch(args.source.read_text(encoding="cp1252"))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result, encoding="cp1252")
    phase_calls = sum(count for _, _, count in PHASE_FUNCTIONS)
    indirect_calls = len(MAIN_INDIRECT_RETURNS) + len(DISPATCH_INDIRECT_RETURNS) + sum(
        len(returns) for returns in PHASE_INDIRECT_RETURNS.values()) + sum(
            len(returns) for _, _, returns in RENDER_CALLBACK_FUNCTIONS)
    print(f"Producer profile scopes: 59 main direct calls, 14 dispatcher direct calls, {phase_calls} phase calls, {indirect_calls} indirect sites, {len(RENDER_PATH_CALLS)} render path calls; excluded helper callees:",
          ", ".join(sorted(EXCLUDED_CALLEES)))


if __name__ == "__main__":
    main()
