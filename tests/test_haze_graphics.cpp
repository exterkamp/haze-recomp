#include <cassert>
#include <cstdio>
#include <map>
#include <string>
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
    clear_options(); config("[graphics]\n"); assert(haze_graphics_initialize());
    for (const auto& o:rsx_graphics_options) assert(!std::strcmp(std::getenv(o.environment),o.fallback));
    clear_options(); _putenv_s("HAZE_GRAPHICS_CONFIG","local/nonexistent-graphics.ini");
    assert(!haze_graphics_initialize());
    std::remove("local/test-graphics.ini");
    std::puts("Graphics configuration: all options, defaults, overrides, BOM/CRLF, invalid values and atomic rejection passed.");
}
