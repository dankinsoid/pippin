// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

// Mutable deftype fields (design §4 «Роли конструкций»); Fixtures/compiler/mutable-fields.clj runs every backend.

private func message(_ rt: Runtime, _ source: String) -> String? {
	do {
		_ = try rt.eval(source)
		return nil
	} catch let e as ClojureError {
		return e.message
	} catch {
		return nil
	}
}

// In DeftypeSlotsTests, not a suite of its own: a new suite moves the committed shard deal (scripts/test-times.json).
extension CoreTests.DeftypeSlotsTests {
	@Test func mutableFieldForms() throws {
		_ = try rt.eval("(defprotocol MfP (mf-m [x]) (mf-put [x v]))")
		// a fn made in a method holds the value it read, as the JVM's closure does, and cannot set! it
		#expect(message(rt, "(deftype MfA [^:unsynchronized-mutable n] MfP (mf-m [_] (fn [] (set! n 1))))") == "Cannot assign to non-mutable: n")
		#expect(message(rt, "(deftype MfB [n] MfP (mf-m [_] (set! n 1)))") == "Cannot assign to non-mutable: n")
		#expect(message(rt, "(defrecord MfR [^:unsynchronized-mutable n])") == "defrecord fields cannot be mutable: MfR")
		#expect(message(rt, "(deftype MfC [^:unsynchronized-mutable ^:volatile-mutable n])") == "Field n is both ^:unsynchronized-mutable and ^:volatile-mutable")
		_ = try rt.eval("(deftype MfD [^:unsynchronized-mutable ^long n] MfP (mf-m [_] n) (mf-put [_ v] (set! n v)))")
		#expect(message(rt, "(let [x (->MfD 1)] (set! (.-n x) 2))") == "Invalid assignment target: (.-n x)")
		#expect(message(rt, "(set! n 2)")?.hasPrefix("Unable to resolve var: n") == true)
		#expect(message(rt, "(let [x (->MfD 1)] (set-field!* x 1 2))") == "set-field!* index out of range")
		#expect(try rt.eval("(let [x (->MfD 1) f (fn [] (mf-put x 5))] (f) [(mf-m x) (.-n x)])") == [5, 5])
		// a param named as the field shadows it
		_ = try rt.eval("(deftype MfE [^:unsynchronized-mutable n] MfP (mf-m [_] n) (mf-put [_ n] n))")
		#expect(try rt.eval("(let [x (->MfE 1)] [(mf-put x 7) (mf-m x)])") == [7, 1])
	}

	// The creator of an instance writes its unsynchronized fields; another execution may write a volatile one.
	@Test func unsynchronizedFieldsBelongToTheirExecution() throws {
		_ = try rt.eval("""
		(defprotocol MfOwn (mf-set [x v]) (mf-vset [x v]) (mf-get [x]))
		(deftype MfO [^:unsynchronized-mutable u ^:volatile-mutable v]
		  MfOwn (mf-set [_ x] (set! u x)) (mf-vset [_ x] (set! v x)) (mf-get [_] [u v]))
		(def mf-shared (->MfO 0 0))
		""")
		// published by the def, written by the execution that made it
		#expect(try rt.eval("(do (mf-set mf-shared 1) (mf-get mf-shared))") == [1, 0])
		#expect(try rt.eval("(do @(future (mf-vset mf-shared 2)) (mf-get mf-shared))") == [1, 2])
		let msg = message(rt, "@(future (mf-set mf-shared 3))")
		#expect(msg?.contains("Field u of user.MfO is ^:unsynchronized-mutable and set! from an execution other than the one that made") == true, "\(msg ?? "nil")")
		#expect(try rt.eval("(mf-get mf-shared)") == [1, 2])
		// one made on the other execution is that one's to write
		#expect(try rt.eval("@(future (let [x (->MfO 0 0)] (mf-set x 4) (mf-get x)))") == [4, 0])
		_ = try rt.eval("(def mf-shared nil)")
	}
}
