#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
int haze_shader_prepare_vertex_source(const uint8_t* code, uint32_t bytes,
    uint32_t start, char* output, uint32_t capacity);
typedef struct HazePixelShaderVariant {
    uint32_t control, cube_mask, shadow_mask, alpha_func, shader_window;
    int reciprocal_w;
    float scale_x, scale_y;
} HazePixelShaderVariant;
int haze_shader_prepare_pixel_source(const uint8_t* code, uint32_t bytes,
    const HazePixelShaderVariant* variant, char* output, uint32_t capacity);
// Common full/half-resolution scene variants. Other states still use the
// renderer's exact-source cache/fallback; this does not promise full coverage.
int haze_shader_prepare_pixel_variants(uint32_t host_width, uint32_t host_height,
    HazePixelShaderVariant variants[6]);
#ifdef __cplusplus
}
// Internal preparation worker mode; runs before graphics/guest initialization.
int haze_shader_prepare_cli();
#endif
