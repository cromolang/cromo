// Benchmark fib: recursive calls (see fib.cro).
package main

import "fmt"

func fib(n int64) int64 {
	if n < 2 {
		return n
	}
	return fib(n-1) + fib(n-2)
}

func main() {
	fmt.Printf("fib(42) = %d\n", fib(42))
}
