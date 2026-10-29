// Benchmark fib: recursive calls (see fib.cro).
fn fib(n: i64) -> i64 {
    if n < 2 {
        return n;
    }
    fib(n - 1) + fib(n - 2)
}

fn main() {
    println!("fib(42) = {}", fib(42));
}
