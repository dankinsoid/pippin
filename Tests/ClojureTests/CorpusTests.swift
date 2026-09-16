// @ai-generated(guided)
import CljCore
import Foundation
import Testing
@testable import Clojure

// The acceptance harness of design §10; the allowlist rules are in NOTES.md, "Corpus".

private struct FormFailure: Hashable {
	let file: String // relative to the library root
	let line: Int
	let name: String? // the def'd name when the form is a def
	let reason: String
	var key: String { "\(file):\(line)" }
}

private struct TestOutcome {
	let name: String // ns/var
	let status: String // pass, fail, error
	let reason: String?
}

private struct Allowlist {
	var forms: [String: Value] = [:] // key → entry map
	var tests: [String: Value] = [:] // name → entry map
	var skipped: Set<String> = []
}

private let corpusRoot = URL(fileURLWithPath: #filePath)
	.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
	.appendingPathComponent("corpus")

private let missingPatterns: [(String, Int)] = [
	("Unable to resolve symbol: (\\S+) in this context", 1), ("No such namespace: (\\S+)", 1), ("Unable to resolve var: (\\S+)", 1),
	("Unable to resolve classname: (\\S+)", 1), ("Unbound var: #'(\\S+)", 1), ("Could not locate (\\S+)\\.cljc", 1),
	("No namespace: (\\S+)", 1), ("var: (\\S+) is not public", 1),
]

// The symbol a resolution failure names, when the message is one.
private func missingSymbol(in message: String) -> String? {
	for (pattern, group) in missingPatterns {
		if let m = message.range(of: pattern, options: .regularExpression) {
			let regex = try! NSRegularExpression(pattern: pattern)
			let text = String(message[m])
			if let match = regex.firstMatch(in: text, range: NSRange(text.startIndex..., in: text)),
			   let r = Range(match.range(at: group), in: text) {
				return String(text[r])
			}
		}
	}
	return nil
}

private func truncated(_ s: String, _ n: Int = 160) -> String {
	let one = s.replacingOccurrences(of: "\n", with: " ")
	return one.count <= n ? one : String(one.prefix(n)) + "…"
}

private func ednString(_ s: String) -> String { Value(s).description }

private func kw(_ s: String) -> Value { Value(keyword: s) }

// Progress on stderr: a hanging namespace is found by the last line printed.
private func progress(_ text: String) {
	FileHandle.standardError.write(Data((text + "\n").utf8))
}

// The harness side of the run, in Clojure: test-ns under a collecting reporter, folded into [var status reason].
private let harnessSource = """
(ns corpus-harness (:require [clojure.test]))
(defn run-ns [ns-sym]
  (let [events (atom [])]
    (binding [clojure.test/report (fn [m] (when (= :begin-test-var (:type m)) (corpus-progress* (str (:var m)))) (swap! events conj m))]
      (clojure.test/test-ns ns-sym))
    (loop [es (seq @events) cur nil out []]
      (if-not es
        out
        (let [m (first es) t (:type m)]
          (cond
            (= t :begin-test-var)
            (let [v (:var m) mt (meta v)]
              (recur (next es) [(str (:ns mt) "/" (:name mt)) :pass nil] out))
            (= t :end-test-var) (recur (next es) nil (conj out cur))
            (and cur (= t :error))
            (recur (next es)
                   (if (= :error (nth cur 1)) cur [(nth cur 0) :error (or (ex-message (:actual m)) (pr-str (:actual m)))])
                   out)
            (and cur (= t :fail))
            (recur (next es)
                   (if (= :pass (nth cur 1)) [(nth cur 0) :fail (pr-str (:expected m))] cur)
                   out)
            :else (recur (next es) cur out)))))))
"""

private struct Library {
	let dir: URL
	let name: String
	let loadPath: [String]
	let features: Set<String>
	let namespaces: [String] // test namespaces in load order

	init(dir: URL) throws {
		self.dir = dir
		let manifest = try Value(reading: String(contentsOf: dir.appendingPathComponent("manifest.edn"), encoding: .utf8))
		let m = manifest.dictionary ?? [:]
		name = m[kw("name")]?.string ?? dir.lastPathComponent
		loadPath = (m[kw("load-path")]?.array ?? []).compactMap(\.string).map { dir.appendingPathComponent($0).path }
		features = Set((m[kw("features")]?.description ?? "#{}").split(whereSeparator: { "#{} ".contains($0) }).map { String($0.dropFirst()) })
		var nss = (m[kw("test-namespaces")]?.array ?? []).map(\.description)
		for testDir in (m[kw("test-dirs")]?.array ?? []).compactMap(\.string) {
			let root = dir.appendingPathComponent(testDir)
			let files = (FileManager.default.enumerator(atPath: root.path)?.allObjects as? [String] ?? []).filter { $0.hasSuffix(".cljc") || $0.hasSuffix(".clj") }
			for file in files.sorted() {
				var stem = file
				stem = String(stem[..<stem.lastIndex(of: ".")!])
				nss.append(stem.replacingOccurrences(of: "/", with: ".").replacingOccurrences(of: "_", with: "-"))
			}
		}
		namespaces = nss
	}

	func relative(_ path: String) -> String {
		path.hasPrefix(dir.path + "/") ? String(path.dropFirst(dir.path.count + 1)) : path
	}
}

private struct RunResult {
	var forms: [FormFailure] = []
	var tests: [TestOutcome] = []
	var skipped: Set<String> = []
	var loadErrors: [String: String] = [:] // ns → message when the require itself failed
}

extension CoreTests {
	@Suite struct CorpusTests {
		private static func run(_ lib: Library) throws -> RunResult {
			var result = RunResult()
			Runtime.loadPath = lib.loadPath
			Runtime.readerFeatures = lib.features
			clj_load_set_lenient(true)
			defer {
				clj_load_set_lenient(false)
				Runtime.readerFeatures = []
				Runtime.loadPath = []
			}
			_ = Value(owning: clj_load_take_failures())
			let output = try capturingOutput {
				for ns in lib.namespaces {
					progress("corpus: loading \(ns)")
					do {
						_ = try cljEval("(require '\(ns))")
					} catch let e as CljEvalFailure {
						result.loadErrors[ns] = e.message
					}
				}
			}
			for line in output.split(separator: "\n") where line.hasPrefix("SKIP - ") {
				result.skipped.insert(String(line.dropFirst("SKIP - ".count)))
			}
			let failures = Value(owning: clj_load_take_failures())
			for f in failures.array ?? [] {
				let m = f.dictionary ?? [:]
				result.forms.append(FormFailure(file: lib.relative(m[kw("file")]?.string ?? "?"), line: m[kw("line")]?.int ?? 0,
				                                name: m[kw("name")].flatMap { $0.isNil ? nil : $0.description },
				                                reason: m[kw("message")]?.string ?? m[kw("message")]?.description ?? "?"))
			}
			_ = try capturingOutput {
				for ns in lib.namespaces where result.loadErrors[ns] == nil {
					let loaded = try cljEval("(some? (find-ns '\(ns)))")
					if loaded != true { continue }
					progress("corpus: testing \(ns)")
					let rows = try cljEval("(corpus-harness/run-ns '\(ns))")
					for row in rows.array ?? [] {
						let cells = row.array ?? []
						result.tests.append(TestOutcome(name: cells[0].string ?? "?", status: String(cells[1].description.dropFirst()),
						                                reason: cells[2].isNil ? nil : cells[2].string ?? cells[2].description))
					}
				}
			}
			return result
		}

		private static func readAllowlist(_ lib: Library) throws -> Allowlist {
			let url = lib.dir.appendingPathComponent("allowlist.edn")
			var list = Allowlist()
			guard FileManager.default.fileExists(atPath: url.path) else { return list }
			let m = try Value(reading: String(contentsOf: url, encoding: .utf8)).dictionary ?? [:]
			for e in m[kw("forms")]?.array ?? [] {
				let d = e.dictionary ?? [:]
				list.forms["\(d[kw("file")]?.string ?? ""):\(d[kw("line")]?.int ?? 0)"] = e
			}
			for e in m[kw("tests")]?.array ?? [] {
				list.tests[e.dictionary?[kw("name")]?.description ?? ""] = e
			}
			for s in m[kw("skipped")]?.array ?? [] { list.skipped.insert(s.description) }
			return list
		}

		// Entries carry the failure's cause: the symbols it could not resolve, else the reason text alone.
		private static func entry(_ fields: [(String, String)]) -> String {
			"  {" + fields.map { ":\($0.0) \($0.1)" }.joined(separator: " ") + "}"
		}

		private static func writeAllowlist(_ lib: Library, _ r: RunResult) throws -> String {
			var lines: [String] = []
			lines.append(";; Generated by CorpusTests with CLJ_CORPUS_UPDATE=1 from a run against \(lib.name); see NOTES.md, \"Corpus\".")
			lines.append(";; A failing entry not listed here fails CI, and so does a listed one that now loads, passes or runs (stale).")
			lines.append(";; :missing names the symbols the runtime lacks; an entry that fails by design cites :design-line, a line of §8.")
			lines.append("{:forms")
			lines.append(" [")
			for f in r.forms.sorted(by: { ($0.file, $0.line) < ($1.file, $1.line) }) {
				var fields = [("file", ednString(f.file)), ("line", String(f.line))]
				if let name = f.name { fields.append(("name", name)) }
				fields.append(("reason", ednString(truncated(f.reason))))
				fields.append(("missing", "[" + (missingSymbol(in: f.reason).map { [$0] } ?? []).joined(separator: " ") + "]"))
				lines.append(entry(fields))
			}
			for (ns, message) in r.loadErrors.sorted(by: { $0.key < $1.key }) {
				lines.append(entry([("file", ednString(ns)), ("line", "0"), ("reason", ednString(truncated(message))), ("missing", "[" + (missingSymbol(in: message).map { [$0] } ?? []).joined() + "]")]))
			}
			lines.append(" ]")
			lines.append(" :tests")
			lines.append(" [")
			for t in r.tests.filter({ $0.status != "pass" }).sorted(by: { $0.name < $1.name }) {
				let reason = t.reason ?? ""
				lines.append(entry([("name", t.name), ("status", ":" + t.status), ("reason", ednString(truncated(reason))),
				                    ("missing", "[" + (missingSymbol(in: reason).map { [$0] } ?? []).joined() + "]")]))
			}
			lines.append(" ]")
			lines.append(" :skipped")
			lines.append(" [" + r.skipped.sorted().joined(separator: " ") + "]}")
			let text = lines.joined(separator: "\n") + "\n"
			try text.write(to: lib.dir.appendingPathComponent("allowlist.edn"), atomically: true, encoding: .utf8)
			return text
		}

		// docs/corpus.md: the counts and the weighted reasons per library, the human-readable backlog.
		private static func summary(_ lib: Library, _ r: RunResult) -> String {
			var out = "## \(lib.name)\n\n"
			let passed = r.tests.filter { $0.status == "pass" }.count
			let failed = r.tests.filter { $0.status == "fail" }.count
			let errored = r.tests.filter { $0.status == "error" }.count
			out += "- namespaces: \(lib.namespaces.count) requested, \(r.loadErrors.count) failed to load entirely\n"
			out += "- top-level forms that failed to load: \(r.forms.count)\n"
			out += "- tests: \(r.tests.count) ran, \(passed) passed, \(failed) failed, \(errored) errored\n"
			out += "- skipped by the suite's own when-var-exists (the var does not exist here): \(r.skipped.count)\n\n"
			var reasons: [String: Int] = [:]
			for f in r.forms { reasons[missingSymbol(in: f.reason).map { "missing `\($0)`" } ?? truncated(f.reason, 90), default: 0] += 1 }
			for t in r.tests where t.status != "pass" {
				reasons[missingSymbol(in: t.reason ?? "").map { "missing `\($0)`" } ?? (t.status == "error" ? "error: " : "assertion: ") + truncated(t.reason ?? "", 90), default: 0] += 1
			}
			out += "Top reasons (forms and tests):\n\n| count | reason |\n|---|---|\n"
			for (reason, count) in reasons.sorted(by: { $0.value > $1.value || ($0.value == $1.value && $0.key < $1.key) }).prefix(40) {
				out += "| \(count) | \(reason.replacingOccurrences(of: "|", with: "\\|")) |\n"
			}
			if !r.skipped.isEmpty {
				out += "\nSkipped vars: " + r.skipped.sorted().map { "`\($0)`" }.joined(separator: ", ") + "\n"
			}
			return out + "\n"
		}

		private static func check(_ lib: Library, _ r: RunResult, _ allow: Allowlist) -> [String] {
			var problems: [String] = []
			let failingForms = Set(r.forms.map(\.key)).union(r.loadErrors.keys.map { "\($0):0" })
			for key in failingForms.sorted() where allow.forms[key] == nil { problems.append("\(lib.name): form fails to load and is not allowlisted: \(key)") }
			for key in allow.forms.keys.sorted() where !failingForms.contains(key) { problems.append("\(lib.name): stale allowlist form entry, it loads now: \(key)") }
			let failingTests = Dictionary(r.tests.filter { $0.status != "pass" }.map { ($0.name, $0) }, uniquingKeysWith: { a, _ in a })
			for name in failingTests.keys.sorted() where allow.tests[name] == nil { problems.append("\(lib.name): test fails and is not allowlisted: \(name) — \(truncated(failingTests[name]?.reason ?? "", 100))") }
			for name in allow.tests.keys.sorted() where failingTests[name] == nil { problems.append("\(lib.name): stale allowlist test entry, it passes now: \(name)") }
			for name in r.skipped.sorted() where !allow.skipped.contains(name) { problems.append("\(lib.name): skipped var not allowlisted: \(name)") }
			for name in allow.skipped.sorted() where !r.skipped.contains(name) { problems.append("\(lib.name): stale skipped entry, the var exists now: \(name)") }
			for (_, e) in allow.tests.sorted(by: { $0.key < $1.key }) {
				let d = e.dictionary ?? [:]
				if d[kw("missing")] == nil && d[kw("design-line")] == nil { problems.append("\(lib.name): allowlist entry without :missing or :design-line: \(d[kw("name")]?.description ?? "?")") }
			}
			return problems
		}

		private static let update = ProcessInfo.processInfo.environment["CLJ_CORPUS_UPDATE"] != nil

		// Opt-in (CLJ_CORPUS=1, CLJ_CORPUS_LIB=name to pick one): the suite run still hangs inside some corpus tests (NOTES.md, "Corpus").
		@Test(.enabled(if: ProcessInfo.processInfo.environment["CLJ_CORPUS"] != nil))
		func librariesLoadAndTheirTestsMatchTheAllowlists() throws {
			clj_init()
			Runtime().define("corpus-progress*", in: "clojure.core", arity: 1...1) { args in
				progress("corpus: test \(args[0].description)")
				return nil
			}
			_ = try cljEval(harnessSource)
			defer { clj_ns_set_current(clj_ns_user()) }
			let dirs = try FileManager.default.contentsOfDirectory(at: corpusRoot, includingPropertiesForKeys: nil)
				.filter { FileManager.default.fileExists(atPath: $0.appendingPathComponent("manifest.edn").path) }
				.filter { dir in ProcessInfo.processInfo.environment["CLJ_CORPUS_LIB"].map { dir.lastPathComponent == $0 } ?? true }
				.sorted { $0.lastPathComponent < $1.lastPathComponent }
			#expect(!dirs.isEmpty)
			var doc = "# Corpus results\n\nGenerated by `CLJ_CORPUS_UPDATE=1 swift test --filter CorpusTests` (CorpusTests.swift); each library's `corpus/<lib>/allowlist.edn` holds the same failures one per line. The vendored sources and their origin are described in each `corpus/<lib>/SOURCE`.\n\n"
			var problems: [String] = []
			for dir in dirs {
				let lib = try Library(dir: dir)
				let first = try Self.run(lib)
				// Loading interns vars and keywords for the process; the second run over the loaded namespaces is the memory check.
				let before = clj_debug_live_objects()
				let second = try Self.run(lib)
				let after = clj_debug_live_objects()
				#expect(after == before, "\(lib.name): live objects after a second test run \(after - before)")
				#expect(second.tests.map(\.status) == first.tests.map(\.status), "\(lib.name): the two runs disagree")
				doc += Self.summary(lib, first)
				if Self.update {
					_ = try Self.writeAllowlist(lib, first)
				} else {
					problems += Self.check(lib, first, try Self.readAllowlist(lib))
				}
			}
			if Self.update {
				try doc.write(to: corpusRoot.deletingLastPathComponent().appendingPathComponent("docs/corpus.md"), atomically: true, encoding: .utf8)
			}
			for p in problems { Issue.record(Comment(rawValue: p)) }
		}
	}
}
