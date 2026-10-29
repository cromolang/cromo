// Bootstrap runtime of Cromo (D58, D93): printing, panics, heap and program exit.
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

// The Microsoft x64 ABI requires this symbol when floating point is used (D19).
int _fltused = 0;

#else

// ---- Linux system calls ----

#define SYS_write 1
#define SYS_mmap 9
#define SYS_munmap 11
#define SYS_mremap 25
#define SYS_exit_group 231
#define EINTR 4

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

// ---- Buffered output ----

#define BUFFER_SIZE 4096

typedef struct {
    u8 data[BUFFER_SIZE];
    u32 length;
} Output;

static Output outputs[2];  // stdout, stderr

#ifdef _WIN32

static u16 wide[BUFFER_SIZE];

static void writeRaw(i32 stream, const u8* data, u32 size) {
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

static void writeRaw(i32 stream, const u8* data, u32 size) {
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

static Output* outputOf(i32 stream) { return &outputs[stream == 2 ? 1 : 0]; }

static void flush(i32 stream) {
    Output* out = outputOf(stream);
    writeRaw(stream, out->data, out->length);
    out->length = 0;
}

static void put(i32 stream, const u8* data, u64 size) {
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

static void putRepeat(i32 stream, u8 c, i32 count) {
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
static void emitPadded(i32 stream, const FmtSpec* spec, const u8* prefix, u32 prefixSize, const u8* body, u32 bodySize,
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

void cromo_write(i32 stream, const u8* data, u64 size) { put(stream, data, size); }

void cromo_fmt_str(i32 stream, const u8* data, u64 size, const FmtSpec* spec) {
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

void cromo_fmt_bool(i32 stream, i32 value, const FmtSpec* spec) {
    if (!spec) spec = &defaultSpec;
    if (value) emitPadded(stream, spec, 0, 0, (const u8*)"true", 4, '<');
    else emitPadded(stream, spec, 0, 0, (const u8*)"false", 5, '<');
}

void cromo_fmt_int(i32 stream, u64 value, i32 isSigned, const FmtSpec* spec) {
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

#define BIG_WORDS 40  // 1280 bits: enough for 10^324 * 2^55 and for 2^1024 * 10

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

static void formatFloat(i32 stream, const FloatParts* f, int negative, int isNan, int isInf, const FmtSpec* spec) {
    if (!spec) spec = &defaultSpec;
    u8 prefix[1];
    u32 prefixSize = signPrefix(negative, spec, prefix);

    if (isNan || isInf) {
        FmtSpec noZeros = *spec;  // inf and nan are padded with spaces, never with zeros
        noZeros.zeroPad = 0;
        emitPadded(stream, &noZeros, prefix, prefixSize, (const u8*)(isNan ? "nan" : "inf"), 3, '>');
        return;
    }

    static u8 text[MAX_DIGITS + 32];
    static Decimal d;
    u32 n = 0;
    int alternate = spec->alternate;
    i32 precision = spec->precision;

    if (spec->type == 'e') {
        i32 p = precision < 0 ? 6 : precision;
        exactDigits(f, p + 1, -1, &d);
        n = writeScientific(&d, p, alternate, text);
    } else if (spec->type == 'f') {
        i32 p = precision < 0 ? 6 : precision;
        exactDigits(f, -1, p, &d);
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

void cromo_fmt_f64(i32 stream, double value, const FmtSpec* spec) {
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

void cromo_fmt_f32(i32 stream, float value, const FmtSpec* spec) {
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

// ---- End of a print, exit and panic ----

void cromo_print_end(i32 stream, i32 newline) {
    if (newline) put(stream, (const u8*)"\n", 1);
    flush(stream);
}

NORETURN void cromo_exit(i32 code) {
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

// D121: an exception that leaves main, with its message; exit code 1.
NORETURN void cromo_uncaught(const u8* message, u64 size) {
    flush(1);
    put(2, (const u8*)"uncaught exception: ", 20);
    put(2, message, size);
    put(2, (const u8*)"\n", 1);
    flush(2);
    exitProcess(1);
}

// ---- Heap, for list and string (D98) ----

static NORETURN void outOfMemory(void) {
    panicStart();
    put(2, (const u8*)"out of memory", 13);
    panicEnd(0);
}

#ifdef _WIN32

static void* heapAlloc(u64 size) { return HeapAlloc(GetProcessHeap(), 0, size); }
static void* heapRealloc(void* memory, u64 size) { return HeapReAlloc(GetProcessHeap(), 0, memory, size); }
static void heapFree(void* memory) { HeapFree(GetProcessHeap(), 0, memory); }

#else

// A simple allocator on top of mmap, without threads (D43). Every block starts
// with 16 bytes that hold how many bytes it can contain. The small blocks, up to
// 4 KB with those 16 bytes, have a size that is a power of two from 32 bytes: they
// are cut from areas of 1 MB, and the freed ones are kept in a list for each size.
// The larger blocks have a mapping of their own.

#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_PRIVATE 2
#define MAP_ANONYMOUS 0x20
#define MREMAP_MAYMOVE 1
#define HEADER 16
#define SMALL_LIMIT 4096
#define SMALL_SIZES 8  // 32, 64, ..., 4096
#define AREA_SIZE (1u << 20)

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
            if ((u64)(areaEnd - areaNext) < total) {  // the rest of the old area, less than 4 KB, is not used
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

void* cromo_alloc(i64 size) {
    void* memory = size >= 0 ? heapAlloc((u64)size) : 0;
    if (!memory) outOfMemory();
    return memory;
}

void* cromo_realloc(void* memory, i64 size) {
    if (!memory) return cromo_alloc(size);
    void* result = size >= 0 ? heapRealloc(memory, (u64)size) : 0;
    if (!result) outOfMemory();
    return result;
}

void cromo_free(void* memory) {
    if (memory) heapFree(memory);
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

i64 cromo_arg_length(i64 index) {
    const u8* text = cromo_arg_data(index);
    i64 length = 0;
    while (text[length]) ++length;
    return length;
}

#endif
