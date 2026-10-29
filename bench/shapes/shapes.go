// Benchmark shapes: a struct with a kind, its methods, an interface and a
// generic function (see shapes.cro). Go has no enums with data: a shape is a
// struct with its kind and two numbers, and its methods switch on the kind.
package main

import "fmt"

type Measured interface {
	area() float64
	perimeter() float64
}

const (
	circle = iota
	rectangle
	square
)

type Shape struct {
	kind int
	a, b float64
}

func (s Shape) area() float64 {
	switch s.kind {
	case circle:
		return 3.141592653589793 * s.a * s.a
	case rectangle:
		return s.a * s.b
	default:
		return s.a * s.a
	}
}

func (s Shape) perimeter() float64 {
	switch s.kind {
	case circle:
		return 2.0 * 3.141592653589793 * s.a
	case rectangle:
		return 2.0 * (s.a + s.b)
	default:
		return 4.0 * s.a
	}
}

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

func (r *Random) size() float64 {
	return 1.0 + float64(r.next()%1000)/10.0
}

func total[T Measured](items []T) float64 {
	sum := 0.0
	for _, item := range items {
		sum += item.area() + item.perimeter()
	}
	return sum
}

func main() {
	random := Random{state: 3}
	var shapes []Shape
	for i := 0; i < 1_000_000; i++ {
		kind := random.next() % 3
		if kind == 0 {
			shapes = append(shapes, Shape{kind: circle, a: random.size()})
		} else if kind == 1 {
			width := random.size()
			shapes = append(shapes, Shape{kind: rectangle, a: width, b: random.size()})
		} else {
			shapes = append(shapes, Shape{kind: square, a: random.size()})
		}
	}
	sum := 0.0
	for pass := 0; pass < 100; pass++ {
		sum += total(shapes)
	}
	fmt.Printf("total %.3f\n", sum)
}
