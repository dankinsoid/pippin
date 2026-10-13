// @ai-generated(solo)
// workloads.render: hiccup as an enum tree, each node rendered to its own string and concatenated by its parent.

indirect enum Node {
	case text(String)
	case number(Int)
	case element(String, [String: String], [Node])
	case list([Node])
}

func escape(_ s: String) -> String {
	var out = ""
	for c in s {
		switch c {
		case "<": out += "&lt;"
		case ">": out += "&gt;"
		case "&": out += "&amp;"
		case "\"": out += "&quot;"
		default: out.append(c)
		}
	}
	return out
}

func attributes(_ attrs: [String: String]) -> String {
	attrs.sorted { $0.key < $1.key }.map { " \($0.key)=\"\(escape($0.value))\"" }.joined()
}

func renderChildren(_ nodes: [Node]) -> String {
	nodes.map(render).joined()
}

func render(_ node: Node) -> String {
	switch node {
	case .text(let s): escape(s)
	case .number(let n): String(n)
	case .list(let nodes): renderChildren(nodes)
	case let .element(tag, attrs, children): "<\(tag)\(attributes(attrs))>\(renderChildren(children))</\(tag)>"
	}
}

func item(_ i: Int) -> Node {
	.element("li", ["class": i.isMultiple(of: 2) ? "even" : "odd", "data-id": String(i)], [
		.element("span.title", [:], [.text("Item <\(i)> & co")]),
		.element("div", ["style": "color: red"], [
			.list((0..<i % 4).map { j in .element("a", ["href": "/items/\(i)/\(j)"], [.text("link "), .number(j)]) }),
		]),
	])
}

func page(_ items: Range<Int>) -> Node {
	.element("html", [:], [
		.element("head", [:], [.element("title", [:], [.text("List")])]),
		.element("body", [:], [
			.element("h1", [:], [.text("Items")]),
			.element("ul", ["id": "items"], [.list(items.map(item))]),
			.element("footer", [:], [.text("\(items.count) items")]),
		]),
	])
}

// Lengths and the prefix are UTF-16 units, Java's String length and indexes.
func run() -> EDN {
	let n = 4000
	let html = render(page(0..<n))
	let small = (0..<n).reduce(0) { $0 + render(item($1)).utf16.count }
	return .vector([.int(html.utf16.count), .int(small), .string(String(html.subs(0, 200)))])
}
