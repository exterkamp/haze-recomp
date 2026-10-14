#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
int haze_shader_prepare_vertex_source(const uint8_t* code, uint32_t bytes,
    uint32_t start, char* output, uint32_t capacity);
#ifdef __cplusplus
}
// Internal preparation worker mode; runs before graphics/guest initialization.
int haze_shader_prepare_cli();
#endif
