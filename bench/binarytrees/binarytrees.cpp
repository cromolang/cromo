// Benchmark binarytrees: allocation and freeing of many small objects (see binarytrees.cro).
#include <cstdint>
#include <cstdio>
#include <vector>

struct Node {
    std::vector<Node> children;
};

Node build(int64_t depth) {
    Node node;
    if (depth > 0) {
        node.children.reserve(2);
        node.children.push_back(build(depth - 1));
        node.children.push_back(build(depth - 1));
    }
    return node;
}

int64_t check(const Node& node) {
    if (node.children.empty()) {
        return 1;
    }
    return 1 + check(node.children[0]) + check(node.children[1]);
}

int main() {
    int64_t maxDepth = 18;
    std::printf("stretch tree of depth %lld check %lld\n", (long long)(maxDepth + 1), (long long)check(build(maxDepth + 1)));
    Node longLived = build(maxDepth);
    for (int64_t depth = 4; depth <= maxDepth; depth += 2) {
        int64_t iterations = int64_t(1) << (maxDepth - depth + 4);
        int64_t sum = 0;
        for (int64_t i = 0; i < iterations; i++) {
            sum += check(build(depth));
        }
        std::printf("%lld trees of depth %lld check %lld\n", (long long)iterations, (long long)depth, (long long)sum);
    }
    std::printf("long lived tree of depth %lld check %lld\n", (long long)maxDepth, (long long)check(longLived));
    return 0;
}
