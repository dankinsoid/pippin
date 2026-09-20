// @ai-generated(solo)
import CryptoKit
import Darwin
import Foundation

struct CorpusCompilationFingerprint {
	private var hash = SHA256()

	mutating func add(_ data: Data) {
		var length = UInt64(data.count).bigEndian
		withUnsafeBytes(of: &length) { hash.update(data: Data($0)) }
		hash.update(data: data)
	}

	mutating func add(_ text: String) { add(Data(text.utf8)) }

	mutating func addFile(_ file: URL) throws {
		add(file.path)
		add(try Data(contentsOf: file))
	}

	mutating func addTree(_ root: URL, matching: (URL) -> Bool = { _ in true }) throws {
		let files = try FileManager.default.contentsOfDirectory(at: root, includingPropertiesForKeys: [.isDirectoryKey])
		for file in files.sorted(by: { $0.path < $1.path }) {
			if try file.resourceValues(forKeys: [.isDirectoryKey]).isDirectory == true {
				try addTree(file, matching: matching)
			} else if matching(file) {
				try addFile(file)
			}
		}
	}

	var key: String { hash.finalize().map { String(format: "%02x", $0) }.joined() }
}

final class CorpusCompilationCache {
	struct Result: Codable {
		let stderr: String
		let status: Int32
	}

	private struct Entry: Codable {
		let result: Result
		let manifestKey: String
	}

	let directory: URL
	private let lock: Int32
	private var completion: URL { directory.appendingPathComponent("complete.json") }

	init(root: URL, library: String, key: String) throws {
		let parent = root.appendingPathComponent(library)
		try FileManager.default.createDirectory(at: parent, withIntermediateDirectories: true)
		directory = parent.appendingPathComponent(key)
		lock = open(parent.appendingPathComponent("lock").path, O_CREAT | O_RDWR | O_CLOEXEC, 0o600)
		guard lock >= 0 else { throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO) }
		while flock(lock, LOCK_EX) != 0 {
			if errno == EINTR { continue }
			let error = POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
			close(lock)
			throw error
		}
	}

	deinit { close(lock) }

	func load() throws -> Result? {
		guard FileManager.default.fileExists(atPath: completion.path) else { return nil }
		let entry = try JSONDecoder().decode(Entry.self, from: Data(contentsOf: completion))
		let manifestURL = directory.appendingPathComponent("units.txt")
		var fingerprint = CorpusCompilationFingerprint()
		try fingerprint.addFile(manifestURL)
		guard fingerprint.key == entry.manifestKey else { throw CocoaError(.fileReadCorruptFile) }
		let manifest = try String(contentsOf: manifestURL, encoding: .utf8)
		for line in manifest.split(separator: "\n") {
			guard let name = line.split(separator: "\t").first,
			      FileManager.default.fileExists(atPath: directory.appendingPathComponent("\(name).dylib").path) else { return nil }
		}
		return entry.result
	}

	func prepare() throws {
		if FileManager.default.fileExists(atPath: directory.path) { try FileManager.default.removeItem(at: directory) }
		try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
	}

	func save(_ result: Result) throws {
		let manifestURL = directory.appendingPathComponent("units.txt")
		let manifest = try String(contentsOf: manifestURL, encoding: .utf8)
		guard !manifest.isEmpty else { throw CocoaError(.fileReadCorruptFile) }
		var fingerprint = CorpusCompilationFingerprint()
		try fingerprint.addFile(manifestURL)
		try JSONEncoder().encode(Entry(result: result, manifestKey: fingerprint.key)).write(to: completion, options: .atomic)
	}
}

func corpusCompilerIdentity() throws -> Data {
	let process = Process()
	process.executableURL = URL(fileURLWithPath: "/usr/bin/xcrun")
	process.arguments = ["clang", "--version"]
	let output = Pipe()
	process.standardOutput = output
	process.standardError = output
	try process.run()
	let data = output.fileHandleForReading.readDataToEndOfFile()
	process.waitUntilExit()
	guard process.terminationStatus == 0 else { throw CocoaError(.executableLoad) }
	let sdk = Process()
	sdk.executableURL = URL(fileURLWithPath: "/usr/bin/xcrun")
	sdk.arguments = ["--show-sdk-path"]
	let sdkOutput = Pipe()
	sdk.standardOutput = sdkOutput
	sdk.standardError = sdkOutput
	try sdk.run()
	let sdkData = sdkOutput.fileHandleForReading.readDataToEndOfFile()
	sdk.waitUntilExit()
	guard sdk.terminationStatus == 0 else { throw CocoaError(.executableLoad) }
	return data + sdkData
}
