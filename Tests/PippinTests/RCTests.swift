// @ai-generated(solo)
import CljCore
import Darwin
import Foundation
import Testing
@testable import Pippin

// The RC invariant checks exist in debug builds only, and an exit test's child cannot start under a sanitizer (TraceTests).
private let debugChecks = clj_debug_live_objects() >= 0 && !underSanitizer

private func stderrText(_ result: ExitTest.Result?) -> String {
	String(decoding: result?.standardErrorContent ?? [], as: UTF8.self)
}

// A cons over v made on a fresh pthread, an execution of its own; clj_cons_new retains v in C, where the check is
// compiled in (Swift's view of the inline retain has no CLJ_DEBUG).
private func consOnAnotherThread(_ v: clj_value) {
	var thread: pthread_t?
	_ = pthread_create(&thread, nil, { arg in
		clj_release(clj_cons_new(UInt(bitPattern: arg), CLJ_NIL))
		return nil
	}, UnsafeMutableRawPointer(bitPattern: v))
	pthread_join(thread!, nil)
}

extension CoreTests {
	@Suite struct RCTests {
		@Test func allocReleaseFrees() {
			let before = clj_debug_live_objects()
			let c = clj_cons_new(clj_fixnum(1), CLJ_NIL)
			#expect(clj_debug_live_objects() == before + 1)
			#expect(clj_is_unique(c) == clj_reuse_enabled())
			#expect(!clj_is_shared(c))
			#expect(String(cString: clj_type_name(c)) == "cons")

			_ = clj_retain(c)
			#expect(!clj_is_unique(c))
			clj_release(c)
			#expect(clj_is_unique(c) == clj_reuse_enabled())

			clj_release(c)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func constructorRetainsChildren() {
			let before = clj_debug_live_objects()
			let inner = clj_cons_new(clj_fixnum(1), CLJ_NIL)
			let outer = clj_cons_new(inner, CLJ_NIL)
			#expect(!clj_is_unique(inner))
			clj_release(inner)
			#expect(clj_is_unique(inner) == clj_reuse_enabled())
			#expect(clj_debug_live_objects() == before + 2)
			clj_release(outer)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func immediatesAreNoOps() {
			for v in [CLJ_NIL, CLJ_TRUE, CLJ_FALSE, clj_fixnum(7), clj_char(97)] {
				#expect(clj_retain(v) == v)
				clj_release(v)
				#expect(!clj_is_unique(v))
				#expect(!clj_is_shared(v))
			}
		}

		@Test func immortalTypesIgnoreRC() {
			withUnsafePointer(to: clj_cons_type) { p in
				let t = clj_from_ptr(UnsafeMutableRawPointer(mutating: p))
				_ = clj_retain(t)
				clj_release(t)
				clj_release(t)
				#expect(!clj_is_unique(t))
				#expect(String(cString: clj_type_name(t)) == "type")
			}
		}

		@Test func longChainDropsIteratively() {
			let before = clj_debug_live_objects()
			var head = CLJ_NIL
			for i in 0..<1_000_000 {
				let next = clj_cons_new(clj_fixnum(i), head)
				clj_release(head)
				head = next
			}
			#expect(clj_debug_live_objects() == before + 1_000_000)
			clj_release(head)
			#expect(clj_debug_live_objects() == before)
		}

		// A 64 KB stack: a teardown recursing per link would overflow it long before 300k links.
		@Test func aMixedChainDropsOnASmallStack() {
			let before = clj_debug_live_objects()
			let probe = clj_double_new(0.5)
			let doubleType = clj_type_of(probe)
			clj_release(probe)
			let doubles = clj_debug_live_objects_of(doubleType)
			nonisolated(unsafe) var during: Int64 = 0
			let done = DispatchSemaphore(value: 0)
			let t = Thread {
				var head = CLJ_NIL
				for i in 0..<300_000 {
					let d = clj_double_new(Double(i) + 0.5)
					let v = [d, head].withUnsafeBufferPointer { clj_vector_from_array($0.baseAddress, 2) }
					clj_release(d)
					clj_release(head)
					head = clj_cons_new(v, CLJ_NIL)
					clj_release(v)
				}
				during = clj_debug_live_objects()
				clj_release(head)
				done.signal()
			}
			t.stackSize = 64 * 1024
			t.start()
			done.wait()
			if before >= 0 {
				#expect(during == before + 900_000)
				#expect(clj_debug_live_objects() == before)
				#expect(clj_debug_live_objects_of(doubleType) == doubles)
			}
		}

		@Test func shareMarksReachableGraph() {
			let before = clj_debug_live_objects()
			let leaf = clj_cons_new(clj_fixnum(1), CLJ_NIL)
			let mid = clj_cons_new(leaf, CLJ_NIL)
			let root = clj_cons_new(mid, CLJ_NIL)
			clj_release(mid)
			clj_release(leaf)

			clj_share(root)
			#expect(clj_is_shared(root))
			#expect(clj_is_shared(mid))
			#expect(clj_is_shared(leaf))

			_ = clj_retain(root)
			#expect(!clj_is_unique(root))
			clj_release(root)
			#expect(clj_is_unique(root) == clj_reuse_enabled())
			clj_release(root)
			#expect(clj_debug_live_objects() == before)
		}

		@Test func valueOwnsAndReleases() {
			let before = clj_debug_live_objects()
			func roundTrip() -> Bool {
				let v = Value(owning: clj_cons_new(clj_fixnum(1), CLJ_NIL))
				let copy = v
				return clj_is_shared(copy.raw) && clj_is_unique(v.raw) == clj_reuse_enabled() && v.typeName == "cons"
			}
			#expect(roundTrip())
			#expect(clj_debug_live_objects() == before)
		}

		// @ai-generated(solo)
		@Test(.enabled(if: debugChecks)) func anUnsharedObjectTouchedByAnotherExecutionDies() async {
			let result = await #expect(processExitsWith: .failure, observing: [\.standardErrorContent]) {
				setenv("CLJ_CRASH_EXIT", "1", 1)
				clj_init()
				consOnAnotherThread(clj_cons_new(clj_fixnum(1), CLJ_NIL))
			}
			#expect(stderrText(result).contains("unshared cons of execution"))
		}

		// @ai-generated(solo)
		@Test(.enabled(if: debugChecks)) func aSharedParentOfAnUnsharedChildDiesWhenFreed() async {
			let result = await #expect(processExitsWith: .failure, observing: [\.standardErrorContent]) {
				setenv("CLJ_CRASH_EXIT", "1", 1)
				clj_init()
				let leaf = clj_cons_new(clj_fixnum(1), CLJ_NIL)
				let root = clj_cons_new(leaf, CLJ_NIL)
				clj_release(leaf)
				clj_header_of(root).pointee.flags |= 1 // CLJ_FLAG_SHARED on the parent alone: the forged violation
				clj_release(root)
			}
			#expect(stderrText(result).contains("free: shared cons over unshared cons"))
		}

		// @ai-generated(solo)
		@Test(.enabled(if: debugChecks)) func aShareStoppingAtABrokenSharedNodeDies() async {
			let result = await #expect(processExitsWith: .failure, observing: [\.standardErrorContent]) {
				setenv("CLJ_CRASH_EXIT", "1", 1)
				clj_init()
				let leaf = clj_cons_new(clj_fixnum(1), CLJ_NIL)
				let mid = clj_cons_new(leaf, CLJ_NIL)
				clj_release(leaf)
				clj_header_of(mid).pointee.flags |= 1 // CLJ_FLAG_SHARED
				// One cutoff in 256 per thread is checked: 256 shares stopping at mid meet one of them.
				for _ in 0..<256 {
					let root = clj_cons_new(mid, CLJ_NIL)
					clj_share(root)
				}
			}
			#expect(stderrText(result).contains("share cutoff: shared cons over unshared cons"))
		}

		// @ai-generated(solo)
		@Test(.enabled(if: debugChecks)) func aStoreIntoAnAtomWithoutItsLockDies() async {
			let result = await #expect(processExitsWith: .failure, observing: [\.standardErrorContent]) {
				setenv("CLJ_CRASH_EXIT", "1", 1)
				clj_init()
				let atom = clj_atom_new(clj_fixnum(0), CLJ_NIL, CLJ_NIL)
				clj_retain(atom) // a second holder: past the creator's fill, the lock is required
				clj_debug_slot_store_check(clj_header_of(atom), clj_fixnum(1))
			}
			#expect(stderrText(result).contains("store into a slot of atom without its lock"))
		}

		// @ai-generated(solo)
		@Test(.enabled(if: debugChecks)) func aStoreIntoAChannelWithoutItsLockDies() async {
			let result = await #expect(processExitsWith: .failure, observing: [\.standardErrorContent]) {
				setenv("CLJ_CRASH_EXIT", "1", 1)
				clj_init()
				let ch = clj_chan_new(clj_fixnum(1))
				clj_retain(ch)
				clj_debug_slot_store_check(clj_header_of(ch), clj_fixnum(1))
			}
			#expect(stderrText(result).contains("store into a slot of channel without its lock"))
		}

		@Test func valueBorrowingRetains() {
			let before = clj_debug_live_objects()
			let c = clj_cons_new(clj_fixnum(1), CLJ_NIL)
			func hold() -> Bool {
				let v = Value(borrowing: c)
				return !clj_is_unique(v.raw)
			}
			#expect(hold())
			#expect(clj_is_unique(c) == clj_reuse_enabled())
			clj_release(c)
			#expect(clj_debug_live_objects() == before)
		}
	}
}
