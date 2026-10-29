// Benchmark mandelbrot: floating point in a tight loop (see mandelbrot.cro).
#[derive(Default)]
struct Totals {
    inside: i64,
    iterations: i64,
}

fn rows(size: i64, first: i64, step: i64) -> Totals {
    let mut totals = Totals::default();
    let mut y = first;
    while y < size {
        let ci = 2.0 * y as f64 / size as f64 - 1.0;
        for x in 0..size {
            let cr = 2.0 * x as f64 / size as f64 - 1.5;
            let (mut zr, mut zi, mut tr, mut ti) = (0.0f64, 0.0f64, 0.0f64, 0.0f64);
            let mut i: i64 = 0;
            while i < 50 && tr + ti <= 4.0 {
                zi = 2.0 * zr * zi + ci;
                zr = tr - ti + cr;
                tr = zr * zr;
                ti = zi * zi;
                i += 1;
            }
            if tr + ti <= 4.0 {
                totals.inside += 1;
            }
            totals.iterations += i;
        }
        y += step;
    }
    totals
}

fn main() {
    let totals = rows(4000, 0, 1);
    println!("inside {} iterations {}", totals.inside, totals.iterations);
}
