// Benchmark binarytrees: allocation of many small objects (see binarytrees.cro).
// Go frees them with its garbage collector, with the default settings.
package main

import "fmt"

type Node struct {
	children []Node
}

func build(depth int64) Node {
	var node Node
	if depth > 0 {
		node.children = make([]Node, 0, 2)
		node.children = append(node.children, build(depth-1))
		node.children = append(node.children, build(depth-1))
	}
	return node
}

func check(node *Node) int64 {
	if len(node.children) == 0 {
		return 1
	}
	return 1 + check(&node.children[0]) + check(&node.children[1])
}

func main() {
	maxDepth := int64(18)
	stretch := build(maxDepth + 1)
	fmt.Printf("stretch tree of depth %d check %d\n", maxDepth+1, check(&stretch))
	longLived := build(maxDepth)
	for depth := int64(4); depth <= maxDepth; depth += 2 {
		iterations := int64(1) << (maxDepth - depth + 4)
		sum := int64(0)
		for i := int64(0); i < iterations; i++ {
			tree := build(depth)
			sum += check(&tree)
		}
		fmt.Printf("%d trees of depth %d check %d\n", iterations, depth, sum)
	}
	fmt.Printf("long lived tree of depth %d check %d\n", maxDepth, check(&longLived))
}
