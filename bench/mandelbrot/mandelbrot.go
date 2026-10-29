// Benchmark mandelbrot: floating point in a tight loop (see mandelbrot.cro).
package main

import "fmt"

type Totals struct {
	inside     int64
	iterations int64
}

func rows(size, first, step int64) Totals {
	var totals Totals
	for y := first; y < size; y += step {
		ci := 2.0*float64(y)/float64(size) - 1.0
		for x := int64(0); x < size; x++ {
			cr := 2.0*float64(x)/float64(size) - 1.5
			zr, zi, tr, ti := 0.0, 0.0, 0.0, 0.0
			i := int64(0)
			for i < 50 && tr+ti <= 4.0 {
				zi = 2.0*zr*zi + ci
				zr = tr - ti + cr
				tr = zr * zr
				ti = zi * zi
				i++
			}
			if tr+ti <= 4.0 {
				totals.inside++
			}
			totals.iterations += i
		}
	}
	return totals
}

func main() {
	totals := rows(4000, 0, 1)
	fmt.Printf("inside %d iterations %d\n", totals.inside, totals.iterations)
}
