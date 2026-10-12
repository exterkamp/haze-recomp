#pragma once
struct haze_display_defaults {
    unsigned render_width, render_height;
    unsigned window_width, window_height;
};
// Detect physical desktop pixels and a 16:9 client fitting the monitor work area.
bool haze_display_detect(const char* monitor, haze_display_defaults& defaults);
// Call on the window-owning thread before creation, then before renderer init.
void haze_display_prepare();
bool haze_display_apply(void* window);
