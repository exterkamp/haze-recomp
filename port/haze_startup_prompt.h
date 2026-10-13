#pragma once
#include <windows.h>
#include <algorithm>
#include <string>

// Window-thread-only hint. An owned, nonactivating window remains readable
// above the swap chain, without taking keyboard focus or consuming a mouse click.
class HazeStartupPrompt {
    HWND hint=nullptr;
    HFONT font=nullptr;
    int height=0;
    std::string text;
    static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
        if (msg==WM_NCHITTEST) return HTTRANSPARENT;
        if (msg==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        if (msg==WM_PAINT) {
            PAINTSTRUCT paint;
            HDC dc=BeginPaint(hwnd,&paint);
            RECT rect; GetClientRect(hwnd,&rect);
            FillRect(dc,&rect,(HBRUSH)GetStockObject(BLACK_BRUSH));
            const auto self=(HazeStartupPrompt*)GetWindowLongPtr(hwnd,GWLP_USERDATA);
            if (self) {
                auto old=SelectObject(dc,self->font);
                SetTextColor(dc,RGB(255,255,255)); SetBkMode(dc,TRANSPARENT);
                DrawTextA(dc,self->text.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
                SelectObject(dc,old);
            }
            EndPaint(hwnd,&paint); return 0;
        }
        return DefWindowProc(hwnd,msg,wp,lp);
    }
public:
    void update(HWND owner,bool visible,const std::string& message) {
        if (!visible || !IsWindowVisible(owner) || IsIconic(owner)) {
            if (hint) ShowWindow(hint,SW_HIDE);
            return;
        }
        if (!hint) {
            WNDCLASSA cls{};
            cls.lpfnWndProc=proc; cls.hInstance=GetModuleHandle(nullptr);
            cls.lpszClassName="HazeStartupHint";
            RegisterClassA(&cls);
            hint=CreateWindowExA(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,"",
                                WS_POPUP,0,0,0,0,owner,nullptr,cls.hInstance,nullptr);
            if (!hint) return;
            SetWindowLongPtr(hint,GWLP_USERDATA,(LONG_PTR)this);
        }
        RECT rect; GetClientRect(owner,&rect);
        POINT origin{0,0}; ClientToScreen(owner,&origin);
        const int size=std::clamp(int(rect.bottom/36),14,64);
        bool changed=text!=message || height!=size;
        text=message;
        if (height!=size) {
            if (font) DeleteObject(font);
            height=size;
            font=CreateFontA(-size,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                             DEFAULT_PITCH,"Segoe UI");
        }
        SetWindowPos(hint,HWND_TOP,origin.x+rect.right/20,origin.y+rect.bottom-size*4,
                     rect.right*9/10,size*2,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        if (changed) InvalidateRect(hint,nullptr,FALSE);
    }
    void close() {
        if (hint) DestroyWindow(hint);
        if (font) DeleteObject(font);
        hint=nullptr; font=nullptr; height=0;
    }
};
