// Benchmark bits: shifts, masks and the bitwise operators (see bits.cro).
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
}

fn pop_count(value: u64) -> u64 {
    let mut x = value - ((value >> 1) & 0x5555555555555555);
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0F;
    x.wrapping_mul(0x0101010101010101) >> 56
}

fn reverse(value: u64) -> u64 {
    let mut x = ((value >> 1) & 0x5555555555555555) | ((value & 0x5555555555555555) << 1);
    x = ((x >> 2) & 0x3333333333333333) | ((x & 0x3333333333333333) << 2);
    x = ((x >> 4) & 0x0F0F0F0F0F0F0F0F) | ((x & 0x0F0F0F0F0F0F0F0F) << 4);
    x = ((x >> 8) & 0x00FF00FF00FF00FF) | ((x & 0x00FF00FF00FF00FF) << 8);
    x = ((x >> 16) & 0x0000FFFF0000FFFF) | ((x & 0x0000FFFF0000FFFF) << 16);
    (x >> 32) | (x << 32)
}

fn rotate_left(x: u64, count: u64) -> u64 {
    (x << count) | (x >> (64 - count))
}

fn main() {
    let de_bruijn: u64 = 0x03F79D71B4CB0A89;
    let mut table = [0i64; 64];
    for i in 0..64u64 {
        table[((de_bruijn << i) >> 58) as usize] = i as i64;
    }
    let mut random = Random { state: 1 };
    let mut ones: u64 = 0;
    let mut zeros: u64 = 0;
    let mut mixed: u64 = 0;
    for _ in 0..100_000_000 {
        let x = random.next();
        ones += pop_count(x);
        if x != 0 {
            let lowest = x & (!x).wrapping_add(1);
            zeros += table[(lowest.wrapping_mul(de_bruijn) >> 58) as usize] as u64;
        }
        mixed ^= rotate_left(reverse(x), 1 + x % 63);
        mixed = mixed.wrapping_add(x ^ (x >> 1));
    }
    println!("ones {} zeros {} mixed {:x}", ones, zeros, mixed);
}
