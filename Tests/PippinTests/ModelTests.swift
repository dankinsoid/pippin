// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

// NOTES "Model tests": the environment variables below widen a pass (`make model-long`) or replay one batch.

private let here = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
private let prelude = (try? String(contentsOf: here.appendingPathComponent("Fixtures/model/prelude.clj"), encoding: .utf8)) ?? ""
private let work = here.deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent(".build/model-tests")
private let sequencesPerBatch = 40

private func setting(_ name: String, _ fallback: Int) -> Int {
	ProcessInfo.processInfo.environment[name].flatMap { Int($0) } ?? fallback
}

private enum Backend: String {
	case interpreted, compiled
}

nonisolated(unsafe) private var runs = 0

// A compiled unit is registered for its file, so every run gets a file of its own.
// @ai-generated(solo)
private func run(_ source: String, _ backend: Backend, name: String) throws -> String {
	runs += 1
	try FileManager.default.createDirectory(at: work, withIntermediateDirectories: true)
	let file = work.appendingPathComponent("\(name)-\(runs).clj").path
	try source.write(toFile: file, atomically: true, encoding: .utf8)
	defer { clj_ns_set_current(clj_ns_user()) }
	switch backend {
	case .interpreted:
		return try capturingOutput { try loadFixtureSource(source, file: file) }
	case .compiled:
		try compileFixtureAsUnit(source, file: file, name: "model_\(name)_\(runs)")
		return try runFixtureUnit(file)
	}
}

private func failures(_ out: String) -> [String] {
	out.split(separator: "\n").filter { $0.hasPrefix("MT-FAIL ") }.map(String.init)
}

// MT-FAIL b<seed>.s<i>.<step> ...
private func sequenceIndex(_ line: String) -> Int? {
	let fields = line.split(separator: " ")
	guard fields.count > 1 else { return nil }
	let parts = fields[1].split(separator: ".")
	guard parts.count > 1, parts[1].hasPrefix("s") else { return nil }
	return Int(parts[1].dropFirst())
}

// @ai-generated(solo)
private func pass(_ backend: Backend, seeds: Range<Int>, replay: String) {
	#expect(!prelude.isEmpty, "Fixtures/model/prelude.clj")
	for seed in seeds {
		// A crash ends the process without an issue; this line is what names the batch to replay.
		FileHandle.standardError.write(Data("model: \(backend.rawValue) batch \(seed)\n".utf8))
		let batch = Model.batch(prelude: prelude, seed: UInt64(seed), count: sequencesPerBatch)
		let out: String
		do {
			out = try run(batch.source, backend, name: "b\(seed)")
		} catch {
			Issue.record("model batch \(seed) (\(backend.rawValue)) threw: \(error)")
			continue
		}
		guard out.contains("MT-DONE") else {
			Issue.record("model batch \(seed) (\(backend.rawValue)) did not finish:\n\(out.suffix(2000))")
			continue
		}
		let failed = failures(out)
		guard let first = failed.first, let i = sequenceIndex(first), i < batch.seqs.count else {
			if let first = failed.first { Issue.record("model batch \(seed) (\(backend.rawValue)): \(first)") }
			continue
		}
		let prefix = Model.prefix(seed: UInt64(seed), i)
		// Each compiled candidate is a clang run.
		let minimal = Model.shrink(batch.seqs[i], budget: backend == .interpreted ? 400 : 40) { c in
			guard let p = Model.program(prelude: prelude, c, prefix: prefix), let o = try? run(p, backend, name: "shrink") else { return false }
			return !failures(o).isEmpty
		}
		let minimalOut = Model.program(prelude: prelude, minimal, prefix: prefix).flatMap { try? run($0, backend, name: "shrink") } ?? ""
		Issue.record("""
			model: \(failed.count) failed checks in batch \(seed) (\(backend.rawValue)), the first in sequence \(i):
			\(first)
			replay: \(replay)=\(seed) and one batch
			minimal sequence (\(minimal.steps.count) of \(batch.seqs[i].steps.count) steps), after Fixtures/model/prelude.clj:
			\(Model.emit(minimal, prefix: prefix) ?? "?")\
			\(failures(minimalOut).joined(separator: "\n"))
			""")
	}
}

extension CoreTests {
	@Suite struct ModelTests {
		@Test func interpreted() {
			let first = setting("CLJ_MODEL_SEED", 1)
			pass(.interpreted, seeds: first..<(first + setting("CLJ_MODEL_BATCHES", 6)), replay: "CLJ_MODEL_SEED")
		}

		@Test func compiled() {
			let first = setting("CLJ_MODEL_COMPILED_SEED", 10_001)
			pass(.compiled, seeds: first..<(first + setting("CLJ_MODEL_COMPILED", 1)), replay: "CLJ_MODEL_COMPILED_SEED")
		}
	}
}
