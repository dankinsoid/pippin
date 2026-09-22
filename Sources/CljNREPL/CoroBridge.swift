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

private final class CoroDoneBox {
	let onDone: () -> Void
	init(_ onDone: @escaping () -> Void) { self.onDone = onDone }
}
