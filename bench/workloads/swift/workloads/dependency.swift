// @ai-generated(solo)
// workloads.dependency: com.stuartsierra.dependency's MapDependencyGraph as a value type.

struct CircularDependency<Node>: Error {
	let node: Node
	let dependency: Node
}

struct DependencyGraph<Node: Hashable> {
	private(set) var dependencies: [Node: Set<Node>] = [:]
	private(set) var dependents: [Node: Set<Node>] = [:]

	func immediateDependencies(_ node: Node) -> Set<Node> { dependencies[node] ?? [] }
	func immediateDependents(_ node: Node) -> Set<Node> { dependents[node] ?? [] }
	func transitiveDependencies(_ node: Node) -> Set<Node> { Self.transitive(dependencies, [node]) }
	func transitiveDependents(_ node: Node) -> Set<Node> { Self.transitive(dependents, [node]) }
	var nodes: Set<Node> { Set(dependencies.keys).union(dependents.keys) }

	/// True if x depends on y, directly or transitively.
	func depends(_ x: Node, on y: Node) -> Bool {
		transitiveDependencies(x).contains(y)
	}

	mutating func depend(_ node: Node, on dep: Node) throws {
		if node == dep || depends(dep, on: node) {
			throw CircularDependency(node: node, dependency: dep)
		}
		dependencies[node, default: []].insert(dep)
		dependents[dep, default: []].insert(node)
	}

	mutating func removeEdge(_ node: Node, _ dep: Node) {
		dependencies[node]?.remove(dep)
		dependents[dep]?.remove(node)
	}

	mutating func removeAll(_ node: Node) {
		dependencies = Self.removing(node, from: dependencies)
		dependents = Self.removing(node, from: dependents)
	}

	mutating func removeNode(_ node: Node) {
		dependencies[node] = nil
	}

	/// Kahn's algorithm: dependencies come before their dependents.
	func topoSort() -> [Node] {
		var sorted: [Node] = []
		var g = self
		var todo = nodes.filter { immediateDependents($0).isEmpty }
		while let node = todo.popFirst() {
			var add = Set<Node>()
			for d in g.immediateDependencies(node) {
				g.removeEdge(node, d)
				if g.immediateDependents(d).isEmpty {
					add.insert(d)
				}
			}
			sorted.append(node)
			g.removeNode(node)
			todo.formUnion(add)
		}
		return sorted.reversed()
	}

	/// Nodes outside the graph sort last.
	func topoComparator() -> (Node, Node) -> Bool {
		let pos = Dictionary(uniqueKeysWithValues: zip(topoSort(), 0...))
		return { (pos[$0] ?? Int.max) < (pos[$1] ?? Int.max) }
	}

	private static func transitive(_ neighbors: [Node: Set<Node>], _ start: Set<Node>) -> Set<Node> {
		var unexpanded = start.flatMap { neighbors[$0] ?? [] }
		var expanded = Set<Node>()
		while let node = unexpanded.popLast() {
			if expanded.insert(node).inserted {
				unexpanded.append(contentsOf: neighbors[node] ?? [])
			}
		}
		return expanded
	}

	private static func removing(_ x: Node, from map: [Node: Set<Node>]) -> [Node: Set<Node>] {
		var out: [Node: Set<Node>] = [:]
		for (k, var vs) in map where k != x {
			vs.remove(x)
			out[k] = vs
		}
		return out
	}
}

func build(_ n: Int) -> DependencyGraph<Int> {
	var g = DependencyGraph<Int>()
	var x = 7
	for i in 1..<n {
		let a = lcg(x)
		let b = lcg(a)
		// Every edge points to a smaller node: no cycle.
		try! g.depend(i, on: a % i)
		try! g.depend(i, on: b % i)
		x = b
	}
	return g
}

func run() -> EDN {
	let n = 1500
	let g = build(n)
	let order = g.topoSort()
	let pos = Dictionary(uniqueKeysWithValues: zip(order, 0...))
	// A set's order is the runtime's, so the sort is checked against every edge rather than printed.
	var forward = 0
	for i in 0..<n {
		for d in g.immediateDependencies(i) where pos[d]! < pos[i]! {
			forward += 1
		}
	}
	let sorted = stride(from: 0, to: n, by: 3).sorted(by: g.topoComparator())
	let trans = (n - 40..<n).reduce(0) { $0 + g.transitiveDependencies($1).count }
	let dependents = g.transitiveDependents(0).count
	let hits = stride(from: 1, to: n, by: 41).filter { g.depends($0, on: 0) }.count
	var pruned = g
	for node in stride(from: 0, to: n, by: 5) {
		pruned.removeAll(node)
	}
	return .ints([order.count, forward, sorted.count, g.nodes.count, trans, dependents, hits, pruned.nodes.count])
}
