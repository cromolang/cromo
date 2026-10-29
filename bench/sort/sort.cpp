// Benchmark sort: indexing of vectors and fixed-size arrays (see sort.cro).
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

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

void insertionSort(std::vector<uint64_t>& v, int64_t low, int64_t high) {
    for (int64_t i = low + 1; i <= high; i++) {
        uint64_t x = v[i];
        int64_t j = i - 1;
        while (j >= low && v[j] > x) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
}

void quickSort(std::vector<uint64_t>& v, int64_t low, int64_t high) {
    int64_t lo = low;
    int64_t hi = high;
    while (hi - lo > 16) {
        uint64_t pivot = v[lo + (hi - lo) / 2];
        int64_t i = lo;
        int64_t j = hi;
        while (i <= j) {
            while (v[i] < pivot) {
                i++;
            }
            while (v[j] > pivot) {
                j--;
            }
            if (i <= j) {
                uint64_t kept = v[i];
                v[i] = v[j];
                v[j] = kept;
                i++;
                j--;
            }
        }
        if (j - lo < hi - i) {
            quickSort(v, lo, j);
            lo = i;
        } else {
            quickSort(v, i, hi);
            hi = j;
        }
    }
    insertionSort(v, lo, hi);
}

void radixSort(std::vector<uint64_t>& v) {
    std::vector<uint64_t> other = v;
    for (uint64_t shift = 0; shift < 64; shift += 8) {
        std::array<int64_t, 256> count{};
        for (uint64_t x : v) {
            count[(x >> shift) & 0xFF] += 1;
        }
        int64_t position = 0;
        for (int64_t& c : count) {
            int64_t next = position + c;
            c = position;
            position = next;
        }
        for (uint64_t x : v) {
            int64_t digit = (x >> shift) & 0xFF;
            other[count[digit]] = x;
            count[digit] += 1;
        }
        std::swap(v, other);
    }
}

int main() {
    Random random;
    random.state = 7;
    std::vector<uint64_t> values;
    for (int64_t i = 0; i < 5'000'000; i++) {
        values.push_back(random.next());
    }
    std::vector<uint64_t> quick = values;
    quickSort(quick, 0, int64_t(quick.size()) - 1);
    std::vector<uint64_t> radix = values;
    radixSort(radix);
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < quick.size(); i++) {
        if (quick[i] != radix[i] || (i > 0 && quick[i - 1] > quick[i])) {
            std::fputs("the two sorts differ\n", stderr);
            std::exit(1);
        }
        hash = (hash ^ quick[i]) * 1099511628211ull;
    }
    std::printf("first %llx last %llx hash %llx\n", (unsigned long long)quick[0], (unsigned long long)quick.back(), (unsigned long long)hash);
    return 0;
}
