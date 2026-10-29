// Benchmark shapes: a variant, its visitors and a template (see shapes.cro).
#include <cstdint>
#include <cstdio>
#include <variant>
#include <vector>

struct Circle {
    double radius;
};

struct Rectangle {
    double width;
    double height;
};

struct Square {
    double side;
};

struct Shape {
    std::variant<Circle, Rectangle, Square> value;

    double area() const {
        struct Visitor {
            double operator()(const Circle& c) const { return 3.141592653589793 * c.radius * c.radius; }
            double operator()(const Rectangle& r) const { return r.width * r.height; }
            double operator()(const Square& s) const { return s.side * s.side; }
        };
        return std::visit(Visitor{}, value);
    }

    double perimeter() const {
        struct Visitor {
            double operator()(const Circle& c) const { return 2.0 * 3.141592653589793 * c.radius; }
            double operator()(const Rectangle& r) const { return 2.0 * (r.width + r.height); }
            double operator()(const Square& s) const { return 4.0 * s.side; }
        };
        return std::visit(Visitor{}, value);
    }
};

struct Random {
    uint64_t state = 0;

    uint64_t next() {
        state += 0x9E3779B97F4A7C15ull;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }

    double size() {
        return 1.0 + double(next() % 1000) / 10.0;
    }
};

template <typename T>
concept Measured = requires(const T& t) {
    t.area();
    t.perimeter();
};

template <Measured T>
double total(const std::vector<T>& items) {
    double sum = 0.0;
    for (const T& item : items) {
        sum += item.area() + item.perimeter();
    }
    return sum;
}

int main() {
    Random random;
    random.state = 3;
    std::vector<Shape> shapes;
    for (int64_t i = 0; i < 1'000'000; i++) {
        uint64_t kind = random.next() % 3;
        if (kind == 0) {
            shapes.push_back(Shape{Circle{random.size()}});
        } else if (kind == 1) {
            double width = random.size();
            shapes.push_back(Shape{Rectangle{width, random.size()}});
        } else {
            shapes.push_back(Shape{Square{random.size()}});
        }
    }
    double sum = 0.0;
    for (int pass = 0; pass < 100; pass++) {
        sum += total(shapes);
    }
    std::printf("total %.3f\n", sum);
    return 0;
}
