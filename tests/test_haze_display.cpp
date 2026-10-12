#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include "../port/haze_display.cpp"
int main() {
    haze_display_prepare();
    WNDCLASSA wc{}; wc.lpfnWndProc=DefWindowProcA; wc.hInstance=GetModuleHandleA(nullptr);
    wc.lpszClassName="HazeDisplayTest"; assert(RegisterClassA(&wc));
    HWND hwnd=CreateWindowA(wc.lpszClassName,"Haze display test",WS_OVERLAPPEDWINDOW,
        0,0,400,300,nullptr,nullptr,wc.hInstance,nullptr); assert(hwnd);
    _putenv_s("RSX_WINDOW_MODE","windowed"); _putenv_s("RSX_WINDOW_SIZE","641x359");
    _putenv_s("RSX_MONITOR","primary"); assert(haze_display_apply(hwnd));
    RECT client{}; GetClientRect(hwnd,&client); assert(client.right==641 && client.bottom==359);
    assert(GetWindowLongPtrA(hwnd,GWL_STYLE)&WS_CAPTION);
    std::vector<MONITORINFOEXA> monitors;
    assert(EnumDisplayMonitors(nullptr,nullptr,collect_monitor,reinterpret_cast<LPARAM>(&monitors)));
    for(size_t i=0;i<monitors.size();++i) {
        char index[16]; std::snprintf(index,sizeof(index),"%zu",i+1); _putenv_s("RSX_MONITOR",index);
        haze_display_defaults detected{}; assert(haze_display_detect(index,detected));
        char size[32]; std::snprintf(size,sizeof(size),"%ux%u",detected.window_width,detected.window_height);
        _putenv_s("RSX_WINDOW_MODE","windowed"); _putenv_s("RSX_WINDOW_SIZE",size);
        assert(haze_display_apply(hwnd));
        RECT auto_frame{}; GetWindowRect(hwnd,&auto_frame);
        MONITORINFO auto_monitor{sizeof(auto_monitor)};
        assert(GetMonitorInfoA(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONULL),&auto_monitor));
        assert(detected.render_width==(std::min)(8192u,static_cast<unsigned>(auto_monitor.rcMonitor.right-auto_monitor.rcMonitor.left)));
        assert(detected.render_height==(std::min)(8192u,static_cast<unsigned>(auto_monitor.rcMonitor.bottom-auto_monitor.rcMonitor.top)));
        GetClientRect(hwnd,&client);
        assert(client.right==detected.window_width && client.bottom==detected.window_height);
        assert(auto_frame.left>=auto_monitor.rcWork.left && auto_frame.top>=auto_monitor.rcWork.top);
        assert(auto_frame.right<=auto_monitor.rcWork.right && auto_frame.bottom<=auto_monitor.rcWork.bottom);
        _putenv_s("RSX_WINDOW_MODE","borderless"); assert(haze_display_apply(hwnd));
        RECT frame{}; GetWindowRect(hwnd,&frame);
        MONITORINFO actual{sizeof(actual)}; assert(GetMonitorInfoA(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONULL),&actual));
        assert(EqualRect(&frame,&actual.rcMonitor));
        GetClientRect(hwnd,&client);
        assert(client.right==frame.right-frame.left && client.bottom==frame.bottom-frame.top);
        assert(!(GetWindowLongPtrA(hwnd,GWL_STYLE)&WS_CAPTION));
        assert(!(GetWindowLongPtrA(hwnd,GWL_EXSTYLE)&WS_EX_TOPMOST));
    }
    _putenv_s("RSX_WINDOW_MODE","fullscreen"); _putenv_s("RSX_MONITOR","primary");
    assert(haze_display_apply(hwnd)); assert(!(GetWindowLongPtrA(hwnd,GWL_STYLE)&WS_CAPTION));
    _putenv_s("RSX_WINDOW_MODE","windowed"); _putenv_s("RSX_WINDOW_SIZE","960x540");
    assert(haze_display_apply(hwnd)); GetClientRect(hwnd,&client);
    assert(client.right==960 && client.bottom==540 && (GetWindowLongPtrA(hwnd,GWL_STYLE)&WS_CAPTION));
    _putenv_s("RSX_MONITOR","16"); if(monitors.size()<16) assert(!haze_display_apply(hwnd));
    assert(!haze_display_apply(nullptr));
    unsigned w=0,h=0;
    fit_window(640,480,16,39,w,h); assert(w==624 && h==351);
    fit_window(1920,1080,16,39,w,h); assert(w==1280 && h==720);
    fit_window(800,400,24,60,w,h); assert(w<=776 && h<=340);
    DestroyWindow(hwnd); UnregisterClassA(wc.lpszClassName,wc.hInstance);
    std::puts("Display windows: exact client pixels, each monitor's borderless bounds, fullscreen placement, windowed restoration and missing-monitor rejection passed.");
}
