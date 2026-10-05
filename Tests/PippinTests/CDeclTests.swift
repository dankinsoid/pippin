// @ai-generated(solo)
#if canImport(AppKit)
import AppKit		// linked so dlsym finds NSAppKitVersionNumber, the fixture's extern const
#endif
import CljCore
import Foundation
import Testing
@testable import Pippin

// Level 0 end to end (design §5 «C — уровень 0», §10 step 8b): c-headergen.py parses what the fixture's ns declares.

private let packageRoot = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
private let fixtureDir = packageRoot.appendingPathComponent("Tests/PippinTests/Fixtures/c")
private let work = packageRoot.appendingPathComponent(".build/c-decls")

// One parse per machine and SDK, cached by the generator; the extra names are the refusals the fixture prints.
private let generated: Result<Void, any Error> = Result {
	let out = work.appendingPathComponent("decls")
	let process = Process()
	process.executableURL = URL(fileURLWithPath: "/usr/bin/env")
	process.arguments = ["python3", packageRoot.appendingPathComponent("scripts/c-headergen.py").path,
	                     "--scan", fixtureDir.appendingPathComponent("decls.clj").path,
	                     "--out", out.path, "--cache", work.appendingPathComponent("cache").path,
	                     "--module", "AppKit",
	                     "--refer", "NSLog,NSUIntegerMax,NSWindowDidResizeNotification,NSMaxRange,NSStringFromRect"]
	process.environment = ProcessInfo.processInfo.environment.filter { ["PATH", "HOME", "TMPDIR", "DEVELOPER_DIR"].contains($0.key) }
	let pipe = Pipe()
	process.standardOutput = pipe
	process.standardError = pipe
	try process.run()
	let text = String(decoding: pipe.fileHandleForReading.readDataToEndOfFile(), as: UTF8.self)
	process.waitUntilExit()
	guard process.terminationStatus == 0 else { throw CljEvalFailure(message: "c-headergen.py:\n\(text)") }
	// Reading the fixture's global here is what links AppKit: dlsym finds a global only in a linked image.
	guard NSAppKitVersion.current.rawValue > 0 else { throw CljEvalFailure(message: "no AppKit") }
	Runtime.loadPath = [out.path] + Runtime.loadPath
}

extension CoreTests {
	@Suite(.serialized) struct CDeclTests {
		@Test func theFixtureRunsAlikeInterpretedAndCompiled() throws {
			clj_init()
			try generated.get()
			let file = fixtureDir.appendingPathComponent("decls.clj").path
			let source = try String(contentsOfFile: file, encoding: .utf8)
			defer { clj_ns_set_current(clj_ns_user()) }
			let interpreted = try capturingOutput { try loadFixtureSource(source, file: file) }
			if ProcessInfo.processInfo.environment["CLJ_FIXTURE_UPDATE"] != nil {
				try interpreted.write(to: fixtureDir.appendingPathComponent("decls.out"), atomically: true, encoding: .utf8)
			}
			let expected = try String(contentsOf: fixtureDir.appendingPathComponent("decls.out"), encoding: .utf8)
			#expect(interpreted == expected, "interpreter output")
			try compileFixtureAsUnit(source, file: file, name: "c_decls")
			#expect(try runFixtureUnit(file) == expected, "compiled output")
			try compileFixtureAsUnit(source, file: file, name: "c_decls_closed", closed: true)
			#expect(try runFixtureUnit(file) == expected, "closed compiled output")
		}

		// The parse is the module's namespace: the values are vars of it, and a second require-c re-parses nothing.
		@Test func aHeaderIsANamespaceOfConstants() throws {
			clj_init()
			try generated.get()
			#expect(try cljEvalScoped("(do (require-c '[AppKit :refer [NSTextAlignmentCenter]]) AppKit/NSTextAlignmentCenter)") == Value(1))
			#expect(try cljEvalScoped("(:header (:pippin/c-parse (meta (find-ns 'AppKit))))") == Value("AppKit/AppKit.h"))
			// Asking for a name the parse was never given is an "Unable to resolve", not a nil var.
			let error = cljEvalErrorScoped("(require-c '[AppKit :refer [NSWindowStyleMaskTitled]])")
			#expect(error?.contains("Unable to resolve AppKit/NSWindowStyleMaskTitled") == true)
		}

		// An exported const is read from the process, so it is the framework's own value and not a copy of a number.
		@Test func anExternConstComesFromTheProcess() throws {
			clj_init()
			try generated.get()
			#expect(try cljEvalScoped("(do (require-c 'AppKit) AppKit/NSAppKitVersionNumber)") == Value(NSAppKitVersion.current.rawValue))
			#expect(cljEvalErrorScoped("(c-global* \"clj_no_such_c_global\" :double)")?
				.contains("Unable to resolve C global") == true)
			#expect(cljEvalErrorScoped("(c-global* \"NSAppKitVersionNumber\" :struct)")?
				.contains("does not read a struct") == true)
		}
	}
}
