// @ai-generated(guided)
import CljCore

extension Runtime {
	/// What a call produced: its value, or the message of what it threw (`ex-message`, or the Swift error's text).
	public enum Outcome: Sendable, CustomStringConvertible {
		case returned(Value)
		case threw(String)

		public var description: String {
			switch self {
			case .returned(let v): "returned \(v)"
			case .threw(let m): "threw \"\(m)\""
			}
		}
	}

	/// One sample a primitive and its specification disagree on.
	public struct Divergence: Sendable, CustomStringConvertible {
		public let args: [Value]
		public let primitive: Outcome
		public let spec: Outcome

		public var description: String {
			"(\(args.map(\.description).joined(separator: " "))): primitive \(primitive), spec \(spec)"
		}
	}

	/// The differential test of the design (§6b): calls `primitive` and `spec` with every sample and reports
	/// the samples they disagree on — a value must be `=` to the spec's, a throw must meet a throw, and the
	/// messages must agree only with `messages`, for a spec that throws the primitive's own text (as one that
	/// delegates to the same leaf does). Empty is the pass; a test asserts on it with the divergences as the
	/// comment.
	public static func differential(primitive: Value, spec: Value, samples: [[Value]], messages: Bool = false) -> [Divergence] {
		samples.compactMap { args in
			let p = outcome(primitive, args), s = outcome(spec, args)
			return agree(p, s, messages: messages) ? nil : Divergence(args: args, primitive: p, spec: s)
		}
	}

	private static func outcome(_ f: Value, _ args: [Value]) -> Outcome {
		do {
			return .returned(try f.apply(args))
		} catch let e as ClojureError {
			return .threw(e.message)
		} catch {
			return .threw(String(describing: error))
		}
	}

	private static func agree(_ p: Outcome, _ s: Outcome, messages: Bool) -> Bool {
		switch (p, s) {
		case (.returned(let x), .returned(let y)): x == y
		case (.threw(let x), .threw(let y)): !messages || x == y
		default: false
		}
	}
}
