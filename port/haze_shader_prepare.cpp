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

uint64_t source_hash(const std::string& source) {
    uint64_t result = 1469598103934665603ull; // SDK cache identity, including its seed.
    for (unsigned char ch : source) result = (result ^ ch) * 1099511628211ull;
    return result;
}

bool valid_cached_vertex(const fs::path& path, const std::string& source, uint64_t hash) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const auto size = input.tellg();
    input.seekg(0);
    CacheHeader header{};
    if (!input.read(reinterpret_cast<char*>(&header), sizeof header) ||
        header.magic != 0x43535A59u || header.version != 2 || header.stage != 'V' ||
        header.reserved || header.source_length != source.size() || header.source_hash != hash ||
        !header.blob_length || header.blob_length > max_blob ||
        size != static_cast<std::streamoff>(sizeof header + source.size() + header.blob_length)) return false;
    std::string retained(source.size(), '\0');
    if (!input.read(retained.data(), retained.size()) || retained != source) return false;
    std::vector<uint8_t> bytecode(header.blob_length);
    if (!input.read(reinterpret_cast<char*>(bytecode.data()), bytecode.size())) return false;
    ComPtr<ID3D11ShaderReflection> reflection;
    if (FAILED(D3DReflect(bytecode.data(), bytecode.size(), __uuidof(ID3D11ShaderReflection),
        reinterpret_cast<void**>(reflection.GetAddressOf())))) return false;
    D3D11_SHADER_DESC desc{};
    return SUCCEEDED(reflection->GetDesc(&desc)) &&
        D3D11_SHVER_GET_TYPE(desc.Version) == D3D11_SHVER_VERTEX_SHADER &&
        D3D11_SHVER_GET_MAJOR(desc.Version) == 5 && D3D11_SHVER_GET_MINOR(desc.Version) == 0;
}

bool write_cache(const fs::path& path, const std::string& source, uint64_t hash, ID3DBlob* blob) {
    static unsigned serial = 0;
    const fs::path temporary = path.wstring() + L".prepare." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(++serial) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    const auto length = blob->GetBufferSize();
    CacheHeader header{0x43535A59u, 2, 'V', static_cast<uint32_t>(source.size()),
        static_cast<uint32_t>(length), 0, hash};
    bool ok = source.size() <= max_source && length && length <= max_blob;
    const void* parts[] = {&header, source.data(), blob->GetBufferPointer()};
    const DWORD sizes[] = {sizeof header, header.source_length, header.blob_length};
    for (unsigned i = 0; ok && i < 3; ++i) {
        DWORD written = 0;
        ok = WriteFile(file, parts[i], sizes[i], &written, nullptr) && written == sizes[i];
    }
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    if (!CloseHandle(file)) ok = false;
    if (ok) ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(temporary.c_str()); // Only this invocation's CREATE_NEW file.
    return ok;
}

int prepare(const fs::path& disc, const fs::path& cache) {
    fs::create_directories(cache);
    std::vector<char> text(max_source);
    std::set<std::pair<std::vector<uint8_t>, uint32_t>> seen;
    unsigned compiled = 0, hits = 0, failed = 0;
    const auto begin = std::chrono::steady_clock::now();
    const auto visitor = [&](const HazeArchiveVertexProgram& program) {
        if (!seen.emplace(program.code, program.start).second) return true;
        if (!haze_shader_prepare_vertex_source(program.code.data(), static_cast<uint32_t>(program.code.size()),
            program.start, text.data(), static_cast<uint32_t>(text.size()))) { ++failed; return true; }
        const std::string source(text.data());
        const auto hash = source_hash(source);
        char name[64];
        std::snprintf(name, sizeof name, "V_%016llX_%08X.bin", static_cast<unsigned long long>(hash),
            static_cast<unsigned>(source.size()));
        const auto path = cache / name;
        if (valid_cached_vertex(path, source, hash)) { ++hits; return true; }
        ComPtr<ID3DBlob> blob, diagnostic;
        const HRESULT result = D3DCompile(source.data(), source.size(), "xvs", nullptr, nullptr,
            "main", "vs_5_0", 0, 0, blob.GetAddressOf(), diagnostic.GetAddressOf());
        if (FAILED(result) || !blob || !write_cache(path, source, hash, blob.Get())) {
            ++failed;
            std::fprintf(stderr, "[shader-prepare] failed %s hr=%08lX\n", name, static_cast<unsigned long>(result));
        } else ++compiled;
        if (seen.size() % 16 == 0) {
            std::printf("[shader-prepare] unique=%zu compiled=%u cached=%u failed=%u\n", seen.size(), compiled, hits, failed);
            std::fflush(stdout);
        }
        return true;
    };
    for (const char* name : {"misc.pak", "71423f03.pak"}) {
        HazeShaderArchiveStats stats{};
        std::string error;
        const auto pak = disc / "PS3_GAME" / "USRDIR" / "pak" / "cell" / name;
        if (!haze_visit_archive_vertex_programs(pak, visitor, stats, error)) {
            std::fprintf(stderr, "[shader-prepare] %s: %s\n", name, error.c_str());
            return 1;
        }
        std::printf("[shader-prepare] archive=%s entries=%llu vertex_programs=%llu large=%llu decode_skips=%llu invalid=%llu\n",
            name, static_cast<unsigned long long>(stats.entries), static_cast<unsigned long long>(stats.vertex_programs),
            static_cast<unsigned long long>(stats.skipped_large), static_cast<unsigned long long>(stats.unsupported_decode),
            static_cast<unsigned long long>(stats.invalid_shader));
        if (stats.invalid_shader) ++failed;
        // The archive scanner reports bounded coverage; a successful worker
        // prepares the supported variant, not every runtime pipeline variant.
    }
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    std::printf("[shader-prepare] complete unique=%zu compiled=%u cached=%u failed=%u seconds=%.3f\n",
        seen.size(), compiled, hits, failed, elapsed);
    return failed || seen.empty() ? 1 : 0;
}
}

int haze_shader_prepare_cli() {
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 2;
    fs::path disc, cache;
    if (count == 4) { disc = args[2]; cache = args[3]; }
    LocalFree(args);
    if (count != 4) {
        std::fprintf(stderr, "Usage: HazeRecomp --prepare-vertex-shaders <disc directory> <cache directory>\n");
        return 2;
    }
    try { return prepare(disc, cache); }
    catch (const std::exception& error) {
        std::fprintf(stderr, "[shader-prepare] %s\n", error.what());
        return 1;
    }
}
