// @ai-generated(solo)
import Foundation
import Testing

@Suite struct CorpusCompilationCacheTests {
	private func temporaryDirectory() throws -> URL {
		let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
		try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
		return root
	}

	@Test func fingerprintTracksContentsAndArguments() throws {
		let root = try temporaryDirectory()
		defer { try? FileManager.default.removeItem(at: root) }
		let source = root.appendingPathComponent("library.clj")
		try Data("(inc 1)".utf8).write(to: source)
		var first = CorpusCompilationFingerprint()
		try first.addTree(root)
		first.add("--lenient")
		var repeated = CorpusCompilationFingerprint()
		try repeated.addTree(root)
		repeated.add("--lenient")
		#expect(first.key == repeated.key)
		repeated.add("--closed")
		#expect(first.key != repeated.key)
		try Data("(inc 2)".utf8).write(to: source)
		var changed = CorpusCompilationFingerprint()
		try changed.addTree(root)
		changed.add("--lenient")
		#expect(first.key != changed.key)
		try Data("header".utf8).write(to: root.appendingPathComponent("runtime.h"))
		var added = CorpusCompilationFingerprint()
		try added.addTree(root)
		added.add("--lenient")
		#expect(changed.key != added.key)
	}

	@Test func fingerprintFramesItsInputs() {
		var first = CorpusCompilationFingerprint()
		first.add("ab")
		first.add("c")
		var second = CorpusCompilationFingerprint()
		second.add("a")
		second.add("bc")
		#expect(first.key != second.key)
	}

	@Test func onlyCompleteLibrariesAreReusedAndDiagnosticsSurvive() throws {
		let root = try temporaryDirectory()
		defer { try? FileManager.default.removeItem(at: root) }
		let cache = try CorpusCompilationCache(root: root, library: "lib", key: "key")
		try cache.prepare()
		try Data("unit\t/library.clj\n".utf8).write(to: cache.directory.appendingPathComponent("units.txt"))
		let dylib = cache.directory.appendingPathComponent("unit.dylib")
		try Data("compiled".utf8).write(to: dylib)
		#expect(try cache.load() == nil)
		try cache.save(.init(stderr: "refused: /library.clj:1:1 node", status: 2))
		let hit = try cache.load()
		let loaded = try #require(hit)
		#expect(loaded.status == 2)
		#expect(loaded.stderr == "refused: /library.clj:1:1 node")
		try FileManager.default.removeItem(at: dylib)
		#expect(try cache.load() == nil)
		try cache.prepare()
		#expect(try cache.load() == nil)
		withExtendedLifetime(cache) {}
	}
}
