// Benchmark wordcount: a hash map with string keys (see wordcount.cro).
// HashMap with its default hasher, SipHash-1-3.
use std::collections::HashMap;

// The generator SplitMix64.
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

fn fnv(text: &str) -> u64 {
    let mut hash: u64 = 14695981039346656037;
    for &b in text.as_bytes() {
        hash ^= b as u64;
        hash = hash.wrapping_mul(1099511628211);
    }
    hash
}

fn main() {
    let mut random = Random { state: 42 };
    let mut vocabulary: Vec<String> = Vec::new();
    for _ in 0..100_000 {
        vocabulary.push(format!("{:x}", random.next()));
    }
    let mut counts: HashMap<String, i64> = HashMap::new();
    let size = vocabulary.len() as u64;
    for _ in 0..5_000_000 {
        let word = &vocabulary[(random.next() % size) as usize];
        // The key is copied only when it is new, as in the other versions.
        match counts.get_mut(word) {
            Some(count) => *count += 1,
            None => {
                counts.insert(word.clone(), 1);
            }
        }
    }
    let mut squares: i64 = 0;
    let mut hashes: u64 = 0;
    for (word, &count) in &counts {
        squares += count * count;
        hashes = hashes.wrapping_add(fnv(word).wrapping_mul(count as u64));
    }
    println!("distinct {} squares {} hashes {:x}", counts.len(), squares, hashes);
}
