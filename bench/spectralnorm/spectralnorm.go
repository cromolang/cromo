// Benchmark spectralnorm: floating point, calls and slices of float64 (see spectralnorm.cro).
package main

import "fmt"

func element(i, j int64) float64 {
	return 1.0 / float64((i+j)*(i+j+1)/2+i+1)
}

func multiplyAv(v []float64, result []float64) {
	n := int64(len(v))
	for i := int64(0); i < n; i++ {
		sum := 0.0
		for j := int64(0); j < n; j++ {
			sum += element(i, j) * v[j]
		}
		result[i] = sum
	}
}

func multiplyAtv(v []float64, result []float64) {
	n := int64(len(v))
	for i := int64(0); i < n; i++ {
		sum := 0.0
		for j := int64(0); j < n; j++ {
			sum += element(j, i) * v[j]
		}
		result[i] = sum
	}
}

func multiplyAtAv(v []float64, result []float64, temporary []float64) {
	multiplyAv(v, temporary)
	multiplyAtv(temporary, result)
}

func main() {
	n := int64(3500)
	var u, v, temporary []float64
	for i := int64(0); i < n; i++ {
		u = append(u, 1.0)
		v = append(v, 0.0)
		temporary = append(temporary, 0.0)
	}
	for step := 0; step < 10; step++ {
		multiplyAtAv(u, v, temporary)
		multiplyAtAv(v, u, temporary)
	}
	vBv := 0.0
	vv := 0.0
	for i := int64(0); i < n; i++ {
		vBv += u[i] * v[i]
		vv += v[i] * v[i]
	}
	fmt.Printf("norm squared %.12f\n", vBv/vv)
}
