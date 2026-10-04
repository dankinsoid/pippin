// @ai-generated(solo)
// The same probe through the Swift host, so the size table has the bridge in it too (docs/notes/ios.md).
import Pippin

let forms = [
	"(+ 1 2)",
	"(mapv inc [1 2 3])",
	"(require '[clojure.core.async :as a])",
	"(a/<!! (a/go (a/<! (a/timeout 10)) :from-a-coroutine))",
	"@(future (reduce + (range 100000)))",
]

let rt = Runtime()
var failures = 0
for form in forms {
	do {
		print("  \(form)\n  => \(try rt.eval(form))")
	} catch {
		print("  \(form)\n  !! \(error)")
		failures += 1
	}
}
print("swift-probe: \(failures) of \(forms.count) forms threw")
