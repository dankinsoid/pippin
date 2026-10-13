// @ai-generated(solo)
// jvm.clj's report lines for the one workload file compiled beside this; `once` is the cold-process run.

let clock = ContinuousClock()

func milliseconds(_ d: Duration) -> Double {
	Double(d.components.seconds) * 1000 + Double(d.components.attoseconds) / 1e15
}

var first: String?

// The result is printed outside the timed span; comparing it with the first keeps every iteration's work live.
func timed() -> Double {
	let start = clock.now
	let r = run()
	let ms = milliseconds(clock.now - start)
	let text = r.printed
	if let first {
		precondition(text == first, "an iteration printed \(text), the first \(first)")
	} else {
		first = text
	}
	return ms
}

let firstMs = timed()
if CommandLine.arguments.dropFirst().first == "once" {
	print("first_ms \(firstMs)")
} else {
	// clj-corpus-bench's policy for our AOT builds: one iteration, then >= 3 and >= 2 s, at most 30.
	var times: [Double] = []
	while times.count < 30 && (times.count < 3 || times.reduce(0, +) < 2000) {
		times.append(timed())
	}
	let sorted = times.sorted()
	print("first_ms \(firstMs)")
	print("warmup_iterations 0")
	print("iterations \(times.count)")
	print("median_ms \(sorted[sorted.count / 2])")
	print("min_ms \(sorted[0])")
}
print("result \(first!)")
