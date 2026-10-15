#pragma once
#include "haze_vertex_parameter_batch.h"

// Private host packet. The producer copies the parameter bytes into storage
// owned by the command stream. Publication and retirement belong to the caller;
// this codec retains no guest pointers and does not publish partially written
// packets. Only a consumer with this extension may receive these bytes.
namespace haze::vertex_parameters {
constexpr std::uint32_t packet_header = 0x0010ffe0u;
constexpr std::uint32_t packet_magic = 0x48564331u; // HVC1
constexpr std::size_t packet_prefix = 20;

inline bool valid(const layout& p) noexcept {
    return p.first < 512 && p.rows <= 512 - p.first && p.lanes <= 4 &&
        (!p.rows || p.lanes) && p.source_bytes == p.rows * p.lanes * 4;
}
inline std::uint32_t read_be(const std::uint8_t* p) noexcept {
    return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 |
        std::uint32_t(p[2]) << 8 | p[3];
}
inline void write_be(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = std::uint8_t(v >> 24); p[1] = std::uint8_t(v >> 16);
    p[2] = std::uint8_t(v >> 8); p[3] = std::uint8_t(v);
}
inline std::size_t packet_bytes(const layout& p) noexcept {
    return valid(p) ? packet_prefix + p.source_bytes : 0;
}

// source and destination must be disjoint. Invalid input leaves destination
// untouched. The source may be mutated or retired immediately on success.
inline bool encode(const layout& p, const std::uint8_t* source, std::size_t bytes,
                   std::uint8_t* destination, std::size_t capacity) noexcept {
    const auto size = packet_bytes(p);
    if (!size || !destination || capacity < size || bytes < p.source_bytes ||
        (p.source_bytes && !source)) return false;
    write_be(destination, packet_header);
    write_be(destination + 4, packet_magic);
    write_be(destination + 8, p.first);
    write_be(destination + 12, p.rows);
    write_be(destination + 16, p.lanes);
    if (p.source_bytes) std::memcpy(destination + packet_prefix, source, p.source_bytes);
    return true;
}

// A successful view borrows the command stream's bytes only until consume
// completes. Trailing commands are allowed; used identifies this packet alone.
// No output arguments change on rejection, including malformed/truncated input.
inline bool decode(const std::uint8_t* packet, std::size_t available,
                   layout& out, const std::uint8_t*& source,
                   std::size_t& used) noexcept {
    if (!packet || available < packet_prefix || read_be(packet) != packet_header ||
        read_be(packet + 4) != packet_magic) return false;
    layout p{read_be(packet + 8), read_be(packet + 12), read_be(packet + 16), 0};
    // Bound fields before multiplying untrusted counts.
    if (p.first >= 512 || p.rows > 512 - p.first || p.lanes > 4 ||
        (p.rows && !p.lanes)) return false;
    p.source_bytes = p.rows * p.lanes * 4;
    const auto size = packet_bytes(p);
    if (available < size) return false;
    out = p; source = packet + packet_prefix; used = size;
    return true;
}

// The legacy renderer mirror also tracks an inclusive dirty register range.
// Preserve its existing range, including when only some lanes are updated.
inline bool apply_legacy(const layout& p, const std::uint8_t* source,
                         std::size_t bytes, void* constants, std::uint32_t& load,
                         std::uint32_t& dirty_lo, std::uint32_t& dirty_hi,
                         int& dirty) noexcept {
    std::uint32_t unused_window[32];
    if (!apply(p, source, bytes, constants, unused_window, load)) return false;
    if (!p.rows) return true;
    const auto last = p.first + p.rows - 1;
    if (!dirty) { dirty_lo = p.first; dirty_hi = last; dirty = 1; }
    else {
        if (p.first < dirty_lo) dirty_lo = p.first;
        if (last > dirty_hi) dirty_hi = last;
    }
    return true;
}
}
