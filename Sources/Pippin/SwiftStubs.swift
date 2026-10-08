// @ai-generated(solo)
import CljCore
import Foundation

/// Level 2 of the bridge (design §5 «Объявленная граница»): what generated Swift stubs register with and call into.
public enum SwiftStubs {
	/// One Swift function, member or accessor as the generator printed it; a nil label is `_`. An instance member's
	/// receiver is its first argument, unlabelled.
	public struct Function: Sendable {
		public let swiftName: String
		/// The Swift type a member belongs to, spelled as in Swift (`Point`, `Outer.Inner`); nil for a free function.
		public let owner: String?
		/// What the var is spelled from: the base name, `setName` for a setter, empty for an initializer.
		public let base: String
		public let labels: [String?]
		/// What the doc adds after the name: `async`, `throws(FixtureError)`.
		public let effects: String
		let call: Call

		enum Call: Sendable {
			case plain(@Sendable ([Value]) throws -> Value)
			// Decodes on the caller's thread and answers the isolated call itself.
			case mainActor(@Sendable ([Value]) throws -> @MainActor @Sendable () throws -> Value)
			// Decodes on the caller's thread; the call runs on a Task the caller parks on.
			case async(@Sendable ([Value]) throws -> @Sendable () async throws -> Value)
		}

		/// A nonisolated synchronous function: the body runs on the caller's thread.
		public init(swiftName: String, owner: String? = nil, base: String? = nil, labels: [String?], effects: String = "",
		            _ body: @escaping @Sendable ([Value]) throws -> Value) {
			self.init(swiftName, owner, base, labels, effects, .plain(body))
		}

		/// A `@MainActor` function: `prepare` decodes the arguments where the caller is and returns the call,
		/// which runs in place on the main thread and after a hop from anywhere else (design §5).
		public init(swiftName: String, owner: String? = nil, base: String? = nil, labels: [String?], effects: String = "",
		            mainActor prepare: @escaping @Sendable ([Value]) throws -> @MainActor @Sendable () throws -> Value) {
			self.init(swiftName, owner, base, labels, effects, .mainActor(prepare))
		}

		/// An `async` function: the caller parks on a Task, and cancelling the parked caller cancels it (design §5).
		public init(swiftName: String, owner: String? = nil, base: String? = nil, labels: [String?], effects: String = "",
		            async prepare: @escaping @Sendable ([Value]) throws -> @Sendable () async throws -> Value) {
			self.init(swiftName, owner, base, labels, effects, .async(prepare))
		}

		private init(_ swiftName: String, _ owner: String?, _ base: String?, _ labels: [String?], _ effects: String, _ call: Call) {
			self.swiftName = swiftName
			self.owner = owner
			self.base = base ?? String(swiftName.prefix { $0 != "(" })
			self.labels = labels
			self.effects = effects
			self.call = call
		}

		/// `make-point`, `Point.scaled`, `Point.` for an initializer (design §5 «Как пишется вызов»).
		var varName: String { (owner.map { "\($0)." } ?? "") + SwiftStubs.kebab(base) }
		var parks: Bool { if case .plain = call { false } else { true } }
		var signature: String {
			let isolation = if case .mainActor = call { " @MainActor" } else { "" }
			return swiftName + isolation + (effects.isEmpty ? "" : " \(effects)")
		}
	}

	/// A public symbol the generator did not bridge, and why: the report design §5 makes part of the product.
	public struct Refusal: Sendable, Equatable, CustomStringConvertible {
		public let swiftName: String
		public let reason: String

		public init(swiftName: String, reason: String) {
			self.swiftName = swiftName
			self.reason = reason
		}

		public var description: String { "\(swiftName): \(reason)" }
	}

	/// Where `require-swift` gets the stubs of a module nothing registered yet (design §5, the dev path): it runs
	/// `scripts/swift-stubgen.py`, which caches by fingerprint, and `dlopen`s the dylib it names. A host that
	/// links its stubs in (the production path) registers them at start and leaves this nil.
	public struct Generator: Sendable {
		/// `scripts/swift-stubgen.py`.
		public var script: URL
		/// Where the generator keeps one directory per fingerprint.
		public var cache: URL
		/// `-I` for the bridged modules' `.swiftmodule`.
		public var moduleSearchPaths: [URL]
		/// `-L` and `-l` for the libraries holding the bridged modules' code.
		public var librarySearchPaths: [URL]
		public var libraries: [String]
		/// The directory holding the running runtime's `Pippin.swiftmodule`: a stub is built against it.
		public var runtimeModules: URL
		/// Module maps of the C modules `Pippin` imports (`CljCore`).
		public var moduleMaps: [URL]

		public init(script: URL, cache: URL, moduleSearchPaths: [URL], librarySearchPaths: [URL] = [], libraries: [String] = [],
		            runtimeModules: URL, moduleMaps: [URL]) {
			self.script = script
			self.cache = cache
			self.moduleSearchPaths = moduleSearchPaths
			self.librarySearchPaths = librarySearchPaths
			self.libraries = libraries
			self.runtimeModules = runtimeModules
			self.moduleMaps = moduleMaps
		}
	}

	/// The generator's run failed; `output` is what it printed.
	public struct GeneratorFailed: Error, CustomStringConvertible {
		public let module: String
		public let output: String
		public var description: String { "The stub generator failed for Swift module \(module):\n\(output)" }
	}

	/// The dev path's configuration; nil leaves `require-swift` to modules already registered.
	public static var generator: Generator? {
		get { registry.withLock { registry.generator } }
		set { registry.withLock { registry.generator = newValue } }
	}

	/// Called by a stub dylib's entry: a var per name in the module's namespace picks the overload by labels.
	public static func register(module: String, functions: [Function], refusals: [Refusal]) {
		var order: [String] = []
		var groups: [String: [Function]] = [:]
		for f in functions {
			if groups[f.varName] == nil { order.append(f.varName) }
			groups[f.varName, default: []].append(f)
		}
		var refused = refusals
		for name in order {
			var overloads = groups[name]!.map(Overload.init)
			// All refused, none picked (design §5); the generator refuses those it sees, kebab spelling makes the rest.
			let clashes = Dictionary(grouping: overloads, by: \.spelling).filter { $0.value.count > 1 }
			for (shape, clash) in clashes {
				let names = clash.map(\.function.swiftName).joined(separator: ", ")
				refused += clash.map {
					Refusal(swiftName: $0.function.swiftName,
					        reason: "overload: \(module)/\(name) \(shape) would name each of \(names), and labels cannot tell them apart (design §5)")
				}
			}
			overloads.removeAll { clashes[$0.spelling] != nil }
			if overloads.isEmpty { continue }
			let doc = overloads.map(\.function.signature).joined(separator: "\n")
			Runtime.bind(name, in: module, doc: "Swift: \(doc)", dispatcher("\(module)/\(name)", overloads))
		}
		registry.withLock { registry.modules[module] = refused }
	}

	/// What the generator reported as not bridged for a registered module; nil for a module not registered.
	public static func refusals(of module: String) -> [Refusal]? {
		registry.withLock { registry.modules[module] }
	}

	/// Makes the module's namespace exist with its stubs bound: `require-swift`'s host side.
	public static func load(_ module: String) throws {
		try registry.loading.withLock {
			if refusals(of: module) != nil { return }
			guard let generator else {
				throw ClojureError(thrown: Value(exInfo: "No stubs for Swift module \(module): nothing registered them and no generator is configured (SwiftStubs.generator, design §5)"))
			}
			let dylib = try generate(module, with: generator)
			guard let handle = dlopen(dylib, RTLD_NOW | RTLD_LOCAL) else {
				throw ClojureError(thrown: Value(exInfo: "dlopen of the stubs of \(module) failed: \(String(cString: dlerror()))"))
			}
			let entry = "pippin_stubs_register_\(module)"
			guard let symbol = dlsym(handle, entry) else {
				throw ClojureError(thrown: Value(exInfo: "\(dylib) has no \(entry)"))
			}
			unsafeBitCast(symbol, to: (@convention(c) () -> Void).self)()
			if refusals(of: module) == nil {
				throw ClojureError(thrown: Value(exInfo: "\(entry) returned without registering \(module)"))
			}
		}
	}

#if os(macOS) || os(Linux)
	// The last line of the generator's output is the dylib; everything it printed is the error when it fails.
	private static func generate(_ module: String, with g: Generator) throws -> String {
		var args = [g.script.path, "--module", module, "--cache", g.cache.path, "--runtime-modules", g.runtimeModules.path]
		for p in g.moduleSearchPaths { args += ["-I", p.path] }
		for p in g.librarySearchPaths { args += ["-L", p.path] }
		for l in g.libraries { args += ["-l", l] }
		for m in g.moduleMaps { args += ["--module-map", m.path] }
		let process = Process()
		process.executableURL = URL(fileURLWithPath: "/usr/bin/env")
		process.arguments = ["python3"] + args
		// The host's DYLD_* variables would reach swiftc and every library it loads; DEVELOPER_DIR picks the Xcode.
		let env = ProcessInfo.processInfo.environment
		process.environment = env.filter { ["PATH", "HOME", "TMPDIR", "DEVELOPER_DIR"].contains($0.key) }
		let out = Pipe()
		process.standardOutput = out
		process.standardError = out
		try process.run()
		let data = out.fileHandleForReading.readDataToEndOfFile()
		process.waitUntilExit()
		let text = String(decoding: data, as: UTF8.self)
		guard process.terminationStatus == 0,
		      let last = text.split(separator: "\n").last, last.hasSuffix(".dylib") else {
			throw GeneratorFailed(module: module, output: text)
		}
		return String(last)
	}
#else
	// iOS has no Foundation `Process`; a device links its stubs in instead (docs/portability.md).
	private static func generate(_ module: String, with _: Generator) throws -> String {
		throw GeneratorFailed(module: module, output: "The stub generator needs a subprocess, which this platform has no API for; link the module's stubs in instead (design §5, the production path)")
	}
#endif

	// MARK: Calls

	struct Overload: Sendable {
		let function: Function
		let labels: [Value?]

		init(_ f: Function) {
			function = f
			labels = f.labels.map { $0.map { Value(keyword: SwiftStubs.kebab($0)) } }
		}

		var arity: Int { labels.reduce(0) { $0 + ($1 == nil ? 1 : 2) } }

		// The positional values when the arguments carry exactly this overload's labels, in order.
		func values(_ args: [Value]) -> [Value]? {
			guard args.count == arity else { return nil }
			var out: [Value] = []
			var i = 0
			for label in labels {
				if let label {
					guard args[i] == label else { return nil }
					i += 1
				}
				out.append(args[i])
				i += 1
			}
			return out
		}

		var spelling: String { "(\(labels.map { $0.map { ":\(String(describing: $0).dropFirst())" } ?? "_" }.joined(separator: " ")))" }
	}

	// Only a var with a parking overload (isolated, async) pays for `host-async-fn`, the frame allowed to park.
	private static func dispatcher(_ name: String, _ overloads: [Overload]) -> Value {
		func pick(_ args: [Value]) throws -> (Overload, [Value]) {
			for o in overloads { if let v = o.values(args) { return (o, v) } }
			let shown = args.map { clj_is_keyword($0.raw) ? $0.description : "_" }.joined(separator: " ")
			let known = overloads.map { "\($0.function.swiftName) \($0.spelling)" }.joined(separator: ", ")
			throw ClojureError(thrown: Value(exInfo: "No overload of \(name) takes (\(shown)); it has \(known)"))
		}
		guard overloads.contains(where: \.function.parks) else {
			return Value(function: name) { args in
				let (o, values) = try pick(args)
				guard case .plain(let body) = o.function.call else { preconditionFailure("a parking overload in a plain dispatcher") }
				return try body(values)
			}
		}
		return Value.awaiting(Value(function: name) { args in
			let (o, values) = try pick(args)
			switch o.function.call {
			case .plain(let body):
				return Value([Value(true), try body(values)])
			case .mainActor(let prepare):
				let call = try prepare(values)
				if Thread.isMainThread { return Value([Value(true), try MainActor.assumeIsolated { try call() }]) }
				try refuseParkWhereIllegal("\(o.function.swiftName) is @MainActor and the caller is off the main thread, so the call hops and parks")
				registry.withLock { registry.hops += 1 }
				return Value.pendingCall { try await MainActor.run { try call() } }
			case .async(let prepare):
				let call = try prepare(values)
				try refuseParkWhereIllegal("\(o.function.swiftName) is async, so the caller parks until it returns")
				return Value.pendingCall { try await call() }
			}
		})
	}

	// Asked before anything Swift runs: a call the caller could not wait for would have run regardless.
	private static func refuseParkWhereIllegal(_ what: String) throws {
		if clj_host_park_allowed() { return }
		let why = Value(owning: clj_take_pending())
		_ = Value(owning: clj_take_pending_trace())
		if why.isCancellation { throw ClojureError(thrown: why) }
		let reason = ClojureError(thrown: why).message
		throw ClojureError(thrown: Value(exInfo: "\(what); here it cannot: \(reason)"))
	}

	static func kebab(_ name: String) -> String {
		name.withCString { text in
			let want = clj_objc_kebab(text, nil, 0)
			var buf = [CChar](repeating: 0, count: want + 1)
			_ = clj_objc_kebab(text, &buf, buf.count)
			return String(decoding: buf.prefix(want).map { UInt8(bitPattern: $0) }, as: UTF8.self)
		}
	}

	/// The value a call with `inout` parameters answers (design §5 «`mutating`, `inout`»): their new values in
	/// declaration order, `self` first, then the result unless it is `Void`; one value as is, several as a vector.
	public static func outcome(_ values: [Value]) -> Value {
		switch values.count {
		case 0: .nil_
		case 1: values[0]
		default: Value(values)
		}
	}

	// MARK: Composite forms (design §5 «Опционалы, коллекции, кортежи»): a generated stub passes each part's own
	// conversion, so an element crosses inside a collection as it would alone.

	/// nil is Swift's nil; anything else is the wrapped type's.
	public static func optional<T>(_ v: Value, _ decode: (Value) throws -> T) rethrows -> T? {
		v.isNil ? nil : try decode(v)
	}

	public static func optional<T>(_ v: T?, _ encode: (T) -> Value) -> Value { v.map(encode) ?? .nil_ }

	/// A vector or any seq, realized: the copy design §5 accepts for a collection of values at level 2.
	public static func array<T>(_ v: Value, _ decode: (Value) throws -> T) throws -> [T] {
		guard let items = v.array ?? v.list else { throw ValueTypeMismatch(value: v, expected: "vector or seq") }
		return try items.map(decode)
	}

	public static func vector<T>(_ v: [T], _ encode: (T) -> Value) -> Value { Value(v.map(encode)) }

	/// A set only: a vector with repeats would collapse silently.
	public static func set<T: Hashable>(_ v: Value, _ decode: (Value) throws -> T) throws -> Set<T> {
		guard clj_is_set(v.raw) else { throw ValueTypeMismatch(value: v, expected: "set") }
		var items: [Value] = []
		withExtendedLifetime(v) {
			withUnsafeMutablePointer(to: &items) { p in
				clj_set_each(v.raw, { item, ctx in
					ctx!.assumingMemoryBound(to: [Value].self).pointee.append(Value(borrowing: item))
					return true
				}, p)
			}
		}
		var out = Set<T>(minimumCapacity: items.count)
		for item in items {
			if !out.insert(try decode(item)).inserted {
				throw ClojureError(thrown: Value(exInfo: "Two elements of \(v) are one \(T.self): a Swift Set would drop one"))
			}
		}
		return out
	}

	public static func hashSet<T>(_ v: Set<T>, _ encode: (T) -> Value) -> Value {
		let items = v.map(encode)
		let set = Value(owning: withExtendedLifetime(items) {
			items.map(\.raw).withUnsafeBufferPointer { clj_set_from_array($0.baseAddress, $0.count) }
		})
		// Each element's conversion is faithful, so distinct Swift elements stay distinct Clojure values.
		precondition(clj_set_count(set.raw) == UInt32(v.count), "two elements of a Swift Set became one Clojure value")
		return set
	}

	/// Two keys of the map that decode to one Swift key are an error, not "the last one wins".
	public static func dictionary<K: Hashable, V>(_ v: Value, key: (Value) throws -> K, value: (Value) throws -> V) throws -> [K: V] {
		guard let entries = v.dictionary else { throw ValueTypeMismatch(value: v, expected: "map") }
		var out: [K: V] = [:]
		out.reserveCapacity(entries.count)
		for (k, val) in entries {
			if out.updateValue(try value(val), forKey: try key(k)) != nil {
				throw ClojureError(thrown: Value(exInfo: "Two keys of \(v) are one \(K.self): a Swift Dictionary would drop one"))
			}
		}
		return out
	}

	public static func map<K, V>(_ v: [K: V], key: (K) -> Value, value: (V) -> Value) -> Value {
		var items: [Value] = []
		items.reserveCapacity(v.count * 2)
		for (k, val) in v {
			items.append(key(k))
			items.append(value(val))
		}
		var dup: UInt32 = 0
		let map = Value(owning: withExtendedLifetime(items) {
			items.map(\.raw).withUnsafeBufferPointer { clj_map_from_items($0.baseAddress, UInt32($0.count), &dup) }
		})
		precondition(map.raw != CLJ_UNBOUND, "two keys of a Swift Dictionary became one Clojure value")
		return map
	}

	/// An unlabelled tuple is a vector of exactly its arity.
	public static func tuple<T>(_ v: Value, count: Int, _ decode: ([Value]) throws -> T) throws -> T {
		guard let items = v.array, items.count == count else {
			throw ValueTypeMismatch(value: v, expected: "vector of \(count)")
		}
		return try decode(items)
	}

	public static func vector<T>(tuple v: T, _ encode: (T) -> [Value]) -> Value { Value(encode(v)) }

	/// An enum's cases or a map struct's fields as kebab keywords (design §5 «Перечисление кейвордом, структура мапой»).
	public final class Keys: Sendable {
		let type: String
		let keys: [Value]
		// Two Swift names may kebab alike (`url`, `URL`); such a type refuses every crossing rather than pick one.
		let clash: String?

		public init(type: String, _ names: [String]) {
			self.type = type
			keys = names.map { Value(keyword: SwiftStubs.kebab($0)) }
			let spelled = Dictionary(grouping: names, by: SwiftStubs.kebab).filter { $0.value.count > 1 }
			clash = spelled.first.map { ":\($0.key) spells \($0.value.joined(separator: " and "))" }
		}

		public subscript(i: Int) -> Value { keys[i] }

		private var listed: String { keys.map(\.description).joined(separator: " ") }

		private func checkClash() throws {
			if let clash { throw ClojureError(thrown: Value(exInfo: "\(type) has no keyword form: \(clash)")) }
		}

		/// The case a keyword names; anything else is an error listing the cases.
		public func index(of v: Value) throws -> Int {
			try checkClash()
			if let i = keys.firstIndex(of: v) { return i }
			throw ClojureError(thrown: Value(exInfo: "\(v) is not a case of \(type): \(listed)"))
		}

		/// A map of these keys in order.
		public func map(_ values: [Value]) -> Value {
			precondition(clash == nil, "\(type) has no keyword form")
			var items: [Value] = []
			items.reserveCapacity(values.count * 2)
			for (k, v) in zip(keys, values) {
				items.append(k)
				items.append(v)
			}
			return Value(owning: withExtendedLifetime(items) {
				items.map(\.raw).withUnsafeBufferPointer { clj_map_from_items($0.baseAddress, UInt32($0.count), nil) }
			})
		}

		/// The map is closed: an unknown key is an error, and so is a missing one unless its field is optional.
		public func fields(of v: Value, required: [Bool]) throws -> [Value] {
			try checkClash()
			guard let entries = v.dictionary else { throw ValueTypeMismatch(value: v, expected: "map of \(type)") }
			if let extra = entries.keys.first(where: { !keys.contains($0) }) {
				throw ClojureError(thrown: Value(exInfo: "\(extra) is not a key of \(type): its keys are \(listed)"))
			}
			return try zip(keys, required).map { key, needed in
				if let value = entries[key] { return value }
				if needed { throw ClojureError(thrown: Value(exInfo: "\(type) needs \(key), which the map lacks")) }
				return .nil_
			}
		}
	}

	// MARK: Boxes

	/// A struct as a Clojure value, boxed with a copy (design §5): `=` and `hash` are the type's own `==` and
	/// `hash(into:)`; the overload `swiftc` picks from the type's conformances decides which a box has.
	public static func box<T: Hashable>(_ value: T) -> Value { make(value, BoxKind<T>.hashable) }
	/// A box with the type's `==`; it refuses to be a map key or a set member.
	public static func box<T: Equatable>(_ value: T) -> Value { make(value, BoxKind<T>.equatable) }
	/// A box with neither: it refuses to be a key, and `=` of two distinct ones refuses.
	public static func box<T>(_ value: T) -> Value { make(value, BoxKind<T>.plain) }

	/// The struct inside a box of `T`; anything else throws. The value is the one boxed, not a rebuilt copy.
	public static func unbox<T>(_ value: Value, as type: T.Type) throws -> T {
		let descriptor = registry.withLock { registry.boxTypes[ObjectIdentifier(type)]?.descriptor }
		guard let descriptor, clj_is_host_box(value.raw), clj_host_box_type(value.raw) == descriptor else {
			throw ValueTypeMismatch(value: value, expected: String(reflecting: type))
		}
		return withExtendedLifetime(value) {
			(Unmanaged<AnyObject>.fromOpaque(clj_host_box_payload(value.raw)!).takeUnretainedValue() as! Boxed<T>).value
		}
	}

	/// A class instance as a Clojure value: the box holds the object itself, typed by its dynamic class, so one
	/// object is one kind of box whatever static type it crossed under (design §5 «Экземпляр класса»).
	public static func box<T: AnyObject>(object value: T) -> Value {
		let dynamic: AnyClass = type(of: value)
		let id = ObjectIdentifier(dynamic)
		let kind = registry.withLock {
			registry.objectTypes[id] ?? {
				let k = ObjectBoxKind(dynamic)
				registry.objectTypes[id] = k
				registry.objectDescriptors.insert(k.descriptor)
				return k
			}()
		}
		return Value(owning: clj_host_box_new(kind.descriptor, Unmanaged.passRetained(value as AnyObject).toOpaque()))
	}

	/// The object inside a class box when it is a `T`, a subclass included; anything else throws.
	public static func unbox<T: AnyObject>(object value: Value, as type: T.Type) throws -> T {
		let ours = clj_is_host_box(value.raw) && registry.withLock { registry.objectDescriptors.contains(clj_host_box_type(value.raw)) }
		let object = ours ? withExtendedLifetime(value) {
			Unmanaged<AnyObject>.fromOpaque(clj_host_box_payload(value.raw)!).takeUnretainedValue() as? T
		} : nil
		guard let object else { throw ValueTypeMismatch(value: value, expected: String(reflecting: type)) }
		return object
	}

	private static func make<T>(_ value: T, _ kind: @autoclosure () -> BoxKind<T>) -> Value {
		let id = ObjectIdentifier(T.self)
		let type = registry.withLock { registry.boxTypes[id] ?? { let t = kind(); registry.boxTypes[id] = t; return t }() }
		let payload = Unmanaged.passRetained(Boxed(value) as AnyObject).toOpaque()
		return Value(owning: clj_host_box_new(type.descriptor, payload))
	}

	static var hops: Int { registry.withLock { registry.hops } }

	private static let registry = Registry()

	private final class Registry: @unchecked Sendable {
		private let lock = NSLock()
		// Held across a load: the generator and dlopen run once per module even when two callers race.
		let loading = NSLock()
		var modules: [String: [Refusal]] = [:]
		var boxTypes: [ObjectIdentifier: AnyBoxKind] = [:]
		// Keyed by the dynamic class; the descriptors tell a class box from a struct box of the same payload shape.
		var objectTypes: [ObjectIdentifier: ObjectBoxKind] = [:]
		var objectDescriptors: Set<UnsafePointer<clj_type>> = []
		var generator: Generator?
		var hops = 0

		func withLock<R>(_ body: () throws -> R) rethrows -> R {
			lock.lock()
			defer { lock.unlock() }
			return try body()
		}
	}
}

private final class Boxed<T>: @unchecked Sendable {
	let value: T
	init(_ value: T) { self.value = value }
}

// One per boxed Swift type, never freed: the core's descriptor points at it for the life of the process.
private class AnyBoxKind: @unchecked Sendable {
	var descriptor: UnsafePointer<clj_type>!

	func equals(_ a: AnyObject, _ b: AnyObject) -> Bool { preconditionFailure("no equality") }
	func hash(_ a: AnyObject) -> UInt32 { preconditionFailure("no hash") }
	func describe(_ a: AnyObject) -> String { preconditionFailure("abstract") }

	fileprivate init(name: String, equatable: Bool, hashable: Bool) {
		var ops = clj_host_box_ops()
		ops.release = { _, payload in Unmanaged<AnyObject>.fromOpaque(payload!).release() }
		ops.describe = { ctx, payload in
			let text = Value(AnyBoxKind.of(ctx).describe(AnyBoxKind.object(payload)))
			return withExtendedLifetime(text) { clj_retain(text.raw) }
		}
		if equatable { ops.equals = { ctx, a, b in AnyBoxKind.of(ctx).equals(AnyBoxKind.object(a), AnyBoxKind.object(b)) } }
		if hashable { ops.hash = { ctx, payload in AnyBoxKind.of(ctx).hash(AnyBoxKind.object(payload)) } }
		let ctx = Unmanaged.passRetained(self).toOpaque()
		descriptor = name.withCString { n in withUnsafePointer(to: ops) { clj_host_box_type_new(n, $0, ctx) } }
	}

	static func of(_ ctx: UnsafeMutableRawPointer?) -> AnyBoxKind { Unmanaged<AnyBoxKind>.fromOpaque(ctx!).takeUnretainedValue() }
	static func object(_ payload: UnsafeMutableRawPointer?) -> AnyObject { Unmanaged<AnyObject>.fromOpaque(payload!).takeUnretainedValue() }

	static func fold(_ hashValue: Int) -> UInt32 {
		let h = UInt64(UInt(bitPattern: hashValue))
		return UInt32(truncatingIfNeeded: h ^ (h >> 32))
	}
}

private final class BoxKind<T>: AnyBoxKind, @unchecked Sendable {
	private let eq: ((T, T) -> Bool)?
	private let hasher: ((T) -> Int)?

	private init(eq: ((T, T) -> Bool)?, hasher: ((T) -> Int)?) {
		self.eq = eq
		self.hasher = hasher
		super.init(name: String(reflecting: T.self), equatable: eq != nil, hashable: hasher != nil)
	}

	static var plain: BoxKind { BoxKind(eq: nil, hasher: nil) }

	override func equals(_ a: AnyObject, _ b: AnyObject) -> Bool { eq!((a as! Boxed<T>).value, (b as! Boxed<T>).value) }
	override func hash(_ a: AnyObject) -> UInt32 { Self.fold(hasher!((a as! Boxed<T>).value)) }
	override func describe(_ a: AnyObject) -> String { String(describing: (a as! Boxed<T>).value) }
}

extension BoxKind where T: Equatable {
	static var equatable: BoxKind { BoxKind(eq: ==, hasher: nil) }
}

extension BoxKind where T: Hashable {
	static var hashable: BoxKind { BoxKind(eq: ==, hasher: { $0.hashValue }) }
}

// A class's conformances are read off its dynamic class at run time: the static type at a stub may be a superclass.
// Both objects of an equals call have this one dynamic class, since boxes of two descriptors are never compared.
private final class ObjectBoxKind: AnyBoxKind, @unchecked Sendable {
	private let eq: (AnyObject, AnyObject) -> Bool
	private let hasher: ((AnyObject) -> Int)?

	init(_ type: AnyClass) {
		if let hashable = type as? any Hashable.Type {
			(eq, hasher) = Self.hashing(hashable)
		} else if let equatable = type as? any Equatable.Type {
			(eq, hasher) = (Self.comparing(equatable), nil)
		} else {
			// The same object, as level 1 compares Objective-C objects: a fact about a reference, not about the box.
			(eq, hasher) = ({ $0 === $1 }, { ObjectIdentifier($0).hashValue })
		}
		super.init(name: String(reflecting: type), equatable: true, hashable: hasher != nil)
	}

	private static func hashing<H: Hashable>(_: H.Type) -> ((AnyObject, AnyObject) -> Bool, (AnyObject) -> Int) {
		({ ($0 as! H) == ($1 as! H) }, { ($0 as! H).hashValue })
	}

	private static func comparing<E: Equatable>(_: E.Type) -> (AnyObject, AnyObject) -> Bool { { ($0 as! E) == ($1 as! E) } }

	override func equals(_ a: AnyObject, _ b: AnyObject) -> Bool { eq(a, b) }
	override func hash(_ a: AnyObject) -> UInt32 { Self.fold(hasher!(a)) }
	override func describe(_ a: AnyObject) -> String { String(describing: a) }
}

// The C side of `require-swift`: installed by `clj_host_boot`, so every Swift host has it.
func cljHostModuleLoad(_ module: clj_value) -> clj_value {
	let name = Value(borrowing: module).description
	do {
		try SwiftStubs.load(name)
		return CLJ_NIL
	} catch {
		return Value.throwing(error)
	}
}

extension Runtime {
	static func installSwiftStubs() { clj_host_module_install(cljHostModuleLoad) }
}
