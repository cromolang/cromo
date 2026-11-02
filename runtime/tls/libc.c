// The pieces of the C library that mbedTLS and Monocypher use (D233), without the
// C library, for Windows and Linux: strings, snprintf, memory, time, the random
// bytes of the system and the mutexes. memcpy, memmove, memset and memcmp are in
// mem_x64.S; the heap and the locks are those of cromo_rt.c.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "mbedtls/build_info.h"
#include "mbedtls/entropy.h"
#include "mbedtls/platform_time.h"
#include "mbedtls/threading.h"
#include "psa/crypto.h"
#include "glue.h"

// ---- Strings ----

size_t strlen(const char* text) {
    const char* end = text;
    while (*end) ++end;
    return (size_t)(end - text);
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t count) {
    for (; count > 0; --count, ++a, ++b) {
        if (*a != *b || !*a) return (unsigned char)*a - (unsigned char)*b;
    }
    return 0;
}

char* strchr(const char* text, int value) {
    for (;; ++text) {
        if (*text == (char)value) return (char*)text;
        if (!*text) return NULL;
    }
}

char* strstr(const char* text, const char* part) {
    size_t length = strlen(part);
    for (; *text; ++text) {
        if (strncmp(text, part, length) == 0) return (char*)text;
    }
    return length == 0 ? (char*)text : NULL;
}

char* strncpy(char* dest, const char* src, size_t count) {
    size_t i = 0;
    for (; i < count && src[i]; ++i) dest[i] = src[i];
    for (; i < count; ++i) dest[i] = 0;
    return dest;
}

// ---- snprintf ----
//
// The conversions that mbedTLS writes: d i u x X o c s p %, with the flags
// - 0 + space #, a width and a precision (also *), and the sizes hh h l ll z j t.

typedef struct {
    char* out;
    size_t size;
    size_t length;  // also what did not fit, as the C function returns
} Writer;

static void putChar(Writer* writer, char c) {
    if (writer->length + 1 < writer->size) writer->out[writer->length] = c;
    writer->length++;
}

static void putPadding(Writer* writer, char c, int count) {
    for (; count > 0; --count) putChar(writer, c);
}

enum { LEFT = 1, ZERO = 2, PLUS = 4, SPACE = 8, ALTERNATE = 16 };

static void putNumber(Writer* writer, uint64_t value, int negative, int base, int upper, int flags, int width,
                      int precision) {
    char digits[24];
    int count = 0;
    const char* symbols = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    while (value) {
        digits[count++] = symbols[value % (uint64_t)base];
        value /= (uint64_t)base;
    }
    int zeros = precision > count ? precision - count : 0;
    if (precision < 0 && count == 0) zeros = 1;  // 0 is written as a digit
    const char* prefix = negative ? "-" : (flags & PLUS) ? "+" : (flags & SPACE) ? " " : "";
    if ((flags & ALTERNATE) && base == 16 && count > 0) prefix = upper ? "0X" : "0x";
    if ((flags & ALTERNATE) && base == 8 && zeros == 0) zeros = 1;
    int prefixLength = (int)strlen(prefix);
    int total = prefixLength + zeros + count;
    int padding = width > total ? width - total : 0;
    if ((flags & ZERO) && !(flags & LEFT) && precision < 0) {
        zeros += padding;
        padding = 0;
    }
    if (!(flags & LEFT)) putPadding(writer, ' ', padding);
    for (int i = 0; i < prefixLength; ++i) putChar(writer, prefix[i]);
    putPadding(writer, '0', zeros);
    while (count > 0) putChar(writer, digits[--count]);
    if (flags & LEFT) putPadding(writer, ' ', padding);
}

int cromo_vsnprintf(char* out, unsigned long long size, const char* format, va_list arguments) {
    Writer writer = {out, (size_t)size, 0};
    for (const char* p = format; *p; ++p) {
        if (*p != '%') {
            putChar(&writer, *p);
            continue;
        }
        ++p;
        int flags = 0;
        for (;; ++p) {
            if (*p == '-') flags |= LEFT;
            else if (*p == '0') flags |= ZERO;
            else if (*p == '+') flags |= PLUS;
            else if (*p == ' ') flags |= SPACE;
            else if (*p == '#') flags |= ALTERNATE;
            else break;
        }
        int width = 0;
        if (*p == '*') {
            width = va_arg(arguments, int);
            if (width < 0) {
                flags |= LEFT;
                width = -width;
            }
            ++p;
        } else {
            while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
        }
        int precision = -1;
        if (*p == '.') {
            ++p;
            precision = 0;
            if (*p == '*') {
                precision = va_arg(arguments, int);
                ++p;
            } else {
                while (*p >= '0' && *p <= '9') precision = precision * 10 + (*p++ - '0');
            }
        }
        int longs = 0;  // 0 int, 1 long, 2 long long or size_t
        for (;; ++p) {
            if (*p == 'h') continue;  // promoted to int anyway
            if (*p == 'l') longs++;
            else if (*p == 'z' || *p == 'j' || *p == 't') longs = 2;
            else break;
        }
        switch (*p) {
        case 'd':
        case 'i': {
            int64_t value = longs == 0 ? va_arg(arguments, int) : va_arg(arguments, int64_t);  // long is 64 bits on Linux
            if (longs == 1) value = (int64_t)(long)value;
            uint64_t magnitude = value < 0 ? (uint64_t)0 - (uint64_t)value : (uint64_t)value;
            putNumber(&writer, magnitude, value < 0, 10, 0, flags, width, precision);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'o': {
            uint64_t value = longs == 0 ? va_arg(arguments, unsigned) : va_arg(arguments, uint64_t);
            if (longs == 1) value = (uint64_t)(unsigned long)value;
            int base = *p == 'u' ? 10 : *p == 'o' ? 8 : 16;
            putNumber(&writer, value, 0, base, *p == 'X', flags & ~(PLUS | SPACE), width, precision);
            break;
        }
        case 'p':
            putNumber(&writer, (uint64_t)(uintptr_t)va_arg(arguments, void*), 0, 16, 0, ALTERNATE, width, -1);
            break;
        case 'c': {
            int padding = width > 1 ? width - 1 : 0;
            if (!(flags & LEFT)) putPadding(&writer, ' ', padding);
            putChar(&writer, (char)va_arg(arguments, int));
            if (flags & LEFT) putPadding(&writer, ' ', padding);
            break;
        }
        case 's': {
            const char* text = va_arg(arguments, const char*);
            if (!text) text = "(null)";
            int length = 0;
            while (text[length] && (precision < 0 || length < precision)) ++length;
            int padding = width > length ? width - length : 0;
            if (!(flags & LEFT)) putPadding(&writer, ' ', padding);
            for (int i = 0; i < length; ++i) putChar(&writer, text[i]);
            if (flags & LEFT) putPadding(&writer, ' ', padding);
            break;
        }
        case '%':
            putChar(&writer, '%');
            break;
        default:  // unknown: written as it is
            putChar(&writer, '%');
            if (!*p) --p;
            else putChar(&writer, *p);
            break;
        }
    }
    if (writer.size > 0) writer.out[writer.length < writer.size ? writer.length : writer.size - 1] = 0;
    return (int)writer.length;
}

int cromo_snprintf(char* out, unsigned long long size, const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    int length = cromo_vsnprintf(out, size, format, arguments);
    va_end(arguments);
    return length;
}

// ---- Memory ----

void* cromo_tls_calloc(unsigned long long count, unsigned long long size) {
    if (size != 0 && count > (UINT64_MAX >> 1) / size) return NULL;
    uint64_t total = count * size;
    void* memory = cromo_alloc((int64_t)(total ? total : 1));
    memset(memory, 0, total);
    return memory;
}

void cromo_tls_release(void* memory) { cromo_free(memory); }

// ---- The system: time, random bytes, files, the environment ----

#ifdef _WIN32

#define WINAPI __stdcall
__declspec(dllimport) void WINAPI GetSystemTimeAsFileTime(uint64_t* time);
__declspec(dllimport) uint64_t WINAPI GetTickCount64(void);
__declspec(dllimport) void* WINAPI LoadLibraryExW(const uint16_t* name, void* file, uint32_t flags);
__declspec(dllimport) void* WINAPI GetProcAddress(void* module, const char* name);

#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x800
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 2

long long cromo_tls_time(long long* now) {
    uint64_t ticks;  // of 100 ns, since 1601
    GetSystemTimeAsFileTime(&ticks);
    long long seconds = (long long)(ticks / 10000000) - 11644473600LL;
    if (now) *now = seconds;
    return seconds;
}

mbedtls_ms_time_t mbedtls_ms_time(void) { return (mbedtls_ms_time_t)GetTickCount64(); }

// A function of a DLL of the system, loaded the first time: only the programs
// that use it load the DLL, and the DLL comes only from System32.
void* cromo_system_function(const uint16_t* dll, const char* name) {
    void* module = LoadLibraryExW(dll, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    return module ? GetProcAddress(module, name) : NULL;
}

typedef long(WINAPI* GenerateRandom)(void* algorithm, uint8_t* buffer, uint32_t size, uint32_t flags);

int32_t cromo_crypto_random(uint8_t* out, int64_t size) {
    static GenerateRandom generate;
    GenerateRandom function = __atomic_load_n(&generate, __ATOMIC_ACQUIRE);
    if (!function) {
        function = (GenerateRandom)cromo_system_function((const uint16_t*)L"bcrypt.dll", "BCryptGenRandom");
        if (!function) return -1;
        __atomic_store_n(&generate, function, __ATOMIC_RELEASE);
    }
    while (size > 0) {
        uint32_t part = size > 0x40000000 ? 0x40000000 : (uint32_t)size;
        if (function(NULL, out, part, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return -1;
        out += part;
        size -= part;
    }
    return 0;
}

#else

#ifdef __aarch64__
#define SYS_getpid 172
#define SYS_clock_gettime 113
#define SYS_getrandom 278
#else
#define SYS_getpid 39
#define SYS_clock_gettime 228
#define SYS_getrandom 318
#endif
#define CLOCK_REALTIME 0
#define CLOCK_MONOTONIC 1
#define EINTR 4

static int64_t clockNow(int clock, int64_t* nanoseconds) {
    int64_t now[2] = {0, 0};  // struct timespec
    cromo_system_call(SYS_clock_gettime, clock, (int64_t)now, 0, 0, 0, 0);
    if (nanoseconds) *nanoseconds = now[1];
    return now[0];
}

long long cromo_tls_time(long long* now) {
    long long seconds = clockNow(CLOCK_REALTIME, NULL);
    if (now) *now = seconds;
    return seconds;
}

mbedtls_ms_time_t mbedtls_ms_time(void) {
    int64_t nanoseconds;
    int64_t seconds = clockNow(CLOCK_MONOTONIC, &nanoseconds);
    return seconds * 1000 + nanoseconds / 1000000;
}

// For the protection of the random numbers of mbedTLS after a fork.
int getpid(void) { return (int)cromo_system_call(SYS_getpid, 0, 0, 0, 0, 0, 0); }

int32_t cromo_crypto_random(uint8_t* out, int64_t size) {
    while (size > 0) {
        int64_t part = size > 0x2000000 ? 0x2000000 : size;
        int64_t done = cromo_system_call(SYS_getrandom, (int64_t)out, part, 0, 0, 0, 0);
        if (done == -EINTR) continue;
        if (done <= 0) return -1;
        out += done;
        size -= done;
    }
    return 0;
}

#endif

// The only source of entropy of mbedTLS (MBEDTLS_ENTROPY_HARDWARE_ALT).
int mbedtls_hardware_poll(void* data, unsigned char* output, size_t length, size_t* written) {
    (void)data;
    if (cromo_crypto_random(output, (int64_t)length) != 0) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *written = length;
    return 0;
}

// The date of a time of Unix, in UTC: the algorithm of Howard Hinnant, "chrono-
// compatible low-level date algorithms".
struct tm* mbedtls_platform_gmtime_r(const mbedtls_time_t* time, struct tm* out) {
    int64_t days = *time / 86400;
    int64_t rest = *time % 86400;
    if (rest < 0) {
        rest += 86400;
        days--;
    }
    out->tm_hour = (int)(rest / 3600);
    out->tm_min = (int)(rest / 60 % 60);
    out->tm_sec = (int)(rest % 60);
    out->tm_wday = (int)(((days + 4) % 7 + 7) % 7);  // 1 January 1970 was a Thursday
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t dayOfEra = z - era * 146097;
    int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);  // from 1 March
    int64_t shifted = (5 * dayOfYear + 2) / 153;
    int64_t month = shifted < 10 ? shifted + 3 : shifted - 9;
    int64_t year = yearOfEra + era * 400 + (month <= 2);
    out->tm_year = (int)(year - 1900);
    out->tm_mon = (int)(month - 1);
    out->tm_mday = (int)(dayOfYear - (153 * shifted + 2) / 5 + 1);
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    out->tm_yday = (int)(month >= 3 ? dayOfYear - 306 + 365 + leap : dayOfYear - 306);
    out->tm_isdst = 0;
    return out;
}

// ---- Mutexes (MBEDTLS_THREADING_ALT): the locks of the runtime ----

static void mutexInit(mbedtls_threading_mutex_t* mutex) { mutex->state = 0; }
static void mutexFree(mbedtls_threading_mutex_t* mutex) { (void)mutex; }

static int mutexLock(mbedtls_threading_mutex_t* mutex) {
    cromo_mutex_lock(&mutex->state);
    return 0;
}

static int mutexUnlock(mbedtls_threading_mutex_t* mutex) {
    cromo_mutex_unlock(&mutex->state);
    return 0;
}

// ---- mbedTLS starts once, with the mutexes of the runtime ----

static uint64_t setupLock;
static int setupState;  // 0 not yet, 1 ready, -1 failed

int cromo_tls_setup(void) {
    int state = __atomic_load_n(&setupState, __ATOMIC_ACQUIRE);
    if (state) return state > 0;
    cromo_mutex_lock(&setupLock);
    if (!setupState) {
        mbedtls_threading_set_alt(mutexInit, mutexFree, mutexLock, mutexUnlock);
        __atomic_store_n(&setupState, psa_crypto_init() == PSA_SUCCESS ? 1 : -1, __ATOMIC_RELEASE);
    }
    cromo_mutex_unlock(&setupLock);
    return __atomic_load_n(&setupState, __ATOMIC_ACQUIRE) > 0;
}
