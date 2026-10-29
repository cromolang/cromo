// Benchmark binarytrees: allocation and freeing of many small objects (see binarytrees.cro).
struct Node {
    children: Vec<Node>,
}

fn build(depth: i64) -> Node {
    let mut node = Node { children: Vec::new() };
    if depth > 0 {
        node.children.reserve(2);
        node.children.push(build(depth - 1));
        node.children.push(build(depth - 1));
    }
    node
}

fn check(node: &Node) -> i64 {
    if node.children.is_empty() {
        return 1;
    }
    1 + check(&node.children[0]) + check(&node.children[1])
}

fn main() {
    let max_depth: i64 = 18;
    println!("stretch tree of depth {} check {}", max_depth + 1, check(&build(max_depth + 1)));
    let long_lived = build(max_depth);
    let mut depth = 4;
    while depth <= max_depth {
        let iterations: i64 = 1 << (max_depth - depth + 4);
        let mut sum: i64 = 0;
        for _ in 0..iterations {
            sum += check(&build(depth));
        }
        println!("{} trees of depth {} check {}", iterations, depth, sum);
        depth += 2;
    }
    println!("long lived tree of depth {} check {}", max_depth, check(&long_lived));
}
