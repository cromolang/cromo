// Benchmark shapes: an enum with data, its methods, a trait and a generic (see shapes.cro).
trait Measured {
    fn area(&self) -> f64;
    fn perimeter(&self) -> f64;
}

enum Shape {
    Circle(f64),
    Rectangle(f64, f64),
    Square(f64),
}

impl Measured for Shape {
    fn area(&self) -> f64 {
        match *self {
            Shape::Circle(r) => 3.141592653589793 * r * r,
            Shape::Rectangle(w, h) => w * h,
            Shape::Square(s) => s * s,
        }
    }

    fn perimeter(&self) -> f64 {
        match *self {
            Shape::Circle(r) => 2.0 * 3.141592653589793 * r,
            Shape::Rectangle(w, h) => 2.0 * (w + h),
            Shape::Square(s) => 4.0 * s,
        }
    }
}

struct Random {
    state: u64,
}

impl Random {
    fn next(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9E3779B97F4A7C15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58476D1CE4E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D049BB133111EB);
        z ^ (z >> 31)
    }

    fn size(&mut self) -> f64 {
        1.0 + (self.next() % 1000) as f64 / 10.0
    }
}

fn total<T: Measured>(items: &[T]) -> f64 {
    let mut sum = 0.0;
    for item in items {
        sum += item.area() + item.perimeter();
    }
    sum
}

fn main() {
    let mut random = Random { state: 3 };
    let mut shapes: Vec<Shape> = Vec::new();
    for _ in 0..1_000_000 {
        let kind = random.next() % 3;
        if kind == 0 {
            shapes.push(Shape::Circle(random.size()));
        } else if kind == 1 {
            let width = random.size();
            shapes.push(Shape::Rectangle(width, random.size()));
        } else {
            shapes.push(Shape::Square(random.size()));
        }
    }
    let mut sum = 0.0;
    for _ in 0..100 {
        sum += total(&shapes);
    }
    println!("total {:.3}", sum);
}
