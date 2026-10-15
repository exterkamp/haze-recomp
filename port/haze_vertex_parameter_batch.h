#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Semantic layout of the complete 0072E398 vertex-parameter operation.
// The caller owns the source bytes and performs guest dirty-state publication.
// This kernel neither borrows guest pointers nor emits/interprets RSX methods.
namespace haze::vertex_parameters {
struct layout {
    std::uint32_t first = 0, rows = 0, lanes = 0, source_bytes = 0;
};

inline bool plan(unsigned type, int count, int first, layout& out) noexcept {
    if (type > 16 || count < 0 || first < 0 || first >= 512) return false;
    constexpr unsigned widths[17] = {0,1,2,3,4,1,2,3,4,3,3,4,4,3,3,4,4};
    constexpr unsigned heights[17] = {0,1,1,1,1,1,1,1,1,3,4,3,4,3,4,3,4};
    const auto rows = std::uint64_t(unsigned(count)) * heights[type];
    if (rows > 512u - unsigned(first)) return false;
    out = {unsigned(first), unsigned(rows), widths[type],
           unsigned(rows) * widths[type] * 4};
    return true;
}

// constants points to 512 tightly packed four-word registers. Using memcpy
// permits both the integer dispatcher mirror and the float legacy mirror.
// window/load retain the final original method-window state so subsequent
// legacy partial uploads observe the same result. Inputs must be disjoint
// from outputs; source lifetime need only cover this synchronous call.
inline bool apply(const layout& p, const std::uint8_t* source, std::size_t bytes,
                  void* constants, std::uint32_t* window,
                  std::uint32_t& load) noexcept {
    if (p.first >= 512 || p.rows > 512 - p.first || p.lanes > 4 ||
        (p.rows && !p.lanes) || p.source_bytes != p.rows * p.lanes * 4 ||
        bytes < p.source_bytes || (p.source_bytes && !source) ||
        !constants || !window) return false;
    if (!p.rows) return true;
    auto* dst = static_cast<std::uint8_t*>(constants);
    for (unsigned row = 0; row < p.rows; ++row) {
        for (unsigned lane = 0; lane < p.lanes; ++lane) {
            const auto* s = source + (row * p.lanes + lane) * 4;
            const std::uint32_t word = std::uint32_t(s[0]) << 24 |
                std::uint32_t(s[1]) << 16 | std::uint32_t(s[2]) << 8 | s[3];
            std::memcpy(dst + ((p.first + row) * 4 + lane) * 4, &word, 4);
            const unsigned window_word = p.lanes == 4 ? (row % 8) * 4 + lane : lane;
            window[window_word] = word;
        }
    }
    load = p.first + (p.lanes == 4 ? ((p.rows - 1) / 8) * 8 : p.rows - 1);
    return true;
}
}
