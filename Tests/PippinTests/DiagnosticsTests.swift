// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// design §3 «Диагностика»: the structure of a diagnostic is asserted over its ex-data (EvalTests,
// MetaTests, Fixtures/compiler/diagnostics*.clj) and the text is a snapshot here, so rewording the
// rendering breaks nothing that is about the data.
extension CoreTests {
	@Suite(.serialized) struct DiagnosticsTests {
		// The source through clj_load_file from a real file, so the renderer can quote the line; the
		// absolute path is replaced by the bare name, which is what a snapshot can hold.
		private func rendered(_ source: String, name: String) throws -> String {
			clj_init()
			let dir = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("clj-diagnostics-\(UUID().uuidString)")
			try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
			defer { try? FileManager.default.removeItem(at: dir) }
			let file = dir.appendingPathComponent("\(name).clj")
			try source.write(to: file, atomically: true, encoding: .utf8)
			let path = Value(file.path)
			let r = withExtendedLifetime(path) { clj_load_file(path.raw) }
			try #require(r == CLJ_THROWN, "the source was expected to fail")
			let ex = Value(owning: clj_take_pending())
			let text = try #require(withExtendedLifetime(ex) { Value(owning: clj_diagnostic_render(ex.raw)).string })
			return text.replacingOccurrences(of: file.path, with: "\(name).clj")
		}

		@Test func anUnresolvedNameIsPlacedUnderlinedAndSuggested() throws {
			defer { clj_ns_set_current(clj_ns_user()) }
			#expect(try rendered("""
			(defn dt-f [x]
			  (let [y (inc x)]
			    (dt-undefined-name y)))
			""", name: "unresolved") == """
			error: Unable to resolve symbol: dt-undefined-name in this context
			 --> unresolved.clj:3:5
			   |
			 3 |     (dt-undefined-name y)))
			   |     ^^^^^^^^^^^^^^^^^^^^^
			 = note: in the top-level form at 1:1

			""")
			// The hint is a name that exists; nothing within the threshold means no help line at all.
			#expect(try rendered("(defn dt-g [x] (inx x))", name: "hint") == """
			error: Unable to resolve symbol: inx in this context
			 --> hint.clj:1:16
			   |
			 1 | (defn dt-g [x] (inx x))
			   |                ^^^^^^^
			 = help: did you mean inc?

			""")
		}

		@Test func aBindingVectorAndARuntimeErrorAreQuotedToo() throws {
			defer { clj_ns_set_current(clj_ns_user()) }
			#expect(try rendered("(defn dt-h []\n  (let [a] a))", name: "binding") == """
			error: let requires an even number of forms in binding vector
			 --> binding.clj:2:3
			   |
			 2 |   (let [a] a))
			   |   ^^^^^^^^^^^
			 = note: in the top-level form at 1:1

			""")
			// A runtime error has no position of its own, so the top-level form is still what it reports.
			#expect(try rendered("(nth 5 0)", name: "runtime") == """
			error: nth not supported on this type: long
			 --> runtime.clj:1:1
			   |
			 1 | (nth 5 0)
			   | ^^^^^^^^^

			""")
		}

		@Test func anArityErrorNamesTheArities() throws {
			defer { clj_ns_set_current(clj_ns_user()) }
			// The call site is in the top-level form here, so the two positions coincide; a call inside a
			// function shows as a `raised at` note instead (docs/notes/diagnostics.md).
			#expect(try rendered("(defn dt-k [x] (+ x 1))\n\n(dt-k 1 2 3)", name: "arity") == """
			error: Wrong number of args (3) passed to: user/dt-k, which takes 1
			 --> arity.clj:3:1
			   |
			 3 | (dt-k 1 2 3)
			   | ^^^^^^^^^^^^

			""")
		}

		// Nothing in a rendered diagnostic is a name only the implementation knows: a node kind, a lattice
		// element or a runtime function (design §3 «Диагностика»). `long` is the word `type` answers.
		@Test func theTextNamesNothingInternal() throws {
			defer { clj_ns_set_current(clj_ns_user()) }
			let text = try rendered("(nth 5 0)", name: "internal")
			#expect(try cljEval("(str (type 5))") == "long")
			for internalName in ["clj_", "CLJ_", "NODE_", "node", "⊤", "⊥", "lattice"] {
				#expect(!text.contains(internalName), "the rendering names \(internalName)")
			}
		}

		// A message that embeds a long printed form keeps its own tail: truncating a diagnostic to a fixed
		// buffer is a defect, not a compromise (design §3 «Диагностика»).
		@Test func aLongMessageKeepsItsTail() throws {
			defer { clj_ns_set_current(clj_ns_user()) }
			let long = String(repeating: "dt-very-long-name-", count: 80)
			let text = try rendered("(defn dt-m [] (\(long) 1))", name: "long")
			#expect(text.contains("in this context"))
			#expect(text.contains(long))
		}
	}
}
