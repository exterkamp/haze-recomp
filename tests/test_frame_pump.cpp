#include "../port/haze_frame_pump.h"
#include <cassert>
#include <vector>

static bool pending;
static unsigned completed, batch;
static std::vector<unsigned> presented;
static int take_flip() { bool p = pending; pending = false; return p; }
static void present() { presented.push_back(batch); batch = 0; }
static void drain() {
    // Each drain reaches the next completed frame, then stops at its flip.
    batch = ++completed;
    pending = true;
}
static void partial_drain() { batch += 10; } // render pass / wrap / semaphore

int main() {
    haze_drain_frames(drain, take_flip, present);
    assert((presented == std::vector<unsigned>{1}));
    haze_drain_frames(drain, take_flip, present);
    assert((presented == std::vector<unsigned>{1, 2}));
    assert(!pending); // regression: count-only presents left this armed

    // Pending on entry must present before another drain mixes in new work.
    batch = 3; pending = true; completed = 3;
    haze_drain_frames(drain, take_flip, present);
    assert((presented == std::vector<unsigned>{1, 2, 3, 4}));
    assert(!pending);

    // A partially drained frame must keep making progress without presenting.
    haze_drain_frames(partial_drain, take_flip, present);
    haze_drain_frames(partial_drain, take_flip, present);
    assert(presented.size() == 4 && batch == 20);
    pending = true;
    haze_drain_frames(partial_drain, take_flip, present);
    assert(presented.back() == 20 && batch == 10 && !pending);
}
