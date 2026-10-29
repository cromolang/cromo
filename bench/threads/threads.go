// Benchmark threads: the work of mandelbrot split among 8 goroutines (see threads.cro).
package main

import (
	"fmt"
	"sync"
)

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
	const size = 4000
	const count = 8
	var all Totals
	var lock sync.Mutex
	var group sync.WaitGroup
	for k := int64(0); k < count; k++ {
		group.Add(1)
		go func(k int64) {
			defer group.Done()
			mine := rows(size, k, count)
			lock.Lock()
			all.inside += mine.inside
			all.iterations += mine.iterations
			lock.Unlock()
		}(k)
	}
	group.Wait()
	fmt.Printf("inside %d iterations %d\n", all.inside, all.iterations)
}
