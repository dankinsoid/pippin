// @ai-generated(solo)
import CljCore
import Foundation

// A name reaches a metatype by mangling; recognition is a cast, because the printed name of an `any Error`
// is `NSError` for every `_BridgedStoredNSError` (design §4, `docs/portability.md`).
enum HostType {
	// The kind letter is not knowable from the symbol, so every candidate is tried and the core caches the hit.
	private static let kinds: [Character] = ["V", "O", "C", "P"]

	/// The type "Module/Name" (or "Module/Outer.Inner") names, with the mangled name that is its identity.
	static func resolve(_ text: String) -> (Any.Type, String)? {
		let parts = text.split(separator: "/", maxSplits: 1)
		guard parts.count == 2 else { return nil }
		let path = parts[1].split(separator: ".").map(String.init)
		guard !path.isEmpty, !path.contains(where: \.isEmpty) else { return nil }
		for candidate in mangles(module: String(parts[0]), path: path) {
			if let t = _typeByName(candidate) { return (t, _mangledTypeName(t) ?? candidate) }
		}
		return nil
	}

	/// How `String(reflecting:)` names a type, as Clojure spells it: the module is the namespace.
	static func display(of t: Any.Type) -> String {
		let name = String(reflecting: t)
		guard let dot = name.firstIndex(of: ".") else { return name }
		return name.replacingCharacters(in: dot...dot, with: "/")
	}

	// Swift's module is the substitution `s`; an Objective-C import is `So…C` with no module (`So7NSErrorC`).
	private static func mangles(module: String, path: [String]) -> [String] {
		var out: [String] = []
		func build(_ i: Int, _ acc: String) {
			if i == path.count {
				out.append(acc)
				return
			}
			for k in kinds where k != "P" || i == path.count - 1 {
				build(i + 1, acc + prefixed(path[i]) + String(k))
			}
		}
		build(0, module == "Swift" ? "s" : prefixed(module))
		if path.count == 1 { out.append("So" + prefixed(path[0]) + "C") }
		return out
	}

	private static func prefixed(_ s: String) -> String { "\(s.utf8.count)\(s)" }
}

// The core resolves one name once and caches both answers, so nothing is cached here.
func cljHostTypeResolve(_ name: UnsafePointer<CChar>?, _ len: Int,
                        _ mangled: UnsafeMutablePointer<UnsafePointer<CChar>?>?) -> UnsafeRawPointer? {
	guard let name, let mangled else { return nil }
	let text = String(decoding: UnsafeRawBufferPointer(start: name, count: len), as: UTF8.self)
	guard let (type, resolved) = HostType.resolve(text) else { return nil }
	// Leaked on purpose: the core keeps the string for the life of the process.
	mangled.pointee = UnsafePointer(strdup(resolved))
	return unsafeBitCast(type, to: UnsafeRawPointer.self)
}

func cljHostTypeCheck(_ metatype: UnsafeRawPointer?, _ payload: UnsafeMutableRawPointer?) -> Bool {
	guard let metatype, let payload else { return false }
	let error = Unmanaged<HostErrorBox>.fromOpaque(payload).takeUnretainedValue().error
	func isInstance<T>(_: T.Type) -> Bool { error is T }
	return _openExistential(unsafeBitCast(metatype, to: Any.Type.self), do: isInstance)
}

extension Runtime {
	/// Installs the resolver a `catch` clause naming a host type needs; `clj_host_boot` calls it.
	static func installHostTypes() { clj_host_type_install(cljHostTypeResolve, cljHostTypeCheck) }
}
