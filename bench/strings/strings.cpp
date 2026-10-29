// Benchmark strings: format, appending and the bytes of a string (see strings.cro).
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>

uint64_t fnv(uint64_t seed, const std::string& text) {
    uint64_t hash = seed;
    for (size_t i = 0; i < text.size(); i++) {
        hash ^= (unsigned char)text[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

int main() {
    uint64_t hash = 14695981039346656037ull;
    int64_t bytes = 0;
    std::string text;
    for (int64_t i = 0; i < 2'000'000; i++) {
        std::string line = std::format("{}:{:x}:{:.3f};", i, i * 7919, double(i) / 7.0);
        text += line;
        if (text.size() >= 4096) {
            hash = fnv(hash, text);
            bytes += text.size();
            text.clear();
        }
    }
    hash = fnv(hash, text);
    bytes += text.size();
    std::fputs(std::format("bytes {} hash {:x}\n", bytes, hash).c_str(), stdout);
    return 0;
}
