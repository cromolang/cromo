// Benchmark bits: shifts, masks and the bitwise operators (see bits.cro).
#include <array>
#include <cstdint>
#include <cstdio>

struct Random {
    uint64_t state = 0;

    uint64_t next() {
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
};

uint64_t popCount(uint64_t value) {
    uint64_t x = value - ((value >> 1) & 0x5555555555555555ull);
    x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0Full;
    return (x * 0x0101010101010101ull) >> 56;
}

uint64_t reverse(uint64_t value) {
    uint64_t x = ((value >> 1) & 0x5555555555555555ull) | ((value & 0x5555555555555555ull) << 1);
    x = ((x >> 2) & 0x3333333333333333ull) | ((x & 0x3333333333333333ull) << 2);
    x = ((x >> 4) & 0x0F0F0F0F0F0F0F0Full) | ((x & 0x0F0F0F0F0F0F0F0Full) << 4);
    x = ((x >> 8) & 0x00FF00FF00FF00FFull) | ((x & 0x00FF00FF00FF00FFull) << 8);
    x = ((x >> 16) & 0x0000FFFF0000FFFFull) | ((x & 0x0000FFFF0000FFFFull) << 16);
    return (x >> 32) | (x << 32);
}

uint64_t rotateLeft(uint64_t x, uint64_t count) {
    return (x << count) | (x >> (64 - count));
}

int main() {
    uint64_t deBruijn = 0x03F79D71B4CB0A89ull;
    std::array<int64_t, 64> table{};
    for (uint64_t i = 0; i < 64; i++) {
        table[(deBruijn << i) >> 58] = int64_t(i);
    }
    Random random;
    random.state = 1;
    uint64_t ones = 0;
    uint64_t zeros = 0;
    uint64_t mixed = 0;
    for (int64_t i = 0; i < 100'000'000; i++) {
        uint64_t x = random.next();
        ones += popCount(x);
        if (x != 0) {
            uint64_t lowest = x & (~x + 1);
            zeros += uint64_t(table[(lowest * deBruijn) >> 58]);
        }
        mixed ^= rotateLeft(reverse(x), 1 + x % 63);
        mixed += x ^ (x >> 1);
    }
    std::printf("ones %llu zeros %llu mixed %llx\n", (unsigned long long)ones, (unsigned long long)zeros, (unsigned long long)mixed);
    return 0;
}
