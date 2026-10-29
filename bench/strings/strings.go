// Benchmark strings: format, appending and the bytes of a string (see strings.cro).
package main

import "fmt"

func fnv(seed uint64, text []byte) uint64 {
	hash := seed
	for i := 0; i < len(text); i++ {
		hash ^= uint64(text[i])
		hash *= 1099511628211
	}
	return hash
}

func main() {
	hash := uint64(14695981039346656037)
	bytes := int64(0)
	var text []byte
	for i := int64(0); i < 2_000_000; i++ {
		line := fmt.Sprintf("%d:%x:%.3f;", i, i*7919, float64(i)/7.0)
		text = append(text, line...)
		if len(text) >= 4096 {
			hash = fnv(hash, text)
			bytes += int64(len(text))
			text = text[:0]
		}
	}
	hash = fnv(hash, text)
	bytes += int64(len(text))
	fmt.Printf("bytes %d hash %x\n", bytes, hash)
}
