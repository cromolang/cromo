// Benchmark wordcount: a hash map with string keys (see wordcount.cro).
package main

import "fmt"

// The generator SplitMix64.
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

func fnv(text string) uint64 {
	hash := uint64(14695981039346656037)
	for i := 0; i < len(text); i++ {
		hash ^= uint64(text[i])
		hash *= 1099511628211
	}
	return hash
}

func main() {
	random := Random{state: 42}
	var vocabulary []string
	for i := 0; i < 100_000; i++ {
		vocabulary = append(vocabulary, fmt.Sprintf("%x", random.next()))
	}
	counts := map[string]int64{}
	size := uint64(len(vocabulary))
	for i := 0; i < 5_000_000; i++ {
		counts[vocabulary[random.next()%size]] += 1
	}
	squares := int64(0)
	hashes := uint64(0)
	for word, count := range counts {
		squares += count * count
		hashes += fnv(word) * uint64(count)
	}
	fmt.Printf("distinct %d squares %d hashes %x\n", len(counts), squares, hashes)
}
