#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unistd.h>
#include <emscripten/emscripten.h>

extern "C" uintptr_t haze_wasm_audio_buffer();
extern "C" void ps3_wasm_audio_submit(const float*, uint32_t);

int main() {
    auto* state = reinterpret_cast<std::atomic<uint32_t>*>(haze_wasm_audio_buffer());
    const double start = emscripten_get_now();
    while (!state[2].load() && emscripten_get_now() - start < 10000) usleep(1000);
    assert(state[2].load());
    const double origin = emscripten_get_now();
    float block[512];
    for (unsigned n = 0; n < 400; ++n) {
        while (emscripten_get_now() < origin + n * 256.0 / 48.0) usleep(500);
        for (unsigned i = 0; i < 256; ++i) {
            block[i * 2] = block[i * 2 + 1] = .25f * std::sin((n * 256 + i) * 440.0 * 6.283185307179586 / 48000.0);
        }
        ps3_wasm_audio_submit(block, 256);
    }
    usleep(100000);
    printf("[audio-smoke] submitted=%u rendered=%u underruns=%u dropped=%u\n",
        state[7].load(), state[4].load(), state[5].load(), state[6].load());
    assert(state[4].load() >= 96000 && state[6].load() == 0);
    puts("[audio-smoke] PASS: shared PCM ring consumed by browser AudioWorklet at 48 kHz");
    return 0;
}
