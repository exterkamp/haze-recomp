#pragma once
#include <cstdint>

// Private diagnostic only. No guest memory, registers or submission state is
// modified. Complete contiguous streams are distinguished from refills/aborts.
namespace haze_indexed_capture {
struct scope {
    scope(std::uint32_t thread, std::uint64_t toc, std::uint64_t lr) noexcept;
    ~scope() noexcept;
    void begin(std::uint32_t site, std::uint64_t context,
               std::uint64_t cursor, std::uint64_t offset,
               std::uint64_t type, std::uint64_t primitive,
               std::uint64_t count, std::uint64_t alignment) noexcept;
    void callback(std::uint64_t target, std::uint64_t words) noexcept;
    void finish() noexcept;
private:
    bool eligible_;
    int slot_ = -1;
    std::uint32_t lr_, callbacks_ = 0, last_target_ = 0, last_words_ = 0;
};
}
