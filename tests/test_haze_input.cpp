// Run the actual port mapper with controlled host keys, focus and mouse events.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <string>
static bool focused=true;
static bool keys[256];
static ULONGLONG ticks=100;
static SHORT test_key(int vk) { return keys[vk] ? (SHORT)0x8000 : 0; }
static HWND test_foreground() { return focused ? (HWND)1 : nullptr; }
static DWORD test_owner(HWND hwnd,DWORD* pid) { *pid=hwnd ? GetCurrentProcessId() : 0; return 1; }
static ULONGLONG test_time() { ticks+=8; return ticks; }
static UINT forwarded_message;
static std::string forwarded_caption;
static RAWINPUT raw_packet;
static UINT test_raw_input(HRAWINPUT,UINT,LPVOID data,PUINT size,UINT) {
    assert(*size==sizeof(raw_packet));
    memcpy(data,&raw_packet,sizeof(raw_packet)); return sizeof(raw_packet);
}
static LRESULT test_default_proc(HWND,UINT,WPARAM,LPARAM) { return 0; }
static LRESULT test_window_proc(WNDPROC,HWND,UINT msg,WPARAM,LPARAM lp) {
    forwarded_message=msg;
    if (msg==WM_SETTEXT) forwarded_caption=(const char*)lp;
    return 0;
}
#define GetAsyncKeyState test_key
#define GetForegroundWindow test_foreground
#define GetWindowThreadProcessId test_owner
#define GetTickCount64 test_time
#undef CallWindowProc
#define CallWindowProc test_window_proc
#define GetRawInputData test_raw_input
#undef DefWindowProc
#define DefWindowProc test_default_proc
#include "../port/haze_input.cpp"
extern "C" void cellPad_set_host_input_callback(CellPadHostInputCallback,void*) {}
extern "C" void cellSail_set_host_playback_callback(CellSailHostPlaybackCallback,void*) {}
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
    raw_packet.header.dwType=RIM_TYPEMOUSE;
    raw_packet.data.mouse.lLastX=8; raw_packet.data.mouse.lLastY=-8;
    window_proc(window,WM_INPUT,0,1);
    p=sample(); assert((p.buttons&CELL_PAD_CTRL_R1) && p.rx==128 && p.ry==128);
    sample(); // Pad polling cannot consume or convert camera motion.
    double dx,dy,speed; bool inverse;
    assert(haze_input_take_mouse(dx,dy,speed,inverse) && dx==8 && dy==-8);
    assert(haze_input_take_mouse(dx,dy,speed,inverse) && dx==0 && dy==0);
    raw_packet.data.mouse.usFlags=MOUSE_MOVE_ABSOLUTE;
    window_proc(window,WM_INPUT,0,1);
    assert(haze_input_take_mouse(dx,dy,speed,inverse) && dx==0 && dy==0);
    raw_packet.data.mouse.usFlags=0;
    keys[VK_RBUTTON]=true; keys[VK_CONTROL]=true; keys['R']=true;
    p=sample(); assert((p.buttons&CELL_PAD_CTRL_R3) && (p.buttons&CELL_PAD_CTRL_L3) &&
                       (p.buttons&CELL_PAD_CTRL_TRIANGLE));
    keys[VK_RBUTTON]=false; keys[VK_CONTROL]=false; keys['R']=false;
    p=sample(); assert(p.rx==128 && p.ry==128); // No residual camera drift.
    InterlockedExchange(&wheel,1);
    assert(sample().buttons&CELL_PAD_CTRL_CIRCLE);
    assert(!(sample().buttons&CELL_PAD_CTRL_CIRCLE));
    focused=false; mouse_motion.add(100,0);
    p=sample(); assert(!p.buttons && p.lx==128 && p.ly==128 && p.rx==128);
    focused=true; memset(keys,0,sizeof(keys));
    assert(sample().rx==128); // Focus loss discarded pending motion.
    assert(haze_input_take_mouse(dx,dy,speed,inverse) && dx==0 && dy==0);
    invert_y=true; mouse_motion.add(0,8);
    assert(sample().ry==128);
    assert(haze_input_take_mouse(dx,dy,speed,inverse) && dy==8 && inverse);
    CellPadHostInput other{}; assert(poll(1,&other,nullptr)==0);
    captured=0; invert_y=false;
    // A quick tap between pad polls survives repeated reads for 120ms.
    window_proc(window,WM_KEYDOWN,VK_SPACE,0);
    window_proc(window,WM_KEYUP,VK_SPACE,0);
    assert(sample().buttons&CELL_PAD_CTRL_CROSS);
    assert(sample().buttons&CELL_PAD_CTRL_CROSS);
    ticks+=121;
    assert(!(sample().buttons&CELL_PAD_CTRL_CROSS));
    window_proc(window,WM_KEYDOWN,'W',0);
    window_proc(window,WM_KILLFOCUS,0,0);
    assert(sample().ly==128); // Focus loss cannot leave a held message key.
    forwarded_message=0;
    window_proc(window,WM_KEYDOWN,VK_ESCAPE,0);
    assert(forwarded_message==0 && (sample().buttons&CELL_PAD_CTRL_START));
    window_proc(window,WM_KEYUP,VK_ESCAPE,0); ticks+=121;
    assert(!(sample().buttons&CELL_PAD_CTRL_START));
    window_proc(window,WM_SETTEXT,0,(LPARAM)"Haze | FPS: 30.00");
    assert(forwarded_caption.find("FPS: 30.00")!=std::string::npos &&
           forwarded_caption.find("Space: Confirm")!=std::string::npos);
    keys[VK_LBUTTON]=true; captured=1; capture_click=true;
    assert(!(sample().buttons&CELL_PAD_CTRL_R1));
    capture_click=false;
    assert(sample().buttons&CELL_PAD_CTRL_R1);
    memset(keys,0,sizeof(keys)); captured=0;
    invert_y=true; captured=1; mouse_motion.add(LONG_MIN,LONG_MAX);
    mouse_motion.add(LONG_MIN,LONG_MAX);
    assert(sample().ry==128);
    assert(haze_input_take_mouse(dx,dy,speed,inverse));
    assert(dx==double(LONG_MIN)*2 && dy==double(LONG_MAX)*2);
    // Every button can press and release, including all four face buttons.
    clear_keys(); ticks+=121;
    for (const auto& b : bindings) {
        keys[b.vk]=true;
        assert(sample().buttons & b.button);
        keys[b.vk]=false;
        assert(!(sample().buttons & b.button));
    }
    window_proc(window,WM_RBUTTONDOWN,0,0);
    window_proc(window,WM_RBUTTONUP,0,0);
    assert(sample().buttons&CELL_PAD_CTRL_R3);
    ticks+=121;
    assert(!(sample().buttons&CELL_PAD_CTRL_R3));
    captured=0; invert_y=false;
    // Lifecycle-bounded skip survives unfocused boot, then cannot confirm a menu.
    const char* uri="x-cell-fs:///dev_bdvd/PS3_GAME/USRDIR/assets/g5/pamf/frd.pam";
    assert(startup_skip.playback(123,uri,true,ticks));
    focused=false; ticks+=400;
    assert(sample().buttons&CELL_PAD_CTRL_CROSS);
    startup_skip.playback(123,uri,false,ticks);
    assert(!sample().buttons);
    enabled=false;
    CellPadHostInput controller{0,128,128,128,128,0,0};
    assert(poll(0,&controller,nullptr)==0); // XInput owns normal controller mode.
    startup_skip.playback(123,uri,true,ticks); ticks+=400;
    assert(poll(0,&controller,nullptr)==1 && (controller.buttons&CELL_PAD_CTRL_CROSS));
    startup_skip.playback(123,uri,false,ticks);
    assert(poll(0,&controller,nullptr)==0);
    HazeStartupSkip isolated; bool pressed=true;
    assert(!HazeStartupSkip::known_video(nullptr));
    assert(!HazeStartupSkip::known_video("frd.pam"));
    assert(!HazeStartupSkip::known_video("x-cell-fs:///dev_bdvd/PS3_GAME/USRDIR/assets/g5/pamf/story.pam"));
    assert(isolated.playback(123,uri,true,100));
    assert(isolated.sample(399,pressed) && !pressed);
    assert(isolated.sample(400,pressed) && pressed);
    assert(isolated.sample(800,pressed) && !pressed);
    isolated.playback(456,uri,false,900); // An unrelated player cannot cancel it.
    assert(isolated.sample(1100,pressed) && pressed);
    assert(!isolated.sample(10100,pressed) && !pressed); // Bounded failure path.
    isolated.playback(123,uri,true,20000);
    assert(!isolated.playback(123,"story.pam",true,20100));
    assert(!isolated.sample(20500,pressed) && !pressed);
    puts("Haze keyboard taps, focus release, mouse capture gating and movie skip isolation passed.");
}
