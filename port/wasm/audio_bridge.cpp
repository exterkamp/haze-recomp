#include <atomic>
#include <cstddef>
#include <cstdint>
#include <emscripten/emscripten.h>

namespace {
constexpr uint32_t capacity = 8192;
// Shared ABI with audio-worklet.js: eight u32 fields, padding, stereo floats.
struct alignas(64) AudioRing {
    std::atomic<uint32_t> read{0}, write{0}, enabled{0};
    uint32_t frames = capacity;
    std::atomic<uint32_t> rendered{0}, underruns{0}, dropped{0}, submitted{0};
    alignas(64) float samples[capacity * 2]{};
};
static_assert(offsetof(AudioRing, samples) == 64);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
AudioRing ring;
}

extern "C" EMSCRIPTEN_KEEPALIVE uintptr_t haze_wasm_audio_buffer() {
    return reinterpret_cast<uintptr_t>(&ring);
}
extern "C" int ps3_wasm_audio_init() { return 0; }
extern "C" void ps3_wasm_audio_shutdown() { ring.enabled.store(0); }
extern "C" void ps3_wasm_audio_submit(const float* samples, uint32_t frames) {
    ring.submitted.fetch_add(frames, std::memory_order_relaxed);
    if (!ring.enabled.load(std::memory_order_acquire)) return;
    const uint32_t write = ring.write.load(std::memory_order_relaxed);
    const uint32_t read = ring.read.load(std::memory_order_acquire);
    if (frames > capacity || write - read > capacity - frames) {
        ring.dropped.fetch_add(frames, std::memory_order_relaxed);
        return;
    }
    for (uint32_t i = 0; i < frames; ++i) {
        const uint32_t slot = ((write + i) % capacity) * 2;
        ring.samples[slot] = samples[i * 2];
        ring.samples[slot + 1] = samples[i * 2 + 1];
    }
    ring.write.store(write + frames, std::memory_order_release);
}
