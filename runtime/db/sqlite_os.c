// SQLite on the runtime of Cromo (D529): the pieces of the C library that it calls,
// and its files, for Windows, Linux and Android, without the C library. The files
// are read and written at a position, and locked by ranges of bytes as SQLite does
// on every system, so that the programs that open the same database, also those
// that are not written in Cromo, wait for each other. For the write-ahead log, the
// file of its index is memory that the programs share.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../third_party/sqlite/sqlite3.h"

void* cromo_alloc(int64_t size);
void* cromo_realloc(void* memory, int64_t size);
void cromo_free(void* memory);
int32_t cromo_crypto_random(uint8_t* out, int64_t size);  // runtime/tls/libc.c

// ---- The C library ----

void* malloc(size_t size) { return cromo_alloc((int64_t)(size ? size : 1)); }
void* realloc(void* memory, size_t size) { return cromo_realloc(memory, (int64_t)(size ? size : 1)); }
void free(void* memory) {
    if (memory) cromo_free(memory);
}

size_t strlen(const char* text) {
    size_t length = 0;
    while (text[length]) ++length;
    return length;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) {
        ++a;
        ++b;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, size_t count) {
    for (; count; --count, ++a, ++b) {
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

char* strrchr(const char* text, int value) {
    const char* found = NULL;
    for (;; ++text) {
        if (*text == (char)value) found = text;
        if (!*text) return (char*)found;
    }
}

size_t strspn(const char* text, const char* accept) {
    size_t length = 0;
    while (text[length] && strchr(accept, text[length])) ++length;
    return length;
}

size_t strcspn(const char* text, const char* reject) {
    size_t length = 0;
    while (text[length] && !strchr(reject, text[length])) ++length;
    return length;
}

void* memchr(const void* data, int value, size_t count) {
    const unsigned char* bytes = data;
    for (; count; --count, ++bytes) {
        if (*bytes == (unsigned char)value) return (void*)bytes;
    }
    return NULL;
}

// ---- The files ----

// The bytes that SQLite locks, past the first gigabyte of the file, where no page
// is: one who wants to write, one who will write, and the readers.
#define PENDING_BYTE 0x40000000
#define RESERVED_BYTE (PENDING_BYTE + 1)
#define SHARED_FIRST (PENDING_BYTE + 2)
#define SHARED_SIZE 510

// The index of the write-ahead log: the file <database>-shm, which every connection
// opens and maps in its memory, in pieces of 64 KB, two regions of SQLite each: a
// size that every system can map at any multiple of it. Each connection has its
// own handle, so its locks are its own, as those of the database.
#define SHM_REGION 32768
#define SHM_CHUNK 65536
#define SHM_BASE ((22 + SQLITE_SHM_NLOCK) * 4)  // where every SQLite puts the locks of the index
#define SHM_DMS (SHM_BASE + SQLITE_SHM_NLOCK)   // held by who uses the index: free, the index is stale

typedef struct {
    int64_t handle;
    int chunks;
    void** maps;
    int64_t* mappings;  // Windows: the mapping of each piece
} Shared;

typedef struct {
    sqlite3_file base;
    int64_t handle;
    int lock;  // SQLITE_LOCK_NONE, SHARED, RESERVED, PENDING or EXCLUSIVE
    const char* path;  // of the database, which SQLite keeps until the file is closed
    Shared* shared;
} File;

// What each system gives: -1 is an error, and for a lock 1 is taken by another.
static int64_t fileOpen(const char* path, int flags);
static void fileClose(int64_t handle);
static int64_t fileRead(int64_t handle, void* data, int64_t count, int64_t position);  // the bytes read
static int fileWrite(int64_t handle, const void* data, int64_t count, int64_t position);
static int fileTruncate(int64_t handle, int64_t size);
static int fileSync(int64_t handle);
static int64_t fileSize(int64_t handle);
static int rangeLock(int64_t handle, int64_t first, int64_t count, int exclusive);
static int rangeUnlock(int64_t handle, int64_t first, int64_t count);
static int rangeTaken(int64_t handle, int64_t first);  // by another, for writing
static int fileDelete(const char* path);
static void* chunkMap(Shared* shared, int chunk);  // NULL if it cannot
static void chunkUnmap(Shared* shared, int chunk);
static int fileAccess(const char* path, int writing);  // 1 if it is there
static int fullPath(const char* path, int size, char* out);
static void sleepFor(int microseconds);
static uint64_t threadId(void);  // of the thread that runs, never 0
static int64_t unixMilliseconds(void);

static void sharedClose(File* file, int delete);

static int ioClose(sqlite3_file* file) {
    sharedClose((File*)file, 0);
    fileClose(((File*)file)->handle);
    return SQLITE_OK;
}

static int ioRead(sqlite3_file* file, void* data, int count, sqlite3_int64 position) {
    int64_t done = fileRead(((File*)file)->handle, data, count, position);
    if (done < 0) return SQLITE_IOERR_READ;
    if (done < count) {  // past the end: the rest is zeros
        memset((char*)data + done, 0, (size_t)(count - done));
        return SQLITE_IOERR_SHORT_READ;
    }
    return SQLITE_OK;
}

static int ioWrite(sqlite3_file* file, const void* data, int count, sqlite3_int64 position) {
    return fileWrite(((File*)file)->handle, data, count, position) < 0 ? SQLITE_IOERR_WRITE : SQLITE_OK;
}

static int ioTruncate(sqlite3_file* file, sqlite3_int64 size) {
    return fileTruncate(((File*)file)->handle, size) < 0 ? SQLITE_IOERR_TRUNCATE : SQLITE_OK;
}

static int ioSync(sqlite3_file* file, int flags) {
    (void)flags;
    return fileSync(((File*)file)->handle) < 0 ? SQLITE_IOERR_FSYNC : SQLITE_OK;
}

static int ioFileSize(sqlite3_file* file, sqlite3_int64* size) {
    int64_t length = fileSize(((File*)file)->handle);
    if (length < 0) return SQLITE_IOERR_FSTAT;
    *size = length;
    return SQLITE_OK;
}

// The locks go up one step at a time, as SQLite asks: a reader holds the range of
// the readers; who will write holds the reserved byte too; who writes first takes
// the pending byte, which stops the new readers, then the whole range of the readers.
static int ioLock(sqlite3_file* base, int level) {
    File* file = (File*)base;
    if (level <= file->lock) return SQLITE_OK;
    int result;
    if (level == SQLITE_LOCK_SHARED) {
        result = rangeLock(file->handle, PENDING_BYTE, 1, 0);  // not while one waits to write
        if (result) return result > 0 ? SQLITE_BUSY : SQLITE_IOERR_LOCK;
        result = rangeLock(file->handle, SHARED_FIRST, SHARED_SIZE, 0);
        rangeUnlock(file->handle, PENDING_BYTE, 1);
        if (result) return result > 0 ? SQLITE_BUSY : SQLITE_IOERR_LOCK;
        file->lock = SQLITE_LOCK_SHARED;
        return SQLITE_OK;
    }
    if (level == SQLITE_LOCK_RESERVED) {
        result = rangeLock(file->handle, RESERVED_BYTE, 1, 1);
        if (result) return result > 0 ? SQLITE_BUSY : SQLITE_IOERR_LOCK;
        file->lock = SQLITE_LOCK_RESERVED;
        return SQLITE_OK;
    }
    if (file->lock < SQLITE_LOCK_PENDING) {
        result = rangeLock(file->handle, PENDING_BYTE, 1, 1);
        if (result) return result > 0 ? SQLITE_BUSY : SQLITE_IOERR_LOCK;
        file->lock = SQLITE_LOCK_PENDING;
    }
    if (level == SQLITE_LOCK_EXCLUSIVE) {
#ifdef _WIN32
        // Windows does not change a lock: the one of the readers is left first.
        rangeUnlock(file->handle, SHARED_FIRST, SHARED_SIZE);
        result = rangeLock(file->handle, SHARED_FIRST, SHARED_SIZE, 1);
        if (result) {
            rangeLock(file->handle, SHARED_FIRST, SHARED_SIZE, 0);
            return result > 0 ? SQLITE_BUSY : SQLITE_IOERR_LOCK;
        }
#else
        result = rangeLock(file->handle, SHARED_FIRST, SHARED_SIZE, 1);
        if (result) return result > 0 ? SQLITE_BUSY : SQLITE_IOERR_LOCK;
#endif
        file->lock = SQLITE_LOCK_EXCLUSIVE;
    }
    return SQLITE_OK;
}

static int ioUnlock(sqlite3_file* base, int level) {
    File* file = (File*)base;
    if (file->lock <= level) return SQLITE_OK;
    if (level == SQLITE_LOCK_SHARED) {
        if (file->lock == SQLITE_LOCK_EXCLUSIVE) {
#ifdef _WIN32
            rangeUnlock(file->handle, SHARED_FIRST, SHARED_SIZE);
#endif
            if (rangeLock(file->handle, SHARED_FIRST, SHARED_SIZE, 0)) return SQLITE_IOERR_UNLOCK;
        }
    } else {
        rangeUnlock(file->handle, SHARED_FIRST, SHARED_SIZE);
    }
    if (file->lock >= SQLITE_LOCK_RESERVED) rangeUnlock(file->handle, RESERVED_BYTE, 1);
    if (file->lock >= SQLITE_LOCK_PENDING) rangeUnlock(file->handle, PENDING_BYTE, 1);
    file->lock = level;
    return SQLITE_OK;
}

static int ioCheckReservedLock(sqlite3_file* base, int* reserved) {
    File* file = (File*)base;
    *reserved = file->lock >= SQLITE_LOCK_RESERVED || rangeTaken(file->handle, RESERVED_BYTE);
    return SQLITE_OK;
}

static int ioFileControl(sqlite3_file* file, int operation, void* argument) {
    (void)file;
    (void)operation;
    (void)argument;
    return SQLITE_NOTFOUND;
}

static int ioSectorSize(sqlite3_file* file) {
    (void)file;
    return 4096;
}

static int ioDeviceCharacteristics(sqlite3_file* file) {
    (void)file;
    return 0;
}

// ---- The shared memory of the write-ahead log ----

static void sharedClose(File* file, int delete) {
    Shared* shared = file->shared;
    if (!shared) return;
    for (int i = 0; i < shared->chunks; ++i) chunkUnmap(shared, i);
    fileClose(shared->handle);  // its locks go with it
    if (delete) {
        size_t length = strlen(file->path);
        char* name = cromo_alloc((int64_t)length + 5);
        memcpy(name, file->path, length);
        memcpy(name + length, "-shm", 5);
        fileDelete(name);
        cromo_free(name);
    }
    if (shared->maps) cromo_free(shared->maps);
    if (shared->mappings) cromo_free(shared->mappings);
    cromo_free(shared);
    file->shared = NULL;
}

// Opens the index. Who finds that nobody uses it empties it: it is what a program
// that ended badly left. Then it holds the byte that says that the index is used.
static int sharedOpen(File* file) {
    size_t length = strlen(file->path);
    char* name = cromo_alloc((int64_t)length + 5);
    memcpy(name, file->path, length);
    memcpy(name + length, "-shm", 5);
    int64_t handle = fileOpen(name, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    cromo_free(name);
    if (handle < 0) return SQLITE_CANTOPEN;
    int result = rangeLock(handle, SHM_DMS, 1, 1);
    if (result == 0) {
        if (fileTruncate(handle, 0) < 0) {
            fileClose(handle);
            return SQLITE_IOERR_SHMOPEN;
        }
#ifdef _WIN32
        rangeUnlock(handle, SHM_DMS, 1);  // Windows does not change a lock
#endif
    }
    if (result < 0 || rangeLock(handle, SHM_DMS, 1, 0)) {
        fileClose(handle);
        return result < 0 ? SQLITE_IOERR_SHMLOCK : SQLITE_BUSY;  // another is emptying it
    }
    Shared* shared = cromo_alloc(sizeof(Shared));
    shared->handle = handle;
    shared->chunks = 0;
    shared->maps = NULL;
    shared->mappings = NULL;
    file->shared = shared;
    return SQLITE_OK;
}

static int ioShmMap(sqlite3_file* base, int region, int size, int extend, void volatile** out) {
    File* file = (File*)base;
    *out = NULL;
    if (size != SHM_REGION) return SQLITE_IOERR_SHMMAP;
    if (!file->shared) {
        int result = sharedOpen(file);
        if (result) return result;
    }
    Shared* shared = file->shared;
    int chunk = region / (SHM_CHUNK / SHM_REGION);
    int64_t length = fileSize(shared->handle);
    if (length < 0) return SQLITE_IOERR_SHMSIZE;
    if (length < (int64_t)(region + 1) * SHM_REGION) {
        if (!extend) return SQLITE_OK;  // not there yet: no memory
        if (fileTruncate(shared->handle, (int64_t)(chunk + 1) * SHM_CHUNK) < 0) return SQLITE_IOERR_SHMSIZE;
    }
    if (chunk >= shared->chunks) {
        int64_t bytes = (int64_t)(chunk + 1) * (int64_t)sizeof(void*);
        shared->maps = shared->maps ? cromo_realloc(shared->maps, bytes) : cromo_alloc(bytes);
        shared->mappings = shared->mappings ? cromo_realloc(shared->mappings, bytes) : cromo_alloc(bytes);
        while (shared->chunks <= chunk) {
            shared->maps[shared->chunks] = NULL;
            shared->mappings[shared->chunks] = 0;
            ++shared->chunks;
        }
    }
    if (!shared->maps[chunk]) {
        shared->maps[chunk] = chunkMap(shared, chunk);
        if (!shared->maps[chunk]) return SQLITE_IOERR_SHMMAP;
    }
    *out = (char*)shared->maps[chunk] + (region % (SHM_CHUNK / SHM_REGION)) * SHM_REGION;
    return SQLITE_OK;
}

static int ioShmLock(sqlite3_file* base, int first, int count, int flags) {
    File* file = (File*)base;
    if (!file->shared) return SQLITE_IOERR_SHMLOCK;
    int64_t handle = file->shared->handle;
    if (flags & SQLITE_SHM_UNLOCK) {
        rangeUnlock(handle, SHM_BASE + first, count);
        return SQLITE_OK;
    }
    int result = rangeLock(handle, SHM_BASE + first, count, (flags & SQLITE_SHM_EXCLUSIVE) != 0);
    return result == 0 ? SQLITE_OK : result > 0 ? SQLITE_BUSY : SQLITE_IOERR_SHMLOCK;
}

static void ioShmBarrier(sqlite3_file* file) {
    (void)file;
    __sync_synchronize();
}

static int ioShmUnmap(sqlite3_file* file, int delete) {
    sharedClose((File*)file, delete);
    return SQLITE_OK;
}

static const sqlite3_io_methods ioMethods = {
    2, ioClose, ioRead, ioWrite, ioTruncate, ioSync, ioFileSize, ioLock, ioUnlock, ioCheckReservedLock,
    ioFileControl, ioSectorSize, ioDeviceCharacteristics, ioShmMap, ioShmLock, ioShmBarrier, ioShmUnmap, 0, 0,
};

static int vfsOpen(sqlite3_vfs* vfs, sqlite3_filename name, sqlite3_file* base, int flags, int* outFlags) {
    (void)vfs;
    File* file = (File*)base;
    file->base.pMethods = NULL;  // SQLite does not close a file that did not open
    if (!name) return SQLITE_CANTOPEN;  // a temporary file: they are in memory (SQLITE_TEMP_STORE)
    int64_t handle = fileOpen(name, flags);
    if (handle < 0 && (flags & SQLITE_OPEN_READWRITE) && !(flags & SQLITE_OPEN_EXCLUSIVE)) {
        flags = (flags & ~(SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE)) | SQLITE_OPEN_READONLY;
        handle = fileOpen(name, flags);  // a database that can only be read
    }
    if (handle < 0) return SQLITE_CANTOPEN;
    file->handle = handle;
    file->lock = SQLITE_LOCK_NONE;
    file->path = name;
    file->shared = NULL;
    file->base.pMethods = &ioMethods;
    if (outFlags) *outFlags = flags;
    return SQLITE_OK;
}

static int vfsDelete(sqlite3_vfs* vfs, const char* name, int sync) {
    (void)vfs;
    (void)sync;
    return fileDelete(name) < 0 ? SQLITE_IOERR_DELETE : SQLITE_OK;
}

static int vfsAccess(sqlite3_vfs* vfs, const char* name, int flags, int* result) {
    (void)vfs;
    *result = fileAccess(name, flags == SQLITE_ACCESS_READWRITE);
    return SQLITE_OK;
}

static int vfsFullPathname(sqlite3_vfs* vfs, const char* name, int size, char* out) {
    (void)vfs;
    return fullPath(name, size, out) < 0 ? SQLITE_CANTOPEN : SQLITE_OK;
}

static int vfsRandomness(sqlite3_vfs* vfs, int size, char* out) {
    (void)vfs;
    if (cromo_crypto_random((uint8_t*)out, size) != 0) memset(out, 0, (size_t)size);
    return size;
}

static int vfsSleep(sqlite3_vfs* vfs, int microseconds) {
    (void)vfs;
    sleepFor(microseconds);
    return microseconds;
}

// The time as SQLite counts it: the milliseconds since noon of 24 November 4714 BC.
static int vfsCurrentTimeInt64(sqlite3_vfs* vfs, sqlite3_int64* now) {
    (void)vfs;
    *now = unixMilliseconds() + 210866760000000LL;
    return SQLITE_OK;
}

static int vfsCurrentTime(sqlite3_vfs* vfs, double* now) {
    sqlite3_int64 milliseconds;
    vfsCurrentTimeInt64(vfs, &milliseconds);
    *now = (double)milliseconds / 86400000.0;
    return SQLITE_OK;
}

static int vfsGetLastError(sqlite3_vfs* vfs, int size, char* out) {
    (void)vfs;
    (void)size;
    (void)out;
    return 0;
}

static sqlite3_vfs cromoVfs = {
    2, sizeof(File), 1024, NULL, "cromo", NULL, vfsOpen, vfsDelete, vfsAccess, vfsFullPathname, 0, 0, 0, 0,
    vfsRandomness, vfsSleep, vfsCurrentTime, vfsGetLastError, vfsCurrentTimeInt64, 0, 0, 0,
};

int sqlite3_os_init(void) { return sqlite3_vfs_register(&cromoVfs, 1); }
int sqlite3_os_end(void) { return SQLITE_OK; }

// ---- The mutexes of SQLite: the locks of the runtime ----
//
// SQLite protects what all its connections share, as its memory and its cache of
// pages, with mutexes of its own, held for an instant: so the threads work in it
// together, each on a connection of its own. Who finds one taken waits as for a
// Mutex of Cromo: a task parks. Only the mutex of the start of SQLite is entered
// again by who has it, and knows its thread.

void cromo_mutex_lock(void* state);
void cromo_mutex_unlock(void* state);

struct sqlite3_mutex {
    uint64_t state;  // the lock of the runtime
    uint64_t owner;  // the thread inside a recursive one
    int32_t recursive;
    int32_t depth;
};

static sqlite3_mutex fixedMutexes[16];  // the ones of SQLite itself, by their number

static int mutexInit(void) { return SQLITE_OK; }
static int mutexEnd(void) { return SQLITE_OK; }

static sqlite3_mutex* mutexAlloc(int kind) {
    if (kind > SQLITE_MUTEX_RECURSIVE) return kind - 2 < 16 ? &fixedMutexes[kind - 2] : NULL;
    sqlite3_mutex* mutex = malloc(sizeof(sqlite3_mutex));
    if (!mutex) return NULL;
    mutex->state = 0;
    mutex->owner = 0;
    mutex->recursive = kind == SQLITE_MUTEX_RECURSIVE;
    mutex->depth = 0;
    return mutex;
}

static void mutexFree(sqlite3_mutex* mutex) {
    if (mutex < fixedMutexes || mutex >= fixedMutexes + 16) free(mutex);
}

static void mutexEnter(sqlite3_mutex* mutex) {
    if (!mutex->recursive) {
        cromo_mutex_lock(&mutex->state);
        return;
    }
    uint64_t self = threadId();
    if (__atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) == self) {
        mutex->depth++;
        return;
    }
    cromo_mutex_lock(&mutex->state);
    __atomic_store_n(&mutex->owner, self, __ATOMIC_RELAXED);
    mutex->depth = 1;
}

static int mutexTry(sqlite3_mutex* mutex) {
    uint64_t self = mutex->recursive ? threadId() : 0;
    if (mutex->recursive && __atomic_load_n(&mutex->owner, __ATOMIC_RELAXED) == self) {
        mutex->depth++;
        return SQLITE_OK;
    }
    uint32_t free = 0;
    if (!__atomic_compare_exchange_n((uint32_t*)&mutex->state, &free, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        return SQLITE_BUSY;
    }
    if (mutex->recursive) {
        __atomic_store_n(&mutex->owner, self, __ATOMIC_RELAXED);
        mutex->depth = 1;
    }
    return SQLITE_OK;
}

static void mutexLeave(sqlite3_mutex* mutex) {
    if (mutex->recursive) {
        if (--mutex->depth > 0) return;
        __atomic_store_n(&mutex->owner, 0, __ATOMIC_RELAXED);
    }
    cromo_mutex_unlock(&mutex->state);
}

static const sqlite3_mutex_methods cromoMutexes = {mutexInit, mutexEnd, mutexAlloc, mutexFree, mutexEnter,
                                                   mutexTry,  mutexLeave, NULL,     NULL};

// SQLite starts once, with these mutexes: std.db calls it before it opens a database.
static uint64_t setupLock;
static int setupState;  // 0 not yet, 1 ready, -1 failed

int32_t cromo_db_setup(void) {
    int state = __atomic_load_n(&setupState, __ATOMIC_ACQUIRE);
    if (state) return state > 0;
    cromo_mutex_lock(&setupLock);
    if (!setupState) {
        int code = sqlite3_config(SQLITE_CONFIG_MUTEX, &cromoMutexes);
        if (code == SQLITE_OK) code = sqlite3_initialize();
        __atomic_store_n(&setupState, code == SQLITE_OK ? 1 : -1, __ATOMIC_RELEASE);
    }
    cromo_mutex_unlock(&setupLock);
    return __atomic_load_n(&setupState, __ATOMIC_ACQUIRE) > 0;
}

#ifdef _WIN32

// ---- Windows ----

#define WINAPI __stdcall
typedef struct {
    uint64_t internal;
    uint64_t internalHigh;
    uint32_t offset;
    uint32_t offsetHigh;
    void* event;
} Overlapped;

__declspec(dllimport) int64_t WINAPI CreateFileW(const uint16_t* name, uint32_t access, uint32_t share, void* security,
                                                 uint32_t creation, uint32_t flags, void* pattern);
__declspec(dllimport) int32_t WINAPI ReadFile(int64_t file, void* data, uint32_t size, uint32_t* done, Overlapped* at);
__declspec(dllimport) int32_t WINAPI WriteFile(int64_t file, const void* data, uint32_t size, uint32_t* done, Overlapped* at);
__declspec(dllimport) int32_t WINAPI CloseHandle(int64_t handle);
__declspec(dllimport) int32_t WINAPI GetFileSizeEx(int64_t file, int64_t* size);
__declspec(dllimport) int32_t WINAPI SetFilePointerEx(int64_t file, int64_t distance, int64_t* position, uint32_t method);
__declspec(dllimport) int32_t WINAPI SetEndOfFile(int64_t file);
__declspec(dllimport) int32_t WINAPI FlushFileBuffers(int64_t file);
__declspec(dllimport) int32_t WINAPI LockFileEx(int64_t file, uint32_t flags, uint32_t reserved, uint32_t low, uint32_t high,
                                                Overlapped* at);
__declspec(dllimport) int32_t WINAPI UnlockFileEx(int64_t file, uint32_t reserved, uint32_t low, uint32_t high, Overlapped* at);
__declspec(dllimport) int32_t WINAPI DeleteFileW(const uint16_t* name);
__declspec(dllimport) uint32_t WINAPI GetFileAttributesW(const uint16_t* name);
__declspec(dllimport) uint32_t WINAPI GetFullPathNameW(const uint16_t* name, uint32_t size, uint16_t* out, uint16_t** part);
__declspec(dllimport) int32_t WINAPI MultiByteToWideChar(uint32_t page, uint32_t flags, const char* text, int32_t size,
                                                         uint16_t* wide, int32_t wideSize);
__declspec(dllimport) int32_t WINAPI WideCharToMultiByte(uint32_t page, uint32_t flags, const uint16_t* wide, int32_t size,
                                                         char* text, int32_t textSize, const char* fallback, int32_t* used);
__declspec(dllimport) uint32_t WINAPI GetLastError(void);
__declspec(dllimport) int64_t WINAPI CreateFileMappingW(int64_t file, void* security, uint32_t protect, uint32_t high,
                                                        uint32_t low, const uint16_t* name);
__declspec(dllimport) void* WINAPI MapViewOfFile(int64_t mapping, uint32_t access, uint32_t high, uint32_t low, size_t size);
__declspec(dllimport) int32_t WINAPI UnmapViewOfFile(void* view);
__declspec(dllimport) void WINAPI Sleep(uint32_t milliseconds);
__declspec(dllimport) void WINAPI GetSystemTimeAsFileTime(uint64_t* time);

#define CP_UTF8 65001
#define ERROR_HANDLE_EOF 38
#define ERROR_LOCK_VIOLATION 33
#define ERROR_IO_PENDING 997

// The path in UTF-16, in memory to be freed, or NULL.
static uint16_t* widePath(const char* path) {
    int32_t size = MultiByteToWideChar(CP_UTF8, 0, path, -1, NULL, 0);
    if (size <= 0) return NULL;
    uint16_t* wide = cromo_alloc((int64_t)size * 2);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide, size);
    return wide;
}

static int64_t fileOpen(const char* path, int flags) {
    uint16_t* wide = widePath(path);
    if (!wide) return -1;
    uint32_t access = flags & SQLITE_OPEN_READWRITE ? 0xC0000000 : 0x80000000;  // GENERIC_READ | GENERIC_WRITE
    uint32_t creation = 3;  // OPEN_EXISTING
    if (flags & SQLITE_OPEN_EXCLUSIVE) {
        creation = 1;  // CREATE_NEW
    } else if (flags & SQLITE_OPEN_CREATE) {
        creation = 4;  // OPEN_ALWAYS
    }
    int64_t handle = CreateFileW(wide, access, 3, NULL, creation, 0x80, NULL);  // shared for reading and writing
    cromo_free(wide);
    return handle;  // INVALID_HANDLE_VALUE is -1
}

static void fileClose(int64_t handle) { CloseHandle(handle); }

static int64_t fileRead(int64_t handle, void* data, int64_t count, int64_t position) {
    int64_t total = 0;
    while (total < count) {
        Overlapped at = {0, 0, (uint32_t)(position + total), (uint32_t)((position + total) >> 32), NULL};
        uint32_t done = 0;
        if (!ReadFile(handle, (char*)data + total, (uint32_t)(count - total), &done, &at)) {
            return GetLastError() == ERROR_HANDLE_EOF ? total : -1;
        }
        if (done == 0) break;
        total += done;
    }
    return total;
}

static int fileWrite(int64_t handle, const void* data, int64_t count, int64_t position) {
    int64_t total = 0;
    while (total < count) {
        Overlapped at = {0, 0, (uint32_t)(position + total), (uint32_t)((position + total) >> 32), NULL};
        uint32_t done = 0;
        if (!WriteFile(handle, (const char*)data + total, (uint32_t)(count - total), &done, &at) || done == 0) return -1;
        total += done;
    }
    return 0;
}

static int fileTruncate(int64_t handle, int64_t size) {
    return SetFilePointerEx(handle, size, NULL, 0) && SetEndOfFile(handle) ? 0 : -1;
}

static int fileSync(int64_t handle) { return FlushFileBuffers(handle) ? 0 : -1; }

static int64_t fileSize(int64_t handle) {
    int64_t size = 0;
    return GetFileSizeEx(handle, &size) ? size : -1;
}

static int rangeLock(int64_t handle, int64_t first, int64_t count, int exclusive) {
    Overlapped at = {0, 0, (uint32_t)first, (uint32_t)(first >> 32), NULL};
    uint32_t flags = 1 | (exclusive ? 2 : 0);  // LOCKFILE_FAIL_IMMEDIATELY | LOCKFILE_EXCLUSIVE_LOCK
    if (LockFileEx(handle, flags, 0, (uint32_t)count, 0, &at)) return 0;
    uint32_t reason = GetLastError();
    return reason == ERROR_LOCK_VIOLATION || reason == ERROR_IO_PENDING ? 1 : -1;
}

static int rangeUnlock(int64_t handle, int64_t first, int64_t count) {
    Overlapped at = {0, 0, (uint32_t)first, (uint32_t)(first >> 32), NULL};
    return UnlockFileEx(handle, 0, (uint32_t)count, 0, &at) ? 0 : -1;
}

static int rangeTaken(int64_t handle, int64_t first) {
    if (rangeLock(handle, first, 1, 1)) return 1;
    rangeUnlock(handle, first, 1);
    return 0;
}

static int fileDelete(const char* path) {
    uint16_t* wide = widePath(path);
    if (!wide) return -1;
    int done = DeleteFileW(wide) || GetFileAttributesW(wide) == 0xFFFFFFFF;  // gone already
    cromo_free(wide);
    return done ? 0 : -1;
}

static void* chunkMap(Shared* shared, int chunk) {
    int64_t end = (int64_t)(chunk + 1) * SHM_CHUNK;
    int64_t mapping = CreateFileMappingW(shared->handle, NULL, 4, (uint32_t)(end >> 32), (uint32_t)end, NULL);  // PAGE_READWRITE
    if (!mapping) return NULL;
    int64_t start = (int64_t)chunk * SHM_CHUNK;
    void* view = MapViewOfFile(mapping, 6, (uint32_t)(start >> 32), (uint32_t)start, SHM_CHUNK);  // FILE_MAP_READ | WRITE
    if (!view) {
        CloseHandle(mapping);
        return NULL;
    }
    shared->mappings[chunk] = mapping;
    return view;
}

static void chunkUnmap(Shared* shared, int chunk) {
    if (!shared->maps[chunk]) return;
    UnmapViewOfFile(shared->maps[chunk]);
    CloseHandle(shared->mappings[chunk]);
    shared->maps[chunk] = NULL;
}

static int fileAccess(const char* path, int writing) {
    uint16_t* wide = widePath(path);
    if (!wide) return 0;
    uint32_t attributes = GetFileAttributesW(wide);
    cromo_free(wide);
    if (attributes == 0xFFFFFFFF) return 0;
    return !writing || !(attributes & 1);  // FILE_ATTRIBUTE_READONLY
}

static int fullPath(const char* path, int size, char* out) {
    uint16_t* wide = widePath(path);
    if (!wide) return -1;
    uint16_t whole[1024];
    uint32_t length = GetFullPathNameW(wide, 1024, whole, NULL);
    cromo_free(wide);
    if (length == 0 || length >= 1024) return -1;
    return WideCharToMultiByte(CP_UTF8, 0, whole, -1, out, size, NULL, NULL) > 0 ? 0 : -1;
}

static void sleepFor(int microseconds) { Sleep((uint32_t)((microseconds + 999) / 1000)); }

__declspec(dllimport) uint32_t WINAPI GetCurrentThreadId(void);

static uint64_t threadId(void) { return GetCurrentThreadId(); }

static int64_t unixMilliseconds(void) {
    uint64_t ticks;  // of 100 ns, since 1601
    GetSystemTimeAsFileTime(&ticks);
    return (int64_t)(ticks / 10000) - 11644473600000LL;
}

#else

// ---- Linux and Android ----

int64_t cromo_system_call(int64_t number, int64_t a, int64_t b, int64_t c, int64_t d, int64_t e, int64_t f);

#ifdef __aarch64__
#define SYS_getcwd 17
#define SYS_gettid 178
#define SYS_fcntl 25
#define SYS_unlinkat 35
#define SYS_ftruncate 46
#define SYS_faccessat 48
#define SYS_openat 56
#define SYS_close 57
#define SYS_lseek 62
#define SYS_pread64 67
#define SYS_pwrite64 68
#define SYS_fsync 82
#define SYS_nanosleep 101
#define SYS_clock_gettime 113
#define SYS_munmap 215
#define SYS_mmap 222
#else
#define SYS_close 3
#define SYS_lseek 8
#define SYS_pread64 17
#define SYS_mmap 9
#define SYS_munmap 11
#define SYS_pwrite64 18
#define SYS_nanosleep 35
#define SYS_fcntl 72
#define SYS_fsync 74
#define SYS_ftruncate 77
#define SYS_getcwd 79
#define SYS_gettid 186
#define SYS_clock_gettime 228
#define SYS_openat 257
#define SYS_unlinkat 263
#define SYS_faccessat 269
#endif

#define AT_FDCWD (-100)
#define EINTR 4
#define ENOENT 2
#define EAGAIN 11
#define EACCES 13
#define F_OFD_GETLK 36
#define F_OFD_SETLK 37
#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2

// struct flock of 64 bits. A lock of the open file, not of the process (F_OFD_SETLK,
// Linux 3.15): two connections of one program wait for each other as two programs,
// and closing another descriptor of the same file takes no lock away.
typedef struct {
    int16_t type;
    int16_t whence;
    int32_t padding;
    int64_t start;
    int64_t length;
    int32_t process;  // 0 for these locks
    int32_t padding2;
} FileLock;

static int64_t call(int64_t number, int64_t a, int64_t b, int64_t c, int64_t d) {
    for (;;) {
        int64_t result = cromo_system_call(number, a, b, c, d, 0, 0);
        if (result != -EINTR) return result;
    }
}

static int64_t fileOpen(const char* path, int flags) {
    int64_t mode = 0x80000;  // O_CLOEXEC | O_RDONLY
    if (flags & SQLITE_OPEN_READWRITE) mode |= 2;  // O_RDWR
    if (flags & SQLITE_OPEN_CREATE) mode |= 0x40;  // O_CREAT
    if (flags & SQLITE_OPEN_EXCLUSIVE) mode |= 0x80;  // O_EXCL
    int64_t handle = call(SYS_openat, AT_FDCWD, (int64_t)path, mode, 420);  // 0644
    return handle < 0 ? -1 : handle;
}

static void fileClose(int64_t handle) { cromo_system_call(SYS_close, handle, 0, 0, 0, 0, 0); }

static int64_t fileRead(int64_t handle, void* data, int64_t count, int64_t position) {
    int64_t total = 0;
    while (total < count) {
        int64_t done = call(SYS_pread64, handle, (int64_t)data + total, count - total, position + total);
        if (done < 0) return -1;
        if (done == 0) break;
        total += done;
    }
    return total;
}

static int fileWrite(int64_t handle, const void* data, int64_t count, int64_t position) {
    int64_t total = 0;
    while (total < count) {
        int64_t done = call(SYS_pwrite64, handle, (int64_t)data + total, count - total, position + total);
        if (done <= 0) return -1;
        total += done;
    }
    return 0;
}

static int fileTruncate(int64_t handle, int64_t size) { return call(SYS_ftruncate, handle, size, 0, 0) < 0 ? -1 : 0; }
static int fileSync(int64_t handle) { return call(SYS_fsync, handle, 0, 0, 0) < 0 ? -1 : 0; }

static int64_t fileSize(int64_t handle) {
    int64_t size = call(SYS_lseek, handle, 0, 2, 0);  // SEEK_END: the reads and the writes have their own position
    return size < 0 ? -1 : size;
}

static int rangeSet(int64_t handle, int64_t first, int64_t count, int type) {
    FileLock lock = {(int16_t)type, 0, 0, first, count, 0, 0};
    int64_t result = call(SYS_fcntl, handle, F_OFD_SETLK, (int64_t)&lock, 0);
    if (result == 0) return 0;
    return result == -EAGAIN || result == -EACCES ? 1 : -1;
}

static int rangeLock(int64_t handle, int64_t first, int64_t count, int exclusive) {
    return rangeSet(handle, first, count, exclusive ? F_WRLCK : F_RDLCK);
}

static int rangeUnlock(int64_t handle, int64_t first, int64_t count) { return rangeSet(handle, first, count, F_UNLCK); }

static int rangeTaken(int64_t handle, int64_t first) {
    FileLock lock = {F_WRLCK, 0, 0, first, 1, 0, 0};
    if (call(SYS_fcntl, handle, F_OFD_GETLK, (int64_t)&lock, 0) < 0) return 0;
    return lock.type != F_UNLCK;
}

static int fileDelete(const char* path) {
    int64_t result = call(SYS_unlinkat, AT_FDCWD, (int64_t)path, 0, 0);
    return result == 0 || result == -ENOENT ? 0 : -1;
}

static void* chunkMap(Shared* shared, int chunk) {
    // PROT_READ | PROT_WRITE, MAP_SHARED
    int64_t memory = cromo_system_call(SYS_mmap, 0, SHM_CHUNK, 3, 1, shared->handle, (int64_t)chunk * SHM_CHUNK);
    return memory < 0 && memory > -4096 ? NULL : (void*)memory;
}

static void chunkUnmap(Shared* shared, int chunk) {
    if (!shared->maps[chunk]) return;
    cromo_system_call(SYS_munmap, (int64_t)shared->maps[chunk], SHM_CHUNK, 0, 0, 0, 0);
    shared->maps[chunk] = NULL;
}

static int fileAccess(const char* path, int writing) {
    return call(SYS_faccessat, AT_FDCWD, (int64_t)path, writing ? 6 : 0, 0) == 0;  // R_OK | W_OK, or F_OK
}

static int fullPath(const char* path, int size, char* out) {
    size_t used = 0;
    if (path[0] != '/') {
        if (call(SYS_getcwd, (int64_t)out, size, 0, 0) < 0) return -1;
        used = strlen(out);
        if (used == 0 || out[used - 1] != '/') out[used++] = '/';
    }
    size_t length = strlen(path);
    if (used + length + 1 > (size_t)size) return -1;
    memcpy(out + used, path, length + 1);
    return 0;
}

static void sleepFor(int microseconds) {
    int64_t time[2] = {microseconds / 1000000, (int64_t)(microseconds % 1000000) * 1000};
    cromo_system_call(SYS_nanosleep, (int64_t)time, 0, 0, 0, 0, 0);
}

static int64_t unixMilliseconds(void) {
    int64_t now[2] = {0, 0};
    cromo_system_call(SYS_clock_gettime, 0, (int64_t)now, 0, 0, 0, 0);  // CLOCK_REALTIME
    return now[0] * 1000 + now[1] / 1000000;
}

static uint64_t threadId(void) { return (uint64_t)cromo_system_call(SYS_gettid, 0, 0, 0, 0, 0, 0); }

#endif
