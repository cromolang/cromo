// A part of <string.h> of C for mbedTLS (D233), without the C library: the
// memory functions are in mem_x64.S, the others in tls/libc.c.
#pragma once
#include <stddef.h>

void* memcpy(void* dest, const void* src, size_t count);
void* memmove(void* dest, const void* src, size_t count);
void* memset(void* dest, int value, size_t count);
int memcmp(const void* a, const void* b, size_t count);
size_t strlen(const char* text);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, size_t count);
char* strchr(const char* text, int value);
char* strstr(const char* text, const char* part);
char* strncpy(char* dest, const char* src, size_t count);

// For SQLite (D529), in runtime/db/sqlite_os.c under the names of sqlite_config.h.
size_t strspn(const char* text, const char* accept);
size_t strcspn(const char* text, const char* reject);
char* strrchr(const char* text, int value);
void* memchr(const void* data, int value, size_t count);
