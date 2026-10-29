// Benchmark spectralnorm: floating point, calls and vectors of double (see spectralnorm.cro).
#include <cstdint>
#include <cstdio>
#include <vector>

double element(int64_t i, int64_t j) {
    return 1.0 / double((i + j) * (i + j + 1) / 2 + i + 1);
}

void multiplyAv(const std::vector<double>& v, std::vector<double>& result) {
    int64_t n = v.size();
    for (int64_t i = 0; i < n; i++) {
        double sum = 0.0;
        for (int64_t j = 0; j < n; j++) {
            sum += element(i, j) * v[j];
        }
        result[i] = sum;
    }
}

void multiplyAtv(const std::vector<double>& v, std::vector<double>& result) {
    int64_t n = v.size();
    for (int64_t i = 0; i < n; i++) {
        double sum = 0.0;
        for (int64_t j = 0; j < n; j++) {
            sum += element(j, i) * v[j];
        }
        result[i] = sum;
    }
}

void multiplyAtAv(const std::vector<double>& v, std::vector<double>& result, std::vector<double>& temporary) {
    multiplyAv(v, temporary);
    multiplyAtv(temporary, result);
}

int main() {
    int64_t n = 3500;
    std::vector<double> u;
    std::vector<double> v;
    std::vector<double> temporary;
    for (int64_t i = 0; i < n; i++) {
        u.push_back(1.0);
        v.push_back(0.0);
        temporary.push_back(0.0);
    }
    for (int step = 0; step < 10; step++) {
        multiplyAtAv(u, v, temporary);
        multiplyAtAv(v, u, temporary);
    }
    double vBv = 0.0;
    double vv = 0.0;
    for (int64_t i = 0; i < n; i++) {
        vBv += u[i] * v[i];
        vv += v[i] * v[i];
    }
    std::printf("norm squared %.12f\n", vBv / vv);
    return 0;
}
