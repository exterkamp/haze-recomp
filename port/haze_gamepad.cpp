#include "haze_gamepad.h"
#include <SDL3/SDL.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {
struct Device {
    SDL_JoystickID id;
    SDL_Gamepad* pad;
    CellPadHostInput previous{0,128,128,128,128,0,0};
};
std::vector<Device> devices;
SDL_JoystickID selected=0;
bool initialized=false;
Uint64 next_scan=0;
float move_deadzone=.15f, look_deadzone=.12f, trigger_deadzone=.05f;
bool invert_look=false;
int forced_family=-1;
SDL_GamepadAxis axes[]={SDL_GAMEPAD_AXIS_LEFTX,SDL_GAMEPAD_AXIS_LEFTY,
                        SDL_GAMEPAD_AXIS_RIGHTX,SDL_GAMEPAD_AXIS_RIGHTY};
struct Binding { const char* name; const char* fallback; u16 mask; int source; };
constexpr int axis_source=32;
Binding bindings[]={
    {"Cross","south",CELL_PAD_CTRL_CROSS,SDL_GAMEPAD_BUTTON_SOUTH},
    {"Circle","east",CELL_PAD_CTRL_CIRCLE,SDL_GAMEPAD_BUTTON_EAST},
    {"Square","west",CELL_PAD_CTRL_SQUARE,SDL_GAMEPAD_BUTTON_WEST},
    {"Triangle","north",CELL_PAD_CTRL_TRIANGLE,SDL_GAMEPAD_BUTTON_NORTH},
    {"L1","leftshoulder",CELL_PAD_CTRL_L1,SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"R1","rightshoulder",CELL_PAD_CTRL_R1,SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"L2","lefttrigger",CELL_PAD_CTRL_L2,axis_source+SDL_GAMEPAD_AXIS_LEFT_TRIGGER},
    {"R2","righttrigger",CELL_PAD_CTRL_R2,axis_source+SDL_GAMEPAD_AXIS_RIGHT_TRIGGER},
    {"L3","leftstick",CELL_PAD_CTRL_L3,SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"R3","rightstick",CELL_PAD_CTRL_R3,SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {"Start","start",CELL_PAD_CTRL_START,SDL_GAMEPAD_BUTTON_START},
    {"Select","back",CELL_PAD_CTRL_SELECT,SDL_GAMEPAD_BUTTON_BACK},
    {"Up","dpup",CELL_PAD_CTRL_UP,SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"Down","dpdown",CELL_PAD_CTRL_DOWN,SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"Left","dpleft",CELL_PAD_CTRL_LEFT,SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"Right","dpright",CELL_PAD_CTRL_RIGHT,SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
};
std::string setting(const char* path,const char* section,const char* key,const char* fallback) {
    char value[1024];
    GetPrivateProfileStringA(section,key,fallback,value,sizeof(value),path);
    return value;
}
float deadzone(const char* path,const char* key,const char* fallback) {
    const auto value=setting(path,"Controller",key,fallback);
    char* end=nullptr;
    const float result=strtof(value.c_str(),&end);
    if (end!=value.c_str() && !*end && std::isfinite(result) && result>=0 && result<.95f) return result;
    fprintf(stderr,"[input] Invalid %s=%s; using %s\n",key,value.c_str(),fallback);
    return strtof(fallback,nullptr);
}
int source(const std::string& value) {
    if (!_stricmp(value.c_str(),"none")) return -1;
    const char* positions[]={"south","east","west","north"};
    for (int i=0;i<4;i++) if (!_stricmp(value.c_str(),positions[i])) return i;
    const auto button=SDL_GetGamepadButtonFromString(value.c_str());
    if (button!=SDL_GAMEPAD_BUTTON_INVALID) return button;
    const auto axis=SDL_GetGamepadAxisFromString(value.c_str());
    if (axis!=SDL_GAMEPAD_AXIS_INVALID) return axis_source+axis;
    fprintf(stderr,"[input] Unknown controller binding '%s'; disabled\n",value.c_str());
    return -1;
}
HazeInputFamily family(SDL_Gamepad* pad) {
    if (forced_family>=0) return static_cast<HazeInputFamily>(forced_family);
    switch (SDL_GetRealGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_PS3: case SDL_GAMEPAD_TYPE_PS4: case SDL_GAMEPAD_TYPE_PS5:
        return HazeInputFamily::PlayStation;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
        return HazeInputFamily::Switch;
    default: return HazeInputFamily::Xbox;
    }
}
void stick(SDL_Gamepad* pad,int first,float dz,u8& x,u8& y) {
    float fx=axes[first]==SDL_GAMEPAD_AXIS_INVALID ? 0 : SDL_GetGamepadAxis(pad,axes[first])/32768.f;
    float fy=axes[first+1]==SDL_GAMEPAD_AXIS_INVALID ? 0 : SDL_GetGamepadAxis(pad,axes[first+1])/32768.f;
    const float magnitude=std::hypot(fx,fy);
    const float scale=magnitude>dz ? (std::min(magnitude,1.f)-dz)/((1-dz)*magnitude) : 0;
    fx*=scale; fy*=scale;
    if (first==2 && invert_look) fy=-fy;
    auto quantize=[](float v) { return static_cast<u8>(std::clamp(std::lround(128+v*(v<0 ? 128 : 127)),0l,255l)); };
    x=quantize(fx); y=quantize(fy);
}
CellPadHostInput read(SDL_Gamepad* pad) {
    CellPadHostInput out{0,128,128,128,128,0,0};
    stick(pad,0,move_deadzone,out.lx,out.ly);
    stick(pad,2,look_deadzone,out.rx,out.ry);
    for (const auto& b:bindings) {
        int pressure=0;
        if (b.source>=axis_source) {
            const float v=SDL_GetGamepadAxis(pad,static_cast<SDL_GamepadAxis>(b.source-axis_source))/32767.f;
            if (v>trigger_deadzone) pressure=std::clamp(int(std::lround((v-trigger_deadzone)/(1-trigger_deadzone)*255)),1,255);
        } else if (b.source>=0 && SDL_GetGamepadButton(pad,static_cast<SDL_GamepadButton>(b.source))) pressure=255;
        if (pressure) out.buttons|=b.mask;
        if (b.mask==CELL_PAD_CTRL_L2) out.l2=static_cast<u8>(pressure);
        if (b.mask==CELL_PAD_CTRL_R2) out.r2=static_cast<u8>(pressure);
    }
    return out;
}
bool activity(const CellPadHostInput& now,const CellPadHostInput& before) {
    // Releases and sub-deadzone noise do not steal the prompt family. Held
    // buttons do not repeatedly override a subsequent keyboard/mouse action.
    if (now.buttons & ~before.buttons) return true;
    const u8 a[]={now.lx,now.ly,now.rx,now.ry}, b[]={before.lx,before.ly,before.rx,before.ry};
    for (int i=0;i<4;i++) if (a[i]!=128 && std::abs(int(a[i])-int(b[i]))>=4) return true;
    return (now.l2>before.l2+4) || (now.r2>before.r2+4);
}
}

void haze_gamepad_configure(const char* path) {
    for (auto& b:bindings) b.source=source(setting(path,"ControllerBindings",b.name,b.fallback));
    const char* keys[]={"MoveX","MoveY","LookX","LookY"};
    const char* defaults[]={"leftx","lefty","rightx","righty"};
    for (int i=0;i<4;i++) {
        const auto value=setting(path,"Controller",keys[i],defaults[i]);
        axes[i]=SDL_GetGamepadAxisFromString(value.c_str());
        if (axes[i]==SDL_GAMEPAD_AXIS_INVALID && _stricmp(value.c_str(),"none"))
            fprintf(stderr,"[input] Unknown controller axis '%s'; disabled\n",value.c_str());
    }
    move_deadzone=deadzone(path,"MoveDeadzone","0.15");
    look_deadzone=deadzone(path,"LookDeadzone","0.12");
    trigger_deadzone=deadzone(path,"TriggerDeadzone","0.05");
    invert_look=GetPrivateProfileIntA("Controller","InvertLookY",0,path)!=0;
    auto style=setting(path,"Controller","IconFamily","auto");
    forced_family=!_stricmp(style.c_str(),"playstation") ? int(HazeInputFamily::PlayStation) :
                  !_stricmp(style.c_str(),"switch") ? int(HazeInputFamily::Switch) :
                  !_stricmp(style.c_str(),"xbox") ? int(HazeInputFamily::Xbox) : -1;
    const auto mapping=setting(path,"Controller","MappingFile","");
    if (!mapping.empty()) {
        const auto file=std::filesystem::path(path).parent_path()/mapping;
        if (SDL_AddGamepadMappingsFromFile(file.string().c_str())<0)
            fprintf(stderr,"[input] Cannot load mapping file %s: %s\n",file.string().c_str(),SDL_GetError());
    }
}
bool haze_gamepad_initialize(const char* config) {
    if (initialized) return true;
    // Use physical face positions consistently on Nintendo hardware too.
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        fprintf(stderr,"[input] SDL gamepad initialization failed: %s\n",SDL_GetError()); return false;
    }
    initialized=true;
    SDL_SetGamepadEventsEnabled(false);
    haze_gamepad_configure(config);
    fprintf(stderr,"[input] SDL %d.%d.%d gamepad wrapper enabled\n",SDL_MAJOR_VERSION,SDL_MINOR_VERSION,SDL_MICRO_VERSION);
    return true;
}
HazeGamepadState haze_gamepad_update() {
    HazeGamepadState out;
    if (!initialized) return out;
    SDL_PumpEvents();
    SDL_UpdateGamepads();
    for (auto it=devices.begin();it!=devices.end();) {
        if (!SDL_GamepadConnected(it->pad)) {
            fprintf(stderr,"[input] Controller disconnected: %u\n",it->id);
            if (selected==it->id) selected=0;
            SDL_CloseGamepad(it->pad); it=devices.erase(it); next_scan=0;
        } else ++it;
    }
    if (SDL_GetTicks()>=next_scan) {
        next_scan=SDL_GetTicks()+500;
        int count=0;
        SDL_JoystickID* ids=SDL_GetGamepads(&count);
        for (int i=0;i<count;i++) {
            if (std::any_of(devices.begin(),devices.end(),[&](const Device& d) { return d.id==ids[i]; })) continue;
            if (auto* pad=SDL_OpenGamepad(ids[i])) {
                devices.push_back({ids[i],pad});
                fprintf(stderr,"[input] Controller connected: %s (%u)\n",SDL_GetGamepadName(pad),ids[i]);
            }
        }
        SDL_free(ids);
    }
    if (!selected && !devices.empty()) selected=devices.front().id;
    for (auto& d:devices) {
        const auto value=read(d.pad);
        const bool used=activity(value,d.previous);
        d.previous=value;
        if (used) { selected=d.id; out.activity=true; }
    }
    for (const auto& d:devices) if (d.id==selected) {
        out.input=d.previous; out.family=family(d.pad); out.connected=true;
    }
    return out;
}
void haze_gamepad_shutdown() {
    for (auto& d:devices) SDL_CloseGamepad(d.pad);
    devices.clear(); selected=0; next_scan=0;
    if (initialized) SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    initialized=false;
}
