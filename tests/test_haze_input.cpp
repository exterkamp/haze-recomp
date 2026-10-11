// Run the actual port mapper with controlled host keys, focus and mouse events.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
static bool focused=true;
static bool keys[256];
static ULONGLONG ticks=100;
static SHORT test_key(int vk) { return keys[vk] ? (SHORT)0x8000 : 0; }
static HWND test_foreground() { return focused ? (HWND)1 : nullptr; }
static DWORD test_owner(HWND hwnd,DWORD* pid) { *pid=hwnd ? GetCurrentProcessId() : 0; return 1; }
static ULONGLONG test_time() { ticks+=8; return ticks; }
#define GetAsyncKeyState test_key
#define GetForegroundWindow test_foreground
#define GetWindowThreadProcessId test_owner
#define GetTickCount64 test_time
#include "../port/haze_input.cpp"
extern "C" void cellPad_set_host_input_callback(CellPadHostInputCallback,void*) {}
static CellPadHostInput sample() {
    CellPadHostInput input{0,128,128,128,128,0,0};
    assert(poll(0,&input,nullptr)==1);
    return input;
}
int main() {
    window=(HWND)1;
    for (auto& b : bindings) b.vk=key_code(b.fallback);
    assert(key_code("w")=='W'); assert(key_code("Mouse2")==VK_RBUTTON);
    assert(sample().lx==128);
    keys['W']=keys['D']=keys[VK_SPACE]=true;
    auto p=sample(); assert(p.lx==255 && p.ly==1 && (p.buttons&CELL_PAD_CTRL_CROSS));
    keys['S']=keys['A']=true;
    p=sample(); assert(p.lx==128 && p.ly==128);
    keys[VK_LBUTTON]=true;
    assert(!(sample().buttons&CELL_PAD_CTRL_R1)); // A UI click does not fire.
    InterlockedExchange(&captured,1);
    InterlockedExchange(&mouse_x,8); InterlockedExchange(&mouse_y,-8);
    p=sample(); assert((p.buttons&CELL_PAD_CTRL_R1) && p.rx>128 && p.ry<128);
    p=sample(); assert(p.rx==128 && p.ry==128); // No residual camera drift.
    InterlockedExchange(&wheel,1);
    assert(sample().buttons&CELL_PAD_CTRL_CIRCLE);
    assert(!(sample().buttons&CELL_PAD_CTRL_CIRCLE));
    focused=false; InterlockedExchange(&mouse_x,100);
    p=sample(); assert(!p.buttons && p.lx==128 && p.ly==128 && p.rx==128);
    focused=true; memset(keys,0,sizeof(keys));
    assert(sample().rx==128); // Focus loss discarded pending motion.
    invert_y=true; InterlockedExchange(&mouse_y,8);
    assert(sample().ry<128);
    assert(mouse_axis(LONG_MAX,8)==255 && mouse_axis(LONG_MIN,8)==1);
    CellPadHostInput other{}; assert(poll(1,&other,nullptr)==0);
    puts("Haze keyboard mapping, raw-mouse consumption, focus release and capture gating checks passed.");
}
