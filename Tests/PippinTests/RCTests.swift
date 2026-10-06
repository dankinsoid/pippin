// @ai-generated(solo)
import CljCore
import Darwin
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
