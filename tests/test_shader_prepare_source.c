#include "../port/haze_shader_prepare.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void word(uint8_t* p, uint32_t value) {
    value = (value << 16) | (value >> 16);
    p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value;
}

int main(void) {
    HazePixelShaderVariant variants[6];
    assert(!haze_shader_prepare_pixel_variants(0, 1440, variants));
    assert(!haze_shader_prepare_pixel_variants(2560, 8193, variants));
    assert(!haze_shader_prepare_pixel_variants(2560, 1440, NULL));
    assert(haze_shader_prepare_pixel_variants(2561, 1441, variants));
    // Half-resolution targets round independently. Using the main surface's
    // ratio here produces a different WPOS shader at odd output dimensions.
    assert(variants[1].scale_x == 1024.0f / 2561.0f);
    assert(variants[2].scale_x == 512.0f / 1281.0f);
    assert(variants[2].scale_y == 288.0f / 721.0f);
    assert(variants[0].scale_x == 1.0f && variants[0].shader_window == 0);
    assert(variants[4].shader_window == (0x1000u | 576u));
    assert(variants[4].alpha_func == 0x206u);

    uint8_t code[32] = {0};
    // MOV r0, fragment position, END; unconditional identity swizzle.
    word(code, (1u << 24) | (15u << 9) | 1u);
    word(code + 4, 1u | (1u << 11) | (2u << 13) | (3u << 15) |
        (7u << 18) | (1u << 23) | (2u << 25) | (3u << 27));
    char source[16384], other[16384];
    variants[1].control = variants[2].control = 0x40;
    assert(haze_shader_prepare_pixel_source(code, 16, &variants[1], source, sizeof source));
    assert(haze_shader_prepare_pixel_source(code, 16, &variants[2], other, sizeof other));
    assert(strcmp(source, other));
    assert(strstr(source, "SV_TARGET"));
    assert(!haze_shader_prepare_pixel_source(NULL, 16, &variants[1], source, sizeof source));
    assert(!haze_shader_prepare_pixel_source(code, 15, &variants[1], source, sizeof source));
    assert(!haze_shader_prepare_pixel_source(code, 32, &variants[1], source, sizeof source));
    struct { char text[8]; char guard[8]; } small;
    memset(&small, 0x5a, sizeof small);
    assert(!haze_shader_prepare_pixel_source(code, 16, &variants[1], small.text, sizeof small.text));
    for (unsigned i = 0; i < sizeof small.guard; ++i) assert(small.guard[i] == 0x5a);
    puts("PASS native pixel preparation: extent rounding, source identity, code/output bounds");
    return 0;
}
