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

struct HazeShaderArchiveStats {
    uint64_t entries = 0;
    uint64_t vertex_programs = 0;
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

