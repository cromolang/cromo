// Benchmark collatz: integer arithmetic and loops (see collatz.cro).
#include <cstdint>
#include <cstdio>

int main() {
    int64_t limit = 5'000'000;
    int64_t total = 0;
    int64_t longest = 0;
    int64_t start = 0;
    for (int64_t n = 1; n < limit; n++) {
        int64_t x = n;
        int64_t steps = 0;
        while (x != 1) {
            if (x % 2 == 0) {
                x = x / 2;
            } else {
                x = 3 * x + 1;
            }
            steps++;
        }
        total += steps;
        if (steps > longest) {
            longest = steps;
            start = n;
        }
    }
    std::printf("steps %lld longest %lld from %lld\n", (long long)total, (long long)longest, (long long)start);
    return 0;
}
