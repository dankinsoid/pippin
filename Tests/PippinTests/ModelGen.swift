// @ai-generated(solo)
import Foundation

// ModelTests' generator, model and shrinker: plain Swift, so a bug in the collections cannot reach the model.
enum Model {
	enum Kind: Equatable {
		case vec, list, queue, hmap, hset, smap, sset

		var isMap: Bool { self == .hmap || self == .smap }
		var isSet: Bool { self == .hset || self == .sset }
		var isSorted: Bool { self == .smap || self == .sset }
		var isSequential: Bool { self == .vec || self == .list || self == .queue }
	}

	indirect enum V {
		// The printed text of a number, keyword, string, nil or boolean.
		case scalar(String)
		case atom(Int)
		case lazy([V])
		case coll(Coll)
	}

	struct Coll {
		var kind: Kind
		// A sequential's items (a list's head first), a set's elements, a map's keys.
		var keys: [V] = []
		var vals: [V] = []
		var meta: Int?
		// A list's meta is its head cell's, as PersistentList keeps it: `pop` exposes the next cell's.
		var cells: [Int?] = []

		init(_ kind: Kind, _ keys: [V] = [], _ vals: [V] = []) {
			self.kind = kind
			self.keys = keys
			self.vals = vals
			if kind == .list { cells = keys.map { _ in nil } }
		}

		var count: Int { keys.count }
	}

	enum Arg {
		case val(V)
		case handle(Int)
	}

	enum Upd {
		case wrap
		case const(Arg)
		case conj(Arg)
	}

	enum Sub {
		case conj(Arg), assoc(Arg, Arg), dissoc(Arg), disj(Arg), pop
	}

	// The paths into a collection: the builtin's conj loop, the fused driver, the transducer arity, a reduce.
	enum Via: CaseIterable {
		case plain, fused, xform, reduce
	}

	struct Init {
		var expr: String
		var value: Coll
		var big = false
	}

	enum Op {
		case make(Init)
		case conj([Arg])
		case conjEntry(Arg, Arg)
		case assoc([(Arg, Arg)])
		case dissoc([Arg])
		case disj([Arg])
		case pop
		case update(Arg, Upd)
		case updateIn(Arg, Arg, Upd)
		case assocIn(Arg, Arg, Arg)
		case merge(Arg)
		case into(Arg, Via)
		case subvec(Int, Int)
		case withMeta(Int?)
		case varyMeta(Int)
		case empty
		case vec(Via)
		case selectKeys([Arg])
		case transient([Sub])
		// The element at a key, or the first one, when it is a collection: the nested value becomes a handle.
		case get(Arg)
		case first
	}

	// How the step's source reaches the operation: each is a different ownership path in both backends.
	enum Wrap: CaseIterable {
		case plain, ifTrue, letAlias, fnCall, loopOnce, tryBody, capture, publish, future, apply, reduce, swap, vswap
		// A seq or a lazy seq over the source, taken before the operation and realized after it.
		case seqView, lazyView
	}

	struct Step {
		var op: Op
		var src: Int?
		var wrap: Wrap
	}

	// Handle i is step i's result. A retained handle is checked again after the last step.
	struct Seq {
		var steps: [Step]
		var retained: Set<Int>
		var viaVar: Bool
	}

	// MARK: the model

	static func int(_ n: Int) -> V { .scalar(String(n)) }
	static let nilV = V.scalar("nil")

	static func intOf(_ v: V) -> Int? {
		if case .scalar(let s) = v { return Int(s) }
		return nil
	}

	static func isNil(_ v: V) -> Bool {
		if case .scalar(let s) = v { return s == "nil" }
		return false
	}

	// Clojure's `sort` of strings compares UTF-16 units; the texts here are ASCII, where that is byte order.
	static func byBytes(_ a: String, _ b: String) -> Bool { a.utf8.lexicographicallyPrecedes(b.utf8) }

	static func text(_ v: V) -> String {
		switch v {
		case .scalar(let s): return s
		case .atom(let i): return "mt-a\(i)"
		case .lazy(let xs): return "(list" + xs.map { " " + text($0) }.joined() + ")"
		case .coll(let c): return text(c)
		}
	}

	// prelude.clj's mt-pr, which is also code that rebuilds the value.
	static func text(_ c: Coll) -> String {
		switch c.kind {
		case .vec: return "[" + c.keys.map(text).joined(separator: " ") + "]"
		case .list: return "(list" + c.keys.map { " " + text($0) }.joined() + ")"
		case .queue: return "(mt-q" + c.keys.map { " " + text($0) }.joined() + ")"
		case .hmap: return "{" + zip(c.keys, c.vals).map { text($0) + " " + text($1) }.sorted(by: byBytes).joined(separator: ", ") + "}"
		case .hset: return "#{" + c.keys.map(text).sorted(by: byBytes).joined(separator: " ") + "}"
		case .smap: return "(sorted-map" + zip(c.keys, c.vals).map { " " + text($0) + " " + text($1) }.joined() + ")"
		case .sset: return "(sorted-set" + c.keys.map { " " + text($0) }.joined() + ")"
		}
	}

	// Equal under Clojure's `=` exactly when the keys are equal: sequentials compare across kinds, as do the hash
	// and sorted maps and sets.
	static func eqKey(_ v: V) -> String {
		switch v {
		case .scalar(let s): return s
		case .atom(let i): return "#a\(i)"
		case .lazy(let xs): return "[" + xs.map(eqKey).joined(separator: " ") + "]"
		case .coll(let c):
			switch c.kind {
			case .vec, .list, .queue: return "[" + c.keys.map(eqKey).joined(separator: " ") + "]"
			case .hmap, .smap: return "{" + zip(c.keys, c.vals).map { eqKey($0) + " " + eqKey($1) }.sorted(by: byBytes).joined(separator: ", ") + "}"
			case .hset, .sset: return "#{" + c.keys.map(eqKey).sorted(by: byBytes).joined(separator: " ") + "}"
			}
		}
	}

	// A key or set element prints the same whichever of two equal values the collection kept.
	static func printUnique(_ v: V) -> Bool {
		switch v {
		case .scalar, .atom: return true
		case .lazy: return false
		case .coll(let c):
			switch c.kind {
			case .vec, .hset: return c.keys.allSatisfy(printUnique)
			case .hmap: return c.keys.allSatisfy(printUnique) && c.vals.allSatisfy(printUnique)
			default: return false
			}
		}
	}

	static func metaText(_ m: Int?) -> String { m.map { "{:m \($0)}" } ?? "nil" }

	static func metaOf(_ c: Coll) -> Int? { c.kind == .list && !c.cells.isEmpty ? c.cells[0] : c.meta }

	static func setMeta(_ c: inout Coll, _ m: Int?) {
		if c.kind == .list && !c.cells.isEmpty { c.cells[0] = m } else { c.meta = m }
	}

	static func validKey(_ c: Coll, _ k: V) -> Bool {
		switch c.kind {
		case .smap, .sset: return intOf(k) != nil
		case .hmap, .hset: return printUnique(k)
		default: return false
		}
	}

	static func find(_ c: Coll, _ k: V) -> Int? {
		let e = eqKey(k)
		return c.keys.firstIndex { eqKey($0) == e }
	}

	static func insertKey(_ c: inout Coll, _ k: V, _ v: V?) {
		var at = c.keys.count
		if c.kind.isSorted, let n = intOf(k) { at = c.keys.firstIndex { intOf($0)! > n } ?? c.keys.count }
		c.keys.insert(k, at: at)
		if let v { c.vals.insert(v, at: at) }
	}

	static func entry(_ k: V, _ v: V) -> V { .coll(Coll(.vec, [k, v])) }

	// The items in seq order, where the model knows that order.
	static func seqItems(_ c: Coll) -> [V]? {
		switch c.kind {
		case .vec, .list, .queue, .sset: return c.keys
		case .smap: return zip(c.keys, c.vals).map { entry($0, $1) }
		case .hmap, .hset: return nil
		}
	}

	static func allItems(_ c: Coll) -> [V] {
		c.kind.isMap ? zip(c.keys, c.vals).map { entry($0, $1) } : c.keys
	}

	static func conj1(_ c: Coll, _ x: V) -> Coll? {
		var c = c
		switch c.kind {
		case .vec, .queue: c.keys.append(x)
		case .list:
			let m = metaOf(c)
			c.keys.insert(x, at: 0)
			c.cells.insert(m, at: 0)
		case .hset, .sset:
			guard validKey(c, x) else { return nil }
			if find(c, x) == nil { insertKey(&c, x, nil) }
		case .hmap, .smap:
			guard case .coll(let e) = x, e.kind == .vec, e.count == 2 else { return nil }
			return assoc(c, e.keys[0], e.keys[1])
		}
		return c
	}

	static func assoc(_ c: Coll, _ k: V, _ v: V) -> Coll? {
		var c = c
		switch c.kind {
		case .vec:
			guard let i = intOf(k), i >= 0, i <= c.count else { return nil }
			if i == c.count { c.keys.append(v) } else { c.keys[i] = v }
		case .hmap, .smap:
			guard validKey(c, k) else { return nil }
			if let i = find(c, k) { c.vals[i] = v } else { insertKey(&c, k, v) }
		default: return nil
		}
		return c
	}

	static func dissoc(_ c: Coll, _ k: V) -> Coll? {
		guard c.kind.isMap, validKey(c, k) else { return nil }
		var c = c
		if let i = find(c, k) {
			c.keys.remove(at: i)
			c.vals.remove(at: i)
		}
		return c
	}

	static func disj(_ c: Coll, _ x: V) -> Coll? {
		guard c.kind.isSet, validKey(c, x) else { return nil }
		var c = c
		if let i = find(c, x) { c.keys.remove(at: i) }
		return c
	}

	static func pop(_ c: Coll) -> Coll? {
		guard c.kind.isSequential, c.count > 0 else { return nil }
		var c = c
		switch c.kind {
		case .vec: c.keys.removeLast()
		case .queue: c.keys.removeFirst()
		default:
			if c.count == 1 { c.meta = c.cells[0] }
			c.keys.removeFirst()
			c.cells.removeFirst()
		}
		return c
	}

	// nil when the lookup is not a valid one; the scalar nil when the key is absent.
	static func get(_ c: Coll, _ k: V) -> V? {
		switch c.kind {
		case .vec:
			guard let i = intOf(k), i >= 0, i < c.count else { return nil }
			return c.keys[i]
		case .hmap, .smap:
			guard validKey(c, k) else { return nil }
			return find(c, k).map { c.vals[$0] } ?? nilV
		default: return nil
		}
	}

	static func value(_ a: Arg, _ h: [Coll]) -> V {
		switch a {
		case .val(let v): return v
		case .handle(let j): return .coll(h[j])
		}
	}

	static func upd(_ f: Upd, _ old: V, _ h: [Coll]) -> V? {
		switch f {
		case .wrap: return .coll(Coll(.vec, [old]))
		case .const(let a): return value(a, h)
		case .conj(let a):
			let x = value(a, h)
			if isNil(old) { return .coll(Coll(.list, [x])) }
			guard case .coll(let c) = old, !c.kind.isMap else { return nil }
			return conj1(c, x).map { .coll($0) }
		}
	}

	static func nested(_ c: Coll, _ k1: V, _ k2: V, _ h: [Coll], _ f: (V) -> V?) -> Coll? {
		guard let old1 = get(c, k1) else { return nil }
		var inner: Coll
		if isNil(old1) {
			inner = Coll(.hmap)
		} else if case .coll(let i) = old1, i.kind == .hmap || i.kind == .smap || i.kind == .vec {
			inner = i
		} else {
			return nil
		}
		let old2: V
		if inner.kind == .vec {
			guard let i = intOf(k2), i >= 0, i <= inner.count else { return nil }
			old2 = i < inner.count ? inner.keys[i] : nilV
		} else {
			guard let o = get(inner, k2) else { return nil }
			old2 = o
		}
		guard let new2 = f(old2), let newInner = assoc(inner, k2, new2) else { return nil }
		return assoc(c, k1, .coll(newInner))
	}

	static func into(_ c: Coll, _ from: V) -> Coll? {
		guard case .coll(let f) = from else { return nil }
		var r = c
		if c.kind.isMap && f.kind.isMap {
			for (k, v) in zip(f.keys, f.vals) {
				guard let n = assoc(r, k, v) else { return nil }
				r = n
			}
			return r
		}
		guard let xs = c.kind.isSet ? allItems(f) : seqItems(f) else { return nil }
		for x in xs {
			guard let n = conj1(r, x) else { return nil }
			r = n
		}
		return r
	}

	static func sub(_ c: Coll, _ s: Sub, _ h: [Coll]) -> Coll? {
		switch s {
		case .conj(let a): return c.kind == .vec || c.kind == .hset ? conj1(c, value(a, h)) : nil
		case .assoc(let k, let v): return c.kind == .vec || c.kind == .hmap ? assoc(c, value(k, h), value(v, h)) : nil
		case .dissoc(let k): return c.kind == .hmap ? dissoc(c, value(k, h)) : nil
		case .disj(let x): return c.kind == .hset ? disj(c, value(x, h)) : nil
		case .pop: return c.kind == .vec ? pop(c) : nil
		}
	}

	static func apply(_ op: Op, _ src: Coll?, _ h: [Coll]) -> Coll? {
		if case .make(let i) = op { return i.value }
		guard let c = src else { return nil }
		switch op {
		case .make: return nil
		case .conj(let xs):
			guard !xs.isEmpty, !c.kind.isMap else { return nil }
			var r = c
			for x in xs {
				guard let n = conj1(r, value(x, h)) else { return nil }
				r = n
			}
			return r
		case .conjEntry(let k, let v):
			guard c.kind.isMap else { return nil }
			return assoc(c, value(k, h), value(v, h))
		case .assoc(let kvs):
			guard !kvs.isEmpty else { return nil }
			var r = c
			for (k, v) in kvs {
				guard let n = assoc(r, value(k, h), value(v, h)) else { return nil }
				r = n
			}
			return r
		case .dissoc(let ks):
			guard !ks.isEmpty else { return nil }
			var r = c
			for k in ks {
				guard let n = dissoc(r, value(k, h)) else { return nil }
				r = n
			}
			return r
		case .disj(let xs):
			guard !xs.isEmpty else { return nil }
			var r = c
			for x in xs {
				guard let n = disj(r, value(x, h)) else { return nil }
				r = n
			}
			return r
		case .pop: return pop(c)
		case .update(let k, let f):
			guard let old = get(c, value(k, h)), let new = upd(f, old, h) else { return nil }
			return assoc(c, value(k, h), new)
		case .updateIn(let k1, let k2, let f):
			guard c.kind.isMap else { return nil }
			return nested(c, value(k1, h), value(k2, h), h) { upd(f, $0, h) }
		case .assocIn(let k1, let k2, let v):
			guard c.kind.isMap else { return nil }
			return nested(c, value(k1, h), value(k2, h), h) { _ in value(v, h) }
		case .merge(let a):
			guard c.kind.isMap, case .coll(let m) = value(a, h), m.kind.isMap else { return nil }
			return into(c, .coll(m))
		case .into(let a, _): return into(c, value(a, h))
		case .subvec(let a, let b):
			guard c.kind == .vec, 0 <= a, a <= b, b <= c.count else { return nil }
			return Coll(.vec, Array(c.keys[a..<b]))
		case .withMeta(let m):
			var r = c
			setMeta(&r, m)
			return r
		case .varyMeta(let m):
			var r = c
			setMeta(&r, m)
			return r
		case .empty:
			var r = Coll(c.kind)
			r.meta = metaOf(c)
			return r
		case .vec:
			guard let xs = seqItems(c) else { return nil }
			return Coll(.vec, xs)
		case .selectKeys(let ks):
			guard c.kind.isMap, !ks.isEmpty else { return nil }
			var r = Coll(.hmap)
			r.meta = metaOf(c)
			for k in ks {
				let kv = value(k, h)
				guard validKey(c, kv) else { return nil }
				if let i = find(c, kv), find(r, kv) == nil { insertKey(&r, c.keys[i], c.vals[i]) }
			}
			return r
		case .get(let k):
			guard let v = get(c, value(k, h)), case .coll(let r) = v else { return nil }
			return r
		case .first:
			guard c.kind.isSequential || c.kind.isSorted, let x = seqItems(c)?.first, case .coll(let r) = x else { return nil }
			return r
		case .transient(let subs):
			guard !subs.isEmpty, c.kind == .vec || c.kind == .hmap || c.kind == .hset else { return nil }
			var r = c
			for s in subs {
				guard let n = sub(r, s, h) else { return nil }
				r = n
			}
			return r
		}
	}

	// Every handle's model value, or nil when a step is not valid on its source.
	static func replay(_ s: Seq) -> [Coll]? {
		guard let first = s.steps.first, case .make = first.op else { return nil }
		var h: [Coll] = []
		for (i, st) in s.steps.enumerated() {
			if let j = st.src, j >= i { return nil }
			for j in handles(st.op) where j >= i { return nil }
			guard let r = apply(st.op, st.src.map { h[$0] }, h) else { return nil }
			h.append(r)
		}
		return h
	}

	// MARK: handles inside an operation

	static func handles(_ op: Op) -> [Int] {
		var out: [Int] = []
		_ = remap(op) { j in
			out.append(j)
			return j
		}
		return out
	}

	static func remap(_ a: Arg, _ f: (Int) -> Int?) -> Arg? {
		switch a {
		case .val: return a
		case .handle(let j): return f(j).map { .handle($0) }
		}
	}

	static func remap(_ u: Upd, _ f: (Int) -> Int?) -> Upd? {
		switch u {
		case .wrap: return u
		case .const(let a): return remap(a, f).map { .const($0) }
		case .conj(let a): return remap(a, f).map { .conj($0) }
		}
	}

	static func remap(_ args: [Arg], _ f: (Int) -> Int?) -> [Arg]? {
		var out: [Arg] = []
		for a in args {
			guard let r = remap(a, f) else { return nil }
			out.append(r)
		}
		return out
	}

	static func remap(_ op: Op, _ f: (Int) -> Int?) -> Op? {
		switch op {
		case .make, .pop, .subvec, .withMeta, .varyMeta, .empty, .vec, .first: return op
		case .get(let k): return remap(k, f).map { .get($0) }
		case .conj(let xs): return remap(xs, f).map { .conj($0) }
		case .conjEntry(let k, let v):
			guard let k = remap(k, f), let v = remap(v, f) else { return nil }
			return .conjEntry(k, v)
		case .assoc(let kvs):
			var out: [(Arg, Arg)] = []
			for (k, v) in kvs {
				guard let k = remap(k, f), let v = remap(v, f) else { return nil }
				out.append((k, v))
			}
			return .assoc(out)
		case .dissoc(let ks): return remap(ks, f).map { .dissoc($0) }
		case .disj(let xs): return remap(xs, f).map { .disj($0) }
		case .update(let k, let u):
			guard let k = remap(k, f), let u = remap(u, f) else { return nil }
			return .update(k, u)
		case .updateIn(let k1, let k2, let u):
			guard let k1 = remap(k1, f), let k2 = remap(k2, f), let u = remap(u, f) else { return nil }
			return .updateIn(k1, k2, u)
		case .assocIn(let k1, let k2, let v):
			guard let k1 = remap(k1, f), let k2 = remap(k2, f), let v = remap(v, f) else { return nil }
			return .assocIn(k1, k2, v)
		case .merge(let a): return remap(a, f).map { .merge($0) }
		case .into(let a, let via): return remap(a, f).map { .into($0, via) }
		case .selectKeys(let ks): return remap(ks, f).map { .selectKeys($0) }
		case .transient(let subs):
			var out: [Sub] = []
			for s in subs {
				switch s {
				case .pop: out.append(.pop)
				case .conj(let a):
					guard let a = remap(a, f) else { return nil }
					out.append(.conj(a))
				case .assoc(let k, let v):
					guard let k = remap(k, f), let v = remap(v, f) else { return nil }
					out.append(.assoc(k, v))
				case .dissoc(let a):
					guard let a = remap(a, f) else { return nil }
					out.append(.dissoc(a))
				case .disj(let a):
					guard let a = remap(a, f) else { return nil }
					out.append(.disj(a))
				}
			}
			return .transient(out)
		}
	}

	// MARK: emission

	static func code(_ a: Arg) -> String {
		switch a {
		case .handle(let j): return "h\(j)"
		case .val(.lazy(let xs)): return "(lazy-seq (list" + xs.map { " " + text($0) }.joined() + "))"
		case .val(let v): return text(v)
		}
	}

	enum Form {
		case call(String, [String])
		case raw((String) -> String)
	}

	static func updArgs(_ u: Upd) -> [String] {
		switch u {
		case .wrap: return ["mt-wrap"]
		case .const(let a): return ["(constantly \(code(a)))"]
		case .conj(let a): return ["conj", code(a)]
		}
	}

	static func form(_ op: Op) -> Form {
		switch op {
		case .make(let i): return .raw { _ in i.expr }
		case .conj(let xs): return .call("conj", xs.map(code))
		case .conjEntry(let k, let v): return .call("conj", ["[\(code(k)) \(code(v))]"])
		case .assoc(let kvs): return .call("assoc", kvs.flatMap { [code($0.0), code($0.1)] })
		case .dissoc(let ks): return .call("dissoc", ks.map(code))
		case .disj(let xs): return .call("disj", xs.map(code))
		case .pop: return .call("pop", [])
		case .update(let k, let u): return .call("update", [code(k)] + updArgs(u))
		case .updateIn(let k1, let k2, let u): return .call("update-in", ["[\(code(k1)) \(code(k2))]"] + updArgs(u))
		case .assocIn(let k1, let k2, let v): return .call("assoc-in", ["[\(code(k1)) \(code(k2))]", code(v)])
		case .merge(let a): return .call("merge", [code(a)])
		case .into(let a, let via):
			switch via {
			case .plain: return .call("into", [code(a)])
			case .fused: return .call("into", ["(map identity \(code(a)))"])
			case .xform: return .call("into", ["(map identity)", code(a)])
			case .reduce: return .raw { s in "(reduce conj \(s) \(code(a)))" }
			}
		case .subvec(let a, let b): return .call("subvec", ["\(a)", "\(b)"])
		case .withMeta(let m): return .call("with-meta", [metaText(m)])
		case .varyMeta(let m): return .call("vary-meta", ["assoc", ":m", "\(m)"])
		case .empty: return .call("empty", [])
		case .vec(let via):
			switch via {
			case .plain: return .call("vec", [])
			case .fused: return .raw { s in "(vec (map identity \(s)))" }
			case .xform: return .raw { s in "(into [] (map identity) \(s))" }
			case .reduce: return .raw { s in "(mapv identity \(s))" }
			}
		case .selectKeys(let ks): return .call("select-keys", ["[" + ks.map(code).joined(separator: " ") + "]"])
		case .get(let k): return .call("get", [code(k)])
		case .first: return .call("first", [])
		case .transient(let subs):
			let body = subs.map { s -> String in
				switch s {
				case .conj(let a): return " (conj! \(code(a)))"
				case .assoc(let k, let v): return " (assoc! \(code(k)) \(code(v)))"
				case .dissoc(let k): return " (dissoc! \(code(k)))"
				case .disj(let x): return " (disj! \(code(x)))"
				case .pop: return " (pop!)"
				}
			}.joined()
			return .raw { s in "(persistent! (-> (transient \(s))\(body)))" }
		}
	}

	static func wraps(_ op: Op) -> [Wrap] {
		switch op {
		case .make: return [.plain]
		case .transient, .into(_, .reduce), .vec(.fused), .vec(.xform), .vec(.reduce):
			return Wrap.allCases.filter { ![.apply, .reduce, .swap, .vswap].contains($0) }
		case .conj, .dissoc, .disj: return Wrap.allCases
		default: return Wrap.allCases.filter { $0 != .reduce }
		}
	}

	// The view's expected text: its items in order, or as a set where the model does not know the order.
	static func viewText(_ c: Coll) -> (unordered: Bool, text: String) {
		if let xs = seqItems(c) { return (false, text(.lazy(xs))) }
		return (true, text(Coll(.hset, allItems(c))))
	}

	static func expr(_ st: Step, tag: String, source: Coll?) -> String {
		let src = st.src.map { "h\($0)" } ?? ""
		let f = form(st.op)
		func x(_ s: String) -> String {
			switch f {
			case .call(let n, let a): return "(" + ([n, s] + a).joined(separator: " ") + ")"
			case .raw(let r): return r(s)
			}
		}
		switch st.wrap {
		case .plain: return x(src)
		case .ifTrue: return "(if (mt-yes) \(x(src)) (mt-no))"
		case .letAlias: return "(let [t \(src)] \(x("t")))"
		case .fnCall: return "((fn [t] \(x("t"))) \(src))"
		case .loopOnce: return "(loop [t \(src) i 0] (if (zero? i) (recur \(x("t")) 1) t))"
		case .tryBody: return "(try \(x(src)) (catch Exception e (mt-no)))"
		case .capture: return "(let [g (fn [] \(src))] \(x("(g)")))"
		case .publish: return "(mt-pub \(x(src)))"
		case .future: return "@(future \(x(src)))"
		case .apply, .reduce:
			guard case .call(let n, let a) = f else { return x(src) }
			return "(\(st.wrap == .apply ? "apply" : "reduce") \(n) \(src) [\(a.joined(separator: " "))])"
		case .swap, .vswap:
			guard case .call(let n, let a) = f else { return x(src) }
			let (make, swap) = st.wrap == .swap ? ("atom", "swap!") : ("volatile!", "vswap!")
			return "(let [a (\(make) \(src))] (\(([swap, "a", n] + a).joined(separator: " "))) @a)"
		case .seqView, .lazyView:
			guard let c = source else { return x(src) }
			let (unordered, t) = viewText(c)
			let view = st.wrap == .seqView ? "(seq \(src))" : "(map identity \(src))"
			return "(let [s \(view) r \(x(src))] (mt-view \(quoted(tag + "v")) s \(unordered) \(quoted(t))) r)"
		}
	}

	static func quoted(_ s: String) -> String {
		"\"" + s.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"") + "\""
	}

	// nil makes mt-chk rebuild a long expected value from its text, which keeps the program small.
	static func check(_ tag: String, _ e: String, _ c: Coll) -> String {
		let t = text(c)
		let exp = t.utf8.count <= 400 ? t : "nil"
		return "(mt-chk \(quoted(tag)) \(e) \(c.count) \(quoted(t)) \(exp) \(quoted(metaText(metaOf(c)))))"
	}

	// The sequence as top-level forms; tags start with the prefix, which the output's MT-FAIL lines carry.
	static func emit(_ s: Seq, prefix: String) -> String? {
		guard let h = replay(s) else { return nil }
		let global = "mt-g-" + prefix.replacingOccurrences(of: ".", with: "-")
		var out = ""
		if s.viaVar, case .make(let i) = s.steps[0].op { out += "(def \(global) \(i.expr))\n" }
		out += "(mt-run \(quoted(prefix)) (fn []\n  (let ["
		for (i, st) in s.steps.enumerated() {
			let e = i == 0 && s.viaVar ? global : expr(st, tag: "\(prefix).\(i)", source: st.src.map { h[$0] })
			out += (i == 0 ? "" : "\n        ") + "h\(i) " + check("\(prefix).\(i)", e, h[i])
		}
		out += "]\n"
		for j in s.retained.sorted() { out += "    " + check("\(prefix).r\(j)", "h\(j)", h[j]) + "\n" }
		if s.viaVar { out += "    " + check("\(prefix).var", global, h[0]) + "\n" }
		out += "    nil)))\n"
		return out
	}

	struct Batch {
		var source: String
		var seqs: [Seq]
	}

	static func prefix(seed: UInt64, _ i: Int) -> String { "b\(seed).s\(i)" }

	static func batch(prelude: String, seed: UInt64, count: Int) -> Batch {
		var rng = Rng(state: seed)
		var seqs: [Seq] = []
		var source = prelude + "\n"
		for i in 0..<count {
			let s = generate(&rng)
			seqs.append(s)
			source += emit(s, prefix: prefix(seed: seed, i))!
		}
		return Batch(source: source + "(println \"MT-DONE\")\n", seqs: seqs)
	}

	static func program(prelude: String, _ s: Seq, prefix: String) -> String? {
		emit(s, prefix: prefix).map { prelude + "\n" + $0 + "(println \"MT-DONE\")\n" }
	}

	// MARK: generation

	struct Rng {
		var state: UInt64

		mutating func next() -> UInt64 {
			state &+= 0x9E37_79B9_7F4A_7C15
			var z = state
			z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
			z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
			return z ^ (z >> 31)
		}

		mutating func below(_ n: Int) -> Int { Int(next() % UInt64(max(n, 1))) }
		mutating func chance(_ p: Double) -> Bool { Double(next() >> 11) / Double(1 << 53) < p }
		mutating func pick<T>(_ xs: [T]) -> T { xs[below(xs.count)] }

		mutating func weighted(_ table: [(String, Int)]) -> String {
			var r = below(table.reduce(0) { $0 + $1.1 })
			for (name, w) in table {
				if r < w { return name }
				r -= w
			}
			return table[0].0
		}
	}

	static let kws = ["a", "b", "c", "d", "e", "f", "g", "h"]
	// Keys whose `hash` is equal in this runtime, which puts them in one collision node of the trie.
	static let collisions: [V] = [int(0), int(-1), nilV, .scalar("\"k15599\""), .scalar("\"k97211\"")]
	// A value's text bound: the expected text is spelled into the program at every step.
	static let textLimit = 2500
	static let bigTextLimit = 12000

	struct Gen {
		var rng: Rng
		var aliasing: Double
		var handles: [Coll] = []

		mutating func scalar() -> V {
			switch rng.below(20) {
			case 0..<11: return int(rng.below(20))
			case 11..<14: return .scalar(":" + rng.pick(kws))
			case 14: return .scalar("\"s\(rng.below(4))\"")
			case 15: return nilV
			case 16: return int(1_000_000 + rng.below(3))
			case 17: return .scalar(rng.chance(0.5) ? "true" : "false")
			default: return int(rng.below(50))
			}
		}

		mutating func handleArg(_ ok: (Coll) -> Bool) -> Arg? {
			guard aliasing > 0, !handles.isEmpty else { return nil }
			let j = rng.below(handles.count)
			guard text(handles[j]).utf8.count <= 300, ok(handles[j]) else { return nil }
			return .handle(j)
		}

		mutating func smallInts(_ n: Int) -> [V] { (0..<n).map { _ in int(rng.below(10)) } }

		mutating func elem(handles allowed: Bool = true) -> Arg {
			let r = rng.below(100)
			if r < 14, allowed, let h = handleArg({ _ in true }) { return h }
			if r < 18 { return .val(.atom(rng.below(3))) }
			if r < 22 { return .val(.lazy(smallInts(1 + rng.below(3)))) }
			if r < 26 { return .val(.coll(Coll(.vec, smallInts(rng.below(3))))) }
			return .val(scalar())
		}

		mutating func key(_ c: Coll) -> Arg {
			if c.kind.isSorted { return .val(int(rng.below(48))) }
			let r = rng.below(100)
			if r < 8, let h = handleArg({ printUnique(.coll($0)) }) { return h }
			if r < 60 { return .val(.scalar(":" + rng.pick(kws))) }
			if r < 75 { return .val(.scalar(":k\(rng.below(40))")) }
			if r < 84 { return .val(int(rng.below(20))) }
			if r < 87 { return .val(.scalar("\"s\(rng.below(4))\"")) }
			if r < 95 { return .val(rng.pick(collisions)) }
			return .val(.atom(rng.below(3)))
		}

		mutating func present(_ c: Coll) -> Arg {
			if !c.keys.isEmpty && rng.chance(0.7) { return .val(rng.pick(c.keys)) }
			return key(c)
		}

		mutating func index(_ c: Coll, end: Bool) -> Arg {
			if c.count == 0 || (end && rng.chance(0.15)) { return .val(int(c.count)) }
			let i = rng.chance(0.5) ? rng.below(c.count) : max(0, c.count - 1 - rng.below(min(c.count, 3)))
			return .val(int(i))
		}

		mutating func updFn() -> Upd {
			switch rng.below(10) {
			case 0..<4: return .wrap
			case 4..<7: return .const(elem())
			default: return .conj(elem())
			}
		}

		mutating func literalMap(sorted: Bool) -> Arg {
			var m = Coll(.hmap)
			for _ in 0..<(1 + rng.below(3)) {
				let k = sorted ? int(rng.below(48)) : (rng.chance(0.7) ? .scalar(":" + rng.pick(kws)) : int(rng.below(20)))
				m = assoc(m, k, scalar())!
			}
			return .val(.coll(m))
		}

		mutating func source(_ c: Coll) -> Arg {
			if rng.chance(0.5), let h = handleArg({ seqItems($0) != nil || c.kind.isSet || (c.kind.isMap && $0.kind.isMap) }) { return h }
			switch c.kind {
			case .hmap, .smap:
				if rng.chance(0.5) { return literalMap(sorted: c.kind == .smap) }
				let pairs = (0..<(1 + rng.below(3))).map { _ in entry(c.kind == .smap ? int(rng.below(48)) : .scalar(":" + rng.pick(kws)), scalar()) }
				return .val(.coll(Coll(.vec, pairs)))
			case .hset, .sset:
				return .val(.coll(Coll(.vec, (0..<(1 + rng.below(4))).map { _ in c.kind == .sset ? int(rng.below(48)) : scalar() })))
			default:
				return .val(.coll(Coll(.vec, (0..<(1 + rng.below(4))).map { _ in scalar() })))
			}
		}

		mutating func subs(_ c: Coll) -> [Sub] {
			var out: [Sub] = []
			var cur = c
			for _ in 0..<(1 + rng.below(4)) {
				let s: Sub
				switch c.kind {
				case .vec:
					switch rng.below(5) {
					case 0, 1: s = .conj(elem())
					case 2, 3: s = .assoc(index(cur, end: true), elem())
					default: s = .pop
					}
				case .hmap:
					switch rng.below(5) {
					case 0, 1, 2: s = .assoc(key(cur), elem())
					default: s = .dissoc(present(cur))
					}
				default:
					s = rng.below(3) < 2 ? .conj(key(cur)) : .disj(present(cur))
				}
				guard let n = sub(cur, s, handles) else { continue }
				out.append(s)
				cur = n
			}
			return out
		}

		mutating func op(_ c: Coll) -> Op {
			let menu: [(String, Int)]
			switch c.kind {
			case .vec:
				menu = [("conj", 18), ("assoc", 14), ("pop", 10), ("update", 6), ("into", 6), ("subvec", 5), ("withMeta", 4),
				        ("varyMeta", 3), ("empty", 2), ("vec", 2), ("transient", 8), ("get", 3), ("first", 2)]
			case .hmap, .smap:
				menu = [("assoc", 18), ("conjEntry", 5), ("dissoc", 12), ("update", 8), ("updateIn", 5), ("assocIn", 4), ("merge", 6),
				        ("into", 5), ("withMeta", 4), ("varyMeta", 3), ("empty", 2), ("selectKeys", 4), ("transient", c.kind == .hmap ? 8 : 0),
				        ("vec", c.kind == .smap ? 2 : 0), ("get", 4), ("first", c.kind == .smap ? 2 : 0)]
			case .hset, .sset:
				menu = [("conj", 18), ("disj", 12), ("into", 6), ("withMeta", 4), ("varyMeta", 2), ("empty", 2), ("transient", c.kind == .hset ? 6 : 0),
				        ("vec", c.kind == .sset ? 2 : 0)]
			case .list, .queue:
				menu = [("conj", 18), ("pop", 10), ("into", 6), ("withMeta", 5), ("varyMeta", 2), ("empty", 2), ("vec", 3), ("first", 3)]
			}
			switch rng.weighted(menu) {
			case "conj":
				let n = rng.chance(0.25) ? 2 + rng.below(2) : 1
				return .conj((0..<n).map { _ in c.kind.isSet ? key(c) : elem() })
			case "conjEntry": return .conjEntry(key(c), elem())
			case "assoc":
				if c.kind == .vec { return .assoc([(index(c, end: true), elem())]) }
				return .assoc((0..<(rng.chance(0.3) ? 2 : 1)).map { _ in (key(c), elem()) })
			case "dissoc": return .dissoc((0..<(rng.chance(0.3) ? 2 : 1)).map { _ in present(c) })
			case "disj": return .disj((0..<(rng.chance(0.3) ? 2 : 1)).map { _ in present(c) })
			case "pop": return .pop
			case "update": return .update(c.kind == .vec ? index(c, end: false) : present(c), updFn())
			case "updateIn": return .updateIn(present(c), c.kind == .smap || rng.chance(0.2) ? .val(int(rng.below(4))) : .val(.scalar(":" + rng.pick(kws))), updFn())
			case "assocIn": return .assocIn(present(c), c.kind == .smap || rng.chance(0.2) ? .val(int(rng.below(4))) : .val(.scalar(":" + rng.pick(kws))), elem())
			case "merge":
				if rng.chance(0.5), let h = handleArg({ $0.kind.isMap }) { return .merge(h) }
				return .merge(literalMap(sorted: c.kind == .smap))
			case "into": return .into(source(c), rng.pick(Via.allCases))
			case "subvec":
				let a = rng.below(c.count + 1)
				return .subvec(a, a + rng.below(c.count - a + 1))
			case "withMeta": return .withMeta(rng.chance(0.2) ? nil : rng.below(5))
			case "varyMeta": return .varyMeta(rng.below(5))
			case "empty": return .empty
			case "vec": return .vec(rng.pick(Via.allCases))
			case "selectKeys": return .selectKeys((0..<(1 + rng.below(3))).map { _ in present(c) })
			case "get": return .get(c.kind == .vec ? index(c, end: false) : present(c))
			case "first": return .first
			default: return .transient(subs(c))
			}
		}

		mutating func initValue() -> Init {
			switch rng.weighted([("vec", 30), ("hmap", 30), ("hset", 10), ("smap", 8), ("sset", 6), ("list", 8), ("queue", 8)]) {
			case "vec":
				switch rng.below(6) {
				case 0:
					let v = Coll(.vec, (0..<rng.below(7)).map { _ in scalar() })
					return Init(expr: text(v), value: v)
				case 1:
					let xs = (0..<(1 + rng.below(8))).map { _ in elem(handles: false) }
					return Init(expr: "(vector" + xs.map { " " + code($0) }.joined() + ")", value: Coll(.vec, xs.map { value($0, []) }))
				case 2, 3:
					let big = rng.chance(0.15)
					let n = big ? rng.pick([1023, 1024, 1025, 1055, 1056, 1057]) : rng.pick([5, 6, 7, 8, 31, 32, 33, 34, 63, 64, 65, 95, 96, 97])
					let v = Coll(.vec, (0..<n).map(int))
					return Init(expr: rng.chance(0.5) ? "(vec (range \(n)))" : "(into [] (range \(n)))", value: v, big: big)
				case 4:
					let x = elem(handles: false)
					return Init(expr: "(conj [] \(code(x)))", value: Coll(.vec, [value(x, [])]))
				default:
					let n = rng.pick([10, 40])
					let a = rng.below(n), b = a + rng.below(n - a + 1)
					return Init(expr: "(subvec (vec (range \(n))) \(a) \(b))", value: Coll(.vec, (a..<b).map(int)))
				}
			case "hmap":
				switch rng.below(7) {
				case 0, 1:
					var m = Coll(.hmap)
					for k in kws.shuffled(using: &rng).prefix(rng.below(6)) { m = assoc(m, .scalar(":" + k), int(rng.below(20)))! }
					return Init(expr: text(m), value: m)
				case 2:
					var m = Coll(.hmap)
					var expr = "(hash-map"
					for k in kws.shuffled(using: &rng).prefix(1 + rng.below(5)) {
						let v = elem(handles: false)
						m = assoc(m, .scalar(":" + k), value(v, []))!
						expr += " :\(k) \(code(v))"
					}
					return Init(expr: expr + ")", value: m)
				case 3:
					let n = rng.pick([8, 31, 32, 33, 40])
					return Init(expr: "(zipmap (range \(n)) (range \(n)))", value: Coll(.hmap, (0..<n).map(int), (0..<n).map(int)))
				case 4:
					let n = rng.pick([8, 30, 31, 32, 33])
					return Init(expr: "(zipmap (map (fn [i] (keyword (str \"k\" i))) (range \(n))) (range \(n)))",
					            value: Coll(.hmap, (0..<n).map { .scalar(":k\($0)") }, (0..<n).map(int)))
				case 5:
					var m = Coll(.hmap, [.scalar(":a")], [int(1)])
					m.meta = 0
					return Init(expr: "(with-meta {:a 1} {:m 0})", value: m)
				default:
					var m = Coll(.hmap)
					for k in [int(1), int(2), .scalar("\"s0\""), nilV].shuffled(using: &rng).prefix(1 + rng.below(3)) { m = assoc(m, k, scalar())! }
					return Init(expr: "(into {} [" + zip(m.keys, m.vals).map { "[\(text($0)) \(text($1))]" }.joined(separator: " ") + "])", value: m)
				}
			case "hset":
				switch rng.below(3) {
				case 0:
					var s = Coll(.hset)
					for _ in 0..<rng.below(5) { s = conj1(s, scalar())! }
					return Init(expr: text(s), value: s)
				case 1:
					var s = Coll(.hset)
					for _ in 0..<(1 + rng.below(4)) { s = conj1(s, .scalar(":" + rng.pick(kws)))! }
					return Init(expr: "(hash-set" + s.keys.map { " " + text($0) }.joined() + ")", value: s)
				default:
					let n = rng.pick([3, 33])
					return Init(expr: "(set (range \(n)))", value: Coll(.hset, (0..<n).map(int)))
				}
			case "smap":
				if rng.chance(0.5) {
					let n = rng.pick([5, 40])
					return Init(expr: "(into (sorted-map) (zipmap (range \(n)) (range \(n))))", value: Coll(.smap, (0..<n).map(int), (0..<n).map(int)))
				}
				var m = Coll(.smap)
				for _ in 0..<rng.below(5) { m = assoc(m, int(rng.below(48)), scalar())! }
				return Init(expr: text(m), value: m)
			case "sset":
				if rng.chance(0.5) {
					let n = rng.pick([5, 40])
					return Init(expr: "(apply sorted-set (range \(n)))", value: Coll(.sset, (0..<n).map(int)))
				}
				var s = Coll(.sset)
				for _ in 0..<rng.below(5) { s = conj1(s, int(rng.below(48)))! }
				return Init(expr: text(s), value: s)
			case "list":
				let l = Coll(.list, (0..<rng.below(5)).map { _ in scalar() })
				return Init(expr: text(l), value: l)
			default:
				let q = Coll(.queue, (0..<rng.below(5)).map { _ in scalar() })
				return Init(expr: text(q), value: q)
			}
		}
	}

	static func generate(_ rng: inout Rng) -> Seq {
		var g = Gen(rng: rng, aliasing: rng.pick([0.0, 0.25, 0.5]))
		let first = g.initValue()
		var steps = [Step(op: .make(first), src: nil, wrap: .plain)]
		g.handles = [first.value]
		let limit = first.big ? bigTextLimit : textLimit
		// A frame marks last uses in its first 64 slots only (NOTES "Analyzer and evaluator").
		let length = first.big ? 2 + g.rng.below(5) : 3 + g.rng.below(16)
		while steps.count < length {
			if g.rng.chance(0.08) {
				let i = g.initValue()
				guard !i.big else { continue }
				steps.append(Step(op: .make(i), src: nil, wrap: .plain))
				g.handles.append(i.value)
				continue
			}
			let src = g.aliasing > 0 && g.rng.chance(g.aliasing) ? g.rng.below(g.handles.count) : g.handles.count - 1
			var made: (Op, Coll)?
			for _ in 0..<8 {
				let op = g.op(g.handles[src])
				if let r = apply(op, g.handles[src], g.handles), text(r).utf8.count <= limit {
					made = (op, r)
					break
				}
			}
			if made == nil {
				let op = Op.withMeta(g.rng.below(5))
				made = (op, apply(op, g.handles[src], g.handles)!)
			}
			let (op, r) = made!
			let wrap = g.rng.chance(0.55) ? .plain : g.rng.pick(wraps(op))
			steps.append(Step(op: op, src: src, wrap: wrap))
			g.handles.append(r)
		}
		let retained = Set(g.handles.indices.filter { _ in g.rng.chance(g.aliasing) })
		let viaVar = g.rng.chance(0.12)
		rng = g.rng
		return Seq(steps: steps, retained: retained, viaVar: viaVar)
	}

	// MARK: shrinking

	// Handle i's readers are pointed at its source, so the rest of the sequence keeps its shape.
	static func dropping(_ s: Seq, _ i: Int) -> Seq? {
		guard s.steps.count > 1 else { return nil }
		let to = s.steps[i].src
		func move(_ j: Int) -> Int? {
			if j == i { return to }
			return j > i ? j - 1 : j
		}
		var steps: [Step] = []
		for (k, st) in s.steps.enumerated() where k != i {
			var n = st
			if let j = st.src {
				guard let m = move(j) else { return nil }
				n.src = m
			}
			guard let op = remap(st.op, move) else { return nil }
			n.op = op
			steps.append(n)
		}
		var retained = Set<Int>()
		for j in s.retained {
			if let m = move(j) { retained.insert(m) }
		}
		return Seq(steps: steps, retained: retained, viaVar: s.viaVar && i != 0)
	}

	static func candidates(_ s: Seq) -> [Seq] {
		var out: [Seq] = []
		for i in s.steps.indices.reversed() {
			if let d = dropping(s, i) { out.append(d) }
		}
		for j in s.retained.sorted() {
			var n = s
			n.retained.remove(j)
			out.append(n)
		}
		if s.viaVar {
			var n = s
			n.viaVar = false
			out.append(n)
		}
		for (i, st) in s.steps.enumerated() {
			if st.wrap != .plain {
				var n = s
				n.steps[i].wrap = .plain
				out.append(n)
			}
			var simpler: [Op] = []
			switch st.op {
			case .conj(let xs) where xs.count > 1: simpler = xs.map { .conj([$0]) }
			case .assoc(let kvs) where kvs.count > 1: simpler = kvs.map { .assoc([$0]) }
			case .dissoc(let ks) where ks.count > 1: simpler = ks.map { .dissoc([$0]) }
			case .disj(let xs) where xs.count > 1: simpler = xs.map { .disj([$0]) }
			case .selectKeys(let ks) where ks.count > 1: simpler = ks.map { .selectKeys([$0]) }
			case .transient(let subs) where subs.count > 1:
				simpler = subs.indices.map { k in .transient(subs.enumerated().filter { $0.offset != k }.map(\.element)) }
			default: break
			}
			for op in simpler {
				var n = s
				n.steps[i].op = op
				if !wraps(op).contains(n.steps[i].wrap) { n.steps[i].wrap = .plain }
				out.append(n)
			}
		}
		return out.filter { replay($0) != nil }
	}

	// Greedy: the first candidate that still fails replaces the sequence, until none does or the budget is spent.
	static func shrink(_ s: Seq, budget: Int, fails: (Seq) -> Bool) -> Seq {
		var cur = s
		var spent = 0
		var progress = true
		while progress && spent < budget {
			progress = false
			for c in candidates(cur) {
				if spent >= budget { break }
				spent += 1
				if fails(c) {
					cur = c
					progress = true
					break
				}
			}
		}
		return cur
	}
}

extension Model.Rng: RandomNumberGenerator {}
