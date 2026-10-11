// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// Exit tests: the child is spawned without a sanitizer's insert library; the verifier exists in debug builds only.
private let exitChecks = clj_debug_live_objects() >= 0 && !underSanitizer

private func analyzed(_ source: String) -> UnsafeMutablePointer<clj_node>? {
	guard let form = try? Value(reading: source) else { return nil }
	var env = clj_env(ns: clj_ns_user(), line: 0, col: 0)
	return withExtendedLifetime(form) { clj_analyze(form.raw, &env) }
}

private func release(_ node: UnsafeMutablePointer<clj_node>) { clj_release(clj_from_ptr(UnsafeMutableRawPointer(node))) }

private func mutable(_ n: UnsafePointer<clj_node>?) -> UnsafeMutablePointer<clj_node> { UnsafeMutablePointer(mutating: n!) }

// (let [v [1]] (let [w (conj v 2)] [w v])): the local v that conj reads, which the liveness pass leaves unmarked.
private func conjOperand(_ root: UnsafeMutablePointer<clj_node>) -> UnsafeMutablePointer<clj_node> {
	let inner = mutable(root.pointee.u.let.body)
	let conj = mutable(inner.pointee.u.let.inits[0])
	return mutable(conj.pointee.u.intrinsic.args[0])
}

private let liveAfterConj = "(let [v [1]] (let [w (conj v 2)] [w v]))"

private func check(_ node: UnsafeMutablePointer<clj_node>, _ flags: UInt32) -> (ok: Bool, kind: String, message: String) {
	var v = clj_verify_violation()
	let ok = clj_verify_tree_check(node, flags, &v)
	let kind = v.kind.map { String(cString: $0) } ?? ""
	let message = withUnsafeBytes(of: v.message) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) }
	return (ok, kind, message)
}

private let everything = CLJ_VERIFY_NUMBERED | CLJ_VERIFY_MARKS

extension CoreTests {
	// Design §3 «Корректность реализации», items 1 and 6 (NOTES "Internal errors", "Tree verifier").
	@Suite struct VerifierTests {
		let rt = Runtime()

		// The seed: a last-use mark the liveness pass would never set, on a read the tree reads again after.
		@Test func aLastUseBeforeAnotherReadIsCaught() throws {
			let node = try #require(analyzed(liveAfterConj))
			defer { release(node) }
			let v = conjOperand(node)
			#expect(v.pointee.kind == CLJ_NODE_LOCAL && !v.pointee.u.local.last)
			#expect(check(node, everything).ok)
			v.pointee.u.local.last = true
			let r = check(node, everything)
			#expect(!r.ok)
			#expect(r.kind == "verify/read-after-last-use")
			v.pointee.u.local.last = false
		}

		@Test func eachStructuralInvariantHasItsKind() throws {
			let node = try #require(analyzed("(let [a 1] (if a (inc a) a))"))
			defer { release(node) }
			let body = mutable(node.pointee.u.let.body)
			let read = mutable(body.pointee.u.if_.else_)
			let last = read.pointee.u.local.last
			read.pointee.u.local.index = 7
			#expect(check(node, everything).kind == "verify/slot-outside-frame")
			read.pointee.u.local.index = 0
			read.pointee.id += 1
			#expect(check(node, everything).kind == "verify/numbering")
			read.pointee.id -= 1
			read.pointee.u.local.last = true
			#expect(check(node, 0).kind == "verify/last-use-before-liveness")
			read.pointee.u.local.last = last
			#expect(check(node, everything).ok)
		}

		@Test func aRecurOutOfTailPositionIsCaught() throws {
			let node = try #require(analyzed("(loop [i 0] (if (< i 3) (recur (inc i)) i))"))
			defer { release(node) }
			#expect(check(node, everything).ok)
			let body = mutable(node.pointee.u.let.body)
			// the test of the if holds the recur: a CLJ_RECUR value where a boolean is read
			let test = body.pointee.u.if_.test, then = body.pointee.u.if_.then
			body.pointee.u.if_.test = then
			body.pointee.u.if_.then = test
			// unnumbered: the swap moved ids out of pre-order, which is a violation of its own
			#expect(check(node, CLJ_VERIFY_MARKS).kind == "verify/recur-not-in-tail")
			body.pointee.u.if_.test = test
			body.pointee.u.if_.then = then
		}

		@Test func theDigestSeesAWrite() throws {
			let node = try #require(analyzed(liveAfterConj))
			defer { release(node) }
			let before = clj_verify_digest(node)
			#expect(clj_verify_digest(node) == before)
			conjOperand(node).pointee.u.local.last = true
			#expect(clj_verify_digest(node) != before)
			conjOperand(node).pointee.u.local.last = false
			#expect(clj_verify_digest(node) == before)
		}

		// What the report carries: the stable kind, the claim, the pass, the backend, the form and the node.
		@Test func theReportNamesPassBackendAndPosition() throws {
			let node = try #require(analyzed(liveAfterConj))
			defer { release(node) }
			let file = Value("app/core.clj")
			let pass = strdup("optimize/liveness")!
			defer { free(pass) }
			var frame = clj_ice_frame()
			frame.pass = UnsafePointer(pass)
			frame.backend = CLJ_BACKEND_INTERPRETER
			frame.file = file.raw
			frame.line = 12
			frame.col = 3
			var buf = [CChar](repeating: 0, count: 4096)
			withExtendedLifetime(file) {
				clj_ice_enter(&frame)
				_ = clj_ice_render(&buf, buf.count, "verify/read-after-last-use", conjOperand(node), "slot 0 is read after a last use")
				clj_ice_leave(&frame)
			}
			let text = String(decoding: buf.prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }, as: UTF8.self)
			#expect(text.hasPrefix("clj: internal error [verify/read-after-last-use]: slot 0 is read after a last use\n"))
			#expect(text.contains("this is a bug in pippin"))
			#expect(text.contains("pass: optimize/liveness"))
			#expect(text.contains("backend: interpreter"))
			#expect(text.contains("form: app/core.clj:12:3"))
			#expect(text.contains("node: local at 1:"))
			#expect(text.contains("optimizations: all on"))
		}

		@Test func theSwitchParsesItsNames() {
			#expect(clj_no_opt_parse("") == 0 && clj_no_opt_parse("0") == 0)
			#expect(clj_no_opt_parse("1") == clj_no_opt_parse("all"))
			#expect(clj_no_opt_parse("reuse,passes") == clj_no_opt_parse("passes,reuse"))
			#expect(clj_no_opt_parse("reuse,nope") == UInt32.max)
			var buf = [CChar](repeating: 0, count: 200)
			#expect(String(cString: clj_no_opt_describe(clj_no_opt_parse("all"), &buf, buf.count)) == "reuse, passes, specialize, lazy-seq, layouts, compiled, emit")
		}

		// The seed end to end: the verifier, run as the pass that produced the tree, kills the process naming it.
		@Test(.enabled(if: exitChecks)) func aViolationKillsTheProcessNamingThePass() async {
			let result = await #expect(processExitsWith: .failure, observing: [\.standardErrorContent]) {
				setenv("CLJ_CRASH_EXIT", "1", 1)
				clj_init()
				let node = analyzed(liveAfterConj)!
				conjOperand(node).pointee.u.local.last = true
				clj_verify_tree(node, "optimize/liveness", everything)
			}
			let stderr = String(decoding: result?.standardErrorContent ?? [], as: UTF8.self)
			#expect(stderr.contains("clj: internal error [verify/read-after-last-use]"))
			#expect(stderr.contains("pass: optimize/liveness"))
			#expect(stderr.contains("the tree as optimize/liveness left it"))
		}

		// CLJ_NO_OPT=1: no optimizer pass, no reuse, no layouts, and the program still answers.
		@Test(.enabled(if: exitChecks)) func theKillSwitchTurnsEverythingOff() async {
			await #expect(processExitsWith: .success) {
				setenv("CLJ_NO_OPT", "1", 1)
				clj_init()
				let node = analyzed("(+ 1 2)")!
				let ok = node.pointee.kind == CLJ_NODE_INVOKE && !clj_reuse_enabled() && !clj_tuples_enabled() && !clj_shapes_enabled() &&
					!clj_specialize_enabled() && clj_no_opt() == clj_no_opt_parse("all") && Value(owning: clj_eval_node(node)) == 3
				exit(ok ? 0 : 1)
			}
		}
	}
}
