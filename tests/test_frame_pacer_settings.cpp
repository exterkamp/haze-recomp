#include "../port/haze_frame_pacer_settings.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>

int main()
{
    // Public setting boundaries, including the normal 30 FPS default.
    assert(!haze_native_pacer_selected(nullptr, nullptr));
    assert(!haze_native_pacer_selected(nullptr, ""));
    for (const char* capped : {"1", "24", "29", "30", "0030"})
        assert(!haze_native_pacer_selected(nullptr, capped));
    for (const char* unlocked : {"0", "0000", "31", "60", "144", "240"}) {
        assert(haze_native_pacer_selected(nullptr, unlocked));
        assert(!haze_native_pacer_selected("0", unlocked));
        assert(haze_native_pacer_selected("", unlocked));
    }
    assert(haze_native_pacer_selected("1", "30"));
    assert(haze_native_pacer_selected("1", nullptr));
    for (const char* invalid : {"-0", "+60", " 60", "60 ", "60fps", "30.5", "241", "99999999999999999999"})
        assert(!haze_native_pacer_selected(nullptr, invalid));
    for (const char* invalid_override : {"2", "on", "false", "-1"})
        assert(!haze_native_pacer_selected(invalid_override, "0"));
    puts("Frame-pacer selection: capped/unlimited boundaries, explicit overrides and invalid inputs passed");
}
