#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include "../port/haze_display.cpp"
#include "../port/haze_graphics.cpp"
static void clear_options() {
    for (const auto& o:rsx_graphics_options) _putenv_s(o.environment,"");
}
static void config(const char* text) {
    FILE* f=std::fopen("local/test-graphics.ini","wb"); assert(f);
    std::fputs(text,f); std::fclose(f);
}
int main() {
    clear_options(); _putenv_s("HAZE_GRAPHICS_CONFIG","local/test-graphics.ini");
    config("\xef\xbb\xbf[graphics]\r\nrender_resolution=1777x997\nanti_aliasing=fxaa\nanisotropic_filtering=16\nshadow_resolution=4096\nmip_lod_bias=-0.5\nsharpening=0.3\nexposure=0.5\nvsync=on\nframe_limit=60\n");
    _putenv_s("RSX_MAX_FPS","0"); assert(haze_graphics_initialize());
    assert(!std::strcmp(std::getenv("RSX_MAX_FPS"),"0"));
    assert(!std::strcmp(std::getenv("RSX_RENDER_RESOLUTION"),"1777x997"));
    assert(!std::strcmp(std::getenv("RSX_ANISOTROPY"),"16"));
    const char* bad[]={"[graphics]\nframe_limit=60\nsharpening=nan\n",
        "[graphics]\nframe_limit=60\nframe_limit=30\n", "[unknown]\nframe_limit=60\n",
        "[graphics]\ndraw_distance=100\n", "[graphics]\nanti_aliasing=msaa\n",
        "[graphics]\nanisotropic_filtering=3\n", "[graphics]\nshadow_resolution=4095\n",
        "[graphics]\nrender_resolution=8193x720\n", "[graphics]\nexposure=inf\n",
        "[graphics]\nframe_limit=-1\n", "[graphics]\nmip_lod_bias=3\n",
        "[graphics]\ndisplay_mode=exclusive\n", "[graphics]\nwindow_size=native\n",
        "[graphics]\nmonitor=0\n", "[graphics]\nmonitor=1.5\n"};
    for (auto text:bad) {
        clear_options(); config(text); assert(!haze_graphics_initialize());
        assert(!std::getenv("RSX_MAX_FPS")); // Failed file never partially applies.
    }
    clear_options(); config("[graphics]\nframe_limit=60\n");
    _putenv_s("RSX_SHARPENING","2"); assert(!haze_graphics_initialize());
    assert(!std::getenv("RSX_MAX_FPS"));
    haze_display_defaults detected{}; assert(haze_display_detect("primary",detected));
    auto dimensions=[](unsigned w,unsigned h) { return std::to_string(w)+"x"+std::to_string(h); };
    auto check_defaults=[&](const char* monitor="primary") {
        assert(std::getenv("RSX_RENDER_RESOLUTION")==dimensions(detected.render_width,detected.render_height));
        assert(std::getenv("RSX_WINDOW_SIZE")==dimensions(detected.window_width,detected.window_height));
        assert(!std::strcmp(std::getenv("RSX_SHADOW_RESOLUTION"),"native"));
        assert(!std::strcmp(std::getenv("RSX_WINDOW_MODE"),"borderless"));
        for (const auto& o:rsx_graphics_options)
            if(!automatic_size(o)) assert(!std::strcmp(std::getenv(o.environment),!std::strcmp(o.key,"monitor") ? monitor : fallback(o)));
    };
    clear_options(); config("[graphics]\n"); assert(haze_graphics_initialize()); check_defaults();
    clear_options(); config("[graphics]\nrender_resolution=auto\nwindow_size=auto\nmonitor=1\n");
    assert(haze_display_detect("1",detected)); assert(haze_graphics_initialize()); check_defaults("1");
    clear_options(); config("[graphics]\nrender_resolution=auto\nwindow_size=auto\n");
    _putenv_s("RSX_RENDER_RESOLUTION","1920x1080"); _putenv_s("RSX_WINDOW_SIZE","960x540");
    assert(haze_graphics_initialize());
    assert(!std::strcmp(std::getenv("RSX_RENDER_RESOLUTION"),"1920x1080"));
    assert(!std::strcmp(std::getenv("RSX_WINDOW_SIZE"),"960x540"));
    clear_options(); config("[graphics]\nrender_resolution=native\nwindow_size=800x450\n");
    _putenv_s("RSX_RENDER_RESOLUTION","auto"); _putenv_s("RSX_WINDOW_SIZE","auto");
    assert(haze_display_detect("primary",detected)); assert(haze_graphics_initialize()); check_defaults();
    clear_options(); config("[graphics]\nrender_resolution=automatic\n"); assert(!haze_graphics_initialize());
    assert(!std::getenv("RSX_MAX_FPS"));
    clear_options(); config("[graphics]\nmonitor=16\n");
    haze_display_defaults unused{};
    if(!haze_display_detect("16",unused)) {
        assert(!haze_graphics_initialize()); assert(!std::getenv("RSX_MAX_FPS"));
    }
    // An absent optional config uses the same defaults as an empty INI.
    clear_options(); _putenv_s("HAZE_GRAPHICS_CONFIG","");
    char cwd[MAX_PATH]; assert(GetCurrentDirectoryA(sizeof(cwd),cwd));
    assert(SetCurrentDirectoryA("local")); assert(haze_graphics_initialize()); check_defaults();
    assert(SetCurrentDirectoryA(cwd));
    clear_options(); _putenv_s("HAZE_GRAPHICS_CONFIG","local/nonexistent-graphics.ini");
    assert(!haze_graphics_initialize());
    std::remove("local/test-graphics.ini");
    std::puts("Graphics configuration: INI, automatic monitor defaults, missing optional file, overrides, BOM/CRLF and atomic rejection passed.");
}
