// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Clojure

// Swift only sees a copy of clj_cons_type; the stable address comes from a real cons.
nonisolated(unsafe) private let consType: UnsafePointer<clj_type> = {
	let c = clj_cons_new(CLJ_NIL, CLJ_NIL)
	let t = clj_header_of(c).pointee.type!
	clj_release(c)
	return t
}()

// Cons-typed objects of arbitrary size: release visits only first/rest, which read as nil when zero.
private func rawObject(_ size: Int) -> clj_value {
	clj_from_ptr(clj_alloc(consType, size))
}

private func word(_ v: clj_value, _ i: Int) -> UnsafeMutablePointer<clj_value> {
	clj_to_ptr(v).assumingMemoryBound(to: clj_value.self).advanced(by: i)
}

// Fixnum words are valid values wherever a type might read them.
private func fill(_ v: clj_value, words: Range<Int>) {
	for i in words { word(v, i).pointee = clj_fixnum(i * 7 + 1) }
}

private func check(_ v: clj_value, words: Range<Int>) -> Bool {
	words.allSatisfy { word(v, $0).pointee == clj_fixnum($0 * 7 + 1) }
}

extension CoreTests {
	@Suite struct AllocTests {
		@Test func sizeClasses() {
			guard clj_debug_pool_enabled() else { return }
			let expected: [(Int, Int)] = [
				(16, 32), (24, 32), (32, 32), (33, 40), (40, 40), (41, 48), (56, 56), (57, 64), (64, 64),
				(65, 80), (80, 80), (81, 96), (128, 128), (129, 160), (200, 224), (256, 256), (257, 320),
				(512, 512), (513, 640), (1000, 1024), (1024, 1024), (1025, 0), (100_000, 0),
			]
			for (size, cell) in expected {
				#expect(clj_debug_cell_size(size) == cell, "size \(size)")
			}
			var last = 0
			for size in 16...1024 {
				let cell = Int(clj_debug_cell_size(size))
				#expect(cell >= size && cell >= last, "size \(size)")
				last = cell
			}
		}

		@Test func allocReturnsZeroedMemory() {
			let before = clj_debug_live_objects()
			var objects: [clj_value] = []
			for _ in 0..<3000 {
				let v = rawObject(48)
				fill(v, words: 2..<6)
				objects.append(v)
			}
			for v in objects { clj_release(v) }
			objects.removeAll()
			for _ in 0..<3000 {
				let v = rawObject(48)
				#expect((2..<6).allSatisfy { word(v, $0).pointee == CLJ_NIL })
				objects.append(v)
			}
			for v in objects { clj_release(v) }
			#expect(clj_debug_live_objects() == before)
		}

		@Test func reallocWithinClassKeepsAddress() {
			guard clj_debug_pool_enabled() else { return }
			let before = clj_debug_live_objects()
			var v = rawObject(44)
			let p = v
			fill(v, words: 2..<5)
			v = clj_from_ptr(clj_realloc(clj_to_ptr(v), 48))
			#expect(v == p)
			v = clj_from_ptr(clj_realloc(clj_to_ptr(v), 41))
			#expect(v == p)
			#expect(check(v, words: 2..<5))
			v = clj_from_ptr(clj_realloc(clj_to_ptr(v), 49))
			#expect(v != p)
			#expect(check(v, words: 2..<5))
			clj_release(v)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func reallocAcrossClassesPreservesBytes() {
			let before = clj_debug_live_objects()
			var v = rawObject(64)
			fill(v, words: 2..<8)
			for size in [200, 2000, 100, 4000, 1024, 32, 5000, 64] {
				v = clj_from_ptr(clj_realloc(clj_to_ptr(v), size))
				#expect(check(v, words: 2..<4), "size \(size)")
				#expect(clj_is_unique(v) == clj_reuse_enabled())
			}
			clj_release(v)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func largeObjects() {
			let before = clj_debug_live_objects()
			var v = rawObject(5000)
			#expect((2..<625).allSatisfy { word(v, $0).pointee == CLJ_NIL })
			fill(v, words: 600..<625)
			v = clj_from_ptr(clj_realloc(clj_to_ptr(v), 9000))
			#expect(check(v, words: 600..<625))
			let big = clj_cons_new(v, CLJ_NIL)
			clj_release(v)
			clj_release(big)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func slabExhaustion() {
			let before = clj_debug_live_objects()
			// 64 KiB holds about 2000 cells of 32 bytes; several slabs are needed.
			let count = 10_000
			var objects: [clj_value] = []
			objects.reserveCapacity(count)
			for i in 0..<count {
				let v = rawObject(32)
				word(v, 2).pointee = clj_fixnum(i)
				objects.append(v)
			}
			#expect(Set(objects).count == count)
			#expect(objects.enumerated().allSatisfy { word($1, 2).pointee == clj_fixnum($0) })
			#expect(clj_debug_live_objects() == before + Int64(count))
			for v in objects { clj_release(v) }
			#expect(clj_debug_live_objects() == before)

			// The freed cells cover the second round, so no new slab gets mapped.
			let again = (0..<count).map { _ in rawObject(32) }
			if clj_debug_pool_enabled() {
				let slabs = { (vs: [clj_value]) in Set(vs.map { $0 & ~UInt(0xFFFF) }) }
				#expect(slabs(again).isSubset(of: slabs(objects)))
			}
			for v in again { clj_release(v) }
			#expect(clj_debug_live_objects() == before)
		}

		@Test func freeFromAnotherThreadReturnsCellToOwner() {
			let before = clj_debug_live_objects()
			let v = rawObject(40)
			clj_share(v)
			let done = DispatchSemaphore(value: 0)
			let t = Thread {
				clj_release(v)
				done.signal()
			}
			t.start()
			done.wait()
			#expect(clj_debug_live_objects() == before)

			var seen: [clj_value] = []
			var reused = false
			while !reused && seen.count < 100_000 {
				let n = rawObject(40)
				seen.append(n)
				reused = n == v
			}
			if clj_debug_pool_enabled() {
				#expect(reused)
			}
			for n in seen { clj_release(n) }
			#expect(clj_debug_live_objects() == before)
		}

		@Test func allocateOnAnotherThreadFreeHere() {
			let before = clj_debug_live_objects()
			nonisolated(unsafe) var made: [clj_value] = []
			let done = DispatchSemaphore(value: 0)
			let t = Thread {
				for i in 0..<5000 {
					let v = rawObject(32 + (i % 8) * 8)
					clj_share(v)
					made.append(v)
				}
				done.signal()
			}
			t.start()
			done.wait()
			#expect(clj_debug_live_objects() == before + 5000)
			for v in made { clj_release(v) }
			#expect(clj_debug_live_objects() == before)
		}
	}
}
