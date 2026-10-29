// Benchmark fib: recursive calls (see fib.cro).
#include <cstdint>
#include <cstdio>

int64_t fib(int64_t n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

int main() {
    std::printf("fib(42) = %lld\n", (long long)fib(42));
    return 0;
}
