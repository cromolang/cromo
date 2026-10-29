// mbedTLS for Cromo (D233): without the C library. The runtime gives memory,
// time, entropy and mutexes (runtime/tls/libc.c); mbedTLS has no files, sockets
// or timers of its own, and the records go through memory (cromo_tls.c).

#undef MBEDTLS_FS_IO
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C
#undef MBEDTLS_PSA_ITS_FILE_C
#undef MBEDTLS_PSA_CRYPTO_STORAGE_C
#undef MBEDTLS_SELF_TEST
#undef MBEDTLS_DEBUG_C
#undef MBEDTLS_VERSION_FEATURES
#undef MBEDTLS_PADLOCK_C
#undef MBEDTLS_AESCE_C

// Only TLS over TCP, without the old ciphers and the renegotiation (D242).
#undef MBEDTLS_SSL_PROTO_DTLS
#undef MBEDTLS_SSL_DTLS_ANTI_REPLAY
#undef MBEDTLS_SSL_DTLS_HELLO_VERIFY
#undef MBEDTLS_SSL_DTLS_CLIENT_PORT_REUSE
#undef MBEDTLS_SSL_DTLS_CONNECTION_ID
#undef MBEDTLS_SSL_COOKIE_C
#undef MBEDTLS_SSL_RENEGOTIATION
#undef MBEDTLS_DES_C
#undef MBEDTLS_ARIA_C
#undef MBEDTLS_CAMELLIA_C

// x64 has 128-bit multiplications, but their division needs __udivti3 of compiler-rt.
#define MBEDTLS_NO_UDBL_DIVISION

// clang for Windows is not GCC: without this, mbedTLS includes sal.h of the Windows SDK.
#define MBEDTLS_CHECK_RETURN __attribute__((__warn_unused_result__))

#define MBEDTLS_PLATFORM_MEMORY
#define MBEDTLS_PLATFORM_NO_STD_FUNCTIONS
#define MBEDTLS_PLATFORM_CALLOC_MACRO cromo_tls_calloc
#define MBEDTLS_PLATFORM_FREE_MACRO cromo_tls_release
#define MBEDTLS_PLATFORM_SNPRINTF_MACRO cromo_snprintf
#define MBEDTLS_PLATFORM_VSNPRINTF_MACRO cromo_vsnprintf
#define MBEDTLS_PLATFORM_TIME_TYPE_MACRO long long
#define MBEDTLS_PLATFORM_TIME_MACRO cromo_tls_time
#define MBEDTLS_PLATFORM_MS_TIME_ALT
#define MBEDTLS_PLATFORM_GMTIME_R_ALT

#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT

#define MBEDTLS_THREADING_C
#define MBEDTLS_THREADING_ALT

#include <stdarg.h>

void* cromo_tls_calloc(unsigned long long count, unsigned long long size);
void cromo_tls_release(void* memory);
long long cromo_tls_time(long long* now);
int cromo_snprintf(char* out, unsigned long long size, const char* format, ...);
int cromo_vsnprintf(char* out, unsigned long long size, const char* format, va_list arguments);
