// @ai-generated(solo)
import CljCore
import Darwin
import Foundation
import Testing
@testable import Pippin

// The first level-2 slice end to end (design §5, §10 step 8): Fixtures/swift/PippinFixture.swift built with swiftc,
// its symbol graph classified and printed as stubs by scripts/swift-stubgen.py, the dylib loaded by require-swift.

private let packageRoot = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
private let fixtureDir = packageRoot.appendingPathComponent("Tests/PippinTests/Fixtures/swift")
private let stubsWork = packageRoot.appendingPathComponent(".build/swift-stubs")

// One build of the fixture module per source and compiler, then the generator configured against this build's
// Pippin: the test bundle's directory holds Modules/Pippin.swiftmodule and CljCore's module map.
private let configured: Result<URL, any Error> = Result {
	let source = fixtureDir.appendingPathComponent("PippinFixture.swift")
	var hasher = CorpusCompilationFingerprint()
	try hasher.addFile(source)
	hasher.add(try toolOutput(["swiftc", "--version"]))
	let built = stubsWork.appendingPathComponent("fixture/\(hasher.key.prefix(24))")
	if !FileManager.default.fileExists(atPath: built.appendingPathComponent("libPippinFixture.dylib").path) {
		let tmp = built.appendingPathExtension("tmp-\(getpid())")
		try? FileManager.default.removeItem(at: tmp)
		try FileManager.default.createDirectory(at: tmp, withIntermediateDirectories: true)
		_ = try toolOutput(["swiftc", "-emit-library", "-emit-module", "-module-name", "PippinFixture", source.path,
		                    "-o", tmp.appendingPathComponent("libPippinFixture.dylib").path,
		                    "-emit-module-path", tmp.appendingPathComponent("PippinFixture.swiftmodule").path,
		                    "-Xlinker", "-install_name", "-Xlinker", "@rpath/libPippinFixture.dylib"])
		if (try? FileManager.default.moveItem(at: tmp, to: built)) == nil { try? FileManager.default.removeItem(at: tmp) }
	}
	var info = Dl_info()
	let entry: @convention(c) () -> Void = clj_init
	guard dladdr(unsafeBitCast(entry, to: UnsafeRawPointer.self), &info) != 0, let image = info.dli_fname else {
		throw CocoaError(.executableLoad)
	}
	// <debug>/PippinPackageTests.xctest/Contents/MacOS/PippinPackageTests
	let debug = URL(fileURLWithPath: String(cString: image)).resolvingSymlinksInPath()
		.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
	SwiftStubs.generator = SwiftStubs.Generator(
		script: packageRoot.appendingPathComponent("scripts/swift-stubgen.py"),
		cache: stubsWork.appendingPathComponent("cache"),
		moduleSearchPaths: [built], librarySearchPaths: [built], libraries: ["PippinFixture"],
		runtimeModules: debug.appendingPathComponent("Modules"),
		moduleMaps: [debug.appendingPathComponent("CljCore.build/module.modulemap")])
	return built
}

// xcrun with the variables that pick the Xcode; the runner's DYLD_* would follow every tool it loads.
private func toolOutput(_ args: [String]) throws -> String {
	let process = Process()
	process.executableURL = URL(fileURLWithPath: "/usr/bin/xcrun")
	process.arguments = args
	process.environment = ProcessInfo.processInfo.environment.filter { ["PATH", "HOME", "TMPDIR", "DEVELOPER_DIR"].contains($0.key) }
	let out = Pipe()
	process.standardOutput = out
	process.standardError = out
	try process.run()
	let data = out.fileHandleForReading.readDataToEndOfFile()
	process.waitUntilExit()
	let text = String(decoding: data, as: UTF8.self)
	guard process.terminationStatus == 0 else { throw CljEvalFailure(message: "xcrun \(args.joined(separator: " ")): \(text)") }
	return text
}

private func fixture() throws {
	_ = try configured.get()
	_ = try cljEvalScoped("(require-swift 'PippinFixture)")
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

extension CoreTests {
	@Suite(.serialized) struct SwiftStubTests {
		@Test func theFixtureRunsAlikeInterpretedAndCompiled() throws {
			try fixture()
			let file = fixtureDir.appendingPathComponent("stubs.clj").path
			let source = try String(contentsOfFile: file, encoding: .utf8)
			defer { clj_ns_set_current(clj_ns_user()) }
			let hops = SwiftStubs.hops
			let interpreted = try capturingOutput { try loadFixtureSource(source, file: file) }
			if ProcessInfo.processInfo.environment["CLJ_FIXTURE_UPDATE"] != nil {
				try interpreted.write(to: fixtureDir.appendingPathComponent("stubs.out"), atomically: true, encoding: .utf8)
			}
			let expected = try String(contentsOf: fixtureDir.appendingPathComponent("stubs.out"), encoding: .utf8)
			#expect(interpreted == expected, "interpreter output")
			// Off the main thread every @MainActor call that got past its arguments hopped: four in the file.
			#expect(SwiftStubs.hops - hops == 4)
			try compileFixtureAsUnit(source, file: file, name: "swift_stubs")
			#expect(try runFixtureUnit(file) == expected, "compiled output")
			try compileFixtureAsUnit(source, file: file, name: "swift_stubs_closed", closed: true)
			#expect(try runFixtureUnit(file) == expected, "closed compiled output")
		}

		// The register entry binds a var per base name; the doc names the Swift symbols and their isolation.
		@Test func aModuleIsANamespaceOfVars() throws {
			try fixture()
			#expect(try cljEvalScoped("(:doc (meta #'PippinFixture/moved))") == Value("Swift: moved(_:by:) @MainActor"))
			#expect(try cljEvalScoped("(fn? @#'PippinFixture/make-point)") == Value(true))
			// Loading again finds the module registered: no generator run, no second registration.
			_ = try cljEvalScoped("(require-swift '[PippinFixture :refer [describe]])")
		}

		@Test func onTheMainThreadTheCallIsMadeInPlace() async throws {
			try fixture()
			let f = try cljEvalScoped("""
				(fn [] (PippinFixture/describe (PippinFixture/moved (PippinFixture/make-point :x 0 :y 0) :by 2)))
				""")
			let hops = SwiftStubs.hops
			// Value.apply is a synchronous host call: a hop here would have to park under it and fail.
			let value = try await MainActor.run { try f.apply([]) }
			#expect(value == Value("(2, 2)"))
			#expect(SwiftStubs.hops == hops)
		}

		// The main thread as the carrier, pumped by hand as AsyncBridgeTests does.
		@Test @MainActor func onTheMainCarrierTheCallIsMadeInPlace() throws {
			try #require(Thread.isMainThread)
			try fixture()
			let f = try cljEvalScoped("""
				(fn [] (PippinFixture/describe (PippinFixture/moved (PippinFixture/make-point :x 1 :y 1) :by 1)))
				""")
			let hops = SwiftStubs.hops
			clj_debug_sched_main_adopt()
			defer {
				// A run-loop source CoroTests installed was signalled by the enqueue; it must fire while this
				// thread is still the carrier, or its pump finds none.
				CFRunLoopRunInMode(CFRunLoopMode.defaultMode, 0, false)
				clj_debug_sched_main_abandon()
			}
			let task = f.callDetached(affinity: .main)
			let result = Outcome()
			Task.detached { result.set(try? await task.value) }
			var pumps = 0
			while !result.isSet && pumps < 20000 {
				clj_sched_main_pump()
				usleep(200)
				pumps += 1
			}
			#expect(pumps < 20000, "the main-carrier call never finished")
			#expect(result.value == Value("(2, 2)"))
			#expect(SwiftStubs.hops == hops)
		}

		@Test func fromAPoolCoroutineTheCallHops() async throws {
			try fixture()
			let f = try cljEvalScoped("(fn [] (PippinFixture/describe (PippinFixture/moved (PippinFixture/make-point :x 5 :y 5) :by 1)))")
			let hops = SwiftStubs.hops
			#expect(try await f.callAsync() == Value("(6, 6)"))
			#expect(SwiftStubs.hops == hops + 1)
		}

		// Off the main thread under a synchronous host call the hop would park where parking is illegal: the error
		// comes with the caller's frames, and before the Swift function ran.
		@Test func offMainUnderAHostCallTheHopRefusesLoudly() async throws {
			try fixture()
			_ = try cljEvalScoped("""
				(ns stub.refuse)
				(defn hop-under-a-host-call [] (PippinFixture/moved (PippinFixture/make-point :x 0 :y 0) :by 1))
				(in-ns 'user)
				""")
			let f = try cljEvalScoped("@#'stub.refuse/hop-under-a-host-call")
			let count = try cljEvalScoped("@#'PippinFixture/move-count")
			let before = try count.apply([])
			let error = await Task.detached { () -> ClojureError? in
				do {
					_ = try f.apply([])
					return nil
				} catch let e as ClojureError {
					return e
				} catch {
					return nil
				}
			}.value
			let e = try #require(error)
			#expect(e.message.hasPrefix("moved(_:by:) is @MainActor and the caller is off the main thread"), "\(e.message)")
			#expect(e.message.contains("Cannot park inside a synchronous host call"), "\(e.message)")
			#expect(e.trace.contains { $0.fn == "stub.refuse/hop-under-a-host-call" }, "\(e)")
			#expect(try count.apply([]) == before)
		}

		@Test func theReportNamesEveryRefusedSymbolWithItsReason() throws {
			try fixture()
			let refusals = try #require(SwiftStubs.refusals(of: "PippinFixture"))
			func reason(_ name: String) -> String? { refusals.first { $0.swiftName == name }?.reason }
			#expect(reason("total(_:)")?.hasPrefix("variadic-parameter") == true)
			#expect(reason("first(_:)")?.hasPrefix("generic") == true)
			#expect(reason("origin()")?.contains("(Int, Int)") == true)
			#expect(reason("maybe(_:)")?.contains("optional") == true)
			#expect(reason("FixtureError.tooBig(_:)")?.hasPrefix("enum case") == true)
			#expect(reason("Point.hash(into:)") != nil)
			#expect(reason("Size.init(w:)")?.contains("crosses as a map") == true)
			#expect(reason("Size.w")?.contains("crosses as a map") == true)
			// Overloads by type: both refused, neither picked.
			#expect(refusals.filter { $0.swiftName == "width(_:)" && $0.reason.hasPrefix("overload") }.count == 2)
			for generated in ["moved(_:by:)", "risky(_:)", "later(_:)", "Point.move(by:)", "Counter.init(name:)", "applyTwice(_:to:)"] {
				#expect(reason(generated) == nil, "\(generated)")
			}
			// A refused symbol is no var: the analyzer reports the name, as for any unresolved one.
			#expect(cljEvalErrorScoped("(PippinFixture/total 1 2)")?.contains("total") == true)
			let reports = try FileManager.default.contentsOfDirectory(at: stubsWork.appendingPathComponent("cache/PippinFixture"),
			                                                          includingPropertiesForKeys: nil)
			#expect(reports.contains { FileManager.default.fileExists(atPath: $0.appendingPathComponent("report.json").path) })
		}

		// The four throws forms differ only in what is printed: the typed error is in the doc, Never has no error path.
		@Test func theDocNamesEffectsAndIsolation() throws {
			try fixture()
			func doc(_ name: String) throws -> Value { try cljEvalScoped("(:doc (meta #'PippinFixture/\(name)))") }
			#expect(try doc("checked") == Value("Swift: checked(_:) throws(FixtureError)"))
			#expect(try doc("safe") == Value("Swift: safe(_:)"))
			#expect(try doc("apply-twice") == Value("Swift: applyTwice(_:to:) rethrows"))
			#expect(try doc("fetch") == Value("Swift: fetch(_:) async throws"))
			#expect(try doc("Point.") == Value("Swift: Point.init(validating:) throws\nPoint.init(x:y:)"))
			#expect(try doc("Screen.title") == Value("Swift: Screen.title() @MainActor"))
		}

		// Uncaught, or caught and rethrown as is, the host error reaches Swift as the Swift error it was made from.
		@Test func aSwiftErrorComesBackToSwiftAsItself() throws {
			try fixture()
			for source in ["(fn [] (PippinFixture/risky 30))", "(fn [] (try (PippinFixture/checked 30) (catch :default e (throw e))))",
			               "(fn [] (PippinFixture/apply-twice (fn [x] (PippinFixture/risky (* 10 x))) :to 2))"] {
				let f = try cljEvalScoped(source)
				do {
					_ = try f.apply([])
					Issue.record("\(source) did not throw")
				} catch {
					#expect(String(reflecting: type(of: error)) == "PippinFixture.FixtureError", "\(source): \(error)")
					#expect("\(error)" == "tooBig(30)" || "\(error)" == "tooBig(20)", "\(source): \(error)")
				}
			}
		}

		@Test func cancellingTheParkedCallerCancelsTheSwiftTask() async throws {
			try fixture()
			let f = try cljEvalScoped("(fn [] (PippinFixture/wait-for-cancel))")
			let count = try cljEvalScoped("@#'PippinFixture/cancellations")
			let before = try count.apply([])
			let task = Task.detached { try await f.callAsync() }
			try await Task.sleep(nanoseconds: 20_000_000)
			task.cancel()
			let outcome = await task.result
			#expect(throws: CancellationError.self) { try outcome.get() }
			var waited = 0
			while try count.apply([]) == before && waited < 2000 {
				try await Task.sleep(nanoseconds: 1_000_000)
				waited += 1
			}
			#expect(try count.apply([]) == Value(before.int! + 1))
		}

		// An async stub parks its caller, so under a synchronous host call it refuses before the Swift function runs.
		@Test func anAsyncCallUnderAHostCallRefusesLoudly() async throws {
			try fixture()
			let f = try cljEvalScoped("(fn [] (PippinFixture/later 1))")
			let message = await Task.detached { () -> String? in
				do {
					_ = try f.apply([])
					return nil
				} catch let e as ClojureError {
					return e.message
				} catch {
					return "\(error)"
				}
			}.value
			#expect(message?.hasPrefix("later(_:) is async, so the caller parks until it returns") == true, "\(message ?? "no error")")
			#expect(try await f.callAsync() == Value(2))
		}

		// A nonisolated member of a @MainActor class runs where its caller is; its initializer hops.
		@Test func aNonisolatedMemberDoesNotHop() async throws {
			try fixture()
			let f = try cljEvalScoped("(fn [] (PippinFixture/Screen.id (PippinFixture/Screen.)))")
			let hops = SwiftStubs.hops
			#expect(try await f.callAsync() == Value(7))
			#expect(SwiftStubs.hops == hops + 1)
		}

		@Test func aModuleNothingCanBuildRefusesWithTheGeneratorsWords() throws {
			try fixture()
			let message = cljEvalErrorScoped("(require-swift 'NoSuchSwiftModule)")
			#expect(message?.contains("no NoSuchSwiftModule.swiftmodule") == true, "\(message ?? "no error")")
		}

		@Test func aHostWithoutTheBridgeRefusesRequireSwift() throws {
			clj_host_module_install(nil)
			defer { Runtime.installSwiftStubs() }
			let message = cljEvalErrorScoped("(require-swift 'PippinFixture)")
			#expect(message?.contains("this host has no Swift bridge") == true, "\(message ?? "no error")")
		}
	}
}

private final class Outcome: @unchecked Sendable {
	private let lock = NSLock()
	private var done = false
	private var stored: Value?
	func set(_ v: Value?) { lock.withLock { stored = v; done = true } }
	var isSet: Bool { lock.withLock { done } }
	var value: Value? { lock.withLock { stored } }
}
