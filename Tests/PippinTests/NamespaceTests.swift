// @ai-generated(guided)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEval(source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func sym(_ s: String) -> Value { Value(symbol: s) }
private func kw(_ s: String) -> Value { Value(keyword: s) }

// Every test leaves the thread in `user`: the current namespace is process state the other suites read.
private func inUser(_ body: () throws -> Void) throws {
	defer { clj_ns_set_current(clj_ns_user()) }
	try body()
}

extension CoreTests {
	@Suite struct NamespaceTests {
		// Namespaces and vars are immortal: the ones a test creates exist before its baseline.
		private static func prepare(_ source: String) throws {
			try inUser { _ = try eval(source) }
		}

		@Test func inNsDefAndResolution() throws {
			clj_init()
			try Self.prepare("(in-ns 'ns-test.a) (def x 1) (def ^:private hidden 2) (defn f [] (inc x)) (in-ns 'ns-test.b) (def y) (in-ns 'user)")
			let before = clj_debug_live_objects()
			try inUser { () throws in
				#expect(try eval("(in-ns 'ns-test.a) x") == 1)
				#expect(try eval("(f)") == 2)
				#expect(try eval("(ns-name *ns*)") == sym("ns-test.a"))
				#expect(try eval("(in-ns 'ns-test.b) (def y ns-test.a/x) y") == 1)
				#expect(message("ns-test.a/hidden") == "var: ns-test.a/hidden is not public")
				#expect(try eval("@#'ns-test.a/hidden") == 2)
				#expect(message("x") == "Unable to resolve symbol: x in this context")
				#expect(try eval("(in-ns 'user) (ns-name *ns*)") == sym("user"))
				#expect(try eval("(str *ns*)") == "#object[namespace]")
				#expect(try eval("(ns? *ns*)") == true)
				#expect(try eval("[(ns? 'user) (var? #'clojure.core/eval) (var? 1)]") == [false, true, false])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func aliasesRefersAndSyntaxQuote() throws {
			clj_init()
			try Self.prepare("(in-ns 'ns-test.lib) (def v 42) (defn g [x] (* 2 x)) (defmacro twice [x] `(g ~x)) (in-ns 'user) (alias 'lib 'ns-test.lib) (refer 'ns-test.lib :only '[v] :rename '{v ns-test-r})")
			for k in ["sym", "lib", "ns-test.lib/k", "user/k"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			try inUser { () throws in
				#expect(try eval("lib/v") == 42)
				#expect(try eval("(lib/g 4)") == 8)
				#expect(try eval("(lib/twice 5)") == 10)
				#expect(try eval("`lib/g") == sym("ns-test.lib/g"))
				#expect(try eval("`(lib/g ::lib/k ::k)").description == "(ns-test.lib/g :ns-test.lib/k :user/k)")
				#expect(try eval("(identical? (get (ns-aliases 'user) 'lib) (the-ns 'ns-test.lib))") == true)
				#expect(message("(alias 'lib 'clojure.core)") == "Alias lib already exists in namespace user, aliasing ns-test.lib")
				#expect(try eval("ns-test-r") == 42)
				#expect(try eval("(identical? (resolve 'ns-test-r) #'ns-test.lib/v)") == true)
				#expect(message("(refer 'ns-test.lib :only '[nope])") == "nope does not exist")
				#expect(message("(refer 'no.such.ns)") == "No namespace: no.such.ns")
				#expect(try eval("(ns-resolve 'ns-test.lib 'v)").description == "#'ns-test.lib/v")
				#expect(try eval("(ns-resolve 'ns-test.lib 'no-such)") == nil)
				#expect(try eval("(resolve 'no.such/x)") == nil)
				#expect(try eval("(identical? (the-ns 'ns-test.lib) (find-ns 'ns-test.lib))") == true)
				#expect(message("(the-ns 'no.such.ns)") == "No namespace: no.such.ns found")
				#expect(try eval("(find-ns 'no.such.ns)") == nil)
				#expect(try eval("(contains? (set (map ns-name (all-ns))) 'clojure.core)") == true)
				let publics = try eval("#{'v 'g 'twice}")
				#expect(try eval("(set (keys (ns-publics 'ns-test.lib)))") == publics)
				#expect(try eval("(contains? (ns-map 'user) 'ns-test-r)") == true)
				#expect(try eval("(= (ns-interns 'ns-test.lib) (ns-publics 'ns-test.lib))") == true)
				#expect(try eval("(get (ns-refers 'user) 'ns-test-r)").description == "#'ns-test.lib/v")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func privateVarsAcrossNamespaces() throws {
			clj_init()
			try Self.prepare("(in-ns 'ns-test.p) (defn- secret [] 1) (defmacro ^:private hush [] 2) (defn open [] (secret)) (in-ns 'user)")
			let before = clj_debug_live_objects()
			try inUser { () throws in
				#expect(try eval("(ns-test.p/open)") == 1)
				#expect(message("(ns-test.p/secret)") == "var: ns-test.p/secret is not public")
				#expect(message("(ns-test.p/hush)") == "var: ns-test.p/hush is not public")
				#expect(message("(refer 'ns-test.p :only '[secret])") == "secret is not public")
				#expect(try eval("(contains? (ns-publics 'ns-test.p) 'secret)") == false)
				#expect(try eval("(contains? (ns-interns 'ns-test.p) 'secret)") == true)
				#expect(try eval("(in-ns 'ns-test.p) (secret)") == 1)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func nsMacroRequireAndReferClojure() throws {
			clj_init()
			try Self.prepare("(ns ns-test.m (:require [clojure.set :as s] [clojure.string :as str :refer [join]]) (:refer-clojure :exclude [max] :rename {min least})) (defn max [& xs] :mine) (in-ns 'user)")
			for k in ["mine", "lib", "sym", "load"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			try inUser { () throws in
				let both = try eval("#{1 2}")
				#expect(try eval("(in-ns 'ns-test.m) (s/union #{1} #{2})") == both)
				#expect(try eval("(join \",\" [1 2])") == "1,2")
				#expect(try eval("(str/upper-case \"ab\")") == "AB")
				#expect(try eval("(max 1 2)") == kw("mine"))
				#expect(try eval("(least 1 2)") == 1)
				#expect(message("(min 1 2)") == "Unable to resolve symbol: min in this context")
				#expect(try eval("(clojure.core/min 1 2)") == 1)
				#expect(try eval("(contains? (ns-map 'ns-test.m) 'join)") == true)
				// (ns x) again without :refer-clojure resets the exclusions.
				#expect(try eval("(ns ns-test.m) (min 1 2)") == 1)
				#expect(try eval("(ns ns-test.m (:refer-clojure :exclude [max] :rename {min least})) (least 1 2)") == 1)
				#expect(try eval("(in-ns 'user) (contains? (loaded-libs) 'clojure.set)") == true)
				#expect(message("(require 'no.such.lib)") == "Could not locate no/such/lib.cljc or no/such/lib.clj on load path.")
				#expect(message("(ns ns-test.m (:load \"x\"))") == "Unsupported ns reference: :load")
				// :import names JVM classes: ignored, the class fails where it is used.
				#expect(try eval("(ns ns-test.m (:import [java.util ArrayList]) (:refer-clojure :exclude [max] :rename {min least})) (in-ns 'user) 1") == 1)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func requireFromTheLoadPath() throws {
			clj_init()
			let dir = FileManager.default.temporaryDirectory.appendingPathComponent("clj-ns-test-\(getpid())")
			try FileManager.default.createDirectory(at: dir.appendingPathComponent("acme/deep"), withIntermediateDirectories: true)
			defer { try? FileManager.default.removeItem(at: dir) }
			try """
			(ns acme.deep.core-lib (:require [clojure.string :as str]))
			(defonce loads (atom 0))
			(swap! loads inc)
			(defn shout [s] (str (str/upper-case s) "!"))
			(defn- secret [] :s)
			""".write(to: dir.appendingPathComponent("acme/deep/core_lib.cljc"), atomically: true, encoding: .utf8)
			try """
			(ns acme.user (:require [acme.deep.core-lib :as lib :refer [shout]]))
			(def greeting (shout "hi"))
			""".write(to: dir.appendingPathComponent("acme/user.clj"), atomically: true, encoding: .utf8)
			try "(ns acme.broken) (def ok 1) (undefined-fn 2)".write(to: dir.appendingPathComponent("acme/broken.clj"), atomically: true, encoding: .utf8)
			try "(def no-ns-form 1)".write(to: dir.appendingPathComponent("acme/nons.clj"), atomically: true, encoding: .utf8)
			let saved = Runtime.loadPath
			Runtime.loadPath = [dir.path]
			defer { Runtime.loadPath = saved }
			// Every namespace and var the test touches exists before the baseline: both are immortal. The first
			// reload happens here too: the thread's parked-root list is allocated on its first fn redefinition.
			try Self.prepare("(require 'acme.user) (require 'acme.deep.core-lib :reload) (try (require 'acme.broken) (catch :default e nil)) (try (require 'acme.nons) (catch :default e nil)) (require '[acme [user :as u] [deep.core-lib :as dl]] '[acme.future :as-alias fut]) (in-ns 'user)")
			for k in ["file", "line", "column", "lib", "reload"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			try inUser { () throws in
				#expect(try eval("acme.user/greeting") == "HI!")
				#expect(try eval("(require 'acme.user) (require 'acme.deep.core-lib) @acme.deep.core-lib/loads") == 2)
				#expect(try eval("(require 'acme.deep.core-lib :reload) @acme.deep.core-lib/loads") == 3)
				#expect(try eval("(:file (meta #'acme.deep.core-lib/shout))") == Value(dir.appendingPathComponent("acme/deep/core_lib.cljc").path))
				#expect(try eval("(:line (meta #'acme.deep.core-lib/shout))") == 4)
				#expect(try eval("(ns-name *ns*)") == sym("user"))
				let broken = message("(require 'acme.broken)") ?? ""
				#expect(broken.hasPrefix("Syntax error compiling at (") && broken.contains("acme/broken.clj:1:29). Unable to resolve symbol: undefined-fn"))
				#expect(message("(require 'acme.nons)") == "namespace 'acme.nons' not found after loading '\(dir.path)/acme/nons.clj'")
				#expect(try eval("(ns-name *ns*)") == sym("user"))
				#expect(try eval("(require '[acme [user :as u] [deep.core-lib :as dl]])") == nil)
				// shout is referred into acme.user, not interned there: u/shout is no var, as in Clojure.
				#expect(try eval("[u/greeting (dl/shout \"y\")]") == ["HI!", "Y!"])
				#expect(message("u/shout") == "Unable to resolve symbol: u/shout in this context")
				#expect(try eval("(require '[acme.future :as-alias fut]) `fut/thing") == sym("acme.future/thing"))
				#expect(try eval("(contains? (loaded-libs) 'acme.future)") == false)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func embeddedNamespacesLoadOnce() throws {
			clj_init()
			try Self.prepare("(require 'clojure.set 'clojure.string 'clojure.walk 'clojure.template)")
			for k in ["a", "b", "lib"] { _ = kw(k) }
			let before = clj_debug_live_objects()
			try inUser { () throws in
				#expect(try eval("(require 'clojure.walk) (clojure.walk/postwalk-replace {:a 1} [:a {:a :a}])") == [1, Value([1: 1])])
				#expect(try eval("(clojure.walk/keywordize-keys {\"a\" {\"b\" 1}})") == Value([kw("a"): Value([kw("b"): 1])]))
				#expect(try eval("(clojure.walk/prewalk (fn [x] (if (number? x) (inc x) x)) '(1 [2 {3 4}]))").description == "(2 [3 {4 5}])")
				#expect(try eval("(clojure.walk/macroexpand-all '(when a (when b c)))").description == "(if a (do (if b (do c))))")
				#expect(try eval("(clojure.template/apply-template '[x y] '(+ x y) [1 2])").description == "(+ 1 2)")
				#expect(try eval("(clojure.template/do-template [a b] (+ a b) 1 2 3 4)") == 7)
				#expect(try eval("(:file (meta #'clojure.set/union))") == "<embedded>/clojure/set.clj")
				#expect(try eval("(load-resource* \"clojure/walk\")") == "<embedded>/clojure/walk.clj")
				#expect(try eval("(load-resource* \"clojure/nope\")") == nil)
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func clojureString() throws {
			clj_init()
			try Self.prepare("(require '[clojure.string :as str])")
			let before = clj_debug_live_objects()
			try inUser { () throws in
				#expect(try eval("[(str/upper-case \"aBc\") (str/lower-case \"aBc\") (str/capitalize \"hELLO\") (str/capitalize \"x\") (str/capitalize \"\")]") == ["ABC", "abc", "Hello", "X", ""])
				#expect(try eval("[(str/reverse \"héllo\") (str/reverse \"\")]") == ["olléh", ""])
				#expect(try eval("[(str/join [1 2 3]) (str/join \", \" [1 2 3]) (str/join \"-\" []) (str/join \"-\" [1])]") == ["123", "1, 2, 3", "", "1"])
				#expect(try eval("[(str/trim \"  a b \\t\\n\") (str/triml \"  a \") (str/trimr \"  a \") (str/trim \"\") (str/trim-newline \"ab\\n\\r\\n\") (str/trim-newline \"\\n\")]") == ["a b", "a ", "  a", "", "ab", ""])
				#expect(try eval("[(str/blank? nil) (str/blank? \"\") (str/blank? \" \\t\") (str/blank? \" a \")]") == [true, true, true, false])
				#expect(try eval("[(str/replace \"xx\" \"x\" \"y\") (str/replace \"xx\" \\x \\y) (str/replace \"x\" \"\" \"y\") (str/replace \"yxy\" \"x\" \"y\") (str/replace \"xx\" \"xxx\" \"y\") (str/replace \"\" \"x\" \"y\")]") == ["yy", "yy", "yxy", "yyy", "xx", ""])
				#expect(try eval("[(str/replace-first \"xxx\" \"x\" \"y\") (str/replace-first \"abc\" \\b \\B) (str/replace-first \"abc\" \"z\" \"y\")]") == ["yxx", "aBc", "abc"])
				#expect(try eval("[(str/split \"a,b,,c,,\" \",\") (str/split \"a,b,c\" \",\" 2) (str/split \"a,b,,\" \",\" -1) (str/split \"abc\" \"\") (str/split \"\" \",\") (str/split \"abc\" \",\")]") == [["a", "b", "", "c"], ["a", "b,c"], ["a", "b", "", ""], ["a", "b", "c"], [""], ["abc"]])
				#expect(try eval("[(str/split-lines \"a\\nb\\r\\nc\\n\") (str/split-lines \"\") (str/split-lines \"a\")]") == [["a", "b", "c"], [""], ["a"]])
				#expect(try eval("[(str/index-of \"héllo\" \"l\") (str/index-of \"hello\" \\l 3) (str/index-of \"hello\" \"z\") (str/index-of \"hello\" \"l\" 10) (str/last-index-of \"hello\" \"l\") (str/last-index-of \"hello\" \"l\" 2) (str/last-index-of \"hello\" \"z\")]") == [2, 3, nil, nil, 3, 2, nil])
				#expect(try eval("[(str/starts-with? \"hello\" \"he\") (str/starts-with? \"h\" \"he\") (str/ends-with? \"hello\" \"lo\") (str/ends-with? \"hello\" \"x\") (str/includes? \"hello\" \"ell\") (str/includes? \"hello\" \"z\")]") == [true, false, true, false, true, false])
				#expect(try eval("(str/escape \"a<b>\" {\\< \"&lt;\" \\> \"&gt;\"})") == "a&lt;b&gt;")
				#expect(try eval("[(subs \"héllo\" 1) (subs \"héllo\" 1 3) (subs \"abc\" 3) (subs \"abc\" 0 0)]") == ["éllo", "él", "", ""])
				#expect(message("(subs \"abc\" 4)") == "String index out of range: 4")
				#expect(message("(subs \"abc\" 2 1)") == "String index out of range: 1")
				#expect(message("(str/split \"a\" 1)") == "expected a string or char, got: long")
				#expect(try eval("[(char 97) (int \\a) (int 3.9) (int -3.9) (int 7)]") == [Value(Unicode.Scalar(97)), 97, 3, -3, 7])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func setBangAndTheCurrentNamespaceBinding() throws {
			clj_init()
			try Self.prepare("(def ^:dynamic ns-test-dyn 1) (def ns-test-plain 1) (in-ns 'ns-test.z) (def loaded-here) (in-ns 'user)")
			let before = clj_debug_live_objects()
			try inUser { () throws in
				#expect(message("(set! ns-test-dyn 2)") == "Can't change/establish root binding of: user/ns-test-dyn with set")
				#expect(message("(let [x 1] (set! x 2))") == "Cannot assign to non-mutable: x")
				#expect(message("(set! 1 2)") == "Invalid assignment target: 1")
				#expect(message("(set! ns-test-dyn)") == "Malformed assignment, expecting (set! target val)")
				// *ns* is dynamic: bound around a load, set by in-ns through the binding, root otherwise.
				#expect(try eval("(push-thread-bindings {#'*ns* *ns*}) (in-ns 'ns-test.z) (let [n (ns-name *ns*)] (pop-thread-bindings) [n (ns-name *ns*)])") == [sym("ns-test.z"), sym("user")])
				#expect(try eval("(push-thread-bindings {#'*ns* (the-ns 'ns-test.z)}) (let [n (ns-name *ns*)] (set! *ns* (the-ns 'user)) (let [m (ns-name *ns*)] (pop-thread-bindings) [n m]))") == [sym("ns-test.z"), sym("user")])
				#expect(try eval("(load-string \"(in-ns 'ns-test.z) (def loaded-here 1)\") (ns-name *ns*)") == sym("user"))
				#expect(try eval("ns-test.z/loaded-here") == 1)
				#expect(message("(pop-thread-bindings)") == "Pop without matching push")
				#expect(message("(push-thread-bindings {#'ns-test-plain 2})") == "Can't dynamically bind non-dynamic var: user/ns-test-plain")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The host's eval keeps its own thread in `user`, as a load does, however the source moves.
		@Test func hostEvalBindsTheNamespace() throws {
			clj_init()
			let runtime = Runtime()
			_ = try runtime.eval("(in-ns 'ns-test.host) (def here 1) (in-ns 'user)")
			let before = clj_debug_live_objects()
			do {
				#expect(try runtime.eval("(ns ns-test.host) here") == 1)
				#expect(runtime.currentNamespace == "user")
				#expect(try runtime.eval("(ns-name *ns*)") == sym("user"))
				runtime.currentNamespace = "ns-test.host"
				#expect(try runtime.eval("here") == 1)
				runtime.currentNamespace = "user"
				#expect(try runtime.eval("ns-test.host/here") == 1)
			}
			#expect(clj_debug_live_objects() == before)
		}
	}
}
