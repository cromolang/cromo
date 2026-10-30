// Bootstrap runtime of Cromo (D58, D93): printing, panics, heap, threads, tasks and program exit.
// Freestanding C without the C library (D19): it talks to Windows through
// kernel32.dll, and to Linux with system calls (D142). CMake compiles it once for
// each system. It will be rewritten in Cromo `unsafe` when extern (M7), the
// bitwise operators and fixed-size arrays exist (D37, D93).

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int i32;
typedef unsigned long long u64;
typedef long long i64;

#define NORETURN __attribute__((noreturn))

void* memcpy(void* dest, const void* src, u64 count);  // in mem_x64.S

#ifdef _WIN32

// ---- Windows API ----

#define WINAPI __stdcall
__declspec(dllimport) void* WINAPI GetStdHandle(u32 which);
__declspec(dllimport) int WINAPI WriteFile(void* file, const void* data, u32 size, u32* written, void* overlapped);
__declspec(dllimport) int WINAPI GetConsoleMode(void* console, u32* mode);
__declspec(dllimport) int WINAPI WriteConsoleW(void* console, const u16* text, u32 count, u32* written, void* reserved);
__declspec(dllimport) int WINAPI MultiByteToWideChar(u32 codePage, u32 flags, const char* text, int size, u16* out,
                                                     int outSize);
__declspec(dllimport) __declspec(noreturn) void WINAPI ExitProcess(u32 code);
__declspec(dllimport) void* WINAPI GetProcessHeap(void);
__declspec(dllimport) void* WINAPI HeapAlloc(void* heap, u32 flags, u64 size);
__declspec(dllimport) void* WINAPI HeapReAlloc(void* heap, u32 flags, void* memory, u64 size);
__declspec(dllimport) int WINAPI HeapFree(void* heap, u32 flags, void* memory);
__declspec(dllimport) u16* WINAPI GetCommandLineW(void);
__declspec(dllimport) int WINAPI WideCharToMultiByte(u32 codePage, u32 flags, const u16* text, int size, char* out,
                                                     int outSize, const char* defaultChar, int* usedDefault);
__declspec(dllimport) void WINAPI AcquireSRWLockExclusive(void* lock);
__declspec(dllimport) void WINAPI ReleaseSRWLockExclusive(void* lock);
__declspec(dllimport) void* WINAPI CreateThread(void* security, u64 stackSize, u32(WINAPI* start)(void*), void* argument,
                                                u32 flags, u32* id);
__declspec(dllimport) u32 WINAPI WaitForSingleObject(void* handle, u32 milliseconds);
__declspec(dllimport) int WINAPI CloseHandle(void* handle);
__declspec(dllimport) int WINAPI SleepConditionVariableSRW(void* condition, void* lock, u32 milliseconds, u32 flags);
__declspec(dllimport) void WINAPI WakeConditionVariable(void* condition);
__declspec(dllimport) void WINAPI WakeAllConditionVariable(void* condition);
__declspec(dllimport) int WINAPI SwitchToThread(void);
__declspec(dllimport) u32 WINAPI TlsAlloc(void);
__declspec(dllimport) void* WINAPI TlsGetValue(u32 index);
__declspec(dllimport) int WINAPI TlsSetValue(u32 index, void* value);
__declspec(dllimport) void* WINAPI ConvertThreadToFiberEx(void* parameter, u32 flags);
__declspec(dllimport) void* WINAPI CreateFiberEx(u64 commit, u64 reserve, u32 flags, void(WINAPI* start)(void*),
                                                 void* parameter);
__declspec(dllimport) void WINAPI SwitchToFiber(void* fiber);
__declspec(dllimport) void WINAPI DeleteFiber(void* fiber);
__declspec(dllimport) u32 WINAPI GetActiveProcessorCount(u16 group);
__declspec(dllimport) int WINAPI QueryPerformanceCounter(i64* count);
__declspec(dllimport) int WINAPI QueryPerformanceFrequency(i64* frequency);
__declspec(dllimport) void* WINAPI CreateIoCompletionPort(void* file, void* port, u64 key, u32 threads);
__declspec(dllimport) int WINAPI GetQueuedCompletionStatusEx(void* port, void* entries, u32 count, u32* removed,
                                                             u32 milliseconds, int alertable);
__declspec(dllimport) int WINAPI PostQueuedCompletionStatus(void* port, u32 bytes, u64 key, void* overlapped);
__declspec(dllimport) int WINAPI SetFileCompletionNotificationModes(void* file, u8 flags);
__declspec(dllimport) void* WINAPI GetModuleHandleW(const u16* name);
__declspec(dllimport) void* WINAPI GetProcAddress(void* module, const char* name);
__declspec(dllimport) void* WINAPI CreateWaitableTimerExW(void* attributes, const u16* name, u32 flags, u32 access);
__declspec(dllimport) int WINAPI SetWaitableTimer(void* timer, const i64* due, i32 period, void* routine, void* argument,
                                                  int resume);
__declspec(dllimport) void WINAPI Sleep(u32 milliseconds);

// The Microsoft x64 ABI requires this symbol when floating point is used (D19).
int _fltused = 0;

#else

// ---- Linux system calls ----

#define SYS_read 0
#define SYS_write 1
#define SYS_mmap 9
#define SYS_mprotect 10
#define SYS_munmap 11
#define SYS_sched_yield 24
#define SYS_mremap 25
#define SYS_nanosleep 35
#define SYS_clone 56
#define SYS_exit 60
#define SYS_arch_prctl 158
#define SYS_futex 202
#define SYS_sched_getaffinity 204
#define SYS_clock_gettime 228
#define SYS_exit_group 231
#define SYS_epoll_wait 232
#define SYS_epoll_ctl 233
#define SYS_eventfd2 290
#define SYS_epoll_create1 291
#define EINTR 4
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE 128

static i64 systemCall(i64 number, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f);

// For the modules of the library for Linux (D202): the result, or -errno.
i64 cromo_system_call(i64 number, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    return systemCall(number, a, b, c, d, e, f);
}

static i64 systemCall(i64 number, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    register i64 r10 __asm__("r10") = d;
    register i64 r8 __asm__("r8") = e;
    register i64 r9 __asm__("r9") = f;
    i64 result;
    __asm__ volatile("syscall"
                     : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return result;  // from -4095 to -1 it is an error, -errno
}

#endif

// ---- Locks, for the threads (D214, D216) ----
//
// A lock is free when its bytes are zero, so the default value of a Mutex of
// Cromo is a free lock. Windows has its SRWLOCK; on Linux a futex holds 0 free,
// 1 taken, 2 taken with threads waiting (Drepper, "Futexes are tricky").

typedef struct {
    u64 state;
} Lock;

#ifdef _WIN32

static void lockAcquire(Lock* lock) { AcquireSRWLockExclusive(lock); }
static void lockRelease(Lock* lock) { ReleaseSRWLockExclusive(lock); }

#else

static void lockAcquire(Lock* lock) {
    u32* state = (u32*)&lock->state;
    u32 seen = 0;
    if (__atomic_compare_exchange_n(state, &seen, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
    // The locks of the runtime are held briefly: a short spin before sleeping, as
    // the SRWLOCK of Windows does, avoids a sleep and a wake for each of them.
    for (u32 spins = 0; spins < 100 && seen != 2; ++spins) {
        __asm__ volatile("pause");
        seen = __atomic_load_n(state, __ATOMIC_RELAXED);
        if (seen == 0 && __atomic_compare_exchange_n(state, &seen, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
    }
    if (seen != 2) seen = __atomic_exchange_n(state, 2, __ATOMIC_ACQUIRE);
    while (seen != 0) {
        systemCall(SYS_futex, (i64)state, FUTEX_WAIT | FUTEX_PRIVATE, 2, 0, 0, 0);
        seen = __atomic_exchange_n(state, 2, __ATOMIC_ACQUIRE);
    }
}

static void lockRelease(Lock* lock) {
    u32* state = (u32*)&lock->state;
    if (__atomic_fetch_sub(state, 1, __ATOMIC_RELEASE) != 1) {
        __atomic_store_n(state, 0, __ATOMIC_RELEASE);
        systemCall(SYS_futex, (i64)state, FUTEX_WAKE | FUTEX_PRIVATE, 1, 0, 0, 0);
    }
}

#endif

// One print at a time on stdout and stderr: the compiler computes the arguments
// first, then takes this lock, writes, and leaves it in cromo_print_end. A panic
// takes it for good. A format writes into its own string, without it.
static Lock printLock;

// Until the first thread starts, the program has nobody to wait for: the locks of
// printing and of the heap are skipped, as glibc does. It changes only from the
// thread that starts that thread, never inside a print or an allocation.
static i32 threaded;

static void sharedAcquire(Lock* lock) {
    if (threaded) lockAcquire(lock);
}

static void sharedRelease(Lock* lock) {
    if (threaded) lockRelease(lock);
}

void cromo_print_begin(void) { sharedAcquire(&printLock); }

// ---- Buffered output ----

#define BUFFER_SIZE 4096

typedef struct {
    u8 data[BUFFER_SIZE];
    u32 length;
} Output;

static Output outputs[2];  // stdout, stderr

#ifdef _WIN32

static u16 wide[BUFFER_SIZE];

static void writeRaw(i64 stream, const u8* data, u32 size) {
    if (size == 0) return;
    void* handle = GetStdHandle(stream == 2 ? (u32)-12 : (u32)-11);
    u32 mode, written;
    if (GetConsoleMode(handle, &mode)) {
        // A console wants UTF-16: converting keeps non-ASCII text readable.
        int count = MultiByteToWideChar(65001, 0, (const char*)data, (int)size, wide, BUFFER_SIZE);
        if (count > 0) {
            WriteConsoleW(handle, wide, (u32)count, &written, 0);
            return;
        }
    }
    WriteFile(handle, data, size, &written, 0);
}

static NORETURN void exitProcess(i32 code) { ExitProcess((u32)code); }

#else

static void writeRaw(i64 stream, const u8* data, u32 size) {
    while (size > 0) {
        i64 written = systemCall(SYS_write, stream == 2 ? 2 : 1, (i64)data, size, 0, 0, 0);
        if (written == -EINTR) continue;
        if (written <= 0) return;  // nowhere to write: the text is lost, as with WriteFile
        data += written;
        size -= (u32)written;
    }
}

// The exit code is the lowest byte of `code`, as in C (D145).
static NORETURN void exitProcess(i32 code) {
    systemCall(SYS_exit_group, code, 0, 0, 0, 0, 0);
    __builtin_unreachable();
}

#endif

static Output* outputOf(i64 stream) { return &outputs[stream == 2 ? 1 : 0]; }

// ---- format (D200) ----
//
// A stream is 1 for stdout, 2 for stderr, or the address of the string that a
// format writes, which starts empty: each format has its own, also among threads.

void* cromo_realloc(void* memory, i64 size);

// The string of the library: data, length and capacity.
typedef struct {
    u8* data;
    i64 length;
    i64 capacity;
} Text;

static void formatPut(Text* text, const u8* data, u64 size) {
    u64 length = (u64)text->length;
    if (length + size > (u64)text->capacity) {
        u64 grown = (u64)text->capacity * 2;
        if (grown < length + size) grown = length + size;
        if (grown < 32) grown = 32;
        text->data = cromo_realloc(text->data, (i64)grown);
        text->capacity = (i64)grown;
    }
    for (u64 i = 0; i < size; ++i) text->data[length + i] = data[i];
    text->length = (i64)(length + size);
}

static void flush(i64 stream) {
    if (stream > 2) return;
    Output* out = outputOf(stream);
    writeRaw(stream, out->data, out->length);
    out->length = 0;
}

static void put(i64 stream, const u8* data, u64 size) {
    if (stream > 2) {
        formatPut((Text*)stream, data, size);
        return;
    }
    Output* out = outputOf(stream);
    while (size > 0) {
        if (out->length == BUFFER_SIZE) flush(stream);
        u32 room = BUFFER_SIZE - out->length;
        u32 chunk = size < room ? (u32)size : room;
        for (u32 i = 0; i < chunk; ++i) out->data[out->length + i] = data[i];
        out->length += chunk;
        data += chunk;
        size -= chunk;
    }
}

static void putRepeat(i64 stream, u8 c, i32 count) {
    while (count-- > 0) put(stream, &c, 1);
}

static i32 textLength(const u8* text, u32 size) {
    i32 count = 0;
    for (u32 i = 0; i < size; ++i) {
        if ((text[i] & 0xC0) != 0x80) ++count;  // UTF-8 continuation bytes are not characters
    }
    return count;
}

// ---- Format specification, filled by the compiler (D38) ----

typedef struct {
    u8 fill;
    u8 align;      // 0, '<', '>', '^'
    u8 sign;       // 0, '+', '-', ' '
    u8 alternate;  // '#'
    u8 zeroPad;    // '0'
    u8 type;       // 0 or one of "dxXbocefgs"
    u8 reserved[2];
    i32 width;     // -1 if absent
    i32 precision; // -1 if absent
} FmtSpec;

static const FmtSpec defaultSpec = {' ', 0, 0, 0, 0, 0, {0, 0}, -1, -1};

// Writes prefix (sign, 0x, ...) and body, padded to the width. Numbers align to the
// right by default, text to the left; '0' pads with zeros after the prefix.
static void emitPadded(i64 stream, const FmtSpec* spec, const u8* prefix, u32 prefixSize, const u8* body, u32 bodySize,
                       u8 defaultAlign) {
    i32 length = textLength(prefix, prefixSize) + textLength(body, bodySize);
    i32 pad = spec->width > length ? spec->width - length : 0;
    if (spec->zeroPad && spec->align == 0) {
        put(stream, prefix, prefixSize);
        putRepeat(stream, '0', pad);
        put(stream, body, bodySize);
        return;
    }
    u8 align = spec->align ? spec->align : defaultAlign;
    i32 before = align == '>' ? pad : align == '^' ? pad / 2 : 0;
    putRepeat(stream, spec->fill, before);
    put(stream, prefix, prefixSize);
    put(stream, body, bodySize);
    putRepeat(stream, spec->fill, pad - before);
}

static u32 signPrefix(int negative, const FmtSpec* spec, u8* prefix) {
    if (negative) prefix[0] = '-';
    else if (spec->sign == '+') prefix[0] = '+';
    else if (spec->sign == ' ') prefix[0] = ' ';
    else return 0;
    return 1;
}

// ---- Entry points used by the generated code ----

void cromo_write(i64 stream, const u8* data, u64 size) { put(stream, data, size); }

void cromo_fmt_str(i64 stream, const u8* data, u64 size, const FmtSpec* spec) {
    if (!spec) spec = &defaultSpec;
    if (spec->precision >= 0) {  // the precision cuts the text to that many characters
        i32 characters = 0;
        u64 end = 0;
        while (end < size) {
            if ((data[end] & 0xC0) != 0x80) {
                if (characters == spec->precision) break;
                ++characters;
            }
            ++end;
        }
        size = end;
    }
    emitPadded(stream, spec, 0, 0, data, (u32)size, '<');
}

void cromo_fmt_bool(i64 stream, i32 value, const FmtSpec* spec) {
    if (!spec) spec = &defaultSpec;
    if (value) emitPadded(stream, spec, 0, 0, (const u8*)"true", 4, '<');
    else emitPadded(stream, spec, 0, 0, (const u8*)"false", 5, '<');
}

void cromo_fmt_int(i64 stream, u64 value, i32 isSigned, const FmtSpec* spec) {
    if (!spec) spec = &defaultSpec;
    if (spec->type == 'c') {  // D63: the character of a char
        u8 c = (u8)value;
        emitPadded(stream, spec, 0, 0, &c, 1, '<');
        return;
    }
    int negative = isSigned && (i64)value < 0;
    u64 magnitude = negative ? 0 - value : value;
    u32 base = 10;
    const char* digits = "0123456789abcdef";
    if (spec->type == 'x') base = 16;
    if (spec->type == 'X') {
        base = 16;
        digits = "0123456789ABCDEF";
    }
    if (spec->type == 'b') base = 2;
    if (spec->type == 'o') base = 8;

    u8 buffer[64];
    i32 count = 0;
    do {
        buffer[63 - count++] = (u8)digits[magnitude % base];
        magnitude /= base;
    } while (magnitude);

    u8 prefix[3];
    u32 prefixSize = signPrefix(negative, spec, prefix);
    if (spec->alternate) {
        if (base == 16) {
            prefix[prefixSize++] = '0';
            prefix[prefixSize++] = spec->type == 'X' ? 'X' : 'x';
        } else if (base == 2) {
            prefix[prefixSize++] = '0';
            prefix[prefixSize++] = 'b';
        } else if (base == 8 && value != 0) {
            prefix[prefixSize++] = '0';
        }
    }
    emitPadded(stream, spec, prefix, prefixSize, buffer + 64 - count, (u32)count, '>');
}

// ---- Exact decimal digits of floating-point numbers ----
//
// The digits are computed with big integers, so they are always exact: the
// shortest form uses the algorithm of Steele & White / Burger & Dybvig, the forms
// with a precision round the exact value half to even.

// 4096 bits: enough for 10^324 * 2^55, for 2^1024 * 10, and for the 800 digits of
// a text with 10^1124 * 2^56 when it is read (D199).
#define BIG_WORDS 128

typedef struct {
    u32 word[BIG_WORDS];
    i32 used;
} Big;

static void bigSet(Big* a, u64 value) {
    a->used = 0;
    while (value) {
        a->word[a->used++] = (u32)value;
        value >>= 32;
    }
}

static void bigMulSmall(Big* a, u32 factor) {
    u64 carry = 0;
    for (i32 i = 0; i < a->used; ++i) {
        u64 product = (u64)a->word[i] * factor + carry;
        a->word[i] = (u32)product;
        carry = product >> 32;
    }
    if (carry) a->word[a->used++] = (u32)carry;
}

static void bigShiftLeft(Big* a, i32 bits) {
    if (a->used == 0 || bits == 0) return;
    i32 words = bits / 32;
    i32 rest = bits % 32;
    if (rest) {
        u32 carry = 0;
        for (i32 i = 0; i < a->used; ++i) {
            u32 word = a->word[i];
            a->word[i] = (word << rest) | carry;
            carry = word >> (32 - rest);
        }
        if (carry) a->word[a->used++] = carry;
    }
    if (words) {
        for (i32 i = a->used - 1; i >= 0; --i) a->word[i + words] = a->word[i];
        for (i32 i = 0; i < words; ++i) a->word[i] = 0;
        a->used += words;
    }
}

static void bigMulPow10(Big* a, i32 exponent) {
    static const u32 powers[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
    while (exponent >= 9) {
        bigMulSmall(a, 1000000000u);
        exponent -= 9;
    }
    if (exponent > 0) bigMulSmall(a, powers[exponent]);
}

static i32 bigCompare(const Big* a, const Big* b) {
    if (a->used != b->used) return a->used < b->used ? -1 : 1;
    for (i32 i = a->used - 1; i >= 0; --i) {
        if (a->word[i] != b->word[i]) return a->word[i] < b->word[i] ? -1 : 1;
    }
    return 0;
}

static void bigAdd(Big* result, const Big* a, const Big* b) {
    i32 count = a->used > b->used ? a->used : b->used;
    u64 carry = 0;
    for (i32 i = 0; i < count; ++i) {
        u64 sum = (u64)(i < a->used ? a->word[i] : 0) + (i < b->used ? b->word[i] : 0) + carry;
        result->word[i] = (u32)sum;
        carry = sum >> 32;
    }
    result->used = count;
    if (carry) result->word[result->used++] = (u32)carry;
}

static void bigSubtract(Big* a, const Big* b) {  // a -= b, with a >= b
    i64 borrow = 0;
    for (i32 i = 0; i < a->used; ++i) {
        i64 difference = (i64)a->word[i] - (i < b->used ? b->word[i] : 0) - borrow;
        borrow = difference < 0;
        if (difference < 0) difference += (i64)1 << 32;
        a->word[i] = (u32)difference;
    }
    while (a->used > 0 && a->word[a->used - 1] == 0) --a->used;
}

// Divides r by s, when the quotient is a single digit; r keeps the remainder.
static u32 bigDigit(Big* r, const Big* s) {
    u32 digit = 0;
    while (bigCompare(r, s) >= 0) {
        bigSubtract(r, s);
        ++digit;
    }
    return digit;
}

static void bigAddSmall(Big* a, u32 value) {
    u64 carry = value;
    for (i32 i = 0; i < a->used && carry; ++i) {
        u64 sum = (u64)a->word[i] + carry;
        a->word[i] = (u32)sum;
        carry = sum >> 32;
    }
    if (carry) a->word[a->used++] = (u32)carry;
}

static void bigShiftRightOne(Big* a) {
    for (i32 i = 0; i < a->used; ++i) {
        a->word[i] = (a->word[i] >> 1) | (i + 1 < a->used ? a->word[i + 1] << 31 : 0);
    }
    if (a->used > 0 && a->word[a->used - 1] == 0) --a->used;
}

// value = mantissa * 2^exponent
typedef struct {
    u64 mantissa;
    i32 exponent;
    i32 minExponent;   // exponent of the subnormal numbers
    i32 mantissaBits;  // 53 for f64, 24 for f32
} FloatParts;

#define MAX_DIGITS 840

// value = 0.d1 d2 d3 ... * 10^exponent; zero has no digits
typedef struct {
    u8 digit[MAX_DIGITS];
    i32 count;
    i32 exponent;
} Decimal;

static i32 bitLength(u64 value) {
    i32 bits = 0;
    while (value) {
        ++bits;
        value >>= 1;
    }
    return bits;
}

// First guess of the decimal exponent: floor(log10(2) * binaryExponent).
// The callers correct it by one step if needed.
static i32 estimateExponent(const FloatParts* f) {
    i64 binary = f->exponent + bitLength(f->mantissa) - 1;
    return (i32)((binary * 78913) >> 18);
}

// Adds one unit to the last digit: 0.999 becomes 0.100 * 10.
static void incrementLast(Decimal* d) {
    i32 i = d->count - 1;
    while (i >= 0 && d->digit[i] == 9) d->digit[i--] = 0;
    if (i >= 0) {
        d->digit[i]++;
        return;
    }
    d->digit[0] = 1;
    for (i32 j = 1; j < d->count; ++j) d->digit[j] = 0;
    d->exponent++;
}

// The shortest digits that read back to the same value (D63).
static void shortestDigits(const FloatParts* f, Decimal* out) {
    Big r, s, plus, minus, t;
    int even = (f->mantissa & 1) == 0;
    // At a power of two the next lower number is twice as close as the next higher one.
    int unequalGaps = f->mantissa == (1ull << (f->mantissaBits - 1)) && f->exponent > f->minExponent;

    if (f->exponent >= 0) {
        bigSet(&r, f->mantissa);
        bigShiftLeft(&r, f->exponent + (unequalGaps ? 2 : 1));
        bigSet(&s, unequalGaps ? 4 : 2);
        bigSet(&plus, 1);
        bigShiftLeft(&plus, f->exponent + (unequalGaps ? 1 : 0));
        bigSet(&minus, 1);
        bigShiftLeft(&minus, f->exponent);
    } else {
        bigSet(&r, f->mantissa);
        bigShiftLeft(&r, unequalGaps ? 2 : 1);
        bigSet(&s, 1);
        bigShiftLeft(&s, -f->exponent + (unequalGaps ? 2 : 1));
        bigSet(&plus, unequalGaps ? 2 : 1);
        bigSet(&minus, 1);
    }

    i32 k = estimateExponent(f);
    if (k >= 0) {
        bigMulPow10(&s, k);
    } else {
        bigMulPow10(&r, -k);
        bigMulPow10(&plus, -k);
        bigMulPow10(&minus, -k);
    }
    // Make 10^(k-1) <= upper boundary < 10^k.
    for (;;) {
        bigAdd(&t, &r, &plus);
        i32 c = bigCompare(&t, &s);
        if (!(even ? c >= 0 : c > 0)) break;
        bigMulSmall(&s, 10);
        ++k;
    }
    for (;;) {
        bigAdd(&t, &r, &plus);
        bigMulSmall(&t, 10);
        i32 c = bigCompare(&t, &s);
        if (even ? c >= 0 : c > 0) break;
        bigMulSmall(&r, 10);
        bigMulSmall(&plus, 10);
        bigMulSmall(&minus, 10);
        --k;
    }

    out->count = 0;
    out->exponent = k;
    for (;;) {
        bigMulSmall(&r, 10);
        bigMulSmall(&plus, 10);
        bigMulSmall(&minus, 10);
        u32 digit = bigDigit(&r, &s);
        i32 c = bigCompare(&r, &minus);
        int low = even ? c <= 0 : c < 0;
        bigAdd(&t, &r, &plus);
        c = bigCompare(&t, &s);
        int high = even ? c >= 0 : c > 0;
        if (!low && !high && out->count < MAX_DIGITS - 1) {
            out->digit[out->count++] = (u8)digit;
            continue;
        }
        if (low && high) {
            t = r;
            bigShiftLeft(&t, 1);
            c = bigCompare(&t, &s);
            if (c > 0 || (c == 0 && (digit & 1))) ++digit;
        } else if (high) {
            ++digit;
        }
        if (digit == 10) {
            out->digit[out->count++] = 9;
            incrementLast(out);
        } else {
            out->digit[out->count++] = (u8)digit;
        }
        break;
    }
    while (out->count > 1 && out->digit[out->count - 1] == 0) --out->count;
}

// Correctly rounded digits: `significant` digits if it is >= 0, otherwise all the
// digits down to 10^-decimals. Ties round half to even.
static void exactDigits(const FloatParts* f, i32 significant, i32 decimals, Decimal* out) {
    out->count = 0;
    out->exponent = 1;
    if (f->mantissa == 0) return;

    Big r, s, t;
    bigSet(&r, f->mantissa);
    bigSet(&s, 1);
    if (f->exponent >= 0) bigShiftLeft(&r, f->exponent);
    else bigShiftLeft(&s, -f->exponent);

    i32 k = estimateExponent(f);
    if (k >= 0) bigMulPow10(&s, k);
    else bigMulPow10(&r, -k);
    while (bigCompare(&r, &s) >= 0) {  // value < 10^k
        bigMulSmall(&s, 10);
        ++k;
    }
    for (;;) {  // value >= 10^(k-1)
        t = r;
        bigMulSmall(&t, 10);
        if (bigCompare(&t, &s) >= 0) break;
        bigMulSmall(&r, 10);
        --k;
    }

    i32 count = significant >= 0 ? significant : k + decimals;
    if (count > MAX_DIGITS) count = MAX_DIGITS;
    out->exponent = k;
    if (count < 0) return;  // far below half a unit: rounds to zero
    if (count == 0) {       // only the rounding decides: 0 or one unit
        t = r;
        bigShiftLeft(&t, 1);
        if (bigCompare(&t, &s) > 0) {
            out->digit[0] = 1;
            out->count = 1;
            out->exponent = k + 1;
        }
        return;
    }
    for (i32 i = 0; i < count; ++i) {
        bigMulSmall(&r, 10);
        out->digit[out->count++] = (u8)bigDigit(&r, &s);
    }
    t = r;
    bigShiftLeft(&t, 1);
    i32 c = bigCompare(&t, &s);
    if (c > 0 || (c == 0 && (out->digit[out->count - 1] & 1))) incrementLast(out);
}

// The digits of {:.Nf} without big integers when they fit: the value is m * 2^e,
// so m * 10^p shifted right by -e bits, rounded half to even on what the shift
// drops, is the exact result of exactDigits. 128 bits hold m * 10^p for p up to
// 19; the result must fit in 64 bits. Returns 0 when it does not apply.
static int fastFixed(const FloatParts* f, i32 decimals, Decimal* out) {
    if (decimals > 19) return 0;
    u64 power = 1;
    for (i32 i = 0; i < decimals; ++i) power *= 10;
    unsigned __int128 scaled = (unsigned __int128)f->mantissa * power;
    unsigned __int128 rounded;
    if (f->exponent >= 0) {
        if (f->exponent >= 64 || (scaled >> (64 - f->exponent)) != 0) return 0;
        rounded = scaled << f->exponent;
    } else if (-f->exponent >= 128) {
        rounded = 0;  // less than 2^117, far below half of 2^128
    } else {
        i32 shift = -f->exponent;
        rounded = scaled >> shift;
        unsigned __int128 rest = scaled - (rounded << shift);
        unsigned __int128 half = (unsigned __int128)1 << (shift - 1);
        if (rest > half || (rest == half && (rounded & 1))) ++rounded;
        if ((rounded >> 64) != 0) return 0;
    }
    u64 value = (u64)rounded;
    out->count = 0;
    out->exponent = 1;
    if (value == 0) return 1;
    u8 reversed[20];
    i32 length = 0;
    while (value) {
        reversed[length++] = (u8)(value % 10);
        value /= 10;
    }
    for (i32 i = 0; i < length; ++i) out->digit[i] = reversed[length - 1 - i];
    out->count = length;
    out->exponent = length - decimals;  // 0.d1 d2 ... * 10^exponent is the value
    return 1;
}

static u8 digitAt(const Decimal* d, i32 index) {
    return (u8)('0' + (index >= 0 && index < d->count ? d->digit[index] : 0));
}

// Fixed notation with `decimals` digits after the point.
static u32 writeFixed(const Decimal* d, i32 decimals, int forcePoint, u8* out) {
    u32 n = 0;
    if (d->count == 0 || d->exponent <= 0) out[n++] = '0';
    else for (i32 i = 0; i < d->exponent; ++i) out[n++] = digitAt(d, i);
    if (decimals > 0 || forcePoint) out[n++] = '.';
    for (i32 i = 0; i < decimals; ++i) out[n++] = digitAt(d, d->exponent + i);
    return n;
}

// Scientific notation d.ddde+XX with `decimals` digits after the point.
static u32 writeScientific(const Decimal* d, i32 decimals, int forcePoint, u8* out) {
    u32 n = 0;
    out[n++] = digitAt(d, 0);
    if (decimals > 0 || forcePoint) out[n++] = '.';
    for (i32 i = 1; i <= decimals; ++i) out[n++] = digitAt(d, i);
    i32 e = d->count > 0 ? d->exponent - 1 : 0;
    out[n++] = 'e';
    out[n++] = e < 0 ? '-' : '+';
    if (e < 0) e = -e;
    if (e >= 100) out[n++] = (u8)('0' + e / 100);
    out[n++] = (u8)('0' + e / 10 % 10);
    out[n++] = (u8)('0' + e % 10);
    return n;
}

static void stripZeros(Decimal* d) {
    while (d->count > 0 && d->digit[d->count - 1] == 0) --d->count;
}

static void formatFloat(i64 stream, const FloatParts* f, int negative, int isNan, int isInf, const FmtSpec* spec) {
    if (!spec) spec = &defaultSpec;
    u8 prefix[1];
    u32 prefixSize = signPrefix(negative, spec, prefix);

    if (isNan || isInf) {
        FmtSpec noZeros = *spec;  // inf and nan are padded with spaces, never with zeros
        noZeros.zeroPad = 0;
        emitPadded(stream, &noZeros, prefix, prefixSize, (const u8*)(isNan ? "nan" : "inf"), 3, '>');
        return;
    }

    // On the stack: format does not take the lock of printing, and several threads
    // can write numbers at once (D221).
    u8 text[MAX_DIGITS + 32];
    Decimal d;
    u32 n = 0;
    int alternate = spec->alternate;
    i32 precision = spec->precision;

    if (spec->type == 'e') {
        i32 p = precision < 0 ? 6 : precision;
        exactDigits(f, p + 1, -1, &d);
        n = writeScientific(&d, p, alternate, text);
    } else if (spec->type == 'f') {
        i32 p = precision < 0 ? 6 : precision;
        if (!fastFixed(f, p, &d)) exactDigits(f, -1, p, &d);
        n = writeFixed(&d, p, alternate, text);
    } else if (spec->type == 'g' || precision >= 0) {
        i32 p = precision < 0 ? 6 : precision == 0 ? 1 : precision;
        exactDigits(f, p, -1, &d);
        i32 x = d.count > 0 ? d.exponent - 1 : 0;  // exponent of the rounded value
        int fixed = x >= -4 && x < p;
        if (!alternate) stripZeros(&d);
        i32 decimals = fixed ? (alternate ? p - 1 - x : (d.count > d.exponent ? d.count - d.exponent : 0))
                             : (alternate ? p - 1 : (d.count > 1 ? d.count - 1 : 0));
        n = fixed ? writeFixed(&d, decimals, alternate, text) : writeScientific(&d, decimals, alternate, text);
    } else if (f->mantissa == 0) {
        text[n++] = '0';
    } else {
        // Shortest form, in fixed or scientific notation, whichever is shorter (as std::format).
        shortestDigits(f, &d);
        i32 k = d.exponent;
        i32 count = d.count;
        i32 fixedLength = k <= 0 ? 2 - k + count : (k >= count ? k : count + 1);
        i32 e = k - 1;
        i32 scientificLength = count + (count > 1 ? 1 : 0) + 2 + (e >= 100 || e <= -100 ? 3 : 2);
        if (fixedLength <= scientificLength) {
            n = writeFixed(&d, count > k ? count - k : 0, alternate, text);
        } else {
            n = writeScientific(&d, count - 1, alternate, text);
        }
    }
    emitPadded(stream, spec, prefix, prefixSize, text, n, '>');
}

void cromo_fmt_f64(i64 stream, double value, const FmtSpec* spec) {
    union {
        double d;
        u64 bits;
    } pun;
    pun.d = value;
    u64 bits = pun.bits;
    u32 exponent = (u32)(bits >> 52) & 0x7FF;
    u64 fraction = bits & ((1ull << 52) - 1);
    FloatParts f;
    f.minExponent = -1074;
    f.mantissaBits = 53;
    if (exponent == 0) {
        f.mantissa = fraction;
        f.exponent = -1074;
    } else {
        f.mantissa = fraction | (1ull << 52);
        f.exponent = (i32)exponent - 1075;
    }
    formatFloat(stream, &f, (int)(bits >> 63), exponent == 0x7FF && fraction != 0, exponent == 0x7FF && fraction == 0, spec);
}

void cromo_fmt_f32(i64 stream, float value, const FmtSpec* spec) {
    union {
        float f;
        u32 bits;
    } pun;
    pun.f = value;
    u32 bits = pun.bits;
    u32 exponent = (bits >> 23) & 0xFF;
    u32 fraction = bits & ((1u << 23) - 1);
    FloatParts f;
    f.minExponent = -149;
    f.mantissaBits = 24;
    if (exponent == 0) {
        f.mantissa = fraction;
        f.exponent = -149;
    } else {
        f.mantissa = fraction | (1u << 23);
        f.exponent = (i32)exponent - 150;
    }
    formatFloat(stream, &f, (int)(bits >> 31), exponent == 0xFF && fraction != 0, exponent == 0xFF && fraction == 0, spec);
}

// ---- Hashes for the keys of a map (D198) ----

// The finalizer of SplitMix64: every bit of the input changes half of the output.
u64 cromo_hash_bits(u64 x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Eight bytes at a time, each word multiplied in, and the length; then mixed by
// cromo_hash_bits, so that the low bits depend on all of them.
typedef u64 __attribute__((aligned(1), may_alias)) Word;

u64 cromo_hash_bytes(const u8* data, i64 size) {
    u64 hash = 0xCBF29CE484222325ull ^ (u64)size;
    i64 i = 0;
    for (; i + 8 <= size; i += 8) {
        hash = (hash ^ *(const Word*)(data + i)) * 0x9E3779B97F4A7C15ull;
        hash ^= hash >> 29;
    }
    u64 rest = 0;
    for (i64 k = 0; i + k < size; ++k) rest |= (u64)data[i + k] << (8 * k);
    hash = (hash ^ rest) * 0x9E3779B97F4A7C15ull;
    return cromo_hash_bits(hash);
}

// ---- Numbers from text (D199) ----

#define MAX_READ_DIGITS 800  // more than the 767 that can decide the rounding of an f64

static i32 bigBitLength(const Big* a) {
    if (a->used == 0) return 0;
    return (a->used - 1) * 32 + bitLength(a->word[a->used - 1]);
}

// The text from `text` is `word`, in any case.
static int sameWord(const u8* text, u64 size, const char* word) {
    u64 i = 0;
    for (; word[i]; ++i) {
        if (i == size || (text[i] | 0x20) != (u8)word[i]) return 0;
    }
    return i == size;
}

// The nearest f64 (bits 64) or f32 (bits 32) to a text that the library has
// already checked: [+|-] digits [. digits] [e [+|-] digits], inf, infinity or nan.
// The value r / s is divided with big integers, so the rounding, half to even,
// is always exact. Too large a value gives an infinity: the library reports it.
double cromo_parse_float(const u8* text, u64 size, i64 bits) {
    u64 i = 0;
    int negative = 0;
    if (i < size && (text[i] == '+' || text[i] == '-')) negative = text[i++] == '-';
    double sign = negative ? -1.0 : 1.0;
    if (sameWord(text + i, size - i, "nan")) return __builtin_nan("");
    if (sameWord(text + i, size - i, "inf") || sameWord(text + i, size - i, "infinity")) return sign * __builtin_inf();

    // value = digits * 10^exponent, the digits without the zeros in front.
    u8 digits[MAX_READ_DIGITS];
    i32 count = 0;
    int dropped = 0;  // nonzero digits beyond the ones kept
    i64 exponent = 0;
    int point = 0;
    for (; i < size && text[i] != 'e' && text[i] != 'E'; ++i) {
        u8 c = text[i];
        if (c == '.') {
            point = 1;
        } else if (count == 0 && c == '0') {
            if (point) --exponent;
        } else if (count < MAX_READ_DIGITS) {
            digits[count++] = (u8)(c - '0');
            if (point) --exponent;
        } else {
            dropped |= c != '0';
            if (!point) ++exponent;
        }
    }
    if (i < size) {
        ++i;
        int down = 0;
        if (text[i] == '+' || text[i] == '-') down = text[i++] == '-';
        i64 written = 0;
        for (; i < size; ++i) {
            if (written < 100000) written = written * 10 + (text[i] - '0');
        }
        exponent += down ? -written : written;
    }
    if (count == 0) return sign * 0.0;
    if (count + exponent > 310) return sign * __builtin_inf();  // above 10^309
    if (count + exponent < -330) return sign * 0.0;            // below 10^-330, half of the smallest

    i32 mantissaBits = bits == 32 ? 24 : 53;
    i32 minExponent = bits == 32 ? -149 : -1074;  // of the last bit of the subnormal numbers
    i32 maxExponent = bits == 32 ? 127 : 1023;    // of the first bit of the largest number
    Big r;
    Big s;
    bigSet(&r, 0);
    for (i32 k = 0; k < count; ++k) {
        bigMulSmall(&r, 10);
        bigAddSmall(&r, digits[k]);
    }
    bigSet(&s, 1);
    if (exponent >= 0) bigMulPow10(&r, (i32)exponent);
    else bigMulPow10(&s, (i32)-exponent);

    // q = floor(r / s / 2^t) gets at least two bits more than the mantissa; the
    // subnormal numbers have fewer, down to the last bit and one to round.
    i32 t = bigBitLength(&r) - bigBitLength(&s) - (mantissaBits + 2);
    if (t < minExponent - 1) t = minExponent - 1;
    if (t < 0) bigShiftLeft(&r, -t);
    else bigShiftLeft(&s, t);
    Big divisor = s;
    bigShiftLeft(&divisor, 56);
    u64 q = 0;
    for (i32 b = 56; b >= 0; --b) {
        q <<= 1;
        if (bigCompare(&r, &divisor) >= 0) {
            bigSubtract(&r, &divisor);
            q |= 1;
        }
        bigShiftRightOne(&divisor);
    }
    int inexact = dropped || r.used > 0;
    while (q >= (u64)1 << (mantissaBits + 1)) {  // the mantissa and the bit to round
        inexact |= (int)(q & 1);
        q >>= 1;
        ++t;
    }
    u64 mantissa = q >> 1;
    if ((q & 1) && (inexact || (mantissa & 1))) ++mantissa;
    i32 last = t + 1;  // value = mantissa * 2^last
    if (mantissa == (u64)1 << mantissaBits) {
        mantissa >>= 1;
        ++last;
    }
    if (mantissa == 0) return sign * 0.0;
    if (last + bitLength(mantissa) - 1 > maxExponent) return sign * __builtin_inf();
    int normal = mantissa >> (mantissaBits - 1) != 0;
    if (bits == 32) {
        u32 pattern = normal ? ((u32)(last + 23 + 127) << 23) | ((u32)mantissa & 0x7FFFFF) : (u32)mantissa;
        if (negative) pattern |= 0x80000000u;
        float value;
        memcpy(&value, &pattern, sizeof value);
        return value;
    }
    u64 pattern = normal ? ((u64)(last + 52 + 1023) << 52) | (mantissa & 0xFFFFFFFFFFFFFull) : mantissa;
    if (negative) pattern |= (u64)1 << 63;
    double value;
    memcpy(&value, &pattern, sizeof value);
    return value;
}

// ---- End of a print, exit and panic ----

void cromo_print_end(i64 stream, i32 newline) {
    if (newline) put(stream, (const u8*)"\n", 1);
    flush(stream);
    sharedRelease(&printLock);
}

NORETURN void cromo_exit(i32 code) {
    sharedAcquire(&printLock);
    flush(1);
    flush(2);
    exitProcess(code);
}

// Where a panic happens: "<file>:<line>:<column>", a constant built by the compiler.
typedef struct {
    const u8* text;
    u64 size;
} Where;

static void panicStart(void) {
    sharedAcquire(&printLock);
    flush(1);
    put(2, (const u8*)"panic: ", 7);
}

static NORETURN void panicEnd(const Where* where) {
    if (where) {
        put(2, (const u8*)" at ", 4);
        put(2, where->text, where->size);
    }
    put(2, (const u8*)"\n", 1);
    flush(2);
    exitProcess(101);
}

// D62: "panic: <reason> at <file>:<line>:<column>" on stderr, exit code 101.
NORETURN void cromo_panic(const u8* message, u64 size, const Where* where) {
    panicStart();
    put(2, message, size);
    panicEnd(where);
}

// D30: an index out of the bounds of a list.
NORETURN void cromo_panic_index(i64 index, i64 length, const Where* where) {
    panicStart();
    put(2, (const u8*)"index out of bounds: the index is ", 34);
    cromo_fmt_int(2, (u64)index, 1, 0);
    put(2, (const u8*)" but the length is ", 19);
    cromo_fmt_int(2, (u64)length, 1, 0);
    panicEnd(where);
}

static NORETURN void uncaught(const char* prefix, u32 prefixSize, const u8* message, u64 size) {
    sharedAcquire(&printLock);
    flush(1);
    put(2, (const u8*)prefix, prefixSize);
    put(2, message, size);
    put(2, (const u8*)"\n", 1);
    flush(2);
    exitProcess(1);
}

// D121: an exception that leaves main, with its message; exit code 1.
NORETURN void cromo_uncaught(const u8* message, u64 size) { uncaught("uncaught exception: ", 20, message, size); }

// ---- Heap, for list and string (D98) ----

// Without the lock of printing: memory can run out while a format holds it.
static NORETURN void outOfMemory(void) {
    writeRaw(2, (const u8*)"panic: out of memory\n", 21);
    exitProcess(101);
}

#ifdef _WIN32

static void* heapAlloc(u64 size) { return HeapAlloc(GetProcessHeap(), 0, size); }
static void* heapRealloc(void* memory, u64 size) { return HeapReAlloc(GetProcessHeap(), 0, memory, size); }
static void heapFree(void* memory) { HeapFree(GetProcessHeap(), 0, memory); }

#else

// A simple allocator on top of mmap, under the lock of the heap. Every block
// starts with 16 bytes that hold how many bytes it can contain. The small blocks,
// up to 128 KB with those 16 bytes, have a size that is a power of two from 32
// bytes: they are cut from areas of 4 MB, whose pages the system gives when they
// are used, and the freed ones are kept in a list for each size. The larger
// blocks have a mapping of their own, as for malloc of glibc: a mapping for every
// block of a few KB costs two system calls, which wait for each other in a
// process with threads.

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MREMAP_MAYMOVE 1
#define HEADER 16
#define SMALL_LIMIT (128u << 10)
#define SMALL_SIZES 13  // 32, 64, ..., 128 KB
#define AREA_SIZE (4u << 20)

typedef struct FreeBlock {
    struct FreeBlock* next;
} FreeBlock;

static FreeBlock* freeBlocks[SMALL_SIZES];
static u8* areaNext;
static u8* areaEnd;

static u8* mapPages(u64 size) {
    i64 result = systemCall(SYS_mmap, 0, (i64)size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return result < 0 && result > -4096 ? 0 : (u8*)result;
}

static u64 pagesFor(u64 size) { return (size + 4095) & ~(u64)4095; }

static i32 sizeIndex(u64 total) {
    i32 index = 0;
    while (((u64)32 << index) < total) ++index;
    return index;
}

static u64* headerOf(void* memory) { return (u64*)((u8*)memory - HEADER); }

static void* heapAlloc(u64 size) {
    u64 total = size + HEADER;
    u8* block;
    if (total <= SMALL_LIMIT) {
        i32 index = sizeIndex(total);
        total = (u64)32 << index;
        if (freeBlocks[index]) {
            block = (u8*)freeBlocks[index];
            freeBlocks[index] = freeBlocks[index]->next;
        } else {
            if ((u64)(areaEnd - areaNext) < total) {  // the rest of the old area is not used, nor touched
                areaNext = mapPages(AREA_SIZE);
                if (!areaNext) return 0;
                areaEnd = areaNext + AREA_SIZE;
            }
            block = areaNext;
            areaNext += total;
        }
    } else {
        total = pagesFor(total);
        block = mapPages(total);
        if (!block) return 0;
    }
    *(u64*)block = total - HEADER;
    return block + HEADER;
}

static void heapFree(void* memory) {
    u64 total = *headerOf(memory) + HEADER;
    if (total <= SMALL_LIMIT) {
        FreeBlock* block = (FreeBlock*)headerOf(memory);
        i32 index = sizeIndex(total);
        block->next = freeBlocks[index];
        freeBlocks[index] = block;
    } else {
        systemCall(SYS_munmap, (i64)headerOf(memory), (i64)total, 0, 0, 0, 0);
    }
}

static void* heapRealloc(void* memory, u64 size) {
    u64 capacity = *headerOf(memory);
    if (size <= capacity) return memory;
    if (capacity + HEADER > SMALL_LIMIT) {  // a large block grows with its mapping
        u64 total = pagesFor(size + HEADER);
        i64 block = systemCall(SYS_mremap, (i64)headerOf(memory), (i64)(capacity + HEADER), (i64)total, MREMAP_MAYMOVE,
                               0, 0);
        if (block < 0 && block > -4096) return 0;
        *(u64*)block = total - HEADER;
        return (u8*)block + HEADER;
    }
    void* result = heapAlloc(size);
    if (!result) return 0;
    memcpy(result, memory, capacity);
    heapFree(memory);
    return result;
}

#endif

// The heap of Windows has its own lock; the allocator for Linux takes this one.
#ifdef _WIN32
static void heapLockAcquire(void) {}
static void heapLockRelease(void) {}
#else
static Lock heapLock;
static void heapLockAcquire(void) { sharedAcquire(&heapLock); }
static void heapLockRelease(void) { sharedRelease(&heapLock); }
#endif

void* cromo_alloc(i64 size) {
    if (size < 0) outOfMemory();
    heapLockAcquire();
    void* memory = heapAlloc((u64)size);
    heapLockRelease();
    if (!memory) outOfMemory();
    return memory;
}

void* cromo_realloc(void* memory, i64 size) {
    if (!memory) return cromo_alloc(size);
    if (size < 0) outOfMemory();
    heapLockAcquire();
    void* result = heapRealloc(memory, (u64)size);
    heapLockRelease();
    if (!result) outOfMemory();
    return result;
}

void cromo_free(void* memory) {
    if (!memory) return;
    heapLockAcquire();
    heapFree(memory);
    heapLockRelease();
}

// ---- Threads and tasks (D214, D217, D269) ----

// What the library keeps of a thread, of a task and of a group: the code and the
// data of the lambda, and the exception that leaves it, as the slot of a call that
// can throw (D36). The functions cromo_thread_* take any of them.
#define KIND_THREAD 0
#define KIND_TASK 1
#define KIND_GROUP 2
#define KIND_GROUP_TASK 3  // a task of a group: nobody joins it

typedef struct {
    void (*code)(void* env, void* raised);
    void* env;
    void* raised[2];  // the object and its descriptor, or nothing
    i64 kind;
} Runnable;

typedef struct Worker Worker;

#ifndef _WIN32
// What the register fs points to in every thread, so that a thread knows whether
// it is a worker of the tasks: the first word is its own address.
typedef struct ThreadBlock {
    struct ThreadBlock* self;
    Worker* worker;
} ThreadBlock;

// The main thread: _start (mem_x64.S) points fs here.
ThreadBlock cromo_main_block = {&cromo_main_block, 0};
#endif

typedef struct {
    Runnable head;
#ifdef _WIN32
    void* handle;
#else
    i32 tid;  // the kernel clears it, and wakes its futex, when the thread ends
    u8* stack;
    ThreadBlock block;
#endif
} ThreadRecord;

static void runThread(ThreadRecord* thread) { thread->head.code(thread->head.env, thread->head.raised); }

static NORETURN void threadFailure(void) {
    static const char text[] = "cannot start a thread: the system has no resources for it";
    cromo_panic((const u8*)text, sizeof text - 1, 0);
}

static void relax(u32 spins) {
    if (spins < 64) {
        __asm__ volatile("pause");
        return;
    }
#ifdef _WIN32
    SwitchToThread();
#else
    systemCall(SYS_sched_yield, 0, 0, 0, 0, 0, 0);
#endif
}

#ifdef _WIN32

static u32 WINAPI threadEntry(void* argument) {
    runThread(argument);
    return 0;
}

static void startSystemThread(ThreadRecord* thread) {
    thread->handle = CreateThread(0, 0, threadEntry, thread, 0, 0);  // the stack of the executable, 1 MB
    if (!thread->handle) threadFailure();
}

static void waitSystemThread(ThreadRecord* thread) { WaitForSingleObject(thread->handle, 0xFFFFFFFF); }
static void freeSystemThread(ThreadRecord* thread) { CloseHandle(thread->handle); }

#else

#define THREAD_STACK ((u64)8 << 20)  // as the main thread; the pages are taken when used
// VM, FS, FILES, SIGHAND, THREAD, SYSVSEM, SETTLS, PARENT_SETTID, CHILD_CLEARTID
#define CLONE_FLAGS 0x3D0F00

static void threadEntry(void* argument) { runThread(argument); }

// clone() without the C library: the new thread starts on its own stack, takes the
// function and its argument from there, and ends with exit(), never returning here.
// Its fs points to the block of its record.
static i64 cloneThread(ThreadRecord* thread, u8* top) {
    u64* sp = (u64*)top;
    *--sp = (u64)thread;
    *--sp = (u64)threadEntry;
    register i64 r10 __asm__("r10") = (i64)&thread->tid;
    register i64 r8 __asm__("r8") = (i64)&thread->block;
    i64 result;
    __asm__ volatile("syscall\n\t"
                     "test %%rax, %%rax\n\t"
                     "jnz 1f\n\t"
                     "xor %%ebp, %%ebp\n\t"
                     "pop %%rax\n\t"
                     "pop %%rdi\n\t"
                     "call *%%rax\n\t"
                     "mov $60, %%eax\n\t"
                     "xor %%edi, %%edi\n\t"
                     "syscall\n\t"
                     "hlt\n"
                     "1:"
                     : "=a"(result)
                     : "a"(SYS_clone), "D"(CLONE_FLAGS), "S"(sp), "d"(&thread->tid), "r"(r10), "r"(r8)
                     : "rcx", "r11", "memory");
    return result;
}

static void startSystemThread(ThreadRecord* thread) {
    i64 stack = systemCall(SYS_mmap, 0, (i64)THREAD_STACK, 3, 0x22 | 0x20000, -1, 0);  // MAP_PRIVATE|ANONYMOUS|STACK
    if (stack < 0 && stack > -4096) threadFailure();
    thread->stack = (u8*)stack;
    systemCall(SYS_mprotect, stack, 4096, 0, 0, 0, 0);  // a guard page under the stack
    thread->block.self = &thread->block;
    if (cloneThread(thread, thread->stack + THREAD_STACK) < 0) threadFailure();
}

static void waitSystemThread(ThreadRecord* thread) {
    for (;;) {
        i32 tid = __atomic_load_n(&thread->tid, __ATOMIC_ACQUIRE);
        if (tid == 0) return;
        systemCall(SYS_futex, (i64)&thread->tid, FUTEX_WAIT, tid, 0, 0, 0);
    }
}

static void freeSystemThread(ThreadRecord* thread) { systemCall(SYS_munmap, (i64)thread->stack, (i64)THREAD_STACK, 0, 0, 0, 0); }

static void futexWait(u32* address, u32 value) {
    systemCall(SYS_futex, (i64)address, FUTEX_WAIT | FUTEX_PRIVATE, value, 0, 0, 0);
}

static void futexWake(u32* address, i32 count) {
    systemCall(SYS_futex, (i64)address, FUTEX_WAKE | FUTEX_PRIVATE, count, 0, 0, 0);
}

#endif

static ThreadRecord* newThreadRecord(void* code, void* env) {
    ThreadRecord* thread = cromo_alloc(sizeof(ThreadRecord));
    u8* bytes = (u8*)thread;
    for (u64 i = 0; i < sizeof(ThreadRecord); ++i) bytes[i] = 0;
    thread->head.code = (void (*)(void*, void*))code;
    thread->head.env = env;
    thread->head.kind = KIND_THREAD;
    return thread;
}

// A thread of the runtime: the workers, the monitor and the poller.
static void startRuntimeThread(ThreadRecord* thread, void (*code)(void*, void*), void* argument) {
    thread->head.code = code;
    thread->head.env = argument;
    thread->head.kind = KIND_THREAD;
    startSystemThread(thread);
}

// ---- Conditions: a thread of the system sleeps until another calls it ----

typedef struct {
    u64 state;  // a CONDITION_VARIABLE on Windows, a futex on Linux
} Condition;

#ifdef _WIN32

static void conditionWait(Condition* condition, Lock* lock) {
    SleepConditionVariableSRW(condition, lock, 0xFFFFFFFF, 0);
}
static void conditionSignal(Condition* condition) { WakeConditionVariable(condition); }

#else

static void conditionWait(Condition* condition, Lock* lock) {
    u32* sequence = (u32*)&condition->state;
    u32 seen = __atomic_load_n(sequence, __ATOMIC_ACQUIRE);
    lockRelease(lock);
    futexWait(sequence, seen);
    lockAcquire(lock);
}

static void conditionSignal(Condition* condition) {
    u32* sequence = (u32*)&condition->state;
    __atomic_add_fetch(sequence, 1, __ATOMIC_RELEASE);
    futexWake(sequence, 1);
}

#endif

// ---- Tasks (D269-D273) ----
//
// Many tasks run on a few threads of the system, the workers, one for each
// processor (D269). A task runs on a stack of its own, 1 MB reserved and taken
// page by page (D272): a fiber on Windows; on Linux a mapping, entered and left
// with cromo_switch (mem_x64.S). The stacks are kept for the next tasks.
//
// A task that waits leaves its worker, which takes the next task of the queue;
// whoever wakes it puts it back in the queue, once its worker has left its stack
// (`running`). The waits:
// - a mutex, join() and the end of a group: a task parks and a thread of the
//   system sleeps, in lists keyed by an address, as the futexes of Linux;
// - the network and time.sleep: the poller, a thread that waits with epoll on
//   Linux and with the polls of AFD on an IOCP on Windows, keeps the timers;
// - a call that blocks the thread of a task keeps it: the monitor, every
//   millisecond while tasks wait, gives the place of a worker that has run the same
//   task all that time to another worker (D271).

#define WAIT_NONE 0
#define WAIT_PARKED 1
#define WAIT_WOKEN 2

#define TIMER_WAKE 0    // the timer wakes the task: time.sleep, and a wait for a socket on Linux
#define TIMER_CANCEL 1  // the timer cancels the poll of AFD, whose end wakes the task (Windows)

typedef struct Stack Stack;
typedef struct Group Group;

#ifdef _WIN32
typedef struct {
    i32 status;
    i32 padding;
    u64 information;
} IoStatus;  // IO_STATUS_BLOCK

typedef struct {
    i64 timeout;
    u32 count;
    u32 exclusive;
    void* handle;
    u32 events;
    i32 status;
} AfdPoll;  // AFD_POLL_INFO with one AFD_POLL_HANDLE_INFO
#endif

typedef struct Task {
    Runnable head;
    struct Task* next;  // in the queue of the tasks ready to run
    Stack* stack;       // from when it first runs to its end
    Group* group;       // of a task of a group (D273)
    u32 finished;       // its lambda has returned: join() goes on
    u32 running;        // its worker has not left its stack yet
    u32 waitState;      // WAIT_PARKED until the first of the event and the timer wakes it
    i32 ready;          // woken by the event, not by the timer
    i64 deadline;       // of its timer, on the monotonic clock in nanoseconds
    i64 timerIndex;     // in the heap of the timers, -1 without a timer
    i32 timerKind;
#ifdef _WIN32
    IoStatus status;  // of its poll of AFD: the completion points here
    AfdPoll poll;
#else
    struct Task* nextWaiter;  // among those that wait for a socket
#endif
} Task;

struct Stack {
    Stack* next;    // among the stacks kept for the next tasks
    void* context;  // Windows: the fiber; Linux: the stack pointer while it does not run
    Task* task;     // the task it runs
#ifndef _WIN32
    u8* base;  // the mapping, with the guard page at the bottom
#endif
};

struct Group {
    Runnable head;  // the first exception of its tasks (D273)
    u32 running;    // its tasks not yet ended
    Lock lock;      // for the exception
};

struct Worker {
    void* context;   // Windows: the fiber of its thread; Linux: its stack pointer while a task runs
    Task* current;   // the task it runs
    i32 ended;       // the task has returned
    i32 stuck;       // the monitor gave its place to another worker (D271)
    u64 switches;    // tasks entered and left: the monitor sees whether it moves
    u64 seen;        // the switches at the start of the tick of the monitor
    Worker* next;    // in the list of all the workers
    ThreadRecord thread;
};

// The workers: `target` of them run tasks at the same time, one for each
// processor; the monitor can give the place of a stuck one to another (D271).
#define EXTRA_WORKERS 256

static struct {
    Lock lock;
    Task* first;  // the queue of the tasks ready to run
    Task* last;
    i64 queued;
    i32 target;      // places: workers that run tasks at the same time
    i32 places;      // places without a worker
    i32 idle;        // workers with a place, waiting for a task
    i32 idleCalls;   // idle workers called for a task, not yet awake
    i32 spares;      // workers without a place
    i32 spareCalls;  // spares given a place, not yet awake
    i32 workers;     // all the workers
    i32 monitorSleeping;
    Worker* all;
    Condition work;
    Condition spare;
    Condition monitorCall;
    ThreadRecord monitor;
    u32 state;  // 0, 1 while it starts, 2 started
} scheduler;

#ifdef _WIN32

static u32 tlsIndex = 0xFFFFFFFF;  // TLS_OUT_OF_INDEXES until the first task

static Worker* currentWorker(void) {
    u32 index = __atomic_load_n(&tlsIndex, __ATOMIC_ACQUIRE);
    return index == 0xFFFFFFFF ? 0 : (Worker*)TlsGetValue(index);
}

#else

static Worker* currentWorker(void) {
    ThreadBlock* block;
    __asm__ volatile("mov %%fs:0, %0" : "=r"(block));
    return block->worker;
}

#endif

static Task* currentTask(void) {
    Worker* worker = currentWorker();
    return worker ? worker->current : 0;
}

static i64 monotonicNanoseconds(void) {
#ifdef _WIN32
    i64 count, frequency;
    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&frequency);
    return count / frequency * 1000000000 + count % frequency * 1000000000 / frequency;
#else
    i64 now[2];
    systemCall(SYS_clock_gettime, 1, (i64)now, 0, 0, 0, 0);  // CLOCK_MONOTONIC
    return now[0] * 1000000000 + now[1];
#endif
}

static i32 processorCount(void) {
#ifdef _WIN32
    u32 count = GetActiveProcessorCount(0xFFFF);  // all the groups
#else
    u64 mask[16];
    i64 size = systemCall(SYS_sched_getaffinity, 0, sizeof mask, (i64)mask, 0, 0, 0);
    u32 count = 0;
    for (i64 i = 0; i < size / 8; ++i) count += (u32)__builtin_popcountll(mask[i]);
#endif
    return count > 0 ? (i32)count : 1;
}

static NORETURN void taskFailure(void) {
    static const char text[] = "cannot start a task: the system has no resources for it";
    cromo_panic((const u8*)text, sizeof text - 1, 0);
}

// ---- Stacks (D272) ----
//
// The stack of a task that ends is kept for the next one, with its memory, up to
// KEPT_STACKS; the others give it back to the system.

#define TASK_STACK ((u64)1 << 20)
#define KEPT_STACKS 1024

static Lock stackLock;
static Stack* keptStacks;
static i64 keptCount;

static NORETURN void stackMain(Stack* stack);

#ifdef _WIN32

#define FIBER_FLAG_FLOAT_SWITCH 1

static void WINAPI fiberMain(void* parameter) { stackMain(parameter); }

static Stack* newStack(void) {
    Stack* stack = cromo_alloc(sizeof(Stack));
    stack->next = 0;
    stack->task = 0;
    stack->context = CreateFiberEx(0, TASK_STACK, FIBER_FLAG_FLOAT_SWITCH, fiberMain, stack);
    if (!stack->context) taskFailure();
    return stack;
}

static void dropStack(Stack* stack) {
    DeleteFiber(stack->context);
    cromo_free(stack);
}

static void switchToTask(Worker* worker, Stack* stack) {
    (void)worker;
    SwitchToFiber(stack->context);
}

static void switchToWorker(Worker* worker, Stack* stack) {
    (void)stack;
    SwitchToFiber(worker->context);
}

#else

void cromo_switch(void** save, void* next);  // mem_x64.S
void cromo_task_entry(void);                 // mem_x64.S: cromo_task_run(rbx)

NORETURN void cromo_task_run(Stack* stack) { stackMain(stack); }

// The stacks are cut from regions of addresses reserved without memory, 4 GB at
// a time: a new stack is one mprotect, which leaves its guard page at the bottom,
// and one given back keeps its addresses for the next. A mapping for each stack
// would take three calls, which wait for each other in a process with threads.
#define REGION_STACKS 4096

static u8* regionNext;
static u8* regionEnd;
static Stack* emptyStacks;  // without memory: they start again from the top

// What cromo_switch takes back the first time: the control words of SSE and x87,
// six registers with the stack in rbx, and where it returns.
static void startFrame(Stack* stack) {
    u64* top = (u64*)(stack->base + TASK_STACK);
    *--top = (u64)cromo_task_entry;
    *--top = 0;           // rbp
    *--top = (u64)stack;  // rbx
    *--top = 0;           // r12
    *--top = 0;           // r13
    *--top = 0;           // r14
    *--top = 0;           // r15
    *--top = 0x037F00001F80ull;  // MXCSR, then the control word of x87
    stack->context = top;
}

// With the lock of the stacks taken.
static Stack* newStack(void) {
    Stack* stack = emptyStacks;
    if (stack) {
        emptyStacks = stack->next;
    } else {
        if (regionNext == regionEnd) {
            // PROT_NONE, MAP_PRIVATE|ANONYMOUS|NORESERVE: addresses, no memory
            i64 region = systemCall(SYS_mmap, 0, (i64)(REGION_STACKS * TASK_STACK), 0, 0x22 | 0x4000, -1, 0);
            if (region < 0 && region > -4096) taskFailure();
            regionNext = (u8*)region;
            regionEnd = regionNext + REGION_STACKS * TASK_STACK;
        }
        u8* base = regionNext;
        regionNext += TASK_STACK;
        // PROT_READ|PROT_WRITE above the guard page: the pages are taken when they are used
        if (systemCall(SYS_mprotect, (i64)(base + 4096), (i64)(TASK_STACK - 4096), 3, 0, 0, 0) < 0) taskFailure();
        stack = cromo_alloc(sizeof(Stack));
        stack->base = base;
    }
    stack->next = 0;
    stack->task = 0;
    startFrame(stack);
    return stack;
}

// Its memory goes back to the system (MADV_DONTNEED), its addresses to the next.
static void dropStack(Stack* stack) {
    systemCall(28, (i64)(stack->base + 4096), (i64)(TASK_STACK - 4096), 4, 0, 0, 0);  // madvise
    lockAcquire(&stackLock);
    stack->next = emptyStacks;
    emptyStacks = stack;
    lockRelease(&stackLock);
}

static void switchToTask(Worker* worker, Stack* stack) { cromo_switch(&worker->context, stack->context); }
static void switchToWorker(Worker* worker, Stack* stack) { cromo_switch(&stack->context, worker->context); }

#endif

static void giveStack(Task* task) {
    lockAcquire(&stackLock);
    Stack* stack = keptStacks;
    if (stack) {
        keptStacks = stack->next;
        --keptCount;
    }
#ifndef _WIN32
    if (!stack) stack = newStack();
#endif
    lockRelease(&stackLock);
#ifdef _WIN32
    if (!stack) stack = newStack();
#endif
    stack->task = task;
    task->stack = stack;
}

static void releaseStack(Stack* stack) {
    lockAcquire(&stackLock);
    if (keptCount < KEPT_STACKS) {
        stack->next = keptStacks;
        keptStacks = stack;
        ++keptCount;
        stack = 0;
    }
    lockRelease(&stackLock);
    if (stack) dropStack(stack);
}

// ---- The queue of the tasks ready to run ----

static void startWorker(void);

// Wakes a worker for a task just queued, or starts one, with the lock taken:
// false when there is none to call.
static i32 callWorker(void) {
    if (scheduler.idle > 0) {
        --scheduler.idle;
        ++scheduler.idleCalls;
        conditionSignal(&scheduler.work);
        return 1;
    }
    if (scheduler.places > 0) {
        --scheduler.places;
        startWorker();
        return 1;
    }
    if (scheduler.monitorSleeping) {
        scheduler.monitorSleeping = 0;
        conditionSignal(&scheduler.monitorCall);
    }
    return 0;
}

// Tasks woken together, put in the queue with one lock: the timers that expire
// at once, the events of one wait of the poller.
typedef struct {
    Task* first;
    Task* last;
    i64 count;
} Batch;

static void batchAdd(Batch* batch, Task* task) {
    task->next = 0;
    if (batch->last) {
        batch->last->next = task;
    } else {
        batch->first = task;
    }
    batch->last = task;
    ++batch->count;
}

// Puts the tasks in the queue, once their workers have left their stacks.
static void enqueue(Batch* batch) {
    if (!batch->first) return;
    for (Task* task = batch->first; task; task = task->next) {
        for (u32 spins = 0; __atomic_load_n(&task->running, __ATOMIC_ACQUIRE); ++spins) relax(spins);
    }
    lockAcquire(&scheduler.lock);
    if (scheduler.last) {
        scheduler.last->next = batch->first;
    } else {
        scheduler.first = batch->first;
    }
    scheduler.last = batch->last;
    scheduler.queued += batch->count;
    for (i64 i = 0; i < batch->count && callWorker(); ++i) {
    }
    lockRelease(&scheduler.lock);
    batch->first = 0;
    batch->last = 0;
    batch->count = 0;
}

static void makeReady(Task* task) {
    Batch batch = {0, 0, 0};
    batchAdd(&batch, task);
    enqueue(&batch);
}

// The task leaves its worker until makeReady(); it can come back on another one.
static void parkTask(Task* task) { switchToWorker(currentWorker(), task->stack); }

// The first of the event and the timer wakes a parked task: into the batch, which
// the caller puts in the queue.
static void wakeTask(Task* task, i32 ready, Batch* batch) {
    u32 parked = WAIT_PARKED;
    if (__atomic_compare_exchange_n(&task->waitState, &parked, WAIT_WOKEN, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        task->ready = ready;
        batchAdd(batch, task);
    }
}

// The next task for a worker, with the lock taken: a worker whose place went to
// another waits as a spare until it gets one back (D271).
static Task* nextTask(Worker* worker) {
    for (;;) {
        if (worker->stuck) {
            worker->stuck = 0;
            if (scheduler.places > 0) {
                --scheduler.places;
            } else {
                ++scheduler.spares;
                while (scheduler.spareCalls == 0) conditionWait(&scheduler.spare, &scheduler.lock);
                --scheduler.spareCalls;
            }
        }
        Task* task = scheduler.first;
        if (task) {
            scheduler.first = task->next;
            if (!scheduler.first) scheduler.last = 0;
            --scheduler.queued;
            return task;
        }
        ++scheduler.idle;
        while (scheduler.idleCalls == 0) conditionWait(&scheduler.work, &scheduler.lock);
        --scheduler.idleCalls;
    }
}

static void freeException(void** raised) {
    void** descriptor = (void**)raised[1];
    void (*drop)(void*) = (void (*)(void*))descriptor[0];
    if (drop) drop(raised[0]);
    cromo_free(raised[0]);
    raised[0] = 0;
    raised[1] = 0;
}

static void unpark(const u32* key, i32 all);

// After its lambda, on its own stack, since destructors can wait too: a task of a
// group destroys the data of the lambda, which it owns, and gives its exception
// to the group if it is the first (D273).
static void finishGroupTask(Task* task) {
    void** data = (void**)task->head.env;
    if (data && data[0]) ((void (*)(void*))data[0])(data);
    if (!task->head.raised[0]) return;
    Group* group = task->group;
    lockAcquire(&group->lock);
    i32 first = group->head.raised[0] == 0;
    if (first) {
        group->head.raised[0] = task->head.raised[0];
        group->head.raised[1] = task->head.raised[1];
    }
    lockRelease(&group->lock);
    if (!first) freeException(task->head.raised);
}

// What a stack runs: a task, then the next ones it is given.
static NORETURN void stackMain(Stack* stack) {
    for (;;) {
        Task* task = stack->task;
        task->head.code(task->head.env, task->head.raised);
        if (task->head.kind == KIND_GROUP_TASK) finishGroupTask(task);
        Worker* worker = currentWorker();
        worker->ended = 1;
        switchToWorker(worker, stack);
    }
}

// The end of a task, on the stack of its worker.
static void endTask(Task* task) {
    releaseStack(task->stack);
    task->stack = 0;
    if (task->head.kind == KIND_GROUP_TASK) {
        Group* group = task->group;
        cromo_free(task);
        if (__atomic_sub_fetch(&group->running, 1, __ATOMIC_ACQ_REL) == 0) unpark(&group->running, 1);
        return;
    }
    __atomic_store_n(&task->finished, 1, __ATOMIC_RELEASE);
    unpark(&task->finished, 1);  // the record can be freed already: only its address is used
}

static void workerLoop(Worker* worker) {
    lockAcquire(&scheduler.lock);
    for (;;) {
        Task* task = nextTask(worker);
        task->running = 1;
        worker->current = task;
        ++worker->switches;
        lockRelease(&scheduler.lock);
        if (!task->stack) giveStack(task);
        switchToTask(worker, task->stack);
        if (worker->ended) {
            worker->ended = 0;
            endTask(task);
        } else {
            __atomic_store_n(&task->running, 0, __ATOMIC_RELEASE);  // parked: it can be woken
        }
        lockAcquire(&scheduler.lock);
        worker->current = 0;
        ++worker->switches;
    }
}

static void workerMain(void* argument, void* raised) {
    (void)raised;
    Worker* worker = argument;
#ifdef _WIN32
    TlsSetValue(tlsIndex, worker);
    worker->context = ConvertThreadToFiberEx(worker, FIBER_FLAG_FLOAT_SWITCH);
    if (!worker->context) taskFailure();
#endif
    workerLoop(worker);
}

// With the lock of the scheduler taken.
static void startWorker(void) {
    Worker* worker = cromo_alloc(sizeof(Worker));
    u8* bytes = (u8*)worker;
    for (u64 i = 0; i < sizeof(Worker); ++i) bytes[i] = 0;
    worker->next = scheduler.all;
    scheduler.all = worker;
    ++scheduler.workers;
#ifndef _WIN32
    worker->thread.block.worker = worker;
#endif
    startRuntimeThread(&worker->thread, workerMain, worker);
}

// A place for another worker, when one is stuck (D271).
static void givePlace(void) {
    if (scheduler.spares > 0) {
        --scheduler.spares;
        ++scheduler.spareCalls;
        conditionSignal(&scheduler.spare);
    } else if (scheduler.workers < scheduler.target + EXTRA_WORKERS) {
        startWorker();
    } else {
        ++scheduler.places;
    }
}

#ifdef _WIN32

#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 2
#define TIMER_ALL_ACCESS 0x1F0003

static void* tickTimer;

static void sleepTick(void) {
    if (!tickTimer) tickTimer = CreateWaitableTimerExW(0, 0, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!tickTimer) tickTimer = CreateWaitableTimerExW(0, 0, 0, TIMER_ALL_ACCESS);  // before Windows 10 1803
    i64 due = -10000;  // 1 ms from now, in units of 100 ns
    if (tickTimer && SetWaitableTimer(tickTimer, &due, 0, 0, 0, 0)) {
        WaitForSingleObject(tickTimer, 0xFFFFFFFF);
    } else {
        Sleep(1);
    }
}

#else

static void sleepTick(void) {
    i64 tick[2] = {0, 1000000};
    systemCall(SYS_nanosleep, (i64)tick, 0, 0, 0, 0, 0);
}

#endif

// While tasks wait and no worker is idle, every millisecond: a worker that has
// run the same task for the whole tick gives its place to another (D271).
static void monitorMain(void* argument, void* raised) {
    (void)argument;
    (void)raised;
    lockAcquire(&scheduler.lock);
    for (;;) {
        while (scheduler.queued == 0 || scheduler.idle > 0) {
            scheduler.monitorSleeping = 1;
            conditionWait(&scheduler.monitorCall, &scheduler.lock);
        }
        scheduler.monitorSleeping = 0;
        for (Worker* worker = scheduler.all; worker; worker = worker->next) worker->seen = worker->switches;
        lockRelease(&scheduler.lock);
        sleepTick();
        lockAcquire(&scheduler.lock);
        if (scheduler.queued == 0 || scheduler.idle > 0) continue;
        for (Worker* worker = scheduler.all; worker; worker = worker->next) {
            if (!worker->current || worker->stuck || worker->switches != worker->seen) continue;
            worker->stuck = 1;
            givePlace();
        }
    }
}

static void startScheduler(void) {
    u32 state = __atomic_load_n(&scheduler.state, __ATOMIC_ACQUIRE);
    if (state == 2) return;
    if (state == 0 && __atomic_compare_exchange_n(&scheduler.state, &state, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        threaded = 1;  // from now on the locks are taken
#ifdef _WIN32
        __atomic_store_n(&tlsIndex, TlsAlloc(), __ATOMIC_RELEASE);
#endif
        scheduler.target = processorCount();
        scheduler.places = scheduler.target;
        startRuntimeThread(&scheduler.monitor, monitorMain, 0);
        __atomic_store_n(&scheduler.state, 2, __ATOMIC_RELEASE);
        return;
    }
    for (u32 spins = 0; __atomic_load_n(&scheduler.state, __ATOMIC_ACQUIRE) != 2; ++spins) relax(spins);
}

static Task* newTask(void* code, void* env, i64 kind) {
    Task* task = cromo_alloc(sizeof(Task));
    u8* bytes = (u8*)task;
    for (u64 i = 0; i < sizeof(Task); ++i) bytes[i] = 0;
    task->head.code = (void (*)(void*, void*))code;
    task->head.env = env;
    task->head.kind = kind;
    task->timerIndex = -1;
    return task;
}

// ---- Parking: the waits of the tasks and of the threads, keyed by an address ----
//
// As FUTEX_WAIT and FUTEX_WAKE of Linux: parkOn() waits only if the value at the
// key is still the one expected, checked with the lock of the list taken.

typedef struct Waiter {
    struct Waiter* next;
    const u32* key;
    Task* task;  // a task parks; a thread of the system sleeps
    u32 woken;
} Waiter;

typedef struct {
    Lock lock;
    Waiter* first;
    Waiter* last;
    Condition sleeping;  // Windows: the threads that wait in this list
} Bucket;

#define BUCKETS 256
static Bucket buckets[BUCKETS];

static Bucket* bucketOf(const u32* key) { return &buckets[((u64)key >> 2) * 0x9E3779B97F4A7C15ull >> 56]; }

static void parkOn(const u32* key, u32 expected) {
    Bucket* bucket = bucketOf(key);
    lockAcquire(&bucket->lock);
    if (__atomic_load_n(key, __ATOMIC_ACQUIRE) != expected) {
        lockRelease(&bucket->lock);
        return;
    }
    Waiter waiter = {0, key, currentTask(), 0};
    if (bucket->last) {
        bucket->last->next = &waiter;
    } else {
        bucket->first = &waiter;
    }
    bucket->last = &waiter;
    if (waiter.task) {
        lockRelease(&bucket->lock);
        parkTask(waiter.task);
        return;
    }
#ifdef _WIN32
    while (!waiter.woken) conditionWait(&bucket->sleeping, &bucket->lock);
    lockRelease(&bucket->lock);
#else
    lockRelease(&bucket->lock);
    while (!__atomic_load_n(&waiter.woken, __ATOMIC_ACQUIRE)) futexWait(&waiter.woken, 0);
#endif
}

static void unpark(const u32* key, i32 all) {
    Bucket* bucket = bucketOf(key);
    Batch woken = {0, 0, 0};
    lockAcquire(&bucket->lock);
    Waiter** link = &bucket->first;
    Waiter* previous = 0;
    i32 sleepers = 0;
    while (*link) {
        Waiter* waiter = *link;
        if (waiter->key != key) {
            previous = waiter;
            link = &waiter->next;
            continue;
        }
        *link = waiter->next;
        if (bucket->last == waiter) bucket->last = previous;
        if (waiter->task) {
            batchAdd(&woken, waiter->task);
        } else {
            sleepers = 1;
            __atomic_store_n(&waiter->woken, 1, __ATOMIC_RELEASE);
#ifndef _WIN32
            futexWake(&waiter->woken, 1);  // it may be gone already: a wake for nobody
#endif
        }
        if (!all) break;
    }
#ifdef _WIN32
    if (sleepers) WakeAllConditionVariable(&bucket->sleeping);
#else
    (void)sleepers;
#endif
    lockRelease(&bucket->lock);
    enqueue(&woken);
}

// ---- Mutex (D216, D220) ----
//
// The first 4 bytes of the Mutex: 0 free, 1 taken, 2 taken with others waiting
// (Drepper, "Futexes are tricky"). A task that waits for it parks, a thread sleeps.

void cromo_mutex_lock(void* state) {
    u32* word = (u32*)state;
    for (u32 spins = 0; spins < 64; ++spins) {
        u32 seen = 0;
        if (__atomic_compare_exchange_n(word, &seen, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) return;
        if (seen == 2) break;
        __asm__ volatile("pause");
    }
    while (__atomic_exchange_n(word, 2, __ATOMIC_ACQUIRE) != 0) parkOn(word, 2);
}

void cromo_mutex_unlock(void* state) {
    u32* word = (u32*)state;
    if (__atomic_exchange_n(word, 0, __ATOMIC_RELEASE) == 2) unpark(word, 0);
}

// ---- Channels (D326, D327) ----
//
// A ring of values of `size` bytes each, behind a lock of the runtime. Who finds
// it full waits on `space`, who finds it empty on `values`: each counter changes
// when a value leaves or arrives and at close(), and parkOn() waits only while it
// is the one seen, as for the Mutex. A channel of capacity 0 has room for one
// value, and its sender also waits on `taken` until a receiver has the value.

typedef struct {
    Lock lock;
    u8* ring;
    u64 size;      // of a value
    u64 capacity;  // of the ring, at least 1
    u64 head;      // where the first value is
    u64 count;
    u64 sent;      // capacity 0: the values sent so far
    u64 received;  // and those received
    u32 space;
    u32 values;
    u32 taken;
    u32 closed;
    u32 handover;  // capacity 0
} Channel;

// The channel of a Cromo Channel, made by the first use: the default value of the
// struct has none, and two threads may use it first together.
void* cromo_channel_ensure(void** slot, i64 capacity, i64 size) {
    Channel* channel = __atomic_load_n((Channel**)slot, __ATOMIC_ACQUIRE);
    if (channel) return channel;
    Channel* made = cromo_alloc(sizeof(Channel));
    u8* bytes = (u8*)made;
    for (u64 i = 0; i < sizeof(Channel); ++i) bytes[i] = 0;
    made->handover = capacity == 0;
    made->capacity = capacity > 0 ? (u64)capacity : 1;
    made->size = (u64)size;
    made->ring = cromo_alloc((i64)(made->capacity * made->size));
    Channel* none = 0;
    if (__atomic_compare_exchange_n((Channel**)slot, &none, made, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return made;
    cromo_free(made->ring);
    cromo_free(made);
    return none;  // the channel that the other thread made
}

// Waits while the counter is still the one seen, with the lock of the channel left.
static void channelWait(Channel* channel, u32* counter) {
    u32 seen = __atomic_load_n(counter, __ATOMIC_RELAXED);
    lockRelease(&channel->lock);
    parkOn(counter, seen);
    lockAcquire(&channel->lock);
}

static void channelChanged(u32* counter) { __atomic_store_n(counter, *counter + 1, __ATOMIC_RELEASE); }

// Copies the value in, waiting while the channel is full: 0 if it is closed.
i64 cromo_channel_send(Channel* channel, const void* value) {
    lockAcquire(&channel->lock);
    while (!channel->closed && channel->count == channel->capacity) channelWait(channel, &channel->space);
    if (channel->closed) {
        lockRelease(&channel->lock);
        return 0;
    }
    u64 tail = (channel->head + channel->count) % channel->capacity;
    memcpy(channel->ring + tail * channel->size, value, channel->size);
    channel->count++;
    u64 number = ++channel->sent;
    channelChanged(&channel->values);
    lockRelease(&channel->lock);
    unpark(&channel->values, 0);
    if (channel->handover) {  // until a receiver has it, or the channel is closed
        lockAcquire(&channel->lock);
        while (!channel->closed && channel->received < number) channelWait(channel, &channel->taken);
        lockRelease(&channel->lock);
    }
    return 1;
}

// Copies the first value out, waiting while the channel is empty: 0 once it is
// closed and empty.
i64 cromo_channel_receive(Channel* channel, void* value) {
    lockAcquire(&channel->lock);
    while (!channel->closed && channel->count == 0) channelWait(channel, &channel->values);
    if (channel->count == 0) {
        lockRelease(&channel->lock);
        return 0;
    }
    memcpy(value, channel->ring + channel->head * channel->size, channel->size);
    channel->head = (channel->head + 1) % channel->capacity;
    channel->count--;
    channel->received++;
    channelChanged(&channel->space);
    if (channel->handover) channelChanged(&channel->taken);
    lockRelease(&channel->lock);
    unpark(&channel->space, 0);
    if (channel->handover) unpark(&channel->taken, 1);
    return 1;
}

// Wakes all who wait: the senders find it closed, the receivers take what is left.
void cromo_channel_close(Channel* channel) {
    lockAcquire(&channel->lock);
    channel->closed = 1;
    channelChanged(&channel->space);
    channelChanged(&channel->values);
    channelChanged(&channel->taken);
    lockRelease(&channel->lock);
    unpark(&channel->space, 1);
    unpark(&channel->values, 1);
    unpark(&channel->taken, 1);
}

// The values left at the end, for the destructor: nobody else uses the channel.
i64 cromo_channel_drain(Channel* channel, void* value) {
    if (channel->count == 0) return 0;
    memcpy(value, channel->ring + channel->head * channel->size, channel->size);
    channel->head = (channel->head + 1) % channel->capacity;
    channel->count--;
    return 1;
}

void cromo_channel_free(Channel* channel) {
    cromo_free(channel->ring);
    cromo_free(channel);
}

// ---- What the library calls (D214, D269, D273) ----

void* cromo_thread_start(void* code, void* env) {
    ThreadRecord* thread = newThreadRecord(code, env);
    threaded = 1;  // from now on the locks are taken
    startSystemThread(thread);
    return thread;
}

void* cromo_task_start(void* code, void* env) {
    Task* task = newTask(code, env, KIND_TASK);
    startScheduler();
    makeReady(task);
    return task;
}

void* cromo_group_new(void) {
    Group* group = cromo_alloc(sizeof(Group));
    u8* bytes = (u8*)group;
    for (u64 i = 0; i < sizeof(Group); ++i) bytes[i] = 0;
    group->head.kind = KIND_GROUP;
    return group;
}

void cromo_group_start(Group* group, void* code, void* env) {
    Task* task = newTask(code, env, KIND_GROUP_TASK);
    task->group = group;
    __atomic_add_fetch(&group->running, 1, __ATOMIC_ACQ_REL);
    startScheduler();
    makeReady(task);
}

// Waits for the end of the thread, of the task or of all the tasks of the group:
// 1 if there is an exception to take.
i32 cromo_thread_wait(Runnable* record) {
    if (record->kind == KIND_THREAD) {
        waitSystemThread((ThreadRecord*)record);
    } else if (record->kind == KIND_TASK) {
        Task* task = (Task*)record;
        while (!__atomic_load_n(&task->finished, __ATOMIC_ACQUIRE)) parkOn(&task->finished, 0);
    } else {
        Group* group = (Group*)record;
        for (;;) {
            u32 running = __atomic_load_n(&group->running, __ATOMIC_ACQUIRE);
            if (running == 0) break;
            parkOn(&group->running, running);
        }
    }
    return record->raised[0] != 0;
}

// The exception goes into the slot of the caller of join() (D217).
void cromo_thread_take(Runnable* record, void** slot) {
    slot[0] = record->raised[0];
    slot[1] = record->raised[1];
    record->raised[0] = 0;
    record->raised[1] = 0;
}

void cromo_thread_free(Runnable* record) {
    if (record->kind == KIND_THREAD) freeSystemThread((ThreadRecord*)record);
    cromo_free(record);
}

// D217: an exception that no join() takes stops the program, with its message
// from the first method of the table of Exception, as for main (D121).
NORETURN void cromo_thread_uncaught(Runnable* record) {
    void* object = record->raised[0];
    void** descriptor = (void**)record->raised[1];
    void** table = (void**)descriptor[1];
    void (*message)(Text*, void*) = (void (*)(Text*, void*))table[0];
    Text text;
    message(&text, object);
    if (record->kind == KIND_THREAD) uncaught("uncaught exception in a thread: ", 32, text.data, (u64)text.length);
    uncaught("uncaught exception in a task: ", 30, text.data, (u64)text.length);
}

// ---- The poller: the timers, and the waits for the network (D269) ----

static struct {
    Lock lock;
    Task** heap;  // the tasks with a timer, the nearest first
    i64 count;
    i64 capacity;
    i64 sleepingUntil;  // the deadline the poller waits for
    u32 state;          // 0, 1 while it starts, 2 started
    ThreadRecord thread;
#ifdef _WIN32
    void* port;  // the IOCP
    void* afd;   // \Device\Afd, for the polls of the sockets
    i32(WINAPI* deviceControl)(void*, void*, void*, void*, void*, u32, void*, u32, void*, u32);
    i32(WINAPI* cancel)(void*, void*, void*);
    i32(WINAPI* socketControl)(u64, u32, void*, u32, void*, u32, u32*, void*, void*);
#else
    i32 epoll;
    i32 wake;  // an eventfd that interrupts the wait
#endif
} poller;

#define NO_DEADLINE 0x7FFFFFFFFFFFFFFFll

static i32 earlier(i64 a, i64 b) { return poller.heap[a]->deadline < poller.heap[b]->deadline; }

static void swapTimers(i64 a, i64 b) {
    Task* first = poller.heap[a];
    poller.heap[a] = poller.heap[b];
    poller.heap[b] = first;
    poller.heap[a]->timerIndex = a;
    poller.heap[b]->timerIndex = b;
}

static void timerUp(i64 index) {
    while (index > 0 && earlier(index, (index - 1) / 2)) {
        swapTimers(index, (index - 1) / 2);
        index = (index - 1) / 2;
    }
}

static void timerDown(i64 index) {
    for (;;) {
        i64 least = index;
        i64 left = 2 * index + 1;
        if (left < poller.count && earlier(left, least)) least = left;
        if (left + 1 < poller.count && earlier(left + 1, least)) least = left + 1;
        if (least == index) return;
        swapTimers(index, least);
        index = least;
    }
}

static void interruptPoller(void);

// With the lock of the poller taken.
static void addTimer(Task* task, i64 deadline, i32 kind) {
    if (poller.count == poller.capacity) {
        poller.capacity = poller.capacity ? poller.capacity * 2 : 64;
        poller.heap = cromo_realloc(poller.heap, poller.capacity * (i64)sizeof(Task*));
    }
    task->deadline = deadline;
    task->timerKind = kind;
    task->timerIndex = poller.count;
    poller.heap[poller.count++] = task;
    timerUp(task->timerIndex);
    if (deadline < poller.sleepingUntil) {
        poller.sleepingUntil = deadline;
        interruptPoller();
    }
}

static void removeTimer(Task* task) {
    i64 index = task->timerIndex;
    if (index < 0) return;
    task->timerIndex = -1;
    Task* last = poller.heap[--poller.count];
    if (index == poller.count) return;
    poller.heap[index] = last;
    last->timerIndex = index;
    timerDown(index);
    timerUp(last->timerIndex);
}

static void timerExpired(Task* task, Batch* woken);

// The timers that have expired; the milliseconds until the next one, -1 without.
static i64 fireTimers(void) {
    Batch woken = {0, 0, 0};
    lockAcquire(&poller.lock);
    i64 now = monotonicNanoseconds();
    while (poller.count > 0 && poller.heap[0]->deadline <= now) {
        Task* task = poller.heap[0];
        removeTimer(task);
        timerExpired(task, &woken);
    }
    i64 wait = -1;
    poller.sleepingUntil = NO_DEADLINE;
    if (poller.count > 0) {
        poller.sleepingUntil = poller.heap[0]->deadline;
        wait = (poller.sleepingUntil - now + 999999) / 1000000;
    }
    lockRelease(&poller.lock);
    enqueue(&woken);
    return wait;
}

#ifdef _WIN32

#define STATUS_PENDING 0x103
#define STATUS_CANCELLED ((i32)0xC0000120)
#define IOCTL_AFD_POLL 0x00012024
#define AFD_POLL_RECEIVE 0x0001
#define AFD_POLL_SEND 0x0004
#define AFD_POLL_DISCONNECT 0x0008
#define AFD_POLL_ABORT 0x0010
#define AFD_POLL_LOCAL_CLOSE 0x0020
#define AFD_POLL_ACCEPT 0x0080
#define AFD_POLL_CONNECT_FAIL 0x0100
#define SIO_BASE_HANDLE 0x48000022

typedef struct {
    u16 length;
    u16 capacity;
    const u16* text;
} UnicodeText;

typedef struct {
    u32 length;
    void* root;
    UnicodeText* name;
    u32 attributes;
    void* security;
    void* quality;
} ObjectAttributes;

typedef struct {
    u64 key;
    void* overlapped;
    u64 internal;
    u32 bytes;
} CompletionEntry;  // OVERLAPPED_ENTRY

static void timerExpired(Task* task, Batch* woken) {
    if (task->timerKind == TIMER_CANCEL) {
        IoStatus ignored;
        poller.cancel(poller.afd, &task->status, &ignored);  // its completion wakes the task
    } else {
        wakeTask(task, 0, woken);
    }
}

static void interruptPoller(void) { PostQueuedCompletionStatus(poller.port, 0, 0, 0); }

// The device of AFD, under the sockets of Windows: its polls end on the IOCP, as
// in libuv and in mio. Without it the tasks wait as the threads do.
static void openPoller(void) {
    poller.port = CreateIoCompletionPort((void*)(i64)-1, 0, 0, 1);
    if (!poller.port) taskFailure();
    void* ntdll = GetModuleHandleW(L"ntdll.dll");
    i32(WINAPI * create)(void**, u32, ObjectAttributes*, IoStatus*, i64*, u32, u32, u32, u32, void*, u32) =
        (i32(WINAPI*)(void**, u32, ObjectAttributes*, IoStatus*, i64*, u32, u32, u32, u32, void*, u32))GetProcAddress(
            ntdll, "NtCreateFile");
    poller.deviceControl = (i32(WINAPI*)(void*, void*, void*, void*, void*, u32, void*, u32, void*, u32))GetProcAddress(
        ntdll, "NtDeviceIoControlFile");
    poller.cancel = (i32(WINAPI*)(void*, void*, void*))GetProcAddress(ntdll, "NtCancelIoFileEx");
    void* sockets = GetModuleHandleW(L"ws2_32.dll");
    if (sockets) {
        poller.socketControl = (i32(WINAPI*)(u64, u32, void*, u32, void*, u32, u32*, void*, void*))GetProcAddress(
            sockets, "WSAIoctl");
    }
    if (!create || !poller.deviceControl || !poller.cancel) return;
    static const u16 path[] = L"\\Device\\Afd\\Cromo";
    UnicodeText name = {sizeof path - 2, sizeof path, path};
    ObjectAttributes attributes = {sizeof(ObjectAttributes), 0, &name, 0, 0, 0};
    IoStatus status;
    void* afd = 0;
    // SYNCHRONIZE, FILE_SHARE_READ|FILE_SHARE_WRITE, FILE_OPEN
    if (create(&afd, 0x00100000, &attributes, &status, 0, 0, 3, 1, 0, 0, 0) != 0) return;
    if (!CreateIoCompletionPort(afd, poller.port, 0, 0)) {
        CloseHandle(afd);
        return;
    }
    SetFileCompletionNotificationModes(afd, 2);  // FILE_SKIP_SET_EVENT_ON_HANDLE
    poller.afd = afd;
}

static void pollEvents(i64 milliseconds) {
    CompletionEntry entries[64];
    u32 count = 0;
    u32 wait = milliseconds < 0 ? 0xFFFFFFFF : milliseconds > 0x7FFFFFFF ? 0x7FFFFFFF : (u32)milliseconds;
    if (!GetQueuedCompletionStatusEx(poller.port, entries, 64, &count, wait, 0)) return;
    Batch woken = {0, 0, 0};
    for (u32 i = 0; i < count; ++i) {
        if (!entries[i].overlapped) continue;  // interruptPoller()
        Task* task = (Task*)((u8*)entries[i].overlapped - __builtin_offsetof(Task, status));
        wakeTask(task, task->status.status != STATUS_CANCELLED, &woken);
    }
    enqueue(&woken);
}

// The socket under the layers of Winsock, which AFD knows.
static void* baseSocket(i64 handle) {
    void* base = 0;
    u32 bytes = 0;
    if (poller.socketControl && poller.socketControl((u64)handle, SIO_BASE_HANDLE, 0, 0, &base, sizeof base, &bytes, 0, 0) == 0) {
        return base;
    }
    return (void*)handle;
}

#else

#define EPOLLIN 0x001
#define EPOLLOUT 0x004
#define EPOLLERR 0x008
#define EPOLLHUP 0x010
#define EPOLLRDHUP 0x2000
#define EPOLLET 0x80000000u
#define WAKE_MARK 0xFFFFFFFFFFFFFFFFull

typedef struct __attribute__((packed)) {
    u32 events;
    u64 data;
} EpollEvent;

// What the tasks wait for on a socket, by its number: registered once in epoll,
// edge-triggered. An event that no task was waiting for stays in `readable` or
// `writable` for the next wait.
typedef struct {
    Lock lock;
    u32 generation;  // changes when the socket is closed: the events of the old one are ignored
    u8 registered;
    u8 readable;
    u8 writable;
    Task* readers;
    Task* writers;
} SocketWaits;

#define SOCKET_CHUNK 1024
static SocketWaits* socketChunks[1024];

static SocketWaits* socketWaits(i64 handle, i32 create) {
    if (handle < 0 || handle >= 1024 * SOCKET_CHUNK) return 0;
    SocketWaits** chunk = &socketChunks[handle / SOCKET_CHUNK];
    SocketWaits* entries = __atomic_load_n(chunk, __ATOMIC_ACQUIRE);
    if (!entries) {
        if (!create) return 0;
        SocketWaits* fresh = cromo_alloc(sizeof(SocketWaits) * SOCKET_CHUNK);
        u8* bytes = (u8*)fresh;
        for (u64 i = 0; i < sizeof(SocketWaits) * SOCKET_CHUNK; ++i) bytes[i] = 0;
        if (__atomic_compare_exchange_n(chunk, &entries, fresh, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            entries = fresh;
        } else {
            cromo_free(fresh);
        }
    }
    return &entries[handle % SOCKET_CHUNK];
}

static void timerExpired(Task* task, Batch* woken) { wakeTask(task, 0, woken); }

static void interruptPoller(void) {
    u64 one = 1;
    systemCall(SYS_write, poller.wake, (i64)&one, 8, 0, 0, 0);
}

static void openPoller(void) {
    poller.epoll = (i32)systemCall(SYS_epoll_create1, 0x80000, 0, 0, 0, 0, 0);  // EPOLL_CLOEXEC
    poller.wake = (i32)systemCall(SYS_eventfd2, 0, 0x80800, 0, 0, 0, 0);       // EFD_CLOEXEC|EFD_NONBLOCK
    if (poller.epoll < 0 || poller.wake < 0) taskFailure();
    EpollEvent event = {EPOLLIN, WAKE_MARK};
    systemCall(SYS_epoll_ctl, poller.epoll, 1, poller.wake, (i64)&event, 0, 0);  // EPOLL_CTL_ADD
}

static void wakeWaiters(Task** list, Batch* woken) {
    Task* task = *list;
    *list = 0;
    while (task) {
        Task* next = task->nextWaiter;
        task->nextWaiter = 0;
        wakeTask(task, 1, woken);
        task = next;
    }
}

static void pollEvents(i64 milliseconds) {
    EpollEvent events[64];
    i64 count = systemCall(SYS_epoll_wait, poller.epoll, (i64)events, 64, milliseconds, 0, 0);
    Batch woken = {0, 0, 0};
    for (i64 i = 0; i < count; ++i) {
        u64 data = events[i].data;
        if (data == WAKE_MARK) {
            u64 value;
            systemCall(SYS_read, poller.wake, (i64)&value, 8, 0, 0, 0);
            continue;
        }
        SocketWaits* waits = socketWaits((i64)(u32)data, 0);
        if (!waits) continue;
        lockAcquire(&waits->lock);
        if (waits->generation == (u32)(data >> 32)) {
            u32 happened = events[i].events;
            if (happened & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                waits->readable = 1;
                wakeWaiters(&waits->readers, &woken);
            }
            if (happened & (EPOLLOUT | EPOLLHUP | EPOLLERR)) {
                waits->writable = 1;
                wakeWaiters(&waits->writers, &woken);
            }
        }
        lockRelease(&waits->lock);
    }
    enqueue(&woken);
}

static void unlinkWaiter(Task** list, Task* task) {
    for (Task** link = list; *link; link = &(*link)->nextWaiter) {
        if (*link == task) {
            *link = task->nextWaiter;
            task->nextWaiter = 0;
            return;
        }
    }
}

#endif

static void pollerMain(void* argument, void* raised) {
    (void)argument;
    (void)raised;
    for (;;) pollEvents(fireTimers());
}

static void startPoller(void) {
    u32 state = __atomic_load_n(&poller.state, __ATOMIC_ACQUIRE);
    if (state == 2) return;
    if (state == 0 && __atomic_compare_exchange_n(&poller.state, &state, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        poller.sleepingUntil = NO_DEADLINE;
        openPoller();
        startRuntimeThread(&poller.thread, pollerMain, 0);
        __atomic_store_n(&poller.state, 2, __ATOMIC_RELEASE);
        return;
    }
    for (u32 spins = 0; __atomic_load_n(&poller.state, __ATOMIC_ACQUIRE) != 2; ++spins) relax(spins);
}

// time.sleep in a task: it parks until its timer. 0 outside a task, where the
// library sleeps as a thread.
i64 cromo_task_sleep(i64 nanoseconds) {
    Task* task = currentTask();
    if (!task) return 0;
    startPoller();
    lockAcquire(&poller.lock);
    task->waitState = WAIT_PARKED;
    addTimer(task, monotonicNanoseconds() + (nanoseconds > 0 ? nanoseconds : 0), TIMER_WAKE);
    lockRelease(&poller.lock);
    parkTask(task);
    task->waitState = WAIT_NONE;
    return 1;
}

// A task waits until the socket can be read or written, or until the timeout
// (D225): 1 ready, 0 timed out, -1 outside a task, where the library waits as a
// thread. Ready can also mean an error or the end, which the next call tells.
i64 cromo_task_wait_socket(i64 handle, i64 reading, i64 milliseconds) {
    Task* task = currentTask();
    if (!task) return -1;
    startPoller();
#ifdef _WIN32
    if (!poller.afd) return -1;
    task->poll.timeout = NO_DEADLINE;
    task->poll.count = 1;
    task->poll.exclusive = 0;
    task->poll.handle = baseSocket(handle);
    task->poll.events = reading ? AFD_POLL_RECEIVE | AFD_POLL_ACCEPT | AFD_POLL_DISCONNECT | AFD_POLL_ABORT |
                                      AFD_POLL_LOCAL_CLOSE | AFD_POLL_CONNECT_FAIL
                                : AFD_POLL_SEND | AFD_POLL_ABORT | AFD_POLL_LOCAL_CLOSE | AFD_POLL_CONNECT_FAIL;
    task->poll.status = 0;
    task->status.status = STATUS_PENDING;
    task->status.information = 0;
    task->waitState = WAIT_PARKED;
    i32 status = poller.deviceControl(poller.afd, 0, 0, &task->status, &task->status, IOCTL_AFD_POLL, &task->poll,
                                      sizeof(AfdPoll), &task->poll, sizeof(AfdPoll));
    if (status != STATUS_PENDING && status < 0) {  // not started: the next call tells why
        task->waitState = WAIT_NONE;
        return 1;
    }
    if (milliseconds >= 0) {
        lockAcquire(&poller.lock);
        addTimer(task, monotonicNanoseconds() + milliseconds * 1000000, TIMER_CANCEL);
        lockRelease(&poller.lock);
    }
    parkTask(task);
#else
    SocketWaits* waits = socketWaits(handle, 1);
    if (!waits) return -1;
    lockAcquire(&waits->lock);
    if (!waits->registered) {
        EpollEvent event = {EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET, (u64)(u32)handle | (u64)waits->generation << 32};
        if (systemCall(SYS_epoll_ctl, poller.epoll, 1, handle, (i64)&event, 0, 0) < 0) {  // EPOLL_CTL_ADD
            lockRelease(&waits->lock);
            return -1;
        }
        waits->registered = 1;
    }
    u8* pending = reading ? &waits->readable : &waits->writable;
    if (*pending) {
        *pending = 0;
        lockRelease(&waits->lock);
        return 1;
    }
    Task** list = reading ? &waits->readers : &waits->writers;
    task->nextWaiter = *list;
    *list = task;
    task->waitState = WAIT_PARKED;
    lockRelease(&waits->lock);
    if (milliseconds >= 0) {
        lockAcquire(&poller.lock);
        addTimer(task, monotonicNanoseconds() + milliseconds * 1000000, TIMER_WAKE);
        lockRelease(&poller.lock);
    }
    parkTask(task);
    lockAcquire(&waits->lock);
    unlinkWaiter(list, task);  // woken by the timer, it is still there
    lockRelease(&waits->lock);
#endif
    if (milliseconds >= 0) {
        lockAcquire(&poller.lock);
        removeTimer(task);  // woken by the event, its timer is still there
        lockRelease(&poller.lock);
    }
    task->waitState = WAIT_NONE;
    return task->ready;
}

// Before a socket is closed: on Linux its number can come back with another one.
void cromo_socket_closed(i64 handle) {
#ifdef _WIN32
    (void)handle;
#else
    SocketWaits* waits = socketWaits(handle, 0);
    if (!waits) return;
    lockAcquire(&waits->lock);
    ++waits->generation;
    waits->registered = 0;
    waits->readable = 0;
    waits->writable = 0;
    lockRelease(&waits->lock);
#endif
}

// ---- Arguments of the program (D154) ----

#ifdef _WIN32

// The command line, split as the C runtime of Microsoft does, and converted
// from UTF-16 to UTF-8 the first time it is needed.
typedef struct {
    u8* data;
    i64 length;
} Argument;

static Argument* arguments;
static i64 argumentTotal = -1;

static void loadArguments(void) {
    if (argumentTotal >= 0) return;
    const u16* line = GetCommandLineW();
    u64 size = 0;
    while (line[size]) ++size;
    // The arguments one after the other, each ended by a zero: they are never longer than the line.
    u16* parsed = cromo_alloc((i64)((size + 2) * sizeof(u16)));
    u64 n = 0;
    u64 i = 0;
    i64 count = 1;
    // The name of the program ends at the first space, or between quotes, without escapes.
    if (line[i] == '"') {
        for (++i; line[i] && line[i] != '"'; ++i) parsed[n++] = line[i];
        if (line[i]) ++i;
    } else {
        for (; line[i] && line[i] != ' ' && line[i] != '\t'; ++i) parsed[n++] = line[i];
    }
    parsed[n++] = 0;
    for (;;) {
        while (line[i] == ' ' || line[i] == '\t') ++i;
        if (!line[i]) break;
        int quoted = 0;
        for (;;) {
            u64 backslashes = 0;
            while (line[i] == '\\') {
                ++backslashes;
                ++i;
            }
            if (line[i] == '"') {
                // 2n backslashes and a quote: n backslashes, and the quote opens or closes.
                // 2n+1 backslashes and a quote: n backslashes and the quote itself.
                for (u64 k = 0; k < backslashes / 2; ++k) parsed[n++] = '\\';
                if (backslashes % 2) {
                    parsed[n++] = '"';
                } else if (quoted && line[i + 1] == '"') {  // "" between quotes is a quote
                    parsed[n++] = '"';
                    ++i;
                } else {
                    quoted = !quoted;
                }
                ++i;
                continue;
            }
            for (u64 k = 0; k < backslashes; ++k) parsed[n++] = '\\';
            if (!line[i] || (!quoted && (line[i] == ' ' || line[i] == '\t'))) break;
            parsed[n++] = line[i++];
        }
        parsed[n++] = 0;
        ++count;
    }

    arguments = cromo_alloc(count * (i64)sizeof(Argument));
    const u16* piece = parsed;
    for (i64 k = 0; k < count; ++k) {
        int wide = 0;
        while (piece[wide]) ++wide;
        int bytes = wide ? WideCharToMultiByte(65001, 0, piece, wide, 0, 0, 0, 0) : 0;
        arguments[k].data = cromo_alloc(bytes);
        if (bytes) WideCharToMultiByte(65001, 0, piece, wide, (char*)arguments[k].data, bytes, 0, 0);
        arguments[k].length = bytes;
        piece += wide + 1;
    }
    cromo_free(parsed);
    argumentTotal = count;
}

i64 cromo_arg_count(void) {
    loadArguments();
    return argumentTotal;
}

i64 cromo_arg_length(i64 index) {
    loadArguments();
    return arguments[index].length;
}

const u8* cromo_arg_data(i64 index) {
    loadArguments();
    return arguments[index].data;
}

#else

// Where _start found the stack: the number of arguments, then their addresses (mem_x64.S).
u64* cromo_initial_stack;

i64 cromo_arg_count(void) { return (i64)cromo_initial_stack[0]; }

const u8* cromo_arg_data(i64 index) { return (const u8*)cromo_initial_stack[1 + index]; }

// The variables of the environment, "NAME=value" ended by a zero, after the
// arguments on the initial stack; null after the last one.
const u8* cromo_environment(i64 index) {
    u64* entries = cromo_initial_stack + 2 + cromo_initial_stack[0];
    for (i64 i = 0; i < index; ++i) {
        if (!entries[i]) return 0;
    }
    return (const u8*)entries[index];
}

i64 cromo_arg_length(i64 index) {
    const u8* text = cromo_arg_data(index);
    i64 length = 0;
    while (text[length]) ++length;
    return length;
}

#endif
