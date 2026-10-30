// cromo for Linux is built on glibc 2.31, with the official LLVM for Linux, which is
// compiled for a newer glibc (D292): the two symbols that LLVM uses and glibc 2.31
// does not have. On a newer glibc these of cromo come first, and do the same.

#include <malloc.h>

#include <cstddef>

extern "C" {

// Whether the process has a single thread: 0, so that the code of libstdc++ inside
// LLVM always uses atomic operations, which is right also with one thread.
char __libc_single_threaded = 0;

// The statistics of malloc, with the fields of size_t of glibc 2.33.
struct CromoMallinfo2 {
    size_t arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost;
};

CromoMallinfo2 mallinfo2() {
    struct mallinfo info = mallinfo();
    return {static_cast<size_t>(info.arena),    static_cast<size_t>(info.ordblks),  static_cast<size_t>(info.smblks),
            static_cast<size_t>(info.hblks),    static_cast<size_t>(info.hblkhd),   static_cast<size_t>(info.usmblks),
            static_cast<size_t>(info.fsmblks),  static_cast<size_t>(info.uordblks), static_cast<size_t>(info.fordblks),
            static_cast<size_t>(info.keepcost)};
}

}  // extern "C"
