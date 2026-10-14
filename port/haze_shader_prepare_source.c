#include "haze_shader_prepare.h"
#include "rsx_vp_decompiler.h"
#include "rsx_vertex_pull.h"

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
