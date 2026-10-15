#include "haze_shader_prepare.h"
#include "haze_shader_archive.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include <chrono>
#include <tuple>
#include <stdexcept>
#include <cwchar>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include "rsx_shader_bundle.h"

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "shell32.lib")

namespace {
namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;
constexpr uint32_t max_source = 256 * 1024, max_blob = 4 * 1024 * 1024;
struct CacheHeader {
    uint32_t magic, version, stage, source_length, blob_length, reserved;
    uint64_t source_hash;
};
static_assert(sizeof(CacheHeader) == 32);

using PrepareClock = std::chrono::steady_clock;
uint64_t elapsed_ns(PrepareClock::time_point begin) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        PrepareClock::now() - begin).count());
}
double ms(uint64_t ns) { return double(ns) / 1000000.0; }

struct PrepareTimings {
    uint64_t dedup_ns = 0;
    uint64_t source_ns = 0;
    uint64_t cache_validate_ns = 0;
    uint64_t compile_ns = 0;
    uint64_t cache_write_ns = 0;
    uint64_t dedup_calls = 0;
    uint64_t duplicate_programs = 0;
    uint64_t source_calls = 0;
    uint64_t cache_validate_calls = 0;
    uint64_t cache_hits = 0;
    uint64_t compile_calls = 0;
    uint64_t cache_write_calls = 0;
    uint64_t cache_write_failures = 0;
};

void print_timings(const PrepareTimings& timings, uint64_t wall_ns) {
    const uint64_t measured_ns = timings.dedup_ns + timings.source_ns +
        timings.cache_validate_ns + timings.compile_ns + timings.cache_write_ns;
    std::printf("[shader-prepare] timing wall_ms=%.3f dedup_ms=%.3f source_ms=%.3f "
        "cache_validate_ms=%.3f compile_ms=%.3f cache_write_ms=%.3f other_ms=%.3f "
        "dedup_calls=%llu duplicates=%llu source_calls=%llu cache_validate_calls=%llu "
        "cache_hits=%llu compile_calls=%llu cache_write_calls=%llu cache_write_failures=%llu\n",
        ms(wall_ns), ms(timings.dedup_ns), ms(timings.source_ns), ms(timings.cache_validate_ns),
        ms(timings.compile_ns), ms(timings.cache_write_ns),
        ms(wall_ns > measured_ns ? wall_ns - measured_ns : 0),
        static_cast<unsigned long long>(timings.dedup_calls),
        static_cast<unsigned long long>(timings.duplicate_programs),
        static_cast<unsigned long long>(timings.source_calls),
        static_cast<unsigned long long>(timings.cache_validate_calls),
        static_cast<unsigned long long>(timings.cache_hits),
        static_cast<unsigned long long>(timings.compile_calls),
        static_cast<unsigned long long>(timings.cache_write_calls),
        static_cast<unsigned long long>(timings.cache_write_failures));
    std::fflush(stdout);
}

uint64_t source_hash(const std::string& source) {
    uint64_t result = 1469598103934665603ull; // SDK cache identity, including its seed.
    for (unsigned char ch : source) result = (result ^ ch) * 1099511628211ull;
    return result;
}

bool valid_bytecode(const void* data, size_t size, char stage) {
    ComPtr<ID3D11ShaderReflection> reflection;
    if (FAILED(D3DReflect(data, size, __uuidof(ID3D11ShaderReflection),
        reinterpret_cast<void**>(reflection.GetAddressOf())))) return false;
    D3D11_SHADER_DESC desc{};
    return SUCCEEDED(reflection->GetDesc(&desc)) &&
        D3D11_SHVER_GET_TYPE(desc.Version) ==
            (stage == 'V' ? D3D11_SHVER_VERTEX_SHADER : D3D11_SHVER_PIXEL_SHADER) &&
        D3D11_SHVER_GET_MAJOR(desc.Version) == 5 && D3D11_SHVER_GET_MINOR(desc.Version) == 0;
}

bool valid_cached_shader(const fs::path& path, const std::string& source, uint64_t hash, char stage,
                         std::vector<uint8_t>& bytecode) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const auto size = input.tellg();
    input.seekg(0);
    CacheHeader header{};
    if (!input.read(reinterpret_cast<char*>(&header), sizeof header) ||
        header.magic != 0x43535A59u || header.version != 2 || header.stage != static_cast<uint32_t>(stage) ||
        header.reserved || header.source_length != source.size() || header.source_hash != hash ||
        !header.blob_length || header.blob_length > max_blob ||
        size != static_cast<std::streamoff>(sizeof header + source.size() + header.blob_length)) return false;
    std::string retained(source.size(), '\0');
    if (!input.read(retained.data(), retained.size()) || retained != source) return false;
    bytecode.resize(header.blob_length);
    if (!input.read(reinterpret_cast<char*>(bytecode.data()), bytecode.size())) return false;
    return valid_bytecode(bytecode.data(), bytecode.size(), stage);
}

// Share the renderer's bounded codec, exact-source lookup and locked merge.
// Per-file entries remain the resumable fallback if bundle persistence fails.
class PreparationBundle {
    std::string path_;
    std::unique_ptr<uint8_t, decltype(&std::free)> bytes_{nullptr, &std::free};
    shader_bundle_view view_{};
    size_t loaded_size_ = 0;
    std::vector<std::vector<uint8_t>> blobs_;
    std::vector<shader_bundle_item> items_;
    size_t size_ = sizeof(shader_bundle_header);
    bool fits_ = true;
    unsigned hits_ = 0;
public:
    explicit PreparationBundle(const fs::path& cache) {
        const auto wide = (cache / "shader_cache.bundle").wstring();
        // The current SDK persistence API uses narrow Windows paths. Refuse
        // lossy conversion; Unicode/long paths retain the existing wide-file
        // preparation path until bundle persistence supports those paths too.
        if (wide.size() >= MAX_PATH || std::any_of(wide.begin(), wide.end(),
                [](wchar_t ch) { return ch > 127; })) {
            std::printf("[shader-prepare] bundle_load=unsupported_path\n");
            return;
        }
        path_.assign(wide.begin(), wide.end());
        uint8_t* data = nullptr;
        size_t size = 0;
        const auto begin = PrepareClock::now();
        const int result = shader_bundle_read_file(path_.c_str(), &data, &size, &view_);
        bytes_.reset(data);
        loaded_size_ = size;
        std::printf("[shader-prepare] bundle_load=%s entries=%u bytes=%zu elapsed_ms=%.3f\n",
            result == 1 ? "loaded" : result == 0 ? "absent" : "rejected",
            result == 1 ? view_.count : 0, size, ms(elapsed_ns(begin)));
    }
    bool load(char stage, const std::string& source, uint64_t hash) {
        const auto* record = shader_bundle_find(&view_, static_cast<uint32_t>(stage),
            source.data(), static_cast<uint32_t>(source.size()), hash);
        if (!record || !valid_bytecode(view_.bytes + record->blob_offset, record->blob_length, stage)) return false;
        ++hits_;
        return true;
    }
    // source belongs to the stable set of generated sources, alive through save.
    void retain(char stage, const std::string& source, uint64_t hash, std::vector<uint8_t> blob) {
        if (path_.empty() || !fits_) return;
        const size_t add = sizeof(shader_bundle_record) + source.size() + blob.size();
        if (add > SHADER_BUNDLE_MAX_BYTES - size_ || items_.size() >= SHADER_BUNDLE_MAX_ENTRIES) {
            fits_ = false;
            items_.clear();
            blobs_.clear();
            return;
        }
        size_ += add;
        blobs_.push_back(std::move(blob));
        shader_bundle_item item{};
        item.stage = static_cast<uint32_t>(stage);
        item.hash = hash;
        item.source_length = static_cast<uint32_t>(source.size());
        item.blob_length = static_cast<uint32_t>(blobs_.back().size());
        item.source = reinterpret_cast<const uint8_t*>(source.data());
        item.blob = blobs_.back().data();
        items_.push_back(item);
    }
    void save(bool complete) {
        // A fully reused snapshot needs no merge or disk write. Incoming
        // entries alone are merged under the shared writer lock when needed.
        if (complete && fits_ && items_.empty() && hits_) {
            std::printf("[shader-prepare] bundle_save=reused entries=%u bytes=%zu hits=%u elapsed_ms=0.000\n",
                view_.count, loaded_size_, hits_);
            return;
        }
        uint32_t entries = 0;
        size_t bytes = 0;
        const auto begin = PrepareClock::now();
        const bool saved = complete && fits_ && !path_.empty() && !items_.empty() &&
            shader_bundle_persist_file(path_.c_str(), items_.data(), static_cast<uint32_t>(items_.size()), &entries, &bytes);
        std::printf("[shader-prepare] bundle_save=%s entries=%u bytes=%zu hits=%u elapsed_ms=%.3f\n",
            saved ? "saved" : "skipped", entries, bytes, hits_, ms(elapsed_ns(begin)));
    }
};

bool write_cache(const fs::path& path, const std::string& source, uint64_t hash, char stage, ID3DBlob* blob) {
    static unsigned serial = 0;
    const fs::path temporary = path.wstring() + L".prepare." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(++serial) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const auto length = blob->GetBufferSize();
    CacheHeader header{0x43535A59u, 2, static_cast<uint32_t>(stage), static_cast<uint32_t>(source.size()),
        static_cast<uint32_t>(length), 0, hash};
    bool ok = source.size() <= max_source && length && length <= max_blob;
    const void* parts[] = {&header, source.data(), blob->GetBufferPointer()};
    const DWORD sizes[] = {sizeof header, header.source_length, header.blob_length};
    for (unsigned i = 0; ok && i < 3; ++i) {
        DWORD written = 0;
        ok = WriteFile(file, parts[i], sizes[i], &written, nullptr) && written == sizes[i];
    }
    // This is rebuildable cache data, not a save. Close before atomically
    // replacing the entry, but do not force a disk barrier for every shader.
    // A lost or damaged entry after power loss is rejected and recompiled by
    // the exact-source/header/bytecode validation above.
    if (!CloseHandle(file)) ok = false;
    if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
    if (!ok) DeleteFileW(temporary.c_str()); // Only this invocation's CREATE_NEW file.
    return ok;
}

int prepare(const fs::path& disc, const fs::path& cache, bool pixels, uint32_t width, uint32_t height) {
    const auto prepare_begin = PrepareClock::now();
    fs::create_directories(cache);
    std::vector<char> text(max_source);
    std::set<std::pair<std::vector<uint8_t>, uint32_t>> seen;
    std::set<std::tuple<std::vector<uint8_t>, uint32_t, uint32_t>> seen_pixels;
    std::set<std::pair<char, std::string>> sources;
    PreparationBundle bundle(cache);
    uint64_t source_bytes = 0, program_bytes = 0;
    unsigned source_duplicates = 0, unsupported_pixels = 0;
    unsigned compiled = 0, hits = 0, failed = 0;
    PrepareTimings timings;
    const auto store_source = [&](char stage) {
        const std::string source(text.data());
        auto phase_begin = PrepareClock::now();
        if (sources.contains({stage, source})) {
            ++source_duplicates;
            timings.dedup_ns += elapsed_ns(phase_begin);
            return;
        }
        if (sources.size() >= 16384 || source_bytes + source.size() > 128u * 1024u * 1024u)
            throw std::runtime_error("Shader preparation source inventory exceeds its bounded capacity");
        const auto source_entry = sources.emplace(stage, source).first;
        source_bytes += source.size();
        timings.dedup_ns += elapsed_ns(phase_begin);
        const auto hash = source_hash(source);
        char name[64];
        std::snprintf(name, sizeof name, "%c_%016llX_%08X.bin", stage, static_cast<unsigned long long>(hash),
            static_cast<unsigned>(source.size()));
        const auto path = cache / name;
        phase_begin = PrepareClock::now();
        std::vector<uint8_t> bytecode;
        const bool bundle_hit = bundle.load(stage, source, hash);
        const bool cache_valid = bundle_hit ||
            valid_cached_shader(path, source, hash, stage, bytecode);
        timings.cache_validate_ns += elapsed_ns(phase_begin);
        ++timings.cache_validate_calls;
        if (cache_valid) {
            ++hits; ++timings.cache_hits;
            if (!bundle_hit) bundle.retain(stage, source_entry->second, hash, std::move(bytecode));
            return;
        }
        ComPtr<ID3DBlob> blob, diagnostic;
        phase_begin = PrepareClock::now();
        const HRESULT result = D3DCompile(source.data(), source.size(), stage == 'V' ? "xvs" : "xps",
            nullptr, nullptr, "main", stage == 'V' ? "vs_5_0" : "ps_5_0", 0, 0,
            blob.GetAddressOf(), diagnostic.GetAddressOf());
        timings.compile_ns += elapsed_ns(phase_begin);
        ++timings.compile_calls;
        bool cache_saved = false;
        if (SUCCEEDED(result) && blob) {
            phase_begin = PrepareClock::now();
            cache_saved = write_cache(path, source, hash, stage, blob.Get());
            timings.cache_write_ns += elapsed_ns(phase_begin);
            ++timings.cache_write_calls;
            if (!cache_saved) ++timings.cache_write_failures;
        }
        if (FAILED(result) || !blob || !cache_saved) {
            ++failed;
            std::fprintf(stderr, "[shader-prepare] failed %s hr=%08lX\n", name, static_cast<unsigned long>(result));
        } else {
            ++compiled;
            const auto* data = static_cast<const uint8_t*>(blob->GetBufferPointer());
            bundle.retain(stage, source_entry->second, hash,
                std::vector<uint8_t>(data, data + blob->GetBufferSize()));
        }
        if (sources.size() % 16 == 0) {
            std::printf("[shader-prepare] stage=%c unique=%zu compiled=%u cached=%u failed=%u\n",
                stage, sources.size(), compiled, hits, failed);
            std::fflush(stdout);
        }
    };
    const auto reserve_program = [&](size_t bytes) {
        if (seen.size() + seen_pixels.size() > 8192 || program_bytes + bytes > 64u * 1024u * 1024u)
            throw std::runtime_error("Shader preparation program inventory exceeds its bounded capacity");
        program_bytes += bytes;
    };
    const auto visitor = [&](const HazeArchiveVertexProgram& program) {
        auto phase_begin = PrepareClock::now();
        const bool inserted = seen.emplace(program.code, program.start).second;
        timings.dedup_ns += elapsed_ns(phase_begin);
        ++timings.dedup_calls;
        if (!inserted) { ++timings.duplicate_programs; return true; }
        reserve_program(program.code.size());
        phase_begin = PrepareClock::now();
        const bool generated = haze_shader_prepare_vertex_source(program.code.data(),
            static_cast<uint32_t>(program.code.size()), program.start,
            text.data(), static_cast<uint32_t>(text.size())) != 0;
        timings.source_ns += elapsed_ns(phase_begin);
        ++timings.source_calls;
        if (!generated) { ++failed; return true; }
        store_source('V');
        return true;
    };
    HazePixelShaderVariant variants[6]{};
    if (pixels && !haze_shader_prepare_pixel_variants(width, height, variants))
        throw std::runtime_error("Invalid shader preparation render extent");
    const auto pixel_visitor = [&](const HazeArchiveFragmentProgram& program) {
        if (program.unsupported_sampler_metadata) { ++unsupported_pixels; return true; }
        const uint32_t control = (program.output_from_h0 ? 0u : 0x40u) |
            (program.depth_replace ? 0x0Eu : 0u);
        auto phase_begin = PrepareClock::now();
        const bool inserted = seen_pixels.emplace(program.code, control, program.cube_mask).second;
        timings.dedup_ns += elapsed_ns(phase_begin);
        ++timings.dedup_calls;
        if (!inserted) { ++timings.duplicate_programs; return true; }
        reserve_program(program.code.size());
        for (auto variant : variants) {
            variant.control = control;
            variant.cube_mask = program.cube_mask;
            phase_begin = PrepareClock::now();
            const bool generated = haze_shader_prepare_pixel_source(program.code.data(),
                static_cast<uint32_t>(program.code.size()), &variant,
                text.data(), static_cast<uint32_t>(text.size())) != 0;
            timings.source_ns += elapsed_ns(phase_begin);
            ++timings.source_calls;
            if (!generated) { ++failed; continue; }
            store_source('P');
        }
        return true;
    };
    for (const char* name : {"misc.pak", "71423f03.pak"}) {
        const auto archive_begin = PrepareClock::now();
        HazeShaderArchiveStats stats{};
        std::string error;
        const auto pak = disc / "PS3_GAME" / "USRDIR" / "pak" / "cell" / name;
        const bool archive_ok = haze_visit_archive_vertex_programs(pak, visitor, stats, error);
        const uint64_t archive_ns = elapsed_ns(archive_begin);
        if (!archive_ok) {
            std::fprintf(stderr, "[shader-prepare] %s: %s\n", name, error.c_str());
            std::printf("[shader-prepare] archive_timing failed=%s wall_ms=%.3f\n", name, ms(archive_ns));
            print_timings(timings, elapsed_ns(prepare_begin));
            return 1;
        }
        std::printf("[shader-prepare] archive=%s entries=%llu vertex_programs=%llu large=%llu decode_skips=%llu invalid=%llu\n",
            name, static_cast<unsigned long long>(stats.entries), static_cast<unsigned long long>(stats.vertex_programs),
            static_cast<unsigned long long>(stats.skipped_large), static_cast<unsigned long long>(stats.unsupported_decode),
            static_cast<unsigned long long>(stats.invalid_shader));
        std::printf("[shader-prepare] archive_timing archive=%s wall_ms=%.3f\n",
            name, ms(archive_ns));
        if (stats.invalid_shader) ++failed;
        if (pixels) {
            HazeShaderArchiveStats pixel_stats{};
            if (!haze_visit_archive_fragment_programs(pak, pixel_visitor, pixel_stats, error)) {
                std::fprintf(stderr, "[shader-prepare] %s: %s\n", name, error.c_str());
                print_timings(timings, elapsed_ns(prepare_begin));
                return 1;
            }
            std::printf("[shader-prepare] archive=%s fragment_programs=%llu large=%llu decode_skips=%llu invalid=%llu\n",
                name, static_cast<unsigned long long>(pixel_stats.fragment_programs),
                static_cast<unsigned long long>(pixel_stats.skipped_large),
                static_cast<unsigned long long>(pixel_stats.unsupported_decode),
                static_cast<unsigned long long>(pixel_stats.invalid_shader));
            if (pixel_stats.invalid_shader) ++failed;
        }
        // The archive scanner reports bounded coverage; a successful worker
        // prepares the supported variant, not every runtime pipeline variant.
    }
    bundle.save(!failed && !sources.empty());
    const uint64_t wall_ns = elapsed_ns(prepare_begin);
    std::printf("[shader-prepare] complete unique=%zu compiled=%u cached=%u failed=%u seconds=%.3f\n",
        sources.size(), compiled, hits, failed, double(wall_ns) / 1000000000.0);
    std::printf("[shader-prepare] coverage vertex_recipes=%zu pixel_recipes=%zu source_duplicates=%u "
        "unsupported_pixel_records=%u pixel_variants=%u width=%u height=%u\n",
        seen.size(), seen_pixels.size(), source_duplicates, unsupported_pixels, pixels ? 6u : 0u, width, height);
    print_timings(timings, wall_ns);
    return failed || sources.empty() ? 1 : 0;
}
}

int haze_shader_prepare_cli() {
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 2;
    fs::path disc, cache;
    const bool pixels = count > 1 && std::wcscmp(args[1], L"--prepare-shaders") == 0;
    uint32_t width = 0, height = 0;
    const auto extent = [](const wchar_t* value) {
        uint32_t result = 0;
        if (!*value) return 0u;
        for (; *value; ++value) {
            if (*value < L'0' || *value > L'9') return 0u;
            result = result * 10 + (*value - L'0');
            if (result > 8192) return 0u;
        }
        return result;
    };
    const bool valid_arguments = (!pixels && count == 4) || (pixels && count == 6);
    if (valid_arguments) {
        disc = args[2]; cache = args[3];
        if (pixels) { width = extent(args[4]); height = extent(args[5]); }
    }
    LocalFree(args);
    if (!valid_arguments || (pixels && (!width || !height))) {
        std::fprintf(stderr, "Usage: HazeRecomp --prepare-vertex-shaders <disc directory> <cache directory>\n"
            "   or: HazeRecomp --prepare-shaders <disc directory> <cache directory> <render width> <render height>\n");
        return 2;
    }
    try { return prepare(disc, cache, pixels, width, height); }
    catch (const std::exception& error) {
        std::fprintf(stderr, "[shader-prepare] %s\n", error.what());
        return 1;
    }
}
