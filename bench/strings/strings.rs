// Benchmark strings: format, appending and the bytes of a string (see strings.cro).
fn fnv(seed: u64, text: &str) -> u64 {
    let mut hash = seed;
    for &b in text.as_bytes() {
        hash ^= b as u64;
        hash = hash.wrapping_mul(1099511628211);
    }
    hash
}

fn main() {
    let mut hash: u64 = 14695981039346656037;
    let mut bytes: i64 = 0;
    let mut text = String::new();
    for i in 0..2_000_000i64 {
        let line = format!("{}:{:x}:{:.3};", i, i * 7919, i as f64 / 7.0);
        text += &line;
        if text.len() >= 4096 {
            hash = fnv(hash, &text);
            bytes += text.len() as i64;
            text.clear();
        }
    }
    hash = fnv(hash, &text);
    bytes += text.len() as i64;
    println!("bytes {} hash {:x}", bytes, hash);
}
