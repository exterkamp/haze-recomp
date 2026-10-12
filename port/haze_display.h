#pragma once
// Call on the window-owning thread before creation, then before renderer init.
void haze_display_prepare();
bool haze_display_apply(void* window);
