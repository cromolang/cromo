// Benchmark wordcount: a hash map with string keys (see wordcount.cro).
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

// The generator SplitMix64.
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

uint64_t fnv(const std::string& text) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < text.size(); i++) {
        hash ^= (unsigned char)text[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

int main() {
    Random random;
    random.state = 42;
    std::vector<std::string> vocabulary;
    for (int64_t i = 0; i < 100'000; i++) {
        vocabulary.push_back(std::format("{:x}", random.next()));
    }
    std::unordered_map<std::string, int64_t> counts;
    uint64_t size = vocabulary.size();
    for (int64_t i = 0; i < 5'000'000; i++) {
        counts[vocabulary[random.next() % size]] += 1;
    }
    int64_t squares = 0;
    uint64_t hashes = 0;
    for (const auto& [word, count] : counts) {
        squares += count * count;
        hashes += fnv(word) * uint64_t(count);
    }
    std::fputs(std::format("distinct {} squares {} hashes {:x}\n", counts.size(), squares, hashes).c_str(), stdout);
    return 0;
}
