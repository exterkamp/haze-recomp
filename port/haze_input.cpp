#include "haze_input.h"
#include "haze_mouse.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cellPad.h"
#include "cellSail.h"
#include "haze_startup.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <atomic>

namespace {
std::atomic<HWND> window;
WNDPROC original_proc;
volatile LONG captured, wheel;
HazeMouseMotion mouse_motion;
POINT old_cursor;
bool enabled = true;
double sensitivity = 0.10;
bool invert_y;
HazeStartupSkip startup_skip;
enum { KEY_HELD=1, KEY_PENDING=2 };
std::atomic<unsigned> message_keys[256];
std::atomic<bool> capture_click;
struct Binding { const char* name; const char* fallback; u16 button; int vk=0; };
Binding bindings[] = {
    {"Cross", "Space", CELL_PAD_CTRL_CROSS},
    {"Circle", "X", CELL_PAD_CTRL_CIRCLE},
    {"Square", "E", CELL_PAD_CTRL_SQUARE},
    {"Triangle", "R", CELL_PAD_CTRL_TRIANGLE},
    {"L1", "G", CELL_PAD_CTRL_L1}, {"R1", "Mouse1", CELL_PAD_CTRL_R1},
    {"L2", "Q", CELL_PAD_CTRL_L2}, {"R2", "F", CELL_PAD_CTRL_R2},
    {"L3", "Ctrl", CELL_PAD_CTRL_L3}, {"R3", "Mouse2", CELL_PAD_CTRL_R3},
    {"Start", "Enter", CELL_PAD_CTRL_START}, {"Select", "Tab", CELL_PAD_CTRL_SELECT},
    {"Up", "Up", CELL_PAD_CTRL_UP}, {"Down", "Down", CELL_PAD_CTRL_DOWN},
    {"Left", "Left", CELL_PAD_CTRL_LEFT}, {"Right", "Right", CELL_PAD_CTRL_RIGHT},
};
int movement[4] = {'W','S','A','D'};
int key_code(const char* name) {
    if (name[0] && !name[1]) return std::toupper((unsigned char)name[0]);
    const struct { const char* name; int vk; } names[] = {
        {"Space",VK_SPACE},{"Enter",VK_RETURN},{"Escape",VK_ESCAPE},{"Tab",VK_TAB},
        {"Shift",VK_SHIFT},{"Ctrl",VK_CONTROL},{"Alt",VK_MENU},
        {"Up",VK_UP},{"Down",VK_DOWN},{"Left",VK_LEFT},{"Right",VK_RIGHT},
        {"Mouse1",VK_LBUTTON},{"Mouse2",VK_RBUTTON},{"Mouse3",VK_MBUTTON},
        {"Mouse4",VK_XBUTTON1},{"Mouse5",VK_XBUTTON2},{"None",0},
    };
    for (const auto& n : names) if (!_stricmp(name,n.name)) return n.vk;
    fprintf(stderr,"[input] Unknown key '%s'; binding disabled\n",name);
    return 0;
}
bool down(int vk) {
    if (vk <= 0 || vk >= 256) return false;
    // Buffer an otherwise missed tap for one host report, rather than holding
    // released buttons for 120ms. The SDK caches that report between pad reads.
    const unsigned state=message_keys[vk].fetch_and(~unsigned(KEY_PENDING));
    return (state & (KEY_HELD|KEY_PENDING)) || (GetAsyncKeyState(vk) & 0x8000);
}
void clear_keys() {
    for (auto& key : message_keys) key=0;
    capture_click=false;
}
void record_key(int vk, bool held, ULONGLONG) {
    if (vk <= 0 || vk >= 256 || vk == VK_F1) return;
    unsigned previous=message_keys[vk].load();
    unsigned next;
    do {
        next=held ? previous | KEY_HELD | ((previous & KEY_HELD) ? 0u : KEY_PENDING)
                  : previous & ~unsigned(KEY_HELD);
    } while (!message_keys[vk].compare_exchange_weak(previous,next));
}
void update_title() {
    SetWindowTextA(window, captured ? "Haze | Mouse captured | Esc: Pause | F1: Release mouse"
        : "Haze | Space: Confirm | Enter: Start | Click/F1: Capture mouse | Esc: Pause");
}
void clip_cursor() {
    RECT rect;
    GetClientRect(window,&rect);
    MapWindowPoints(window,nullptr,(POINT*)&rect,2);
    ClipCursor(&rect);
}
void capture(bool on) {
    if (on == (InterlockedCompareExchange(&captured,0,0) != 0)) return;
    if (on && GetForegroundWindow() != window) return;
    InterlockedExchange(&captured,on ? 1 : 0);
    mouse_motion.clear();
    InterlockedExchange(&wheel,0);
    if (on) {
        GetCursorPos(&old_cursor);
        SetCapture(window); clip_cursor();
        while (ShowCursor(FALSE) >= 0) {}
    } else {
        ClipCursor(nullptr);
        if (GetCapture() == window) ReleaseCapture();
        while (ShowCursor(TRUE) < 0) {}
        if (GetForegroundWindow() == window) SetCursorPos(old_cursor.x,old_cursor.y);
    }
    update_title();
    fprintf(stderr,"[input] Mouse %s (F1 toggles capture)\n",on ? "captured" : "released");
}
LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SETTEXT:
        if (lp && std::strstr((const char*)lp," | FPS:")) {
            char caption[512];
            snprintf(caption,sizeof(caption),"%s | %s",(const char*)lp,
                captured ? "Esc: Pause | F1: Release mouse"
                         : "Space: Confirm | Enter: Start | Click/F1: Mouse | Esc: Pause");
            return CallWindowProc(original_proc,hwnd,msg,wp,(LPARAM)caption);
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        record_key((int)wp,true,GetTickCount64());
        if (wp == VK_F1) { if (!(lp & (1ll<<30))) capture(!captured); return 0; }
        if (wp == VK_ESCAPE) { capture(false); return 0; } // Pause, rather than SDK's quit.
        for (const auto& b : bindings)
            if (b.button==CELL_PAD_CTRL_START && b.vk==(int)wp) capture(false);
        break;
    case WM_KEYUP: case WM_SYSKEYUP:
        record_key((int)wp,false,GetTickCount64()); break;
    case WM_LBUTTONDOWN:
        if (!captured && GetForegroundWindow() == hwnd) {
            capture_click=true;
            capture(true);
            return 0; // The click that captures must not fire the weapon.
        }
        record_key(VK_LBUTTON,true,GetTickCount64());
        break;
    case WM_LBUTTONUP:
        record_key(VK_LBUTTON,false,GetTickCount64());
        capture_click=false; break;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        record_key(VK_RBUTTON,msg==WM_RBUTTONDOWN,GetTickCount64()); break;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        record_key(VK_MBUTTON,msg==WM_MBUTTONDOWN,GetTickCount64()); break;
    case WM_XBUTTONDOWN: case WM_XBUTTONUP:
        record_key(HIWORD(wp)==XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2,
                   msg==WM_XBUTTONDOWN,GetTickCount64()); break;
    case WM_KILLFOCUS: case WM_CLOSE: case WM_DESTROY:
        clear_keys(); capture(false); break;
    case WM_CAPTURECHANGED:
        if (captured && (HWND)lp != hwnd) capture(false);
        break;
    case WM_MOVE: case WM_SIZE:
        if (captured) clip_cursor(); break;
    case WM_SETCURSOR:
        if (captured && LOWORD(lp) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
        break;
    case WM_MOUSEWHEEL:
        if (captured) InterlockedExchange(&wheel,1);
        break;
    case WM_INPUT: {
        RAWINPUT input{}; UINT size = sizeof(input);
        if (captured && GetRawInputData((HRAWINPUT)lp,RID_INPUT,&input,&size,sizeof(RAWINPUTHEADER)) == sizeof(input)
            && input.header.dwType == RIM_TYPEMOUSE && !(input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
            mouse_motion.add(input.data.mouse.lLastX,input.data.mouse.lLastY);
        }
        // DefWindowProc must see foreground raw input for system cleanup.
        return DefWindowProc(hwnd,msg,wp,lp);
    }
    }
    return CallWindowProc(original_proc,hwnd,msg,wp,lp);
}
int poll(u32 port, CellPadHostInput* input, void*) {
    if (port) return 0;
    ULONGLONG now = GetTickCount64();
    bool skip_pressed;
    bool skipping=startup_skip.sample(now,skip_pressed);
    if (skip_pressed) input->buttons |= CELL_PAD_CTRL_CROSS;
    if (!enabled) return skipping ? 1 : 0;
    HWND foreground=GetForegroundWindow();
    if (!foreground || foreground != window.load()) {
        mouse_motion.clear();
        InterlockedExchange(&wheel,0);
        clear_keys();
        return 1; // All controls neutral immediately on focus loss.
    }
    // A rebound key may drive several buttons or movement. Consume its pending
    // tap once, then share the same snapshot across all bindings in this report.
    bool sampled[256]={}, states[256]={};
    auto key_down=[&](int vk) {
        if (vk <= 0 || vk >= 256) return false;
        if (!sampled[vk]) { states[vk]=down(vk); sampled[vk]=true; }
        return states[vk];
    };
    for (const auto& b : bindings) {
        if (b.vk >= VK_LBUTTON && b.vk <= VK_XBUTTON2 && !captured) continue;
        if (b.vk == VK_LBUTTON && capture_click) continue;
        if (key_down(b.vk)) input->buttons |= b.button;
    }
    // Escape always pauses and releases mouse capture, even with Start rebound.
    if (key_down(VK_ESCAPE)) input->buttons |= CELL_PAD_CTRL_START;
    input->lx = (u8)(128 + 127*((int)key_down(movement[3])-(int)key_down(movement[2])));
    input->ly = (u8)(128 + 127*((int)key_down(movement[1])-(int)key_down(movement[0])));
    if (captured) {
        if (InterlockedExchange(&wheel,0)) input->buttons |= CELL_PAD_CTRL_CIRCLE;
    }
    input->l2=(input->buttons & CELL_PAD_CTRL_L2) ? 255 : 0;
    input->r2=(input->buttons & CELL_PAD_CTRL_R2) ? 255 : 0;
    return 1;
}
void playback(u32 player, const char* uri, int started, void*) {
    if (startup_skip.playback(player,uri,started!=0,GetTickCount64()))
        fprintf(stderr,"[startup] Skipping undecoded startup video: %s\n",uri);
}
}
void haze_input_initialize() {
    const char* mode=getenv("HAZE_INPUT");
    enabled=!(mode && !_stricmp(mode,"controller"));
    cellSail_set_host_playback_callback(playback,nullptr);
    cellPad_set_host_input_callback(poll,nullptr);
    if (!enabled) return;
    const char* config=getenv("HAZE_INPUT_CONFIG");
    char path[MAX_PATH]{};
    if (config && *config) GetFullPathNameA(config,MAX_PATH,path,nullptr);
    for (auto& b : bindings) {
        char value[32];
        GetPrivateProfileStringA("Bindings",b.name,b.fallback,value,sizeof(value),path);
        b.vk=key_code(value);
    }
    const char* movement_names[]={"Forward","Backward","StrafeLeft","StrafeRight"};
    const char* movement_defaults[]={"W","S","A","D"};
    for (int i=0;i<4;i++) {
        char value[32];
        GetPrivateProfileStringA("Bindings",movement_names[i],movement_defaults[i],value,sizeof(value),path);
        movement[i]=key_code(value);
    }
    char value[32];
    GetPrivateProfileStringA("Mouse","Sensitivity","0.10",value,sizeof(value),path);
    double requested=strtod(value,nullptr);
    if (std::isfinite(requested) && requested > 0 && requested <= 10) sensitivity=requested;
    invert_y=GetPrivateProfileIntA("Mouse","InvertY",0,path)!=0;
    fprintf(stderr,"[input] WASD keyboard profile enabled; click/F1 captures raw mouse; Escape pauses\n");
}
void haze_input_attach(void* hwnd) {
    if (!enabled || !hwnd) return;
    window=(HWND)hwnd;
    RAWINPUTDEVICE device{0x01,0x02,0,window};
    if (!RegisterRawInputDevices(&device,1,sizeof(device))) {
        fprintf(stderr,"[input] Raw mouse registration failed: %lu\n",GetLastError()); return;
    }
    original_proc=(WNDPROC)SetWindowLongPtr(window,GWLP_WNDPROC,(LONG_PTR)window_proc);
    if (!original_proc) fprintf(stderr,"[input] Window input hook failed: %lu\n",GetLastError());
    update_title();
}
bool haze_input_take_mouse(double& x, double& y, double& speed, bool& invert) {
    mouse_motion.take(x,y);
    speed=sensitivity; invert=invert_y;
    return enabled && captured && GetForegroundWindow()==window.load();
}
#elif defined(__EMSCRIPTEN__)
#include "cellPad.h"
#include "../external/ps3recomp/libs/codec/cellSail.h"
#include "haze_startup.h"
#include "win32_compat.h"
#include <emscripten/emscripten.h>
#include <atomic>
namespace {
std::atomic<uint32_t> browser_buttons{0}, browser_axes{0x80808080};
std::atomic<bool> browser_mouse_captured{false};
HazeMouseMotion browser_mouse_motion;
HazeStartupSkip browser_startup;
int browser_poll(u32 port, CellPadHostInput* input, void*) {
    if (port) return 0;
    const uint32_t buttons = browser_buttons.load();
    const uint32_t axes = browser_axes.load();
    *input = {(u16)buttons, (u8)axes, (u8)(axes >> 8), (u8)(axes >> 16), (u8)(axes >> 24),
              (u8)((buttons & CELL_PAD_CTRL_L2) ? 255 : 0), (u8)((buttons & CELL_PAD_CTRL_R2) ? 255 : 0)};
    bool pressed;
    if (browser_startup.sample(GetTickCount64(), pressed) && pressed) input->buttons |= CELL_PAD_CTRL_CROSS;
    return 1;
}
void browser_playback(u32 player, const char* uri, int started, void*) {
    browser_startup.playback(player, uri, started != 0, GetTickCount64());
}
}
extern "C" EMSCRIPTEN_KEEPALIVE void haze_wasm_input(uint32_t buttons, uint32_t axes) {
    browser_buttons.store(buttons); browser_axes.store(axes);
}
extern "C" EMSCRIPTEN_KEEPALIVE void haze_wasm_mouse(int32_t x, int32_t y, int captured) {
    if (browser_mouse_captured.exchange(captured != 0) != (captured != 0) || !captured)
        browser_mouse_motion.clear();
    if (captured) browser_mouse_motion.add(x,y);
}
void haze_input_initialize() {
    cellPad_set_host_input_callback(browser_poll, nullptr);
    cellSail_set_host_playback_callback(browser_playback, nullptr);
}
void haze_input_attach(void*) {}
bool haze_input_take_mouse(double& x,double& y,double& speed,bool& invert) {
    browser_mouse_motion.take(x,y);
    speed=0.10; invert=false;
    return browser_mouse_captured.load();
}
#else
void haze_input_initialize() {}
void haze_input_attach(void*) {}
bool haze_input_take_mouse(double&,double&,double&,bool&) { return false; }
#endif
