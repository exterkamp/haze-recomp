#pragma once
#include "haze_gamepad.h"

// Initialize before renderer creation; update and PNG decoding use the window
// thread. Immutable snapshots are shared with the render consumer.
void haze_prompts_initialize(const char* config);
void haze_prompts_update(HazeInputFamily family);
