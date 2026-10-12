#pragma once
#include "cellPad.h"

enum class HazeInputFamily { Keyboard, Xbox, PlayStation, Switch };
struct HazeGamepadState {
    CellPadHostInput input{0,128,128,128,128,0,0};
    HazeInputFamily family=HazeInputFamily::Xbox;
    bool connected=false;
    bool activity=false;
};

// These functions belong to the window thread. Guest threads consume a copy.
bool haze_gamepad_initialize(const char* config);
void haze_gamepad_configure(const char* config);
HazeGamepadState haze_gamepad_update();
void haze_gamepad_shutdown();
