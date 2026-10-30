// Benchmark tasks: goroutines and channels (see tasks.cro).
package main

import "fmt"

func spread() int64 {
	numbers := make(chan int64, 1024)
	for i := int64(0); i < 100000; i++ {
		go func(i int64) { numbers <- i }(i)
	}
	var sum int64
	for i := 0; i < 100000; i++ {
		sum += <-numbers
	}
	return sum
}

func pingPong() int64 {
	ping := make(chan int64)
	pong := make(chan int64)
	go func() {
		for v := range ping {
			pong <- v + 1
		}
	}()
	done := make(chan int64)
	go func() {
		value := int64(0)
		for i := 0; i < 200000; i++ {
			ping <- value
			value = <-pong
		}
		close(ping)
		done <- value
	}()
	return <-done
}

func main() {
	sum := spread()
	fmt.Printf("tasks %d pingpong %d\n", sum, pingPong())
}
