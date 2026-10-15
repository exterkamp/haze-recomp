#pragma once
#include "haze_vertex_parameter_packet.h"
#include "rsx_dispatch.h"
#include "rsx_commands.h"

namespace haze::vertex_parameters {
// These operate on renderer-owned mirrors, on their owning consumer thread.
// They are also used by the differential test against the actual SDK handlers.
inline bool apply_dispatch(rsx_dispatch& state, const layout& p,
                           const std::uint8_t* source, std::size_t bytes) noexcept {
    if (!apply(p, source, bytes, state.constants, state.regs + (0x1f00 / 4),
               state.regs[0x1efc / 4])) return false;
    if (!p.rows) return true;
    if (p.lanes == 4) {
        const auto blocks = p.rows / 8, tail = (p.rows % 8) * 4;
        state.seen[0x1efc / 4] += blocks + (tail != 0);
        for (unsigned i = 0; i < 32; ++i)
            state.seen[0x1f00 / 4 + i] += blocks + (i < tail);
    } else {
        state.seen[0x1efc / 4] += p.rows;
        for (unsigned i = 0; i < p.lanes; ++i)
            state.seen[0x1f00 / 4 + i] += p.rows;
    }
    return true;
}
inline bool apply_state(rsx_state& state, const layout& p,
                        const std::uint8_t* source, std::size_t bytes) noexcept {
    return apply_legacy(p, source, bytes, state.vertex_constants,
        state.transform_constant_load, state.vertex_constants_lo,
        state.vertex_constants_hi, state.vertex_constants_dirty);
}
}
