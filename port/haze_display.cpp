#include "haze_display.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "rsx_render_resolution.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
namespace {
BOOL CALLBACK collect_monitor(HMONITOR monitor,HDC,LPRECT,LPARAM data) {
    MONITORINFOEXA info{}; info.cbSize=sizeof(info);
    if (GetMonitorInfoA(monitor,&info))
        reinterpret_cast<std::vector<MONITORINFOEXA>*>(data)->push_back(info);
    return TRUE;
}
}
void haze_display_prepare() {
    // The renderer/window thread must use physical monitor pixels, including
    // when other monitors use a different desktop scaling percentage.
    using SetContext=DPI_AWARENESS_CONTEXT (WINAPI*)(DPI_AWARENESS_CONTEXT);
    auto set_context=reinterpret_cast<SetContext>(GetProcAddress(GetModuleHandleA("user32.dll"),"SetThreadDpiAwarenessContext"));
    if (set_context) set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}
bool haze_display_apply(void* window) {
    HWND hwnd=static_cast<HWND>(window);
    if (!IsWindow(hwnd)) return false;
    std::vector<MONITORINFOEXA> monitors;
    if (!EnumDisplayMonitors(nullptr,nullptr,collect_monitor,reinterpret_cast<LPARAM>(&monitors)) || monitors.empty()) {
        std::fprintf(stderr,"[display] Cannot enumerate monitors\n"); return false;
    }
    std::sort(monitors.begin(),monitors.end(),[](const auto& a,const auto& b){return std::strcmp(a.szDevice,b.szDevice)<0;});
    size_t selected=0;
    const char* choice=std::getenv("RSX_MONITOR");
    if (!choice || !std::strcmp(choice,"primary")) {
        for(size_t i=0;i<monitors.size();++i) if(monitors[i].dwFlags&MONITORINFOF_PRIMARY) selected=i;
    } else {
        const int number=std::atoi(choice);
        if(number<1 || static_cast<size_t>(number)>monitors.size()) {
            std::fprintf(stderr,"[display] Monitor %s unavailable (%zu connected)\n",choice,monitors.size()); return false;
        }
        selected=number-1;
    }
    for(size_t i=0;i<monitors.size();++i)
        std::fprintf(stderr,"[display] monitor %zu: %s %ldx%ld%s\n",i+1,monitors[i].szDevice,
            monitors[i].rcMonitor.right-monitors[i].rcMonitor.left,
            monitors[i].rcMonitor.bottom-monitors[i].rcMonitor.top,
            monitors[i].dwFlags&MONITORINFOF_PRIMARY ? " primary":"");
    const auto& monitor=monitors[selected];
    const char* mode=std::getenv("RSX_WINDOW_MODE"); if(!mode) mode="windowed";
    const bool decorated=!std::strcmp(mode,"windowed");
    const LONG_PTR style=decorated ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    RECT target=monitor.rcMonitor;
    if(decorated) {
        RECT previous{}; GetWindowRect(hwnd,&previous);
        if(!SetWindowPos(hwnd,nullptr,
            (monitor.rcMonitor.left+monitor.rcMonitor.right-(previous.right-previous.left))/2,
            (monitor.rcMonitor.top+monitor.rcMonitor.bottom-(previous.bottom-previous.top))/2,
            0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE)) return false;
        rsx_render_resolution size;
        if(rsx_render_resolution_parse(&size,1280,720,std::getenv("RSX_WINDOW_SIZE"))<0) return false;
        RECT frame{0,0,static_cast<LONG>(size.host_w),static_cast<LONG>(size.host_h)};
        using GetDpi=UINT (WINAPI*)(HWND);
        using AdjustDpi=BOOL (WINAPI*)(LPRECT,DWORD,BOOL,DWORD,UINT);
        auto user=GetModuleHandleA("user32.dll");
        auto get_dpi=reinterpret_cast<GetDpi>(GetProcAddress(user,"GetDpiForWindow"));
        auto adjust_dpi=reinterpret_cast<AdjustDpi>(GetProcAddress(user,"AdjustWindowRectExForDpi"));
        if(!(get_dpi && adjust_dpi ? adjust_dpi(&frame,static_cast<DWORD>(style),FALSE,0,get_dpi(hwnd)) :
            AdjustWindowRectEx(&frame,static_cast<DWORD>(style),FALSE,0))) return false;
        // Keep the title bar reachable when a requested client exceeds the
        // work area; client size is still exactly the requested pixel size.
        const LONG w=frame.right-frame.left,h=frame.bottom-frame.top;
        target.left=monitor.rcWork.left+(std::max)(0L,(monitor.rcWork.right-monitor.rcWork.left-w)/2);
        target.top=monitor.rcWork.top+(std::max)(0L,(monitor.rcWork.bottom-monitor.rcWork.top-h)/2);
        target.right=target.left+w; target.bottom=target.top+h;
    }
    SetLastError(0);
    if(!SetWindowLongPtrA(hwnd,GWL_STYLE,style|WS_VISIBLE) && GetLastError()) return false;
    if(!SetWindowPos(hwnd,HWND_NOTOPMOST,target.left,target.top,target.right-target.left,target.bottom-target.top,
        SWP_FRAMECHANGED|SWP_NOACTIVATE|SWP_SHOWWINDOW)) return false;
    if(!decorated) { ShowWindow(hwnd,SW_SHOW); SetForegroundWindow(hwnd); }
    RECT client{}; GetClientRect(hwnd,&client);
    std::fprintf(stderr,"[display] mode=%s monitor=%zu client=%ldx%ld\n",mode,selected+1,client.right,client.bottom);
    return true;
}
#else
void haze_display_prepare() {}
bool haze_display_apply(void*) { return true; }
#endif
