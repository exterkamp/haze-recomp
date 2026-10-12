// Exercise browser-specific requirements before a multi-minute game boot.
#include "haze_wasm_vm.h"
#include "ppu_recomp.h"
#include "win32_compat.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static HANDLE ready;
static uint8_t* arena;
extern "C" { uint8_t* vm_base = nullptr; }
static DWORD WINAPI child(void*)
{
    // High guest stacks and main memory must share one guest address domain.
    const uint32_t value = 0x12345678;
    memcpy(arena + 0xD0010000u, &value, sizeof value);
    SetEvent(ready);
    return 42;
}

int main(int argc, char** argv)
{
    static_assert(sizeof(void*) == 8, "Haze currently requires Wasm64");
    arena = haze_wasm_reserve_guest_vm();
    vm_base = arena;
    assert(arena && ((uintptr_t)arena & 0xFFFFFFFFull) == 0);
    assert(arena[0x10000] == 0 && arena[0xD0010000u] == 0);
    assert((uint32_t)(uintptr_t)(arena + 0xC0001234u) == 0xC0001234u);
    void* host = malloc(1024);
    assert(host && ((uintptr_t)host < (uintptr_t)arena || (uintptr_t)host >= (uintptr_t)arena + 0x100010000ull));
    free(host);
    // Exercise the actual linker substitution, including overlapping high-EA
    // ranges and the guest ABI's destination return value/callee-saved register.
    ppu_context context{};
    uint8_t expected[64];
    for (unsigned i = 0; i < sizeof expected; ++i) expected[i] = (uint8_t)i;
    memcpy(arena + 0xD0010100u, expected, sizeof expected);
    memmove(expected + 3, expected, 32);
    context.gpr[3] = 0xD0010103u;
    context.gpr[4] = 0xD0010100u;
    context.gpr[5] = 32;
    context.gpr[27] = 0x123456789ABCDEF0ull;
    func_00C07818(&context);
    assert(context.gpr[3] == 0xD0010103u && context.gpr[27] == 0x123456789ABCDEF0ull);
    assert(memcmp(arena + 0xD0010100u, expected, sizeof expected) == 0);
    ready = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    assert(ready);
    HANDLE thread = CreateThread(nullptr, 4 * 1024 * 1024, child, nullptr, 0, nullptr);
    assert(thread && WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0);
    assert(WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
    DWORD code = 0;
    assert(GetExitCodeThread(thread, &code) && code == 42);
    uint32_t value = 0;
    memcpy(&value, arena + 0xD0010000u, sizeof value);
    assert(value == 0x12345678);
    CloseHandle(thread);
    CloseHandle(ready);
    if (argc > 1) {
        FILE* file = fopen(argv[1], "rb");
        assert(file);
        unsigned char magic[4];
        assert(fread(magic, 1, 4, file) == 4 && memcmp(magic, "\x7f" "ELF", 4) == 0);
        assert(fseek(file, 0, SEEK_END) == 0 && ftell(file) > 4);
        fclose(file);
    }
    puts("[wasm-smoke] PASS: aligned 4GiB guest arena, high-address stacks, heap isolation, bulk memmove ABI/overlap, pthread events/join and file reads");
    return 0;
}
