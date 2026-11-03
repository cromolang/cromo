// Bootstrap runtime of Cromo (D58, D93): printing, panics, threads, tasks and program exit.
// Freestanding C without the C library (D19): it talks to Windows through
// kernel32.dll, and to Linux with system calls (D142). CMake compiles it once for
// each system and architecture (D405). It will be rewritten in Cromo `unsafe` when extern (M7), the
// bitwise operators and fixed-size arrays exist (D37, D93).

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int i32;
typedef unsigned long long u64;
typedef long long i64;

#define NORETURN __attribute__((noreturn))

void* memcpy(void* dest, const void* src, u64 count);  // in mem_x64.S and mem_arm64.S

#ifdef _WIN32

// ---- Windows API ----

#define WINAPI __stdcall
__declspec(dllimport) void* WINAPI GetStdHandle(u32 which);
__declspec(dllimport) int WINAPI WriteFile(void* file, const void* data, u32 size, u32* written, void* overlapped);
__declspec(dllimport) __declspec(noreturn) void WINAPI ExitProcess(u32 code);
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
__declspec(dllimport) u16 WINAPI RtlCaptureStackBackTrace(u32 skip, u32 count, void** frames, u32* hash);
__declspec(dllimport) void* WINAPI AddVectoredExceptionHandler(u32 first, void* handler);
__declspec(dllimport) int WINAPI SetThreadStackGuarantee(u32* size);
__declspec(dllimport) u32 WINAPI GetFileType(void* file);
__declspec(dllimport) void* WINAPI LoadLibraryW(const u16* name);
__declspec(dllimport) int WINAPI MultiByteToWideChar(u32 codePage, u32 flags, const char* text, int size, u16* out,
                                                     int outSize);
__declspec(dllimport) u32 WINAPI GetModuleFileNameW(void* module, u16* name, u32 size);

// The Microsoft x64 ABI requires this symbol when floating point is used (D19).
int _fltused = 0;

#else

// ---- Linux system calls ----
//
// Their numbers are those of each architecture; ARM64 has only the newer calls,
// which x86-64 has too, and both use them (P23).

#ifdef __aarch64__
#define SYS_read 63
#define SYS_write 64
#define SYS_mmap 222
#define SYS_mprotect 226
#define SYS_munmap 215
#define SYS_madvise 233
#define SYS_rt_sigaction 134
#define SYS_sched_yield 124
#define SYS_nanosleep 101
#define SYS_clone 220
#define SYS_exit 93
#define SYS_sigaltstack 132
#define SYS_futex 98
#define SYS_sched_getaffinity 123
#define SYS_clock_gettime 113
#define SYS_exit_group 94
#define SYS_epoll_pwait 22
#define SYS_epoll_ctl 21
#define SYS_eventfd2 19
#define SYS_epoll_create1 20
#else
#define SYS_read 0
#define SYS_write 1
#define SYS_mmap 9
#define SYS_mprotect 10
#define SYS_munmap 11
#define SYS_madvise 28
#define SYS_rt_sigaction 13
#define SYS_sched_yield 24
#define SYS_nanosleep 35
#define SYS_clone 56
#define SYS_exit 60
#define SYS_sigaltstack 131
#define SYS_futex 202
#define SYS_sched_getaffinity 204
#define SYS_clock_gettime 228
#define SYS_exit_group 231
#define SYS_epoll_pwait 281
#define SYS_epoll_ctl 233
#define SYS_eventfd2 290
#define SYS_epoll_create1 291
#endif
#define EINTR 4
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE 128

// The size of a page: 4 KB on x86-64, and 4, 16 or 64 KB on ARM64, as the kernel
// says at the start (readSystem). The guard pages of the stacks are one page.
static u64 pageSize = 4096;

static i64 systemCall(i64 number, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f);

// For the glue of Mbed TLS (runtime/tls): the result, or -errno. The library
// written in Cromo makes its system calls itself: the compiler writes the
// instruction for systemCall (D202).
i64 cromo_system_call(i64 number, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    return systemCall(number, a, b, c, d, e, f);
}

#ifdef __aarch64__

static i64 systemCall(i64 number, i64 a, i64 b, i64 c, i64 d, i64 e, i64 f) {
    register i64 x8 __asm__("x8") = number;
    register i64 x0 __asm__("x0") = a;
    register i64 x1 __asm__("x1") = b;
    register i64 x2 __asm__("x2") = c;
    register i64 x3 __asm__("x3") = d;
    register i64 x4 __asm__("x4") = e;
    register i64 x5 __asm__("x5") = f;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
    return x0;  // from -4095 to -1 it is an error, -errno
}

#else

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

#endif

// The hint of a loop that waits for another thread.
#ifdef __aarch64__
#define SPIN_HINT() __asm__ volatile("yield")
#else
#define SPIN_HINT() __asm__ volatile("pause")
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
        SPIN_HINT();
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

// ---- The output: in Cromo, in std.runtime (P25) ----
//
// The buffers of the standard output and of the standard error, and the formatting
// of every value, are written in Cromo (D393): the runtime in C writes its panics
// and its stack traces through them, under the lock of printing. The hashes for the
// keys of a map (D198) and the numbers from text (D199) are there too.

void cromo_write(i64 stream, const u8* data, u64 size);
void cromo_flush(i64 stream);
void cromo_fmt_int(i64 stream, u64 value, i32 isSigned, const void* spec);
void* cromo_realloc(void* memory, i64 size);

// The string of the library: data, length and capacity.
typedef struct {
    u8* data;
    i64 length;
    i64 capacity;
} Text;

#ifdef _WIN32

// Only for the message of out of memory, which writes without the buffers.
static void writeRaw(const u8* data, u32 size) {
    u32 written;
    WriteFile(GetStdHandle((u32)-12), data, size, &written, 0);
}

static NORETURN void exitProcess(i32 code) { ExitProcess((u32)code); }

#else

static void writeRaw(const u8* data, u32 size) {
    while (size > 0) {
        i64 written = systemCall(SYS_write, 2, (i64)data, size, 0, 0, 0);
        if (written == -EINTR) continue;
        if (written <= 0) return;  // nowhere to write: the text is lost
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

// ---- End of a print, exit and panic ----

void cromo_print_end(i64 stream, i32 newline) {
    if (newline) cromo_write(stream, (const u8*)"\n", 1);
    cromo_flush(stream);
    sharedRelease(&printLock);
}

NORETURN void cromo_exit(i32 code) {
    sharedAcquire(&printLock);
    cromo_flush(1);
    cromo_flush(2);
    exitProcess(code);
}

// ---- Stack traces (D339-D341) ----
//
// cromo writes in every program the table of its code (backend.cpp): from a
// return address, the function, the file and the line, with the inlined calls.
// The frames of the runtime and of C are not in it, and are left out.

typedef struct {
    const u8* code;      // cromo_code: where the offsets count from
    u64 size;
    u64 count;
    const u32* offsets;  // where each point starts, in order
    const u32* chains;   // its first frame, or none
    const u32* frames;   // name, file, line | 1 << 31 when a caller follows
    const char* strings;
} TraceTable;

extern const TraceTable cromo_trace_table;

#define TRACE_FRAMES 63
#define TRACE_MORE 0x80000000u
#define CRASH_STACK (64u << 10)  // the room of the handler of a stack overflow

// D340: in front of the object of every exception, the stack of its throw.
typedef struct {
    u64 count;
    void* frames[TRACE_FRAMES];
} ExceptionTrace;

#ifdef _WIN32

// D341: the unwind tables, which LLVM writes for every function of Windows. The
// first address is in the caller of this function.
static u32 traceCapture(void** frames, u32 most) { return RtlCaptureStackBackTrace(1, most, frames, 0); }

#else

// D375: the unwind tables that LLVM writes for every function, .eh_frame, found
// through .eh_frame_hdr (ld.lld --eh-frame-hdr), in the program headers that
// __ehdr_start shows. For the address of a frame, the rules of DWARF say where
// the frame of the caller starts (the CFA) and where the return address and the
// frame pointer were saved. The stack ends where an address has no rules: in
// _start and in cromo_task_entry, which are in assembly, and after the first call
// of a thread. On ARM64 the return address is in x30 until a function saves it
// (D405).

extern const u8 __ehdr_start[] __attribute__((weak));
extern const u8 cromo_thread_bottom[];  // in cloneThread

// The numbers of the registers in DWARF.
#ifdef __aarch64__
#define REG_SP 31
#define REG_FP 29  // x29
#define REG_RA 30  // x30
#else
#define REG_SP 7   // rsp
#define REG_FP 6   // rbp
#define REG_RA 16  // the return address
#endif

typedef struct {
    u64 pc;
    u64 sp;
    u64 fp;
    u64 lr;  // ARM64: x30, for the first frame only; 0 when it is not known
} Registers;

typedef struct {
    i64 cfaRegister;  // REG_SP or REG_FP
    i64 cfaOffset;
    i64 raOffset;     // from the CFA
    i64 fpOffset;
    int raSaved;
    int fpSaved;
} Rules;

static u64 readUnsigned(const u8* p, int size) {
    u64 value = 0;
    for (int i = 0; i < size; ++i) value |= (u64)p[i] << (8 * i);
    return value;
}

static u64 readUleb(const u8** p) {
    u64 value = 0;
    for (int shift = 0;; shift += 7) {
        u8 byte = *(*p)++;
        if (shift < 64) value |= (u64)(byte & 0x7F) << shift;
        if (!(byte & 0x80)) return value;
    }
}

static i64 readSleb(const u8** p) {
    i64 value = 0;
    int shift = 0;
    u8 byte;
    do {
        byte = *(*p)++;
        if (shift < 64) value |= (i64)(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);
    if (shift < 64 && (byte & 0x40)) value |= -((i64)1 << shift);
    return value;
}

// A pointer of .eh_frame with its encoding (DW_EH_PE_*); 0 if it is not supported.
static u64 readEncoded(const u8** p, u8 encoding, u64 dataBase) {
    const u8* start = *p;
    u64 value;
    switch (encoding & 0x0F) {
        case 0x00: value = readUnsigned(*p, 8); *p += 8; break;
        case 0x01: value = readUleb(p); break;
        case 0x02: value = readUnsigned(*p, 2); *p += 2; break;
        case 0x03: value = readUnsigned(*p, 4); *p += 4; break;
        case 0x04: value = readUnsigned(*p, 8); *p += 8; break;
        case 0x09: value = (u64)readSleb(p); break;
        case 0x0A: value = (u64)(i64)(short)readUnsigned(*p, 2); *p += 2; break;
        case 0x0B: value = (u64)(i64)(i32)readUnsigned(*p, 4); *p += 4; break;
        case 0x0C: value = readUnsigned(*p, 8); *p += 8; break;
        default: return 0;
    }
    switch (encoding & 0x70) {
        case 0x00: break;
        case 0x10: value += (u64)start; break;  // pcrel
        case 0x30: value += dataBase; break;    // datarel
        default: return 0;
    }
    if (encoding & 0x80) value = readUnsigned((const u8*)value, 8);  // indirect
    return value;
}

// .eh_frame_hdr, from the program headers: 0 without it.
static const u8* frameIndex(void) {
    if (!__ehdr_start) return 0;
    const u8* header = __ehdr_start;
    u64 table = readUnsigned(header + 32, 8);  // e_phoff
    u64 size = readUnsigned(header + 54, 2);   // e_phentsize
    u64 count = readUnsigned(header + 56, 2);  // e_phnum
    u64 bias = 0;
    for (u64 i = 0; i < count; ++i) {  // where the headers are linked: the load with offset 0
        const u8* entry = header + table + i * size;
        if (readUnsigned(entry, 4) == 1 && readUnsigned(entry + 8, 8) == 0) bias = (u64)header - readUnsigned(entry + 16, 8);
    }
    for (u64 i = 0; i < count; ++i) {
        const u8* entry = header + table + i * size;
        if (readUnsigned(entry, 4) == 0x6474E550) return (const u8*)(bias + readUnsigned(entry + 16, 8));  // PT_GNU_EH_FRAME
    }
    return 0;
}

// The rules at `pc`: the CIE, then the instructions of the FDE up to `pc`.
static int findRules(u64 pc, Rules* rules) {
    const u8* index = frameIndex();
    if (!index || index[0] != 1 || index[3] != 0x3B) return 0;  // a table of 4-byte offsets from the index
    const u8* p = index + 4;
    readEncoded(&p, index[1], (u64)index);  // .eh_frame
    u64 count = readEncoded(&p, index[2], (u64)index);
    const u8* table = p;
    u64 low = 0;
    u64 high = count;
    while (low < high) {  // the last function that starts at or before pc
        u64 middle = (low + high) / 2;
        if ((u64)index + (u64)(i64)(i32)readUnsigned(table + 8 * middle, 4) <= pc) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low == 0) return 0;
    const u8* fde = index + (i64)(i32)readUnsigned(table + 8 * (low - 1) + 4, 4);
    u64 fdeLength = readUnsigned(fde, 4);
    if (fdeLength == 0 || fdeLength == 0xFFFFFFFF) return 0;
    const u8* fdeEnd = fde + 4 + fdeLength;
    p = fde + 4;
    const u8* cie = p - readUnsigned(p, 4);
    p += 4;

    // The CIE: the alignments, the encoding of the addresses, the first rules.
    u64 cieLength = readUnsigned(cie, 4);
    if (cieLength == 0xFFFFFFFF) return 0;
    const u8* cieEnd = cie + 4 + cieLength;
    const u8* q = cie + 8;
    u8 version = *q++;
    const char* augmentation = (const char*)q;
    while (*q) ++q;
    ++q;
    if (version >= 4) q += 2;  // the sizes of an address and of a segment
    u64 codeAlign = readUleb(&q);
    i64 dataAlign = readSleb(&q);
    u64 raRegister = version == 1 ? *q++ : readUleb(&q);
    u8 fdeEncoding = 0;
    int hasData = augmentation[0] == 'z';
    if (hasData) {
        u64 dataLength = readUleb(&q);
        const u8* dataEnd = q + dataLength;
        for (const char* a = augmentation + 1; *a; ++a) {
            if (*a == 'R') {
                fdeEncoding = *q++;
            } else if (*a == 'P') {
                u8 encoding = *q++;
                readEncoded(&q, encoding, 0);
            } else if (*a == 'L') {
                ++q;
            }
        }
        q = dataEnd;
    }
    if (raRegister != REG_RA) return 0;

    u64 start = readEncoded(&p, fdeEncoding, 0);
    u64 range = readEncoded(&p, fdeEncoding & 0x0F, 0);
    if (pc < start || pc >= start + range) return 0;
    if (hasData) {
        u64 dataLength = readUleb(&p);
        p += dataLength;
    }

#ifdef __aarch64__
    Rules initial = {REG_SP, 0, 0, 0, 0, 0};  // the return address in x30
#else
    Rules initial = {REG_SP, 8, -8, 0, 1, 0};  // pushed by the call
#endif
    Rules saved[4];
    int depth = 0;
    for (int pass = 0; pass < 2; ++pass) {  // the CIE, then the FDE
        const u8* code = pass == 0 ? q : p;
        const u8* end = pass == 0 ? cieEnd : fdeEnd;
        Rules* r = rules;
        if (pass == 0) *r = initial;
        u64 location = start;
        while (code < end) {
            u8 op = *code++;
            u8 operand = op & 0x3F;
            u64 reg = 0;
            i64 offset = 0;
            switch (op & 0xC0) {
                case 0x40:  // advance_loc
                    location += operand * codeAlign;
                    if (location > pc) return 1;
                    continue;
                case 0x80:  // offset
                    reg = operand;
                    offset = (i64)readUleb(&code) * dataAlign;
                    goto setOffset;
                case 0xC0:  // restore
                    reg = operand;
                    goto restore;
            }
            switch (op) {
                case 0x00: continue;
                case 0x01: location = readEncoded(&code, fdeEncoding, 0); if (location > pc) return 1; continue;
                case 0x02: location += *code++ * codeAlign; if (location > pc) return 1; continue;
                case 0x03: location += readUnsigned(code, 2) * codeAlign; code += 2; if (location > pc) return 1; continue;
                case 0x04: location += readUnsigned(code, 4) * codeAlign; code += 4; if (location > pc) return 1; continue;
                case 0x05: reg = readUleb(&code); offset = (i64)readUleb(&code) * dataAlign; goto setOffset;
                case 0x06: reg = readUleb(&code); goto restore;
                case 0x07:  // undefined: the return address has none at the end of the stack
                case 0x08:  // same value
                    reg = readUleb(&code);
                    if (reg == REG_RA) r->raSaved = 0;
                    if (reg == REG_FP) r->fpSaved = 0;
                    continue;
                case 0x09: reg = readUleb(&code); readUleb(&code); if (reg == REG_FP || reg == REG_RA) return 0; continue;
                case 0x0A: if (depth == 4) return 0; saved[depth++] = *r; continue;
                case 0x0B: if (depth == 0) return 0; *r = saved[--depth]; continue;
                case 0x0C: r->cfaRegister = (i64)readUleb(&code); r->cfaOffset = (i64)readUleb(&code); continue;
                case 0x0D: r->cfaRegister = (i64)readUleb(&code); continue;
                case 0x0E: r->cfaOffset = (i64)readUleb(&code); continue;
                case 0x10: reg = readUleb(&code); code += readUleb(&code); if (reg == REG_FP || reg == REG_RA) return 0; continue;
                case 0x11: reg = readUleb(&code); offset = readSleb(&code) * dataAlign; goto setOffset;
                case 0x12: r->cfaRegister = (i64)readUleb(&code); r->cfaOffset = readSleb(&code) * dataAlign; continue;
                case 0x13: r->cfaOffset = readSleb(&code) * dataAlign; continue;
                case 0x14: case 0x15: reg = readUleb(&code); readUleb(&code); if (reg == REG_FP || reg == REG_RA) return 0; continue;
                case 0x16: reg = readUleb(&code); code += readUleb(&code); if (reg == REG_FP || reg == REG_RA) return 0; continue;
                case 0x2E: readUleb(&code); continue;  // GNU_args_size
                case 0x2F: reg = readUleb(&code); offset = -(i64)readUleb(&code) * dataAlign; goto setOffset;
                default: return 0;  // def_cfa_expression and the rest: not in the code of LLVM
            }
        setOffset:
            if (reg == REG_RA) {
                r->raOffset = offset;
                r->raSaved = 1;
            } else if (reg == REG_FP) {
                r->fpOffset = offset;
                r->fpSaved = 1;
            }
            continue;
        restore:
            if (reg == REG_RA) {
                r->raOffset = initial.raOffset;
                r->raSaved = initial.raSaved;
            } else if (reg == REG_FP) {
                r->fpOffset = initial.fpOffset;
                r->fpSaved = initial.fpSaved;
            }
            continue;
        }
        if (pass == 0) initial = *r;
    }
    return 1;
}

// From the frame of `r` to that of its caller: 0 at the end of the stack. The
// first frame is at its exact address, the others at their return address.
static int unwindStep(Registers* r, int returned) {
    Rules rules;
    if (!findRules(returned ? r->pc - 1 : r->pc, &rules) || (!rules.raSaved && !r->lr)) return 0;
    u64 base = rules.cfaRegister == REG_SP ? r->sp : rules.cfaRegister == REG_FP ? r->fp : 0;
    if (!base) return 0;
    u64 cfa = base + (u64)rules.cfaOffset;
    // The frame of the caller is above, unless the return address is still in x30;
    // a frame larger than 16 MB is a mistake.
    if (cfa < r->sp || (cfa == r->sp && rules.raSaved) || cfa - r->sp > ((u64)16 << 20)) return 0;
    u64 ra = rules.raSaved ? readUnsigned((const u8*)(cfa + (u64)rules.raOffset), 8) : r->lr;
    if (rules.fpSaved) r->fp = readUnsigned((const u8*)(cfa + (u64)rules.fpOffset), 8);
    r->sp = cfa;
    r->pc = ra;
    r->lr = 0;
    return ra != 0 && ra != (u64)cromo_thread_bottom;
}

static u32 traceFrom(Registers* r, int returned, void** frames, u32 count, u32 most) {
    while (count < most && unwindStep(r, returned)) {
        frames[count++] = (void*)r->pc;
        returned = 1;
    }
    return count;
}

// The first address is in the caller of this function, as on Windows.
static __attribute__((noinline)) u32 traceCapture(void** frames, u32 most) {
    Registers r;
#ifdef __aarch64__
    __asm__ volatile("adr %0, .\n\tmov %1, sp\n\tmov %2, x29\n\tmov %3, x30"
                     : "=r"(r.pc), "=r"(r.sp), "=r"(r.fp), "=r"(r.lr));
#else
    __asm__ volatile("lea 0(%%rip), %0\n\tmov %%rsp, %1\n\tmov %%rbp, %2" : "=r"(r.pc), "=r"(r.sp), "=r"(r.fp));
    r.lr = 0;
#endif
    return traceFrom(&r, 0, frames, 0, most);
}

#endif

// ---- The text of a fatal error (D427) ----
//
// It goes to the standard error. In an executable without a console (D426) whose
// standard error is not redirected, it goes to a string instead, which a message
// box shows when the program ends.

static i64 fatalStream = 2;

#ifdef _WIN32

extern const u8 __ImageBase[];  // the header of the executable, from lld-link
static Text fatalText;
static const u8* fatalData;
static u64 fatalSize;

// The subsystem in the header of the executable: 2 for the windows, with [[window]].
static int windowed(void) {
    const u8* headers = __ImageBase + *(const u32*)(__ImageBase + 0x3C);
    return *(const u16*)(headers + 4 + 20 + 68) == 2;
}

// Whether the text has somewhere to go: a file, a pipe or a console.
static int errorRedirected(void) {
    void* handle = GetStdHandle((u32)-12);
    return handle && handle != (void*)-1 && GetFileType(handle) != 0;  // not FILE_TYPE_UNKNOWN
}

// The message box, on a thread of its own: the handler of a stack overflow has
// little stack, and user32 needs more.
static u32 WINAPI fatalWindow(void* argument) {
    (void)argument;
    static const u16 user32[] = L"user32.dll";
    void* library = LoadLibraryW(user32);
    i32(WINAPI * messageBox)(void*, const u16*, const u16*, u32) =
        library ? (i32(WINAPI*)(void*, const u16*, const u16*, u32))GetProcAddress(library, "MessageBoxW") : 0;
    if (!messageBox) return 0;
    // Each byte of UTF-8 is at most a unit of UTF-16: the text is cut to fit.
    static u16 text[8192];
    u64 size = fatalSize < 8191 ? fatalSize : 8191;
    i32 count = MultiByteToWideChar(65001, 0, (const char*)fatalData, (int)size, text, 8191);
    text[count > 0 ? count : 0] = 0;
    static u16 path[1024];
    u32 length = GetModuleFileNameW(0, path, 1024);
    const u16* title = path;
    for (u32 i = 0; i < length; ++i) {
        if (path[i] == '\\') title = path + i + 1;
    }
    messageBox(0, text, title, 0x10 | 0x10000);  // MB_ICONERROR, MB_SETFOREGROUND
    return 0;
}

static void showFatal(const u8* data, u64 size) {
    fatalData = data;
    fatalSize = size;
    void* thread = CreateThread(0, 0, fatalWindow, 0, 0, 0);
    if (!thread) {
        fatalWindow(0);
        return;
    }
    WaitForSingleObject(thread, 0xFFFFFFFF);
}

#endif

// After the lock of printing is taken.
static void fatalStart(void) {
    cromo_flush(1);
#ifdef _WIN32
    if (windowed() && !errorRedirected()) fatalStream = (i64)&fatalText;
#endif
}

static NORETURN void fatalEnd(i32 code) {
#ifdef _WIN32
    if (fatalStream != 2) showFatal(fatalText.data, (u64)fatalText.length);
#endif
    cromo_flush(2);
    exitProcess(code);
}

static void putText(const char* text) {
    u64 size = 0;
    while (text[size]) ++size;
    cromo_write(fatalStream, (const u8*)text, size);
}

// The frames of the program at a return address, from the innermost, or none.
static const u32* traceFind(const void* address) {
    const TraceTable* table = &cromo_trace_table;
    const u8* at = (const u8*)address;
    if (at <= table->code || (u64)(at - table->code) > table->size) return 0;
    u64 offset = (u64)(at - table->code) - 1;  // inside the call
    u64 low = 0;
    u64 high = table->count;
    while (low < high) {
        u64 middle = (low + high) / 2;
        if (table->offsets[middle] <= offset) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low == 0 || table->chains[low - 1] == 0xFFFFFFFF) return 0;
    return table->frames + 3 * (u64)table->chains[low - 1];
}

static void traceRepeats(u64 repeats) {
    if (repeats == 0) return;
    putText("    ... the same ");
    cromo_fmt_int(fatalStream, repeats, 0, 0);
    putText(repeats == 1 ? " more time\n" : " more times\n");
}

// A line for each frame of the program, "    at main (main.cro:12)": the same
// frame one after the other, as in a recursion, is written once.
static void tracePrint(void* const* frames, u32 count) {
    const char* strings = cromo_trace_table.strings;
    const u32* last = 0;
    u64 repeats = 0;
    for (u32 i = 0; i < count; ++i) {
        const u32* frame = traceFind(frames[i]);
        for (; frame; frame += 3) {
            if (last && frame[0] == last[0] && frame[1] == last[1] && frame[2] == last[2]) {
                ++repeats;
            } else {
                traceRepeats(repeats);
                repeats = 0;
                putText("    at ");
                putText(strings + frame[0]);
                putText(" (");
                putText(strings + frame[1]);
                putText(":");
                cromo_fmt_int(fatalStream, frame[2] & ~TRACE_MORE, 0, 0);
                putText(")\n");
                last = frame;
            }
            if (!(frame[2] & TRACE_MORE)) break;
        }
    }
    traceRepeats(repeats);
    if (count == TRACE_FRAMES) putText("    ...\n");
}

// Where a panic happens: "<file>:<line>:<column>", a constant built by the compiler.
typedef struct {
    const u8* text;
    u64 size;
} Where;

static void panicStart(void) {
    sharedAcquire(&printLock);
    fatalStart();
    cromo_write(fatalStream, (const u8*)"panic: ", 7);
}

static NORETURN void panicEnd(const Where* where) {
    if (where) {
        cromo_write(fatalStream, (const u8*)" at ", 4);
        cromo_write(fatalStream, where->text, where->size);
    }
    cromo_write(fatalStream, (const u8*)"\n", 1);
    void* frames[TRACE_FRAMES];
    tracePrint(frames, traceCapture(frames, TRACE_FRAMES));  // D339
    fatalEnd(101);
}

// D62: "panic: <reason> at <file>:<line>:<column>" on stderr, exit code 101.
NORETURN void cromo_panic(const u8* message, u64 size, const Where* where) {
    panicStart();
    cromo_write(fatalStream, message, size);
    panicEnd(where);
}

// D30: an index out of the bounds of a list.
NORETURN void cromo_panic_index(i64 index, i64 length, const Where* where) {
    panicStart();
    cromo_write(fatalStream, (const u8*)"index out of bounds: the index is ", 34);
    cromo_fmt_int(fatalStream, (u64)index, 1, 0);
    cromo_write(fatalStream, (const u8*)" but the length is ", 19);
    cromo_fmt_int(fatalStream, (u64)length, 1, 0);
    panicEnd(where);
}

// With the stack of its throw (D340).
static NORETURN void uncaught(const char* prefix, u32 prefixSize, const u8* message, u64 size, const void* object) {
    sharedAcquire(&printLock);
    fatalStart();
    cromo_write(fatalStream, (const u8*)prefix, prefixSize);
    cromo_write(fatalStream, message, size);
    cromo_write(fatalStream, (const u8*)"\n", 1);
    const ExceptionTrace* trace = (const ExceptionTrace*)object - 1;
    tracePrint(trace->frames, (u32)trace->count);
    fatalEnd(1);
}

// D121: an exception that leaves main, with its message; exit code 1.
NORETURN void cromo_uncaught(const u8* message, u64 size, const void* object) {
    uncaught("uncaught exception: ", 20, message, size, object);
}

// ---- Heap, for list and string (D98) ----
//
// cromo_alloc, cromo_realloc and cromo_free are written in Cromo, in std.runtime
// (D393); the heap takes its lock from the first thread on.

void* cromo_alloc(i64 size);
void cromo_free(void* memory);
void cromo_heap_threaded(void);

// Without the lock of printing: memory can run out while a format holds it.
NORETURN void cromo_out_of_memory(void) {
#ifdef _WIN32
    if (windowed() && !errorRedirected()) showFatal((const u8*)"panic: out of memory\n", 21);  // D427
#endif
    writeRaw((const u8*)"panic: out of memory\n", 21);
    exitProcess(101);
}

// The object of an exception, made by throw with the stack where it is (D340).
void* cromo_exception_new(i64 size) {
    ExceptionTrace* trace = cromo_alloc((i64)sizeof(ExceptionTrace) + size);
    trace->count = traceCapture(trace->frames, TRACE_FRAMES);
    return trace + 1;
}

void cromo_exception_free(void* object) {
    if (object) cromo_free((ExceptionTrace*)object - 1);
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
// What the register of the thread points to in every thread, fs on x86-64 and
// tpidr_el0 on ARM64, so that a thread knows whether it is a worker of the tasks:
// the first word is its own address, which x86-64 reads.
typedef struct ThreadBlock {
    struct ThreadBlock* self;
    Worker* worker;
} ThreadBlock;

// The main thread: _start (mem_x64.S, mem_arm64.S) points the register here.
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

static void crashStack(ThreadRecord* thread);

static void runThread(ThreadRecord* thread) {
    crashStack(thread);
    thread->head.code(thread->head.env, thread->head.raised);
}

static NORETURN void threadFailure(void) {
    static const char text[] = "cannot start a thread: the system has no resources for it";
    cromo_panic((const u8*)text, sizeof text - 1, 0);
}

static void relax(u32 spins) {
    if (spins < 64) {
        SPIN_HINT();
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
// Its register of the thread points to the block of its record.
#ifdef __aarch64__

static __attribute__((noinline)) i64 cloneThread(ThreadRecord* thread, u8* top) {
    u64* sp = (u64*)top - 2;
    sp[0] = (u64)threadEntry;
    sp[1] = (u64)thread;
    register i64 x8 __asm__("x8") = SYS_clone;
    register i64 x0 __asm__("x0") = CLONE_FLAGS;
    register i64 x1 __asm__("x1") = (i64)sp;
    register i64 x2 __asm__("x2") = (i64)&thread->tid;    // parent_tid
    register i64 x3 __asm__("x3") = (i64)&thread->block;  // tls
    register i64 x4 __asm__("x4") = (i64)&thread->tid;    // child_tid
    __asm__ volatile("svc #0\n\t"
                     "cbnz x0, 1f\n\t"
                     "mov x29, #0\n\t"
                     "ldp x9, x0, [sp], #16\n\t"
                     "blr x9\n"
                     ".globl cromo_thread_bottom\n"  // the stack of the thread ends here (D375)
                     "cromo_thread_bottom:\n\t"
                     "mov x8, #93\n\t"
                     "mov x0, #0\n\t"
                     "svc #0\n\t"
                     "brk #0\n"
                     "1:"
                     : "+r"(x0)
                     : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4)
                     : "x9", "memory");
    return x0;
}

#else

static __attribute__((noinline)) i64 cloneThread(ThreadRecord* thread, u8* top) {
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
                     ".globl cromo_thread_bottom\n"  // the stack of the thread ends here (D375)
                     "cromo_thread_bottom:\n\t"
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

#endif

static void startSystemThread(ThreadRecord* thread) {
    i64 stack = systemCall(SYS_mmap, 0, (i64)THREAD_STACK, 3, 0x22 | 0x20000, -1, 0);  // MAP_PRIVATE|ANONYMOUS|STACK
    if (stack < 0 && stack > -4096) threadFailure();
    thread->stack = (u8*)stack;
    // At the bottom a guard page, the stack of SIGSEGV, and a guard page under the
    // stack of the thread (crashStack).
    systemCall(SYS_mprotect, stack, (i64)pageSize, 0, 0, 0, 0);
    systemCall(SYS_mprotect, stack + (i64)pageSize + CRASH_STACK, (i64)pageSize, 0, 0, 0, 0);
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

// ---- Stack overflow (D339) ----
//
// A panic with the stack trace, as the others. Windows calls a vectored handler
// on the stack that overflowed, in the room that SetThreadStackGuarantee keeps for
// each thread and each fiber; Linux sends SIGSEGV, which runs on a stack of its
// own for each thread.

static NORETURN void stackOverflow(void** frames, u32 count) {
    panicStart();
    putText("stack overflow\n");
    tracePrint(frames, count);
    fatalEnd(101);
}

#ifdef _WIN32

typedef struct {
    u32 code;  // the rest of EXCEPTION_RECORD is not read
} ExceptionRecord;

typedef struct {
    ExceptionRecord* record;
    void* context;
} ExceptionPointers;

static i32 WINAPI crashHandler(ExceptionPointers* exception) {
    if (exception->record->code != 0xC00000FD) return 0;  // not EXCEPTION_STACK_OVERFLOW: the others go on
    void* frames[TRACE_FRAMES];
    stackOverflow(frames, traceCapture(frames, TRACE_FRAMES));
}

static void crashStack(ThreadRecord* thread) {
    (void)thread;
    u32 size = CRASH_STACK;
    SetThreadStackGuarantee(&size);
}

static void crashStart(void) {
    AddVectoredExceptionHandler(1, (void*)crashHandler);
    crashStack(0);
}

#else

typedef struct {
    void* handler;
    u64 flags;
    void (*restorer)(void);
    u64 mask;
} SignalAction;

typedef struct {
    void* base;
    i32 flags;
    i32 unused;
    u64 size;
} SignalStack;

void cromo_sigreturn(void);  // mem_x64.S, mem_arm64.S

static u8 mainCrashStack[CRASH_STACK];

// The registers in the ucontext of the kernel.
#ifdef __aarch64__
#define CONTEXT_REGISTERS 184  // the regs of uc_mcontext: x0 to x30, sp, pc
#define CONTEXT_FP 29
#define CONTEXT_SP 31
#define CONTEXT_PC 32
#else
#define CONTEXT_REGISTERS 40   // the gregs of uc_mcontext
#define CONTEXT_FP 10          // rbp
#define CONTEXT_SP 15          // rsp
#define CONTEXT_PC 16          // rip
#endif

// An address near the stack pointer is the guard page of a stack: other faults,
// of C code, end the program as usual, faulting again without this handler.
static void crashHandler(i32 signal, void* info, void* context) {
    (void)signal;
    u64 address = *(u64*)((u8*)info + 16);  // si_addr
    u64* registers = (u64*)((u8*)context + CONTEXT_REGISTERS);
    u64 sp = registers[CONTEXT_SP];
    u64 distance = address > sp ? address - sp : sp - address;
    if (distance > ((u64)1 << 20)) {
        SignalAction usual = {0, 0, 0, 0};
        systemCall(SYS_rt_sigaction, 11, (i64)&usual, 0, 8, 0, 0);
        return;
    }
    void* frames[TRACE_FRAMES];
    frames[0] = (void*)(registers[CONTEXT_PC] + 1);  // where it stopped, as if a call returned there
#ifdef __aarch64__
    Registers r = {registers[CONTEXT_PC], sp, registers[CONTEXT_FP], registers[30]};  // x30 too
#else
    Registers r = {registers[CONTEXT_PC], sp, registers[CONTEXT_FP], 0};
#endif
    stackOverflow(frames, traceFrom(&r, 0, frames, 1, TRACE_FRAMES));
}

static void crashStack(ThreadRecord* thread) {
    SignalStack stack = {thread ? thread->stack + pageSize : mainCrashStack, 0, 0, CRASH_STACK};
    systemCall(SYS_sigaltstack, (i64)&stack, 0, 0, 0, 0, 0);
}

static void crashStart(void) {
    crashStack(0);
    // SA_SIGINFO, SA_ONSTACK and SA_RESTORER, without the C library to give it.
    SignalAction action = {(void*)crashHandler, 0x4 | 0x08000000 | 0x04000000, cromo_sigreturn, 0};
    systemCall(SYS_rt_sigaction, 11, (i64)&action, 0, 8, 0, 0);
}

#endif

#ifndef _WIN32

extern u64* cromo_initial_stack;

#ifdef __aarch64__
// Whether the processor has the atomics of ARMv8.1, which the functions of the
// atomics in mem_arm64.S use (D407). Until it is known they use the loops of
// ARMv8.0.
u8 cromo_lse_atomics;
#endif

// What the kernel says of the system, in the pairs that follow the environment on
// the initial stack: the size of a page, AT_PAGESZ, and on ARM64 HWCAP_ATOMICS in
// AT_HWCAP.
static void readSystem(void) {
    u64* entry = cromo_initial_stack + 2 + cromo_initial_stack[0];
    while (*entry) ++entry;
    for (++entry; entry[0]; entry += 2) {
        if (entry[0] == 6) pageSize = entry[1];
#ifdef __aarch64__
        if (entry[0] == 16) cromo_lse_atomics = (entry[1] >> 8) & 1;
#endif
    }
}

#endif

// Called first by cromo_start (codegen.cpp).
void cromo_runtime_start(void) {
#ifndef _WIN32
    readSystem();
#endif
    crashStart();
}

i64 cromo_arg_count(void);
i64 cromo_arg_length(i64 index);
const u8* cromo_arg_data(i64 index);

// D352: the executable of cromo test runs the test whose number is its first
// argument; -1 without it.
i64 cromo_test_index(void) {
    if (cromo_arg_count() < 2) return -1;
    const u8* digits = cromo_arg_data(1);
    i64 length = cromo_arg_length(1);
    i64 index = 0;
    for (i64 i = 0; i < length; ++i) {
        if (digits[i] < '0' || digits[i] > '9' || index > 100000000) return -1;
        index = index * 10 + (digits[i] - '0');
    }
    return length > 0 ? index : -1;
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
// with cromo_switch (mem_x64.S, mem_arm64.S). The stacks are kept for the next tasks.
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
    i32 spun;        // it looked at the queue, which was empty: now it sleeps
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
    i32 spinning;    // workers that look at the queue a little before they sleep
    i32 claimed;     // spinning workers counted on for a task just queued
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
#ifdef __aarch64__
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(block));
#else
    __asm__ volatile("mov %%fs:0, %0" : "=r"(block));
#endif
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

static void WINAPI fiberMain(void* parameter) {
    crashStack(0);  // for each fiber its own room
    stackMain(parameter);
}

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

void cromo_switch(void** save, void* next);  // mem_x64.S, mem_arm64.S
void cromo_task_entry(void);                 // cromo_task_run(rbx), cromo_task_run(x19)

NORETURN void cromo_task_run(Stack* stack) { stackMain(stack); }

// The stacks are cut from regions of addresses reserved without memory, 4 GB at
// a time: a new stack is one mprotect, which leaves its guard page at the bottom,
// and one given back keeps its addresses for the next. A mapping for each stack
// would take three calls, which wait for each other in a process with threads.
#define REGION_STACKS 4096

static u8* regionNext;
static u8* regionEnd;
static Stack* emptyStacks;  // without memory: they start again from the top

#ifdef __aarch64__

// What cromo_switch takes back the first time: x19 to x30, with the stack in x19
// and where it returns in x30, d8 to d15, and the control register of the
// floating point at its default value.
#define SWITCH_WORDS 22

static void startFrame(Stack* stack) {
    u64* top = (u64*)(stack->base + TASK_STACK) - SWITCH_WORDS;
    for (i32 i = 0; i < SWITCH_WORDS; ++i) top[i] = 0;
    top[0] = (u64)stack;              // x19
    top[11] = (u64)cromo_task_entry;  // x30
    stack->context = top;
}

#else

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

#endif

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
        if (systemCall(SYS_mprotect, (i64)(base + pageSize), (i64)(TASK_STACK - pageSize), 3, 0, 0, 0) < 0) taskFailure();
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
    systemCall(SYS_madvise, (i64)(stack->base + pageSize), (i64)(TASK_STACK - pageSize), 4, 0, 0, 0);
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
    if (scheduler.spinning > scheduler.claimed) {  // one that spins takes it, without a wake
        ++scheduler.claimed;
        return 1;
    }
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
    __atomic_add_fetch(&scheduler.queued, batch->count, __ATOMIC_RELEASE);
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

#define SPIN_ROUNDS 2000  // of pause: some tens of microseconds

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
            __atomic_sub_fetch(&scheduler.queued, 1, __ATOMIC_RELAXED);
            worker->spun = 0;
            return task;
        }
        // As Go: up to half of the workers look at the queue for a few tens of
        // microseconds before they sleep, so that a task that another wakes soon,
        // as through a channel, runs without the wake of a thread.
        if (!worker->spun && scheduler.spinning * 2 < scheduler.target) {
            ++scheduler.spinning;
            lockRelease(&scheduler.lock);
            for (u32 i = 0; i < SPIN_ROUNDS && __atomic_load_n(&scheduler.queued, __ATOMIC_ACQUIRE) == 0; ++i) {
                SPIN_HINT();
            }
            lockAcquire(&scheduler.lock);
            if (scheduler.claimed > 0) --scheduler.claimed;
            --scheduler.spinning;
            worker->spun = 1;  // once, then it sleeps if there is still nothing
            continue;
        }
        worker->spun = 0;
        ++scheduler.idle;
        while (scheduler.idleCalls == 0) conditionWait(&scheduler.work, &scheduler.lock);
        --scheduler.idleCalls;
    }
}

static void freeException(void** raised) {
    void** descriptor = (void**)raised[1];
    void (*drop)(void*) = (void (*)(void*))descriptor[0];
    if (drop) drop(raised[0]);
    cromo_exception_free(raised[0]);
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
        cromo_heap_threaded();
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
        SPIN_HINT();
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
    u32 receivers;  // waiting for a value
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
    // A receiver that waits takes it for sure: then the value is handed over, as in Go.
    int given = channel->receivers >= channel->count;
    channelChanged(&channel->values);
    lockRelease(&channel->lock);
    unpark(&channel->values, 0);
    if (channel->handover && !given) {  // until a receiver has it, or the channel is closed
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
    while (!channel->closed && channel->count == 0) {
        channel->receivers++;
        channelWait(channel, &channel->values);
        channel->receivers--;
    }
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
    cromo_heap_threaded();
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
    // The string comes back as a struct of C: through x8 on ARM64, as the method
    // expects its sret, and as the first argument on x86-64.
    Text (*message)(void*) = (Text (*)(void*))table[0];
    Text text = message(object);
    if (record->kind == KIND_THREAD) uncaught("uncaught exception in a thread: ", 32, text.data, (u64)text.length, object);
    uncaught("uncaught exception in a task: ", 30, text.data, (u64)text.length, object);
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

// Packed on x86-64 only, as in the headers of the kernel.
#ifdef __aarch64__
typedef struct {
#else
typedef struct __attribute__((packed)) {
#endif
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
    i64 count = systemCall(SYS_epoll_pwait, poller.epoll, (i64)events, 64, milliseconds, 0, 8);
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

// Where _start found the stack: the number of arguments, then their addresses
// (mem_x64.S, mem_arm64.S).
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
