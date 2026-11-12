// A part of <stdlib.h> of C for mbedTLS (D233), without the C library.
#pragma once
#include <stddef.h>

// For SQLite (D529), in runtime/db/sqlite_os.c under the names of sqlite_config.h.
void* malloc(size_t size);
void* realloc(void* memory, size_t size);
void free(void* memory);
