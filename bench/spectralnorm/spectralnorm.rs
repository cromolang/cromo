// Benchmark spectralnorm: floating point, calls and vectors of f64 (see spectralnorm.cro).
fn element(i: i64, j: i64) -> f64 {
    1.0 / ((i + j) * (i + j + 1) / 2 + i + 1) as f64
}

fn multiply_av(v: &[f64], result: &mut [f64]) {
    let n = v.len() as i64;
    for i in 0..n {
        let mut sum = 0.0;
        for j in 0..n {
            sum += element(i, j) * v[j as usize];
        }
        result[i as usize] = sum;
    }
}

fn multiply_atv(v: &[f64], result: &mut [f64]) {
    let n = v.len() as i64;
    for i in 0..n {
        let mut sum = 0.0;
        for j in 0..n {
            sum += element(j, i) * v[j as usize];
        }
        result[i as usize] = sum;
    }
}

fn multiply_atav(v: &[f64], result: &mut [f64], temporary: &mut [f64]) {
    multiply_av(v, temporary);
    multiply_atv(temporary, result);
}

fn main() {
    let n: i64 = 3500;
    let mut u: Vec<f64> = Vec::new();
    let mut v: Vec<f64> = Vec::new();
    let mut temporary: Vec<f64> = Vec::new();
    for _ in 0..n {
        u.push(1.0);
        v.push(0.0);
        temporary.push(0.0);
    }
    for _ in 0..10 {
        multiply_atav(&u, &mut v, &mut temporary);
        multiply_atav(&v, &mut u, &mut temporary);
    }
    let mut vbv = 0.0;
    let mut vv = 0.0;
    for i in 0..n as usize {
        vbv += u[i] * v[i];
        vv += v[i] * v[i];
    }
    println!("norm squared {:.12}", vbv / vv);
}
