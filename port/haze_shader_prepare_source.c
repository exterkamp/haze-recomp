#include "haze_shader_prepare.h"
#include "rsx_vp_decompiler.h"
#include "rsx_vertex_pull.h"
#include "rsx_fp_decompiler.h"
#include "rsx_render_resolution.h"

int haze_shader_prepare_vertex_source(const uint8_t* code, uint32_t bytes,
    uint32_t start, char* output, uint32_t capacity)
{
    const u32 count = rsx_vp_program_size_instrs(code, bytes);
    if (!count) return 0;
    rsx_vp_input_analysis analysis;
    u32 mask = 0xFFFFu;
    if (rsx_vp_analyze_inputs(code, count * 16, &analysis) == (int)count && analysis.exact)
        mask = analysis.input_mask;
    /* Exact current default: compact inputs, GPU pulling, no bound vertex
     * textures. Other runtime variants retain their ordinary cache fallback.
     * This worker runs alone: the SDK branch-base state is not thread-local. */
    rsx_vp_set_branch_base(start);
    const int result = rsx_vp_decompile_compact_ex(code, count * 16, 0, mask, output, capacity);
    return result > 0 && rsx_vertex_pull_wrap(output, capacity, mask);
}

int haze_shader_prepare_pixel_source(const uint8_t* code, uint32_t bytes,
    const HazePixelShaderVariant* variant, char* output, uint32_t capacity)
{
    if (!variant || !code || !output || !capacity || !bytes || (bytes & 15u) ||
        rsx_fp_program_size(code, bytes) != bytes) return 0;
    u32 constants = 0;
    const int result = rsx_fp_decompile_shadow_ex(code, bytes, variant->control,
        variant->cube_mask, variant->shadow_mask, 1, output, capacity, &constants);
    if (result <= 0) return 0;
    if (variant->alpha_func &&
        rsx_fp_apply_alpha_test_buffered(output, capacity, variant->alpha_func) < 0) return 0;
    return rsx_fp_apply_window_position_scaled(output, capacity, variant->shader_window,
        variant->reciprocal_w, variant->scale_x, variant->scale_y) >= 0;
}

int haze_shader_prepare_pixel_variants(uint32_t host_width, uint32_t host_height,
    HazePixelShaderVariant variants[6])
{
    if (!variants || !host_width || !host_height || host_width > 8192 || host_height > 8192)
        return 0;
    rsx_render_resolution resolution = {1024, 576, host_width, host_height, 1024, 576, 1, 0, 0};
    for (unsigned alpha = 0; alpha < 2; ++alpha) {
        for (unsigned surface = 0; surface < 3; ++surface) {
            HazePixelShaderVariant variant = {0};
            variant.alpha_func = alpha ? 0x206u : 0u;
            variant.scale_x = variant.scale_y = 1.0f;
            if (surface) {
                const u32 width = surface == 1 ? 1024u : 512u;
                const u32 height = surface == 1 ? 576u : 288u;
                u32 actual_width, actual_height;
                rsx_render_surface_extent(&resolution, width, height, &actual_width, &actual_height);
                variant.shader_window = 0x1000u | height;
                variant.scale_x = (float)width / actual_width;
                variant.scale_y = (float)height / actual_height;
            }
            variants[alpha * 3 + surface] = variant;
        }
    }
    return 1;
}
