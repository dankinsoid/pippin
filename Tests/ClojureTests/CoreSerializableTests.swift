// @ai-generated(guided)
import CljCore
import Foundation
import Testing
@testable import Clojure

// A compiler reads the same trees: no core.clj macro may embed a constant that does not print and read back.
extension CoreTests {
	@Suite struct CoreSerializableTests {
		private static let formCount = 214

		private final class Tree {
			let node: UnsafeMutablePointer<clj_node>
			init(_ node: UnsafeMutablePointer<clj_node>) { self.node = node }
			deinit { clj_release(clj_from_ptr(UnsafeMutableRawPointer(node))) }
		}

		private static func analyze(_ form: Value, env: UnsafePointer<clj_env>?) throws -> Tree {
			guard let node = withExtendedLifetime(form, { clj_analyze(form.raw, env) }) else { throw ClojureError.takePending() }
			return Tree(node)
		}

		private static func data(_ tree: Tree) throws -> Value {
			let raw = clj_node_to_data(tree.node)
			if raw == CLJ_THROWN { throw ClojureError.takePending() }
			return Value(owning: raw)
		}

		private static func fromData(_ data: Value) throws -> Tree {
			guard let node = withExtendedLifetime(data, { clj_node_from_data(data.raw) }) else { throw ClojureError.takePending() }
			return Tree(node)
		}

		private static func position(_ form: Value) -> String {
			var line: UInt32 = 0, col: UInt32 = 0
			return clj_form_position(form.raw, &line, &col) ? "\(line):\(col)" : "?:?"
		}

		private static func describe(_ error: any Error) -> String { (error as? ClojureError)?.message ?? "\(error)" }

		// Syntax-quote resolves through the current namespace, the analyzer through env; boot has bound every var.
		private static func roundTripEveryForm() throws -> Int {
			let file = URL(fileURLWithPath: #filePath)
				.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
				.appendingPathComponent("Sources/CljCore/boot/core.clj")
			let source = try String(contentsOf: file, encoding: .utf8)
			clj_ns_set_current(clj_ns_core())
			defer { clj_ns_set_current(clj_ns_user()) }
			let forms = try Value.readAll(source)
			var env = clj_env(ns: clj_ns_core(), line: 0, col: 0)
			for form in forms { try roundTrip(form, env: &env) }
			return forms.count
		}

		private static func roundTrip(_ form: Value, env: UnsafePointer<clj_env>?) throws {
			let at = "\(position(form)) \(String(form.description.prefix(60)))"
			do {
				let tree = try analyze(form, env: env)
				let first = try data(tree)
				let text = try #require(Value(owning: clj_pr_str(first.raw)).string)
				let again = try data(try fromData(try Value(reading: text)))
				#expect(again == first, Comment(rawValue: at))
			} catch {
				Issue.record(Comment(rawValue: "\(at): \(describe(error))"))
			}
		}

		private static func internKeywords() {
			for k in ["const", "local", "last", "captured", "var", "the-var", "if", "do", "let", "loop", "recur", "fn", "invoke", "def", "vector", "map", "set",
			          "try", "throw", "all", "error", "intrinsic", "fused", "outer", "direct-fn", "direct-call"] {
				_ = Value(keyword: k)
			}
		}

		// Every var core.clj names exists after boot, so analysis interns nothing: one pass holds the baseline.
		@Test func everyFormRoundTrips() throws {
			clj_init()
			Self.internKeywords()
			let before = clj_debug_live_objects()
			#expect(try Self.roundTripEveryForm() == Self.formCount)
			#expect(clj_debug_live_objects() == before)
		}

		// core.clj defines the type macros without using them: their expansions are checked on user forms.
		@Test func typeMacroExpansionsRoundTrip() throws {
			clj_init()
			Self.internKeywords()
			for k in ["cs-m", "cs-n", "seq", "first", "next", "count", "invoke"] { _ = Value(keyword: k) }
			_ = try cljEval("(def cs-P) (def cs-m) (def cs-Q) (def cs-n) (def cs-T) (def ->cs-T)")
			let before = clj_debug_live_objects()
			do {
				let forms = try Value.readAll("""
				(defprotocol cs-P "doc" (cs-m [this] [this a] "m doc"))
				(defprotocol cs-Q (cs-n [this]))
				(deftype cs-T [a b] cs-P (cs-m ([this] a) ([this x] (+ b x))) cs-Q (cs-n [this] b))
				(deftype cs-T [k] ISeq (seq [this] this) (first [_] k) (next [_] nil) Counted (count [_] 1))
				(extend-type String cs-P (cs-m ([this] this) ([this a] a)))
				(extend-protocol cs-P Long (cs-m [this] this) nil (cs-m [this] 0))
				(let [x 1] (reify cs-P (cs-m [this] x) (cs-m [this a] (+ x a)) cs-Q (cs-n [this] this)))
				(fn [k] (reify ISeq (seq [this] this) (first [_] k) (next [_] nil) Counted (count [_] 1) IFn (invoke [_ y] y)))
				""")
				#expect(forms.count == 8)
				for form in forms { try Self.roundTrip(form, env: nil) }
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
