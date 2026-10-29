// Benchmark sort: indexing of vectors and fixed-size arrays (see sort.cro).
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

fn insertion_sort(v: &mut [u64], low: i64, high: i64) {
    for i in low + 1..=high {
        let x = v[i as usize];
        let mut j = i - 1;
        while j >= low && v[j as usize] > x {
            v[(j + 1) as usize] = v[j as usize];
            j -= 1;
        }
        v[(j + 1) as usize] = x;
    }
}

fn quick_sort(v: &mut [u64], low: i64, high: i64) {
    let mut lo = low;
    let mut hi = high;
    while hi - lo > 16 {
        let pivot = v[(lo + (hi - lo) / 2) as usize];
        let mut i = lo;
        let mut j = hi;
        while i <= j {
            while v[i as usize] < pivot {
                i += 1;
            }
            while v[j as usize] > pivot {
                j -= 1;
            }
            if i <= j {
                v.swap(i as usize, j as usize);
                i += 1;
                j -= 1;
            }
        }
        if j - lo < hi - i {
            quick_sort(v, lo, j);
            lo = i;
        } else {
            quick_sort(v, i, hi);
            hi = j;
        }
    }
    insertion_sort(v, lo, hi);
}

fn radix_sort(v: &mut Vec<u64>) {
    let mut other = v.clone();
    let mut shift = 0;
    while shift < 64 {
        let mut count = [0i64; 256];
        for &x in v.iter() {
            count[((x >> shift) & 0xFF) as usize] += 1;
        }
        let mut position: i64 = 0;
        for c in count.iter_mut() {
            let next = position + *c;
            *c = position;
            position = next;
        }
        for &x in v.iter() {
            let digit = ((x >> shift) & 0xFF) as usize;
            other[count[digit] as usize] = x;
            count[digit] += 1;
        }
        std::mem::swap(v, &mut other);
        shift += 8;
    }
}

fn main() {
    let mut random = Random { state: 7 };
    let mut values: Vec<u64> = Vec::new();
    for _ in 0..5_000_000 {
        values.push(random.next());
    }
    let mut quick = values.clone();
    let last = quick.len() as i64 - 1;
    quick_sort(&mut quick, 0, last);
    let mut radix = values.clone();
    radix_sort(&mut radix);
    let mut hash: u64 = 14695981039346656037;
    for i in 0..quick.len() {
        if quick[i] != radix[i] || (i > 0 && quick[i - 1] > quick[i]) {
            eprintln!("the two sorts differ");
            std::process::exit(1);
        }
        hash = (hash ^ quick[i]).wrapping_mul(1099511628211);
    }
    println!("first {:x} last {:x} hash {:x}", quick[0], quick[quick.len() - 1], hash);
}
