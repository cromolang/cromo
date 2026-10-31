// What the files of runtime/tls share with each other and with cromo_rt.c (D233).
#pragma once
#include <stdint.h>

// From the runtime: the heap, which panics when the memory ends (std.runtime,
// D393), and the locks (cromo_rt.c).
void* cromo_alloc(int64_t size);
void* cromo_realloc(void* memory, int64_t size);
void cromo_free(void* memory);
void cromo_mutex_lock(void* state);
void cromo_mutex_unlock(void* state);

// From libc.c.
int32_t cromo_crypto_random(uint8_t* out, int64_t size);  // 0, or -1 if the system has none
int cromo_tls_setup(void);  // mbedTLS with the mutexes of the runtime and PSA, once: 1 when ready

// From crypto_hash.c: the hash of the enum Hash of std.crypto.
const struct mbedtls_md_info_t* cromo_crypto_md(int32_t kind);
#ifdef _WIN32
void* cromo_system_function(const uint16_t* dll, const char* name);
#else
int64_t cromo_system_call(int64_t number, int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f);
const uint8_t* cromo_environment(int64_t index);
#endif
