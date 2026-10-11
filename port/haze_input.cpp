#include "haze_input.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "cellPad.h"
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
volatile LONG captured, mouse_x, mouse_y, wheel;
POINT old_cursor;
bool enabled = true;
double sensitivity = 0.20;
int deadzone = 24;
bool invert_y;
struct Binding { const char* name; const char* fallback; u16 button; int vk; };
Binding bindings[] = {
    {"Cross", "Space", CELL_PAD_CTRL_CROSS},
    {"Circle", "X", CELL_PAD_CTRL_CIRCLE},
    {"Square", "E", CELL_PAD_CTRL_SQUARE},
    {"Triangle", "C", CELL_PAD_CTRL_TRIANGLE},
    {"L1", "G", CELL_PAD_CTRL_L1}, {"R1", "Mouse1", CELL_PAD_CTRL_R1},
    {"L2", "Q", CELL_PAD_CTRL_L2}, {"R2", "F", CELL_PAD_CTRL_R2},
    {"L3", "Mouse2", CELL_PAD_CTRL_L3}, {"R3", "R", CELL_PAD_CTRL_R3},
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
bool down(int vk) { return vk && (GetAsyncKeyState(vk) & 0x8000); }
void clip_cursor() {
    RECT rect;
    GetClientRect(window,&rect);
    MapWindowPoints(window,nullptr,(POINT*)&rect,2);
    ClipCursor(&rect);
}
void capture(bool on) {
    if (on == (InterlockedCompareExchange(&captured,0,0) != 0)) return;
    if (on && GetForegroundWindow() != window) return;
    InterlockedExchange(&mouse_x,0); InterlockedExchange(&mouse_y,0);
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
    InterlockedExchange(&captured,on ? 1 : 0);
    fprintf(stderr,"[input] Mouse %s (F1 toggles capture)\n",on ? "captured" : "released");
}
LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wp == VK_F1) { if (!(lp & (1ll<<30))) capture(!captured); return 0; }
        if (wp == VK_ESCAPE) { capture(false); return 0; } // Pause, rather than SDK's quit.
        break;
    case WM_KILLFOCUS: case WM_CLOSE: case WM_DESTROY:
        capture(false); break;
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
            InterlockedExchangeAdd(&mouse_x,input.data.mouse.lLastX);
            InterlockedExchangeAdd(&mouse_y,input.data.mouse.lLastY);
        }
        // DefWindowProc must see foreground raw input for system cleanup.
        return DefWindowProc(hwnd,msg,wp,lp);
    }
    }
    return CallWindowProc(original_proc,hwnd,msg,wp,lp);
}
u8 mouse_axis(LONG delta, double ms) {
    if (!delta) return 128;
    double amount = std::min(127.0,deadzone + std::abs((double)delta)*1000.0*sensitivity/ms);
    return (u8)std::clamp(128 + (delta < 0 ? -(int)amount : (int)amount),0,255);
}
int poll(u32 port, CellPadHostInput* input, void*) {
    if (port) return 0;
    static std::atomic<ULONGLONG> previous;
    ULONGLONG now = GetTickCount64();
    double elapsed = (double)std::clamp<ULONGLONG>(now-previous.exchange(now),1,50);
    HWND foreground=GetForegroundWindow();
    if (!foreground || foreground != window.load()) {
        InterlockedExchange(&mouse_x,0); InterlockedExchange(&mouse_y,0);
        InterlockedExchange(&wheel,0);
        return 1; // All controls neutral immediately on focus loss.
    }
    for (const auto& b : bindings) {
        if (b.vk >= VK_LBUTTON && b.vk <= VK_XBUTTON2 && !captured) continue;
        if (down(b.vk)) input->buttons |= b.button;
    }
    // Escape always pauses and releases mouse capture, even with Start rebound.
    if (down(VK_ESCAPE)) input->buttons |= CELL_PAD_CTRL_START;
    input->lx = (u8)(128 + 127*((int)down(movement[3])-(int)down(movement[2])));
    input->ly = (u8)(128 + 127*((int)down(movement[1])-(int)down(movement[0])));
    if (captured) {
        input->rx=mouse_axis(InterlockedExchange(&mouse_x,0),elapsed);
        LONG dy=InterlockedExchange(&mouse_y,0);
        input->ry=mouse_axis(invert_y ? -dy : dy,elapsed);
        if (InterlockedExchange(&wheel,0)) input->buttons |= CELL_PAD_CTRL_CIRCLE;
    }
    input->l2=(input->buttons & CELL_PAD_CTRL_L2) ? 255 : 0;
    input->r2=(input->buttons & CELL_PAD_CTRL_R2) ? 255 : 0;
    return 1;
}
}
void haze_input_initialize() {
    const char* mode=getenv("HAZE_INPUT");
    enabled=!(mode && !_stricmp(mode,"controller"));
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
    GetPrivateProfileStringA("Mouse","Sensitivity","0.20",value,sizeof(value),path);
    double requested=strtod(value,nullptr);
    if (std::isfinite(requested) && requested > 0 && requested <= 10) sensitivity=requested;
    deadzone=(int)std::min(126u,GetPrivateProfileIntA("Mouse","StickDeadzone",24,path));
    invert_y=GetPrivateProfileIntA("Mouse","InvertY",0,path)!=0;
    cellPad_set_host_input_callback(poll,nullptr);
    fprintf(stderr,"[input] WASD keyboard profile enabled; F1 captures/releases raw mouse; Escape pauses\n");
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
}
#else
void haze_input_initialize() {}
void haze_input_attach(void*) {}
#endif
