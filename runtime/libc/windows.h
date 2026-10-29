// The part of <windows.h> that mbedTLS uses on Windows (D233), without the
// Windows SDK: SecureZeroMemory, which the SDK defines inline as well.
#pragma once
#include <stddef.h>

static inline void* SecureZeroMemory(void* data, size_t size) {
    volatile unsigned char* byte = (volatile unsigned char*)data;
    while (size--) *byte++ = 0;
    return data;
}
