#pragma once
#include <cstdint>
#include <unistd.h>

// The HLE's host-pointer -> guest-EA conversions truncate to 32 bits.
// Keep guest memory at a 4 GiB boundary and move the heap break past it.
// memory.grow supplies zero-initialized pages; do not touch all 4 GiB eagerly.
inline uint8_t* haze_wasm_reserve_guest_vm()
{
    constexpr uintptr_t size = 0x100010000ull;
    const uintptr_t current = reinterpret_cast<uintptr_t>(sbrk(0));
    const uintptr_t aligned = (current + 0xFFFFFFFFull) & ~0xFFFFFFFFull;
    if (sbrk(static_cast<intptr_t>(aligned + size - current)) == reinterpret_cast<void*>(-1))
        return nullptr;
    return reinterpret_cast<uint8_t*>(aligned);
}
