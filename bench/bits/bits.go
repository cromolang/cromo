// Benchmark bits: shifts, masks and the bitwise operators (see bits.cro).
package main

import "fmt"

type Random struct {
	state uint64
}

func (r *Random) next() uint64 {
	r.state += 0x9E3779B97F4A7C15
	z := r.state
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
	z = (z ^ (z >> 27)) * 0x94D049BB133111EB
	return z ^ (z >> 31)
}

func popCount(value uint64) uint64 {
	x := value - ((value >> 1) & 0x5555555555555555)
	x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333)
	x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0F
	return (x * 0x0101010101010101) >> 56
}

func reverse(value uint64) uint64 {
	x := ((value >> 1) & 0x5555555555555555) | ((value & 0x5555555555555555) << 1)
	x = ((x >> 2) & 0x3333333333333333) | ((x & 0x3333333333333333) << 2)
	x = ((x >> 4) & 0x0F0F0F0F0F0F0F0F) | ((x & 0x0F0F0F0F0F0F0F0F) << 4)
	x = ((x >> 8) & 0x00FF00FF00FF00FF) | ((x & 0x00FF00FF00FF00FF) << 8)
	x = ((x >> 16) & 0x0000FFFF0000FFFF) | ((x & 0x0000FFFF0000FFFF) << 16)
	return (x >> 32) | (x << 32)
}

func rotateLeft(x uint64, count uint64) uint64 {
	return (x << count) | (x >> (64 - count))
}

func main() {
	deBruijn := uint64(0x03F79D71B4CB0A89)
	var table [64]int64
	for i := uint64(0); i < 64; i++ {
		table[(deBruijn<<i)>>58] = int64(i)
	}
	random := Random{state: 1}
	ones := uint64(0)
	zeros := uint64(0)
	mixed := uint64(0)
	for i := 0; i < 100_000_000; i++ {
		x := random.next()
		ones += popCount(x)
		if x != 0 {
			lowest := x & (^x + 1)
			zeros += uint64(table[(lowest*deBruijn)>>58])
		}
		mixed ^= rotateLeft(reverse(x), 1+x%63)
		mixed += x ^ (x >> 1)
	}
	fmt.Printf("ones %d zeros %d mixed %x\n", ones, zeros, mixed)
}
