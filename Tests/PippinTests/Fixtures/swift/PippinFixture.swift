// @ai-generated(solo)
// The Swift module SwiftStubTests bridges, built by the test with swiftc as an app's own module would be.
import Foundation

/// A box: no stored state is public, so no shape crosses as a map. `serial` tells this value from an equal one.
public struct Point: Hashable, CustomStringConvertible {
	let x: Int
	let y: Int
	let serial: Int

	init(x: Int, y: Int) {
		self.x = x
		self.y = y
		serial = Ledger.shared.next()
	}

	public static func == (a: Point, b: Point) -> Bool { a.x == b.x && a.y == b.y }
	public func hash(into hasher: inout Hasher) {
		hasher.combine(x)
		hasher.combine(y)
	}

	public var description: String { "(\(x), \(y))" }
}

/// Equality without a hash: `=` works, being a map key refuses.
public struct Tag: Equatable {
	let name: String
}

/// Neither: `=` of two distinct boxes refuses, and so does being a key.
public struct Blob {
	let size: Int
}

final class Ledger: @unchecked Sendable {
	static let shared = Ledger()
	private let lock = NSLock()
	private var serial = 0
	private var moves = 0
	private var last: Point?

	func next() -> Int { lock.withLock { serial += 1; return serial } }
	func made(_ p: Point) -> Point { lock.withLock { last = p }; return p }
	func isLast(_ p: Point) -> Bool { lock.withLock { last?.serial == p.serial } }
	func moved() { lock.withLock { moves += 1 } }
	var moveCount: Int { lock.withLock { moves } }
}

public func makePoint(x: Int, y: Int) -> Point { Ledger.shared.made(Point(x: x, y: y)) }

/// The slice's symbol: `@MainActor`, a struct in and a struct out.
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

// Refused, each with its reason in the generator's report.
public func total(_ xs: Int...) -> Int { xs.reduce(0, +) }

public func first<T>(_ xs: [T]) -> T? { xs.first }

public func risky(_ n: Int) throws -> Int { n }

public func later(_ n: Int) async -> Int { n }

public func origin() -> (Int, Int) { (0, 0) }
