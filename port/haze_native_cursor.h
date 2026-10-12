#pragma once
#include <stdint.h>

// A PC operation over ordinary host data; no guest registers or LS semantics.
typedef struct haze_native_cursor {
    float value, increment;
    uint32_t index, remaining;
    float secondary, secondary_increment;
} haze_native_cursor;
#ifdef __cplusplus
static_assert(sizeof(haze_native_cursor)==24,"Cursor boundary requires six 32-bit fields");
#else
_Static_assert(sizeof(haze_native_cursor)==24,"Cursor boundary requires six 32-bit fields");
#endif
static inline void haze_native_cursor_advance(haze_native_cursor* cursor)
{
    cursor->value += cursor->increment;
    ++cursor->index;
    --cursor->remaining;
    cursor->secondary += cursor->secondary_increment;
}
