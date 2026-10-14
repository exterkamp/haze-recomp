#include "haze_shader_archive.h"

#include <array>
#include <fstream>
#include <limits>
#include <memory>
#include <system_error>

extern "C" int stbi_zlib_decode_buffer(char* output, int output_size,
                                         const char* input, int input_size);

namespace {

constexpr uint32_t kPbckMagic = 0x5042434b;
constexpr uint32_t kCgVertexProfile = 7003;
constexpr uint32_t kCgRevision = 6;
constexpr uint64_t kEntryLimit = 16 * 1024;
constexpr uint64_t kCompressedLimit = 64 * 1024;
constexpr uint64_t kPbckHeaderSize = 24;
constexpr uint64_t kPbckEntrySize = 20;
constexpr uint64_t kMaxPbckEntries = 1'000'000;
constexpr uint64_t kMaxPbckIndexBytes = 32 * 1024 * 1024;
constexpr uint64_t kCgHeaderSize = 8 * 4;
constexpr uint64_t kCgParameterSize = 48;
constexpr uint64_t kCgProgramSize = 24;

uint32_t read_be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

bool range_fits(uint64_t offset, uint64_t size, uint64_t limit) {
    return offset <= limit && size <= limit - offset;
}

bool overlaps(uint64_t a, uint64_t a_size, uint64_t b, uint64_t b_size) {
    return a_size && b_size && a < b + b_size && b < a + a_size;
}

struct CgView {
    uint32_t instruction_count;
    uint32_t start;
    uint32_t ucode_offset;
    uint32_t ucode_size;
};

bool parse_vertex_program(const std::vector<uint8_t>& bytes, CgView& out) {
    if (bytes.size() < kCgHeaderSize || read_be32(bytes.data()) != kCgVertexProfile)
        return false;

    const uint32_t revision = read_be32(bytes.data() + 4);
    const uint32_t total_size = read_be32(bytes.data() + 8);
    const uint32_t parameter_count = read_be32(bytes.data() + 12);
    const uint32_t parameter_offset = read_be32(bytes.data() + 16);
    const uint32_t program_offset = read_be32(bytes.data() + 20);
    const uint32_t ucode_size = read_be32(bytes.data() + 24);
    const uint32_t ucode_offset = read_be32(bytes.data() + 28);
    const uint64_t total = bytes.size();

    if (revision != kCgRevision || total_size != total)
        return false;
    if (parameter_count > std::numeric_limits<uint64_t>::max() / kCgParameterSize)
        return false;
    const uint64_t parameter_bytes = uint64_t(parameter_count) * kCgParameterSize;
    if (parameter_count) {
        if (parameter_offset < kCgHeaderSize ||
            !range_fits(parameter_offset, parameter_bytes, total))
            return false;
    } else if (parameter_offset > total) {
        return false;
    }
    if (program_offset < kCgHeaderSize ||
        !range_fits(program_offset, kCgProgramSize, total))
        return false;
    if (ucode_offset < kCgHeaderSize || (ucode_offset & 15u) ||
        !ucode_size || (ucode_size & 15u) ||
        !range_fits(ucode_offset, ucode_size, total))
        return false;

    // Program metadata: instruction count, entry instruction slot, register
    // count, input/output masks, and clip mask. The second word is a VP branch
    // base, not a file offset.
    const uint8_t* program = bytes.data() + program_offset;
    const uint32_t instruction_count = read_be32(program + 0);
    const uint32_t start = read_be32(program + 4);
    if (!instruction_count || instruction_count > 512 ||
        start >= 544 || instruction_count > 544 - start ||
        uint64_t(instruction_count) * 16 != ucode_size)
        return false;

    if (overlaps(program_offset, kCgProgramSize, ucode_offset, ucode_size) ||
        (parameter_count &&
         (overlaps(parameter_offset, parameter_bytes, program_offset, kCgProgramSize) ||
          overlaps(parameter_offset, parameter_bytes, ucode_offset, ucode_size))))
        return false;

    out = {instruction_count, start, ucode_offset, ucode_size};
    return true;
}

std::filesystem::path shard_name(const std::filesystem::path& pak, unsigned shard) {
    auto result = pak;
    result += ".";
    if (shard < 10) result += "0";
    result += std::to_string(shard);
    return result;
}

bool read_at(std::ifstream& file, uint64_t offset, uint8_t* output, size_t size) {
    if (offset > uint64_t(std::numeric_limits<std::streamoff>::max()) ||
        size > size_t(std::numeric_limits<std::streamsize>::max()))
        return false;
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) return false;
    file.read(reinterpret_cast<char*>(output), static_cast<std::streamsize>(size));
    return file.good() || (file.eof() && size_t(file.gcount()) == size);
}

} // namespace

bool haze_visit_archive_vertex_programs(
    const std::filesystem::path& pak,
    const std::function<bool(const HazeArchiveVertexProgram&)>& visitor,
    HazeShaderArchiveStats& stats,
    std::string& error) {
    stats = {};
    error.clear();
    if (!visitor) {
        error = "Missing vertex-program visitor";
        return false;
    }

    std::error_code ec;
    const uint64_t pak_size = std::filesystem::file_size(pak, ec);
    if (ec || pak_size < kPbckHeaderSize ||
        pak_size > uint64_t(std::numeric_limits<std::streamoff>::max())) {
        error = "Cannot read PBCK archive size";
        return false;
    }
    std::ifstream archive(pak, std::ios::binary);
    std::array<uint8_t, kPbckHeaderSize> header{};
    if (!archive || !read_at(archive, 0, header.data(), header.size())) {
        error = "Cannot read PBCK header";
        return false;
    }
    const uint32_t magic = read_be32(header.data() + 0);
    const uint32_t table_offset = read_be32(header.data() + 4);
    const uint32_t entry_count = read_be32(header.data() + 8);
    const uint32_t auxiliary_offset = read_be32(header.data() + 12);
    const uint32_t auxiliary_size = read_be32(header.data() + 16);
    const uint32_t version = read_be32(header.data() + 20);
    const uint32_t shards = version >> 16;
    const uint64_t table_bytes = uint64_t(entry_count) * kPbckEntrySize;

    if (magic != kPbckMagic || table_offset != kPbckHeaderSize ||
        entry_count > kMaxPbckEntries ||
        uint64_t(table_offset) + table_bytes > kMaxPbckIndexBytes ||
        (version & 0xffffu) != 2 || shards == 0 || shards > 256 ||
        auxiliary_offset != uint64_t(table_offset) + table_bytes ||
        !range_fits(auxiliary_offset, auxiliary_size, pak_size) ||
        uint64_t(auxiliary_offset) + auxiliary_size != pak_size) {
        error = "Unsupported or invalid PBCK header/table bounds";
        return false;
    }

    std::array<std::unique_ptr<std::ifstream>, 256> shard_files{};
    std::array<uint64_t, 256> shard_sizes{};
    for (uint32_t index = 0; index < entry_count; ++index) {
        std::array<uint8_t, kPbckEntrySize> row{};
        const uint64_t row_offset = uint64_t(table_offset) + uint64_t(index) * kPbckEntrySize;
        if (!read_at(archive, row_offset, row.data(), row.size())) {
            error = "Cannot read PBCK entry table";
            return false;
        }
        const uint32_t decoded_size = read_be32(row.data() + 4);
        const uint32_t compressed_size = read_be32(row.data() + 8);
        const uint32_t data_offset = read_be32(row.data() + 12);
        const uint32_t metadata = read_be32(row.data() + 16);
        const uint32_t shard = metadata & 0xffu;
        ++stats.entries;
        if (shard >= shards) {
            error = "PBCK entry references an unavailable shard";
            return false;
        }

        const uint64_t stored_size = compressed_size ? compressed_size : decoded_size;
        if (decoded_size > kEntryLimit || stored_size > kCompressedLimit) {
            ++stats.skipped_large;
            continue;
        }
        if (!decoded_size || !stored_size) {
            // Empty records cannot contain a VP program.
            continue;
        }

        if (!shard_files[shard]) {
            const auto name = shard_name(pak, shard);
            const uint64_t shard_size = std::filesystem::file_size(name, ec);
            if (ec || shard_size > uint64_t(std::numeric_limits<std::streamoff>::max())) {
                error = "Cannot read PBCK shard size";
                return false;
            }
            shard_sizes[shard] = shard_size;
            shard_files[shard] = std::make_unique<std::ifstream>(name, std::ios::binary);
            if (!*shard_files[shard]) {
                error = "Cannot open PBCK shard";
                return false;
            }
        }
        if (!range_fits(data_offset, stored_size, shard_sizes[shard])) {
            error = "PBCK entry extends beyond shard";
            return false;
        }

        std::vector<uint8_t> stored(static_cast<size_t>(stored_size));
        if (!read_at(*shard_files[shard], data_offset, stored.data(), stored.size())) {
            error = "Cannot read PBCK entry data";
            return false;
        }
        std::vector<uint8_t> decoded;
        if (compressed_size) {
            decoded.resize(decoded_size);
            const int produced = stbi_zlib_decode_buffer(
                reinterpret_cast<char*>(decoded.data()), static_cast<int>(decoded.size()),
                reinterpret_cast<const char*>(stored.data()), static_cast<int>(stored.size()));
            if (produced < 0 || uint32_t(produced) != decoded_size) {
                ++stats.unsupported_decode;
                continue;
            }
        } else {
            decoded = std::move(stored);
        }

        if (decoded.size() < 4 || read_be32(decoded.data()) != kCgVertexProfile)
            continue;
        CgView view{};
        if (!parse_vertex_program(decoded, view)) {
            ++stats.invalid_shader;
            continue;
        }

        HazeArchiveVertexProgram program;
        program.start = view.start;
        program.code.resize(view.ucode_size);
        for (uint32_t offset = 0; offset < view.ucode_size; offset += 4) {
            const uint32_t word = read_be32(decoded.data() + view.ucode_offset + offset);
            program.code[offset + 0] = uint8_t(word);
            program.code[offset + 1] = uint8_t(word >> 8);
            program.code[offset + 2] = uint8_t(word >> 16);
            program.code[offset + 3] = uint8_t(word >> 24);
        }
        ++stats.vertex_programs;
        if (!visitor(program)) return true;
    }
    return true;
}

