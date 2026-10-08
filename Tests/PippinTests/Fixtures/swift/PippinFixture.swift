// @ai-generated(solo)
// The Swift module SwiftStubTests bridges, built by the test with swiftc as an app's own module would be.
import Foundation

/// A box: no stored state is public, so no shape crosses as a map. `serial` tells this value from an equal one.
public struct Point: Hashable, CustomStringConvertible {
	var x: Int
	var y: Int
	let serial: Int

	public init(x: Int, y: Int) {
		self.x = x
		self.y = y
		serial = Ledger.shared.next()
	}

	public init(validating x: Int) throws {
		guard x >= 0 else { throw FixtureError.negative(x) }
		self.init(x: x, y: x)
	}

	public static func == (a: Point, b: Point) -> Bool { a.x == b.x && a.y == b.y }
	public func hash(into hasher: inout Hasher) {
		hasher.combine(x)
		hasher.combine(y)
	}

	public var description: String { "(\(x), \(y))" }

	public var sum: Int { x + y }

	public var first: Int {
		get { x }
		set { x = newValue }
	}

	public func scaled(by f: Int) -> Point { Point(x: x * f, y: y * f) }

	public mutating func move(by d: Int) {
		x += d
		y += d
	}

	/// Mutating with a result: the old x, and x becomes 0.
	public mutating func take() -> Int {
		defer { x = 0 }
		return x
	}

	public static func origin() -> Point { Point(x: 0, y: 0) }

	public static var unit: Point { Point(x: 1, y: 1) }
}

/// Equality without a hash: `=` works, being a map key refuses.
public struct Tag: Equatable {
	let name: String
}

/// Neither: `=` of two distinct boxes refuses, and so does being a key.
public struct Blob {
	let size: Int
}

/// Every stored property public: it crosses as a map, and init(w:) is the way back from one.
public struct Size {
	public var w: Int
	public init(w: Int) { self.w = w }
}

/// A map holding a map, an optional, a collection and a keyword.
public struct Frame {
	public var origin: Size
	public var label: String?
	public var tags: [String]
	public var heading: Heading

	public init(origin: Size, label: String?, tags: [String], heading: Heading) {
		self.origin = origin
		self.label = label
		self.tags = tags
		self.heading = heading
	}

	public var area: Int { origin.w * origin.w }

	public mutating func grow(by d: Int) { origin.w += d }
}

public func frame(_ w: Int) -> Frame { Frame(origin: Size(w: w), label: nil, tags: ["t\(w)"], heading: .north) }

public func widen(_ f: Frame) -> Frame {
	var g = f
	g.origin.w += 1
	return g
}

/// A stored property that is not public: a map would lose it, so this is a box.
public struct Reading {
	public let value: Int
	let secret: Int
	public init(value: Int) {
		self.value = value
		secret = value * 2
	}
}

/// All public, but no initializer takes `at`: it crosses out as a map and cannot come back in.
public struct Stamp {
	public let at: Int
	public init(seconds: Int) { at = seconds }
}

public func stamp() -> Stamp { Stamp(seconds: 7) }

public func age(_ s: Stamp) -> Int { s.at }

/// No payloads: a keyword.
public enum Heading {
	case north, southEast

	public var degrees: Int { self == .north ? 0 : 135 }
}

public func turn(_ h: Heading) -> Heading { h == .north ? .southEast : .north }

/// A raw value does not change the keyword: the case's name is its identity.
public enum Shade: String {
	case light, dark
}

public func shade(_ s: Shade) -> String { s.rawValue }

// The core's inst, uuid and URI are Foundation's Date, UUID and URL here.
public func epoch() -> Date { Date(timeIntervalSince1970: 0.0019) }

public func shifted(_ d: Date, by s: Double) -> Date { d.addingTimeInterval(s) }

public func idText(_ u: UUID) -> String { u.uuidString }

public func fixedID() -> UUID { UUID(uuidString: "6BA7B810-9DAD-11D1-80B4-00C04FD430C8")! }

public func host(of u: URL) -> String? { u.host }

public func resolved() -> URL { URL(string: "b/c?q=1", relativeTo: URL(string: "https://x.dev/a/"))! }

public func headings(_ hs: Set<Heading>) -> [Heading: Int] { Dictionary(uniqueKeysWithValues: hs.map { ($0, $0.degrees) }) }

/// A class with neither conformance: its boxes compare by the object.
public final class Counter {
	public var count = 0
	public let name: String
	public nonisolated(unsafe) static var made = 0
	public static let shared = Counter(name: "shared")

	public init(name: String) {
		self.name = name
		Counter.made += 1
	}

	public func increment(by n: Int) -> Int {
		count += n
		return count
	}

	public func incrementLater() async -> Int {
		await Task.yield()
		count += 1
		return count
	}
}

/// A Hashable class: its own == and hash.
public final class Version: Hashable {
	let major: Int
	public init(major: Int) { self.major = major }
	public static func == (a: Version, b: Version) -> Bool { a.major == b.major }
	public func hash(into hasher: inout Hasher) { hasher.combine(major) }
}

/// An Equatable class: = works, being a key refuses.
public final class Label: Equatable {
	let text: String
	public init(text: String) { self.text = text }
	public static func == (a: Label, b: Label) -> Bool { a.text == b.text }
}

/// A hierarchy: a Square crosses back where a Shape is expected, and its override answers.
open class Shape {
	public init() {}
	open func area() -> Int { 0 }
}

public final class Square: Shape {
	let side: Int
	public init(side: Int) {
		self.side = side
		super.init()
	}
	override public func area() -> Int { side * side }
}

public func asShape(_ s: Square) -> Shape { s }

@MainActor public final class Screen {
	public init() {}
	public func title() -> String {
		MainActor.assertIsolated()
		return "main"
	}
	public nonisolated func id() -> Int { 7 }
}

public enum FixtureError: Error {
	case tooBig(Int)
	case negative(Int)
}

final class Ledger: @unchecked Sendable {
	static let shared = Ledger()
	private let lock = NSLock()
	private var serial = 0
	private var moves = 0
	private var cancels = 0
	private var sawFixtureError = false
	private var last: Point?

	func next() -> Int { lock.withLock { serial += 1; return serial } }
	func made(_ p: Point) -> Point { lock.withLock { last = p }; return p }
	func isLast(_ p: Point) -> Bool { lock.withLock { last?.serial == p.serial } }
	func moved() { lock.withLock { moves += 1 } }
	var moveCount: Int { lock.withLock { moves } }
	func cancelled() { lock.withLock { cancels += 1 } }
	var cancelCount: Int { lock.withLock { cancels } }
	func saw(_ error: any Error) { lock.withLock { sawFixtureError = error is FixtureError } }
	var lastSawFixtureError: Bool { lock.withLock { sawFixtureError } }
}

public func makePoint(x: Int, y: Int) -> Point { Ledger.shared.made(Point(x: x, y: y)) }

/// `@MainActor`, a struct in and a struct out.
@MainActor public func moved(_ p: Point, by d: Int) -> Point {
	MainActor.assertIsolated()
	Ledger.shared.moved()
	return Ledger.shared.made(Point(x: p.x + d, y: p.y + d))
}

/// True only for the very value Swift made last: an equal point made again is not it.
public func isLastMade(_ p: Point) -> Bool { Ledger.shared.isLast(p) }

public func moveCount() -> Int { Ledger.shared.moveCount }

public func describe(_ p: Point) -> String { "\(p)" }

public func tag(_ name: String) -> Tag { Tag(name: name) }

public func blob(_ size: Int) -> Blob { Blob(size: size) }

public func scaled(_ x: Double, by f: Double) -> Double { x * f }

public var greeting = "hello"

public let answer = 42

/// Two inout parameters and a result: [a' b' result].
public func swapped(_ a: inout Int, _ b: inout Int) -> Bool {
	swap(&a, &b)
	return a > b
}

/// One inout parameter and no result: the new value alone.
public func bump(_ p: inout Point) { p.move(by: 100) }

// The four forms of throws (design §5).
public func risky(_ n: Int) throws -> Int {
	guard n <= 10 else { throw FixtureError.tooBig(n) }
	return n
}

public func checked(_ n: Int) throws(FixtureError) -> Int {
	guard n <= 10 else { throw .tooBig(n) }
	return n
}

public func safe(_ n: Int) throws(Never) -> Int { n + 1 }

/// Records whether what it rethrows is the Swift error itself.
public func applyTwice(_ f: (Int) throws -> Int, to x: Int) rethrows -> Int {
	do {
		return try f(try f(x))
	} catch {
		Ledger.shared.saw(error)
		throw error
	}
}

public func sawFixtureError() -> Bool { Ledger.shared.lastSawFixtureError }

// async, async throws, and a call that only cancellation ends.
public func later(_ n: Int) async -> Int {
	await Task.yield()
	return n * 2
}

public func fetch(_ n: Int) async throws -> Int {
	try await Task.sleep(nanoseconds: 1_000_000)
	guard n >= 0 else { throw FixtureError.negative(n) }
	return n
}

public func waitForCancel() async throws -> Int {
	do {
		try await Task.sleep(nanoseconds: 60_000_000_000)
	} catch {
		Ledger.shared.cancelled()
		throw error
	}
	return 0
}

public func cancellations() -> Int { Ledger.shared.cancelCount }

@MainActor public func mainLater(_ n: Int) async -> Int {
	MainActor.assertIsolated()
	await Task.yield()
	return n + 1
}

// Refused, each with its reason in the generator's report.
public func total(_ xs: Int...) -> Int { xs.reduce(0, +) }

public func first<T>(_ xs: [T]) -> T? { xs.first }

public func doubly(_ n: Int??) -> Int { (n ?? 0) ?? 0 }

public func bounds(_ xs: [Int]) -> (min: Int, max: Int) { (xs.min() ?? 0, xs.max() ?? 0) }

// Optionals, collections, tuples and fixed-width numbers: a slot's form is built from its parts' forms.
public func origin() -> (Int, Int) { (0, 0) }

public func maybe(_ n: Int) -> Int? { n < 0 ? nil : n }

public func orZero(_ n: Int?) -> Int { n ?? 0 }

public func lookup(_ p: Point?) -> String { p.map { "\($0)" } ?? "none" }

public func sum(of xs: [Int]) -> Int { xs.reduce(0, +) }

public func points(_ n: Int) -> [Point] { (0..<n).map { Point(x: $0, y: $0) } }

public func xs(of ps: [Point]) -> [Int] { ps.map(\.x) }

public func counts(_ words: [String]) -> [String: Int] { words.reduce(into: [:]) { $0[$1, default: 0] += 1 } }

public func unique(_ xs: [Int]) -> Set<Int> { Set(xs) }

public func size(of s: Set<String>) -> Int { s.count }

public func echo(_ m: [String: [Int?]]) -> [String: [Int?]] { m }

public func swapPair(_ p: (Int, String)) -> (String, Int) { (p.1, p.0) }

public func widths(_ b: UInt8, _ f: Float, _ big: UInt64) -> [Int32] { [Int32(b), Int32(f), Int32(big % 1000)] }

public func maxUInt() -> UInt64 { .max }

extension Point {
	public init?(nonNegative x: Int) {
		guard x >= 0 else { return nil }
		self.init(x: x, y: x)
	}
}

/// Overloads by type: labels cannot tell them apart, so both are refused.
public func width(_ n: Int) -> Int { n }
public func width(_ s: String) -> Int { s.count }
