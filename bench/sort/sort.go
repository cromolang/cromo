// Benchmark sort: indexing of slices and fixed-size arrays (see sort.cro).
package main

import (
	"fmt"
	"os"
)

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

func insertionSort(v []uint64, low, high int64) {
	for i := low + 1; i <= high; i++ {
		x := v[i]
		j := i - 1
		for j >= low && v[j] > x {
			v[j+1] = v[j]
			j--
		}
		v[j+1] = x
	}
}

func quickSort(v []uint64, low, high int64) {
	lo := low
	hi := high
	for hi-lo > 16 {
		pivot := v[lo+(hi-lo)/2]
		i := lo
		j := hi
		for i <= j {
			for v[i] < pivot {
				i++
			}
			for v[j] > pivot {
				j--
			}
			if i <= j {
				kept := v[i]
				v[i] = v[j]
				v[j] = kept
				i++
				j--
			}
		}
		if j-lo < hi-i {
			quickSort(v, lo, j)
			lo = i
		} else {
			quickSort(v, i, hi)
			hi = j
		}
	}
	insertionSort(v, lo, hi)
}

func radixSort(v []uint64) []uint64 {
	other := append([]uint64(nil), v...)
	for shift := uint64(0); shift < 64; shift += 8 {
		var count [256]int64
		for _, x := range v {
			count[(x>>shift)&0xFF] += 1
		}
		position := int64(0)
		for k := range count {
			next := position + count[k]
			count[k] = position
			position = next
		}
		for _, x := range v {
			digit := (x >> shift) & 0xFF
			other[count[digit]] = x
			count[digit] += 1
		}
		v, other = other, v
	}
	return v
}

func main() {
	random := Random{state: 7}
	var values []uint64
	for i := 0; i < 5_000_000; i++ {
		values = append(values, random.next())
	}
	quick := append([]uint64(nil), values...)
	quickSort(quick, 0, int64(len(quick))-1)
	radix := radixSort(append([]uint64(nil), values...))
	hash := uint64(14695981039346656037)
	for i := range quick {
		if quick[i] != radix[i] || (i > 0 && quick[i-1] > quick[i]) {
			fmt.Fprintln(os.Stderr, "the two sorts differ")
			os.Exit(1)
		}
		hash = (hash ^ quick[i]) * 1099511628211
	}
	fmt.Printf("first %x last %x hash %x\n", quick[0], quick[len(quick)-1], hash)
}
