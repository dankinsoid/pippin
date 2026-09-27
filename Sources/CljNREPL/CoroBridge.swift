// @ai-generated(solo)
import CljCore
import Pippin

/// A pool coroutine, not a bare thread, so a park inside (`(<! ch)`) suspends rather than blocks (NOTES "Coroutines").
@discardableResult
func spawnCoroutine(_ body: @escaping () -> Void, onDone: @escaping () -> Void) -> Value {
	let fn = Value(function: "clj-nrepl/eval", arity: 0...0) { _ in
		body()
		return .nil_
	}
	let box = Unmanaged.passRetained(CoroDoneBox(onDone)).toOpaque()
	// Capture-free, so it converts to on_done's C function pointer without naming the opaque clj_coro type.
	return withExtendedLifetime(fn) {
		Value(owning: clj_coro_spawn(fn.raw, nil, 0, Int32(CLJ_AFFINITY_POOL), { _, ctx in
			Unmanaged<CoroDoneBox>.fromOpaque(ctx!).takeRetainedValue().onDone()
		}, box))
	}
}

/// Releases an owned result, draining the two pending slots when the call threw: nobody is left to report it to.
func discardResult(_ result: clj_value) {
	if result == CLJ_THROWN {
		_ = Value(owning: clj_take_pending_trace())
		_ = Value(owning: clj_take_pending())
	} else {
		clj_release(result)
	}
}

/// Pushes an output capture handing every write to `sink`: `:out` reaches the client while the form still runs.
func pushOutputStream(_ sink: @escaping (String) -> Void) {
	let box = Unmanaged.passRetained(OutputSinkBox(sink)).toOpaque()
	clj_output_push_stream({ bytes, len, ctx in
		let sink = Unmanaged<OutputSinkBox>.fromOpaque(ctx!).takeUnretainedValue().sink
		sink(String(decoding: UnsafeRawBufferPointer(start: bytes, count: len), as: UTF8.self))
	}, box, { Unmanaged<OutputSinkBox>.fromOpaque($0!).release() })
}

private final class OutputSinkBox {
	let sink: (String) -> Void
	init(_ sink: @escaping (String) -> Void) { self.sink = sink }
}

private final class CoroDoneBox {
	let onDone: () -> Void
	init(_ onDone: @escaping () -> Void) { self.onDone = onDone }
}
