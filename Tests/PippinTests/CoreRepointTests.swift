// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'repoint-tests) " + source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalErrorScoped("(in-ns 'repoint-tests) " + source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

// Design §3 «"Наш хост" — это C-ядро». Import interns into immortal namespaces, so its tests take no baseline.
extension CoreTests {
	@Suite struct CoreRepointTests {
		init() throws {
			clj_init()
			for k in ["scheme", "user-info", "host", "port", "path", "query", "fragment", "eof", "e", "z", "none", "x", "nope", "d", "k"] { _ = kw(k) }
			_ = try cljEvalScoped("""
			(ns repoint.model)
			(defrecord User [name])
			(defprotocol Greet (greet [x]) (-shout [x]))
			(deftype Box [v] Greet (greet [_] (str "hi " v)) (-shout [_] (str "HI " v)))
			(ns repoint.dashed-ns)
			(defrecord Thing [])
			(ns repoint.other)
			(def Box 1)
			(ns repoint-tests
			  (:import [repoint.model User] (java.util Date) clojure.lang.ArityException [repoint.dashed_ns Thing]))
			(defn msg [f] (try (f) (catch :default e (ex-message e))))
			""")
		}

		@Test func importNamesATypeOfTheProgram() throws {
			#expect(try eval("[(User. \"a\") (instance? User (repoint.model/->User \"b\")) (identical? User repoint.model/User) (Thing.)]").description
				== "[#repoint.model.User{:name \"a\"} true true #repoint.dashed-ns.Thing{}]")
			#expect(try eval("(try (throw (repoint.model/->User \"x\")) (catch User u (:name u)))") == "x")
			// The import runs before the next form is analyzed, as on the JVM, where Box resolves at compile time.
			#expect(try eval("[(import '[repoint.model User]) (import 'repoint.model.Box)]").description == "[repoint.model.User repoint.model.Box]")
			#expect(try eval("[(identical? Box repoint.model/Box) (repoint.model/greet (Box. 1))]") == [true, "hi 1"])
			#expect(try eval("[(= (select-keys (ns-imports *ns*) '[User Box Thing]) {'User User 'Box Box 'Thing Thing}) (identical? Box (get (ns-map *ns*) 'Box))]") == [true, true])
			// A JVM class and a name no namespace or bridge provides stay unmapped; using one is the error.
			#expect(try eval("[(import '[nowhere.at.all Nothing] 'java.util.UUID 'NoPackage) (contains? (ns-imports *ns*) 'Date) (contains? (ns-imports *ns*) 'Nothing)]") == [nil, false, false])
			#expect(message("Nothing")?.contains("Unable to resolve symbol: Nothing") == true)
			#expect(try eval("(do (in-ns 'repoint.other) (repoint-tests/msg #(import 'repoint.model.Box)))") == "Box already refers to: #'repoint.other/Box in namespace: repoint.other")
			#expect(try eval("[(msg #(import 5)) (msg #(import '[5 A])) (msg #(import '[repoint.model 5]))]") == [
				"import expects a symbol or a list (package Name*), got: long", "import expects a package symbol first, got: long",
				"import expects an unqualified class name, got: long",
			])
			#expect(try eval("(do (ns-unmap *ns* 'Box) (import 'repoint.model.Box) [(contains? (ns-imports *ns*) 'Box) (do (ns-unmap *ns* 'Box) (contains? (ns-imports *ns*) 'Box))])") == [true, false])
		}

		// Not NSError: a host type prints the spelling that interned it first, and HostErrorTests reads that one.
		@Test func importNamesAHostTypeThroughABridge() throws {
			#expect(try eval("(some? (import '[Foundation NSNotification]))") == true)
			#expect(try eval("[(identical? NSNotification Foundation/NSNotification) (identical? NSNotification (get (ns-imports *ns*) 'NSNotification)) (identical? NSNotification (host-type 'Foundation/NSNotification))]") == [true, true, true])
		}

		@Test func dotDotAndMemfnSendTheGeneralMethodForm() throws {
			#expect(try eval("[(.. (repoint.model/->Box 1) greet) (.. (repoint.model/->Box 2) shout) (map (memfn greet) [(repoint.model/->Box 3)])]").description == "[\"hi 1\" \"HI 2\" (\"hi 3\")]")
			#expect(try eval("(.. (objc-class \"NSDate\") (date-with-time-interval-since-reference-date 2.5) time-interval-since-reference-date)") == 2.5)
			#expect(try eval("""
			(let [epoch (.date-with-time-interval-since-reference-date (objc-class "NSDate") 0.0)
			      init (memfn init-with-time-interval t :since-date d)]
			  (.time-interval-since-reference-date (init (.alloc (objc-class "NSDate")) 1.5 epoch)))
			""") == 1.5)
		}

		// The C identifier clj-compile gives a name (NOTES "Compiler", "Names"), not the JVM's munge.
		@Test func mungeIsTheCompilersCIdentifier() throws {
			#expect(try eval("[(munge \"a-b?\") (munge 'clojure.core/map) (munge \"1x\") (munge \"\") (munge :k) (munge \"a_b.c\")]") == [
				"a_b_QMARK_", Value(symbol: "clojure_core_SLASH_map"), "_1x", "_", "_COLON_k", "a_USCORE_b_c",
			])
		}

		@Test func uriIsAValueOfTheCore() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try eval("(let [u (parse-uri \"http://user@host:8080/p/a?q=1#f\")] [(uri? u) (:scheme u) (:user-info u) (:host u) (:port u) (:path u) (:query u) (:fragment u) (str u) (pr-str u)])") == [
					true, "http", "user", "host", 8080, "/p/a", "q=1", "f", "http://user@host:8080/p/a?q=1#f", "#object[URI \"http://user@host:8080/p/a?q=1#f\"]",
				])
				#expect(try eval("(mapv (juxt :scheme :host :port :path :query) (map parse-uri [\"mailto:a@b.c\" \"//h/x\" \"../a/b?c\" \"http://[::1]:80/\" \"file:///tmp/x\" \"\" \"http://h:/\"]))").description
					== "[[\"mailto\" nil nil \"a@b.c\" nil] [nil \"h\" nil \"/x\" nil] [nil nil nil \"../a/b\" \"c\"] [\"http\" \"[::1]\" 80 \"/\" nil] [\"file\" nil nil \"/tmp/x\" nil] [nil nil nil \"\" nil] [\"http\" \"h\" nil \"/\" nil]]")
				#expect(try eval("(mapv parse-uri [\"a b\" \"http://h:99999999999/\" \"1a:b\" \"x/%zz\" \"http://[::1/\" \"http://a@b@c/\" \"a:b|c\"])") == [nil, nil, nil, nil, nil, nil, nil])
				#expect(try eval("(let [a (parse-uri \"a:b\") b (parse-uri \"a:b\")] [(= a b) (= (hash a) (hash b)) (= a (parse-uri \"A:b\")) (= a \"a:b\") (uri? \"a:b\") (get a :nope :d)])") == [true, true, false, false, false, Value(keyword: "d")])
				#expect(message("(parse-uri 5)") == "long cannot be cast to a string")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// Every expectation is what JVM Clojure 1.12.6 answers for the same with-in-str.
		@Test func readTakesFromTheInMap() throws {
			#expect(try eval("(with-in-str \"1 (+ 1 2)\\n:x rest of line\\nnext\" [(read) (read) (read) (read-line) (read-line) (read-line)])").description == "[1 (+ 1 2) :x \" rest of line\" \"next\" nil]")
			#expect(try eval("(with-in-str \"  ; c\\n {:a\\n 1}  tail\" [(read+string) (read-line)])").description == "[[{:a 1} \"; c\\n {:a\\n 1}\"] \"  tail\"]")
			#expect(try eval("(with-in-str \"\" [(read *in* false :eof) (read {:eof :e} *in*) (read+string *in* false :z) (msg read)])").description == "[:eof :e [:z \"\"] \"EOF while reading\"]")
			#expect(try eval("[(with-in-str \"(1 2\" (msg #(read *in* false :eof))) (with-in-str \"1\\n\" [(read) (read-line) (read-line)]) (with-in-str \"; only\\n\" (read+string *in* false :z)) (with-in-str \"a\\nb\" [(read-line) (read-line) (read-line)])]").description
				== "[\"EOF while reading\" [1 \"\" nil] [:z \"; only\"] [\"a\" \"b\" nil]]")
			#expect(try eval("[(with-in-str \")\" (msg read)) (binding [*in* nil] [(read-line) (read *in* false :none) (msg read)]) (binding [*in* {:lines nil}] (msg read))]").description
				== "[\"Unmatched delimiter: )\" [nil :none \"EOF while reading\"] \"This *in* has no :pushback, so a read could not leave the rest of a line\"]")
		}
	}
}
