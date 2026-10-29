// Benchmark threads: the work of mandelbrot split among 8 threads (see threads.cro).
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

struct Totals {
    int64_t inside = 0;
    int64_t iterations = 0;
};

Totals rows(int64_t size, int64_t first, int64_t step) {
    Totals totals;
    for (int64_t y = first; y < size; y += step) {
        double ci = 2.0 * double(y) / double(size) - 1.0;
        for (int64_t x = 0; x < size; x++) {
            double cr = 2.0 * double(x) / double(size) - 1.5;
            double zr = 0.0;
            double zi = 0.0;
            double tr = 0.0;
            double ti = 0.0;
            int64_t i = 0;
            while (i < 50 && tr + ti <= 4.0) {
                zi = 2.0 * zr * zi + ci;
                zr = tr - ti + cr;
                tr = zr * zr;
                ti = zi * zi;
                i++;
            }
            if (tr + ti <= 4.0) {
                totals.inside++;
            }
            totals.iterations += i;
        }
    }
    return totals;
}

int main() {
    const int64_t size = 4000;
    const int64_t count = 8;
    Totals all;
    std::mutex lock;
    std::vector<std::thread> workers;
    for (int64_t k = 0; k < count; k++) {
        workers.emplace_back([&, k] {
            Totals mine = rows(size, k, count);
            std::lock_guard<std::mutex> guard(lock);
            all.inside += mine.inside;
            all.iterations += mine.iterations;
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    std::printf("inside %lld iterations %lld\n", (long long)all.inside, (long long)all.iterations);
    return 0;
}
