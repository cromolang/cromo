// Benchmark collatz: integer arithmetic and loops (see collatz.cro).
package main

import "fmt"

func main() {
	limit := int64(5_000_000)
	total := int64(0)
	longest := int64(0)
	start := int64(0)
	for n := int64(1); n < limit; n++ {
		x := n
		steps := int64(0)
		for x != 1 {
			if x%2 == 0 {
				x = x / 2
			} else {
				x = 3*x + 1
			}
			steps++
		}
		total += steps
		if steps > longest {
			longest = steps
			start = n
		}
	}
	fmt.Printf("steps %d longest %d from %d\n", total, longest, start)
}
