#pragma once
#include "haze_gamepad.h"
void haze_input_initialize();
void haze_input_attach(void* window);
void haze_input_update();
void haze_input_shutdown();
HazeInputFamily haze_input_family();
// Simulation-thread consumer: raw device counts and degrees per count.
bool haze_input_take_mouse(double& x, double& y, double& sensitivity, bool& invert_y);
