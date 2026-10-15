#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct HazeArchiveVertexProgram {
    // Numeric RSX VP words in little-endian host order, as consumed by the SDK
    // decompiler. The archive stores these words big-endian.
    std::vector<uint8_t> code;
    uint32_t start = 0;
};

struct HazeArchiveFragmentProgram {
    // Raw guest bytes from the Cg ucode section. Unlike VP words, the FP
    // decoder consumes this byte stream without endian conversion.
    std::vector<uint8_t> code;
    uint32_t instruction_count = 0;
    uint32_t input_mask = 0;
    uint32_t partial_texture_type = 0;
    uint16_t texture_coord_mask = 0;
    uint16_t texture_coord2d_mask = 0;
    uint16_t centroid_mask = 0;
    uint16_t sampler_mask = 0;
    uint16_t cube_mask = 0;
    uint16_t unsupported_sampler_mask = 0;
    uint8_t register_count = 0;
    uint8_t output_from_h0 = 0;
    uint8_t depth_replace = 0;
    uint8_t kill = 0;
    bool unsupported_sampler_metadata = false;
};

struct HazeShaderArchiveStats {
    uint64_t entries = 0;
    uint64_t vertex_programs = 0;
    uint64_t fragment_programs = 0;
    uint64_t skipped_large = 0;
    uint64_t unsupported_decode = 0;
    uint64_t invalid_shader = 0;
};

// Read-only scan of a version-2 PBCK archive. The visitor is called once for
// each valid profile-7003 vertex program; returning false stops the scan and
// returns success. No archive or extracted shader data is written.
bool haze_visit_archive_vertex_programs(
    const std::filesystem::path& pak,
    const std::function<bool(const HazeArchiveVertexProgram&)>& visitor,
    HazeShaderArchiveStats& stats,
    std::string& error);

// Read-only scan of valid profile-7004 fragment programs. Sampler masks are
// derived only from referenced Cg parameters with recognized types (1066=2D,
// 1069=cube); unsupported sampler metadata is reported on the program.
bool haze_visit_archive_fragment_programs(
    const std::filesystem::path& pak,
    const std::function<bool(const HazeArchiveFragmentProgram&)>& visitor,
    HazeShaderArchiveStats& stats,
    std::string& error);

