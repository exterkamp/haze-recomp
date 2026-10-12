#pragma once
void haze_input_initialize();
void haze_input_attach(void* window);
// Simulation-thread consumer: raw device counts and degrees per count.
bool haze_input_take_mouse(double& x, double& y, double& sensitivity, bool& invert_y);
