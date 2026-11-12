// How SQLite is compiled for std.db (D529): for a system that it does not know, so
// without the C library. This header goes in front of sqlite3.c and of sqlite_os.c.
#pragma once

#define SQLITE_OS_OTHER 1            // the files and the locks are those of sqlite_os.c
#define SQLITE_THREADSAFE 2          // the threads are in SQLite together, each with a connection of its own:
                                     // its mutexes are those of sqlite_os.c, and std.db has a lock for each connection
#define SQLITE_OMIT_LOAD_EXTENSION 1
#define SQLITE_OMIT_LOCALTIME 1      // no time zones without the C library: the times are UTC
#define SQLITE_OMIT_SHARED_CACHE 1
#define SQLITE_OMIT_DEPRECATED 1
#define SQLITE_DQS 0                 // "text" is a name, never a string
#define SQLITE_DEFAULT_MEMSTATUS 0
#define SQLITE_TEMP_STORE 3          // the temporary tables in memory: no temporary files
#define SQLITE_MAX_MMAP_SIZE 0
#define SQLITE_WITHOUT_MSIZE 1       // the size of a block is kept in front of it
#define LONGDOUBLE_TYPE double       // no 128-bit floats of software on ARM64
#define NDEBUG 1

// On Windows clang says that it is the compiler of Microsoft too: SQLite then takes
// the headers of its intrinsics, which need the C library. It is compiled as with GCC.
#undef _MSC_VER

// The pieces of the C library that SQLite calls, under names of their own, so that
// they never meet those of glibc in a program that has it (D428).
#define malloc cromo_db_malloc
#define realloc cromo_db_realloc
#define free cromo_db_free
#define strlen cromo_db_strlen
#define strcmp cromo_db_strcmp
#define strncmp cromo_db_strncmp
#define strspn cromo_db_strspn
#define strcspn cromo_db_strcspn
#define strchr cromo_db_strchr
#define strrchr cromo_db_strrchr
#define memchr cromo_db_memchr
#define fabs __builtin_fabs
