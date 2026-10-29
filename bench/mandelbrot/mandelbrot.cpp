// Benchmark mandelbrot: floating point in a tight loop (see mandelbrot.cro).
#include <cstdint>
#include <cstdio>

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
    Totals totals = rows(4000, 0, 1);
    std::printf("inside %lld iterations %lld\n", (long long)totals.inside, (long long)totals.iterations);
    return 0;
}
