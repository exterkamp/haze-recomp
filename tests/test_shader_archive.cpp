#include "../port/haze_shader_archive.h"

#define STBI_ONLY_ZLIB
#define STBI_SUPPORT_ZLIB
#define STB_IMAGE_IMPLEMENTATION
#include "../external/ps3recomp/libs/codec/stb_image.h"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
void put_be32(std::vector<uint8_t>& data, size_t offset, uint32_t value) {
    assert(offset + 4 <= data.size());
    data[offset + 0] = uint8_t(value >> 24);
    data[offset + 1] = uint8_t(value >> 16);
    data[offset + 2] = uint8_t(value >> 8);
    data[offset + 3] = uint8_t(value);
}

void append_be32(std::vector<uint8_t>& data, uint32_t value) {
    data.push_back(uint8_t(value >> 24));
    data.push_back(uint8_t(value >> 16));
    data.push_back(uint8_t(value >> 8));
    data.push_back(uint8_t(value));
}

std::vector<uint8_t> make_cg(uint32_t start = 1) {
    // One 48-byte parameter record, one six-word program record, two 16-byte
    // instructions. The ucode starts on a 16-byte boundary.
    std::vector<uint8_t> cg(144, 0);
    put_be32(cg, 0, 7003);
    put_be32(cg, 4, 6);
    put_be32(cg, 8, uint32_t(cg.size()));
    put_be32(cg, 12, 1);
    put_be32(cg, 16, 32);
    put_be32(cg, 20, 80);
    put_be32(cg, 24, 32);
    put_be32(cg, 28, 112);
    put_be32(cg, 80, 2); // instruction count
    put_be32(cg, 84, start); // branch-base instruction slot
    put_be32(cg, 88, 8); // register count
    put_be32(cg, 92, 0x1234); // input mask
    put_be32(cg, 96, 0x5678); // output mask
    put_be32(cg, 100, 0x9abc); // clip mask
    for (size_t i = 0; i < 32; i += 4) {
        const uint32_t word = 0x10203040u + uint32_t(i);
        put_be32(cg, 112 + i, word);
    }
    return cg;
}

std::vector<uint8_t> zlib_stored(const std::vector<uint8_t>& raw) {
    assert(raw.size() <= 65535);
    const uint16_t len = uint16_t(raw.size());
    const uint16_t nlen = uint16_t(~len);
    std::vector<uint8_t> z{0x78, 0x01, 0x01,
                           uint8_t(len), uint8_t(len >> 8),
                           uint8_t(nlen), uint8_t(nlen >> 8)};
    z.insert(z.end(), raw.begin(), raw.end());
    uint32_t a = 1, b = 0;
    for (uint8_t value : raw) {
        a = (a + value) % 65521;
        b = (b + a) % 65521;
    }
    append_be32(z, (b << 16) | a);
    return z;
}

struct Entry { uint32_t decoded, compressed, offset, metadata; std::vector<uint8_t> bytes; };

void write_archive(const std::filesystem::path& path, const std::vector<Entry>& entries,
                   bool malformed_header = false) {
    constexpr uint32_t table = 24;
    const uint32_t auxiliary = table + uint32_t(entries.size()) * 20;
    std::vector<uint8_t> pak;
    append_be32(pak, malformed_header ? 0u : 0x5042434b);
    append_be32(pak, table);
    append_be32(pak, uint32_t(entries.size()));
    append_be32(pak, auxiliary);
    append_be32(pak, 0); // auxiliary table size
    append_be32(pak, 0x00010002); // one shard, version 2
    for (const auto& entry : entries) {
        append_be32(pak, 0x12345678);
        append_be32(pak, entry.decoded);
        append_be32(pak, entry.compressed);
        append_be32(pak, entry.offset);
        append_be32(pak, entry.metadata);
    }
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(pak.data()), std::streamsize(pak.size()));
    out.close();
    auto shard_path = path;
    shard_path += ".00";
    std::ofstream shard(shard_path, std::ios::binary);
    for (const auto& entry : entries) {
        if (entry.bytes.empty()) continue;
        shard.seekp(entry.offset);
        shard.write(reinterpret_cast<const char*>(entry.bytes.data()),
                    std::streamsize(entry.bytes.size()));
    }
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("haze_shader_archive_fixture_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ec;
    const bool created = std::filesystem::create_directory(root, ec);
    assert(created && !ec);
    if (!created || ec) return 2;
    const auto pak = root / L"fixture-\u6d4b\u8bd5.pak";
    const auto raw_cg = make_cg(100);
    const auto compressed_cg = zlib_stored(make_cg(0));
    const auto invalid_cg = [] {
        auto bad = make_cg();
        put_be32(bad, 84, 543); // two instructions would exceed the 544-slot file
        return bad;
    }();
    const std::vector<Entry> entries{
        {uint32_t(raw_cg.size()), 0, 0, 0, raw_cg},
        {uint32_t(make_cg(0).size()), uint32_t(compressed_cg.size()), 256, 0, compressed_cg},
        {uint32_t(invalid_cg.size()), 0, 512, 0, invalid_cg},
        {20 * 1024, 0, 0, 0, {}}, // skipped before shard I/O
        {uint32_t(raw_cg.size()), uint32_t(compressed_cg.size()), 768, 0, {0, 1, 2}}, // bad stream
        {8, 0, 1024, 0, {0, 0, 0, 1, 2, 3, 4, 5}} // non-Cg payload
    };
    write_archive(pak, entries);

    HazeShaderArchiveStats stats{};
    std::string error;
    std::vector<HazeArchiveVertexProgram> programs;
    assert(haze_visit_archive_vertex_programs(pak,
        [&](const HazeArchiveVertexProgram& program) { programs.push_back(program); return true; },
        stats, error));
    assert(error.empty());
    assert(stats.entries == entries.size());
    assert(stats.vertex_programs == 2 && programs.size() == 2);
    assert(stats.skipped_large == 1 && stats.unsupported_decode == 1 && stats.invalid_shader == 1);
    assert(programs[0].start == 100 && programs[1].start == 0);
    assert(programs[0].code.size() == 32);
    assert(programs[0].code[0] == 0x40 && programs[0].code[1] == 0x30 &&
           programs[0].code[2] == 0x20 && programs[0].code[3] == 0x10);

    size_t callbacks = 0;
    assert(haze_visit_archive_vertex_programs(pak,
        [&](const HazeArchiveVertexProgram&) { ++callbacks; return false; }, stats, error));
    assert(callbacks == 1 && stats.vertex_programs == 1);

    const auto bad_header = root / "bad-header.pak";
    write_archive(bad_header, {}, true);
    assert(!haze_visit_archive_vertex_programs(bad_header,
        [](const HazeArchiveVertexProgram&) { return true; }, stats, error));
    assert(!error.empty());

    const auto bad_cg_pak = root / "bad-cg.pak";
    auto bad_cg = make_cg();
    put_be32(bad_cg, 28, 0xfffffff0u); // invalid code bounds/alignment
    write_archive(bad_cg_pak, {{uint32_t(bad_cg.size()), 0, 0, 0, bad_cg}});
    assert(haze_visit_archive_vertex_programs(bad_cg_pak,
        [](const HazeArchiveVertexProgram&) { assert(false); return true; }, stats, error));
    assert(stats.vertex_programs == 0 && stats.invalid_shader == 1);

    const auto bad_shard_pak = root / "bad-shard.pak";
    write_archive(bad_shard_pak, {{uint32_t(raw_cg.size()), 0, 4096, 0, {}}});
    assert(!haze_visit_archive_vertex_programs(bad_shard_pak,
        [](const HazeArchiveVertexProgram&) { assert(false); return true; }, stats, error));
    assert(error.find("beyond shard") != std::string::npos);

    assert(std::filesystem::canonical(root).parent_path() ==
        std::filesystem::canonical(std::filesystem::temp_directory_path()));
    std::filesystem::remove_all(root, ec);
    return 0;
}

