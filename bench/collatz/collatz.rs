// Benchmark collatz: integer arithmetic and loops (see collatz.cro).
fn main() {
    let limit: i64 = 5_000_000;
    let mut total: i64 = 0;
    let mut longest: i64 = 0;
    let mut start: i64 = 0;
    for n in 1..limit {
        let mut x = n;
        let mut steps: i64 = 0;
        while x != 1 {
            if x % 2 == 0 {
                x = x / 2;
            } else {
                x = 3 * x + 1;
            }
            steps += 1;
        }
        total += steps;
        if steps > longest {
            longest = steps;
            start = n;
        }
    }
    println!("steps {} longest {} from {}", total, longest, start);
}
