#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static volatile uint64_t result;
__declspec(noinline) uint64_t oracle_leaf(uint64_t value)
{
    for (unsigned i = 0; i < 1000000; ++i) {
        value ^= value << 13;
        value ^= value >> 7;
        value ^= value << 17;
        result = value;
    }
    return value;
}
__declspec(noinline) uint64_t oracle_middle(uint64_t value)
{
    return oracle_leaf(value) + 3;
}
__declspec(noinline) uint64_t oracle_entry(uint64_t value)
{
    return oracle_middle(value) + 7;
}
int main(int argc, char** argv)
{
    unsigned seconds = argc == 2 ? (unsigned)atoi(argv[1]) : 8;
    if (!seconds || seconds > 20) return 2;
    printf("%lu\n", GetCurrentThreadId());
    fflush(stdout);
    ULONGLONG deadline = GetTickCount64() + 1000 * seconds;
    uint64_t value = 1;
    while (GetTickCount64() < deadline) value = oracle_entry(value);
    return 0;
}
