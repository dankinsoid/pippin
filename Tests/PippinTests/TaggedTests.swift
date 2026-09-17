// @ai-generated(solo)
import CljCore
import Testing
@testable import Pippin

private func kw(_ s: String) -> Value { Value(keyword: s) }
private func sym(_ s: String) -> Value { Value(symbol: s) }

private func printed(_ source: String) throws -> String { try cljEval("(pr-str \(source))").string! }

private func message(_ source: String) -> String? {
	guard let text = cljEvalError(source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func readError(_ s: String) -> String? {
	do {
		_ = try Value(reading: s)
		return nil
	} catch let e as ReaderError {
		return e.message
	} catch {
		return nil
	}
}

extension CoreTests {
	// Tagged literals (reader.c F_TAG, runtime.c clj_reader_read_tag), the uuid and inst values behind #uuid and #inst,
	// namespaced maps and record literals.
	@Suite struct TaggedTests {
		init() {
			clj_init()
			for k in ["a", "b", "c", "tag", "form", "x", "y", "foo/a", "foo/b", "ns/a", "user/a", "user/b", "clojure.core/a", "foo/x", "foo/y", "_/b", "z", "key", "mine", "tg", "b/c", "bar/x", "bar/a", "cljs", "clj", "default"] {
				_ = kw(k)
			}
		}

		@Test func uuidValues() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try printed(##"#uuid "F81D4FAE-7DEC-11D0-A765-00A0C91E6BF6""##) == ##"#uuid "f81d4fae-7dec-11d0-a765-00a0c91e6bf6""##)
				#expect(try cljEval(##"(str #uuid "f81d4fae-7dec-11d0-a765-00a0c91e6bf6")"##) == "f81d4fae-7dec-11d0-a765-00a0c91e6bf6")
				#expect(try cljEval(##"(print-str #uuid "f81d4fae-7dec-11d0-a765-00a0c91e6bf6")"##) == ##"#uuid "f81d4fae-7dec-11d0-a765-00a0c91e6bf6""##)
				#expect(try cljEval(##"[(uuid? #uuid "00000000-0000-0000-0000-000000000000") (uuid? "00000000-0000-0000-0000-000000000000") (uuid? nil)]"##) == [true, false, false])
				#expect(try cljEval(##"(= #uuid "f81d4fae-7dec-11d0-a765-00a0c91e6bf6" (parse-uuid "F81D4FAE-7DEC-11D0-A765-00A0C91E6BF6"))"##) == true)
				#expect(try cljEval(##"(= (hash #uuid "f81d4fae-7dec-11d0-a765-00a0c91e6bf6") (hash (parse-uuid "f81d4fae-7dec-11d0-a765-00a0c91e6bf6")))"##) == true)
				// UUID.hashCode: (int)(hilo >> 32) ^ (int)hilo.
				#expect(try cljEval(##"(hash #uuid "f81d4fae-7dec-11d0-a765-00a0c91e6bf6")"##) == Value(Int(Int32(bitPattern: 0xF81D4FAE ^ 0x7DEC11D0 ^ 0xA765_00A0 ^ 0xC91E6BF6))))
				#expect(try cljEval(##"[(instance? UUID (random-uuid)) (type (random-uuid))]"##).description == "[true UUID]")
				#expect(try cljEval("(not= (random-uuid) (random-uuid))") == true)
				// Version 4, variant 1: the 13th hex digit is 4 and the 17th one of 8, 9, a, b.
				#expect(try cljEval(##"(re-matches #"[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}" (str (random-uuid)))"##) != nil)
				#expect(try cljEval(##"(count (into #{} (repeatedly 100 random-uuid)))"##) == 100)
				// parse-uuid: nil on a malformed string, a throw on a non-string; fromString's lenient grouping.
				#expect(try cljEval(##"[(parse-uuid "") (parse-uuid "0") (parse-uuid "df0993") (parse-uuid "b6883c0a-0342-4007-9966-bc2dfa6b109eb") (parse-uuid "ab6883c0a-0342-4007-9966-bc2dfa6b109e")]"##) == [nil, nil, nil, nil, nil])
				#expect(try cljEval(##"[(parse-uuid "----") (parse-uuid "1-2-3-4") (parse-uuid "1-2-3-4-5-6") (parse-uuid "g-0-0-0-0")]"##) == [nil, nil, nil, nil])
				#expect(try printed(##"(parse-uuid "0-0-0-0-0")"##) == ##"#uuid "00000000-0000-0000-0000-000000000000""##)
				#expect(try printed(##"(parse-uuid "12-34-56-78-9")"##) == ##"#uuid "00000012-0034-0056-0078-000000000009""##)
				#expect(try printed(##"(parse-uuid "5-4-3-DEADBEEF0002-9000000001")"##) == ##"#uuid "00000005-0004-0003-0002-009000000001""##)
				#expect(message("(parse-uuid 1000)") == "long cannot be cast to a string")
				#expect(message("(parse-uuid :key)") == "keyword cannot be cast to a string")
				// compare: the JVM's signed halves, so a uuid starting with 8 sorts before one starting with 0.
				#expect(try cljEval(##"(compare #uuid "80000000-0000-0000-0000-000000000000" #uuid "00000000-0000-0000-0000-000000000000")"##) == -1)
				#expect(try cljEval(##"(compare #uuid "00000000-0000-0000-0000-000000000001" #uuid "00000000-0000-0000-0000-000000000000")"##) == 1)
				#expect(try cljEval(##"(compare #uuid "00000000-0000-0000-0000-000000000000" #uuid "00000000-0000-0000-0000-000000000000")"##) == 0)
				#expect(message(##"(compare #uuid "00000000-0000-0000-0000-000000000000" 1)"##) == "long cannot be cast to a UUID")
				#expect(try cljEval(##"(get {#uuid "00000000-0000-0000-0000-000000000001" :a} (parse-uuid "00000000-0000-0000-0000-000000000001"))"##) == kw("a"))
				#expect(readError(##"#uuid "nope""##) == "Invalid UUID string: nope")
				#expect(readError("#uuid 1") == "#uuid literal expects a string, got: long")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func instValues() throws {
			let before = clj_debug_live_objects()
			do {
				#expect(try printed(##"#inst "1985-04-12T23:20:50.52Z""##) == ##"#inst "1985-04-12T23:20:50.520-00:00""##)
				#expect(try printed(##"#inst "1985-04-12T23:20:50.52-00:00""##) == ##"#inst "1985-04-12T23:20:50.520-00:00""##)
				#expect(try printed(##"#inst "2010-11-12T13:14:15.666-05:00""##) == ##"#inst "2010-11-12T18:14:15.666-00:00""##)
				#expect(try printed(##"#inst "2010-11-12T13:14:15.666+05:30""##) == ##"#inst "2010-11-12T07:44:15.666-00:00""##)
				#expect(try printed(##"#inst "1970""##) == ##"#inst "1970-01-01T00:00:00.000-00:00""##)
				#expect(try printed(##"#inst "2020-02""##) == ##"#inst "2020-02-01T00:00:00.000-00:00""##)
				#expect(try printed(##"#inst "2020-02-29""##) == ##"#inst "2020-02-29T00:00:00.000-00:00""##)
				#expect(try printed(##"#inst "2020-02-29T10""##) == ##"#inst "2020-02-29T10:00:00.000-00:00""##)
				#expect(try printed(##"#inst "2020-02-29T10:30""##) == ##"#inst "2020-02-29T10:30:00.000-00:00""##)
				#expect(try printed(##"#inst "2020-02-29T10:30:59""##) == ##"#inst "2020-02-29T10:30:59.000-00:00""##)
				#expect(try printed(##"#inst "2020-02-29T10:30:59.123456789""##) == ##"#inst "2020-02-29T10:30:59.123-00:00""##)
				#expect(try printed(##"#inst "2020-02-29T10:30:59.9999""##) == ##"#inst "2020-02-29T10:30:59.999-00:00""##)
				#expect(try printed(##"#inst "2020-02-29T10:30:59.5""##) == ##"#inst "2020-02-29T10:30:59.500-00:00""##)
				#expect(try printed(##"#inst "1969-12-31T23:59:59.999Z""##) == ##"#inst "1969-12-31T23:59:59.999-00:00""##)
				#expect(try printed(##"#inst "0001-01-01T00:00:00Z""##) == ##"#inst "0001-01-01T00:00:00.000-00:00""##)
				#expect(try printed(##"#inst "9999-12-31T23:59:60Z""##) == ##"#inst "10000-01-01T00:00:00.000-00:00""##)
				#expect(try cljEval(##"(inst-ms #inst "1970-01-01T00:00:00.000-00:00")"##) == 0)
				#expect(try cljEval(##"(inst-ms #inst "1969-12-31T23:59:59.999-00:00")"##) == -1)
				#expect(try cljEval(##"(inst-ms #inst "1985-04-12T23:20:50.52Z")"##) == 482196050520)
				#expect(try cljEval(##"(inst-ms #inst "2010-11-12T13:14:15.666-05:00")"##) == 1289585655666)
				#expect(try cljEval(##"[(inst? #inst "2020") (inst? 0) (inst? "2020") (instance? Date #inst "2020")]"##) == [true, false, false, true])
				#expect(try cljEval(##"(str #inst "2020-01-02T03:04:05.006Z")"##) == "2020-01-02T03:04:05.006-00:00")
				#expect(try cljEval(##"[(= #inst "2020" #inst "2020-01-01T00:00:00Z") (= #inst "2020" 1577836800000) (= (hash #inst "2020") (hash #inst "2020-01-01"))]"##) == [true, false, true])
				#expect(try cljEval(##"[(compare #inst "2020" #inst "2021") (compare #inst "2021" #inst "2020") (compare #inst "2020" #inst "2020")]"##) == [-1, 1, 0])
				#expect(try cljEval(##"(sort [#inst "2021" #inst "2019" #inst "2020"])"##).description == ##"(#inst "2019-01-01T00:00:00.000-00:00" #inst "2020-01-01T00:00:00.000-00:00" #inst "2021-01-01T00:00:00.000-00:00")"##)
				#expect(message("(inst-ms 1)") == "inst-ms not supported on this type: long")
				#expect(readError(##"#inst "2020-13""##) == "Assert failed: (<= 1 months 12)")
				#expect(readError(##"#inst "2019-02-29""##) == "Assert failed: (<= 1 days (days-in-month months (leap-year? years)))")
				#expect(readError(##"#inst "2020-01-01T24""##) == "Assert failed: (<= 0 hours 23)")
				#expect(readError(##"#inst "2020-01-01T10:60""##) == "Assert failed: (<= 0 minutes 59)")
				#expect(readError(##"#inst "2020-01-01T10:30:60""##) == "Assert failed: (<= 0 seconds (if (= minutes 59) 60 59))")
				#expect(readError(##"#inst "2020-01-01T10:30:00+24:00""##) == "Assert failed: (<= -23 offset-hours 23)")
				#expect(readError(##"#inst "2020-01-01T10:30:00+05:60""##) == "Assert failed: (<= 0 offset-minutes 59)")
				#expect(readError(##"#inst "2020-1-1""##) == "Unrecognized date/time syntax: 2020-1-1")
				#expect(readError(##"#inst "2020-01-01T10:30:00.""##) == "Unrecognized date/time syntax: 2020-01-01T10:30:00.")
				#expect(readError(##"#inst "2020-01-01 10:30""##) == "Unrecognized date/time syntax: 2020-01-01 10:30")
				#expect(readError(##"#inst "20200""##) == "Unrecognized date/time syntax: 20200")
				#expect(readError(##"#inst "2020Z+01:00""##) == "Unrecognized date/time syntax: 2020Z+01:00")
				#expect(readError("#inst 2020") == "#inst literal expects a string, got: long")
			}
			#expect(clj_debug_live_objects() == before)
		}

		// The tag fn runs at read time, so a literal is a constant and the analyzer folds it like any other.
		@Test func tagResolution() throws {
			_ = try cljEval("(def tg-reader nil) (def tg-form nil)")
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval(##"[#uuid "00000000-0000-0000-0000-000000000000" 1]"##).description == ##"[#uuid "00000000-0000-0000-0000-000000000000" 1]"##)
				#expect(try cljEval(##"(let [u #uuid "00000000-0000-0000-0000-000000000000"] (uuid? u))"##) == true)
				#expect(try cljEval(##"'#uuid "00000000-0000-0000-0000-000000000000""##).description == ##"#uuid "00000000-0000-0000-0000-000000000000""##)
				#expect(try cljEval(##"(read-string "#inst \"2020\"")"##).description == ##"#inst "2020-01-01T00:00:00.000-00:00""##)
				#expect(try cljEval(##"(= (read-string (pr-str #uuid "f81d4fae-7dec-11d0-a765-00a0c91e6bf6")) #uuid "f81d4fae-7dec-11d0-a765-00a0c91e6bf6")"##) == true)
				// *data-readers* wins over the built-in tag; *default-data-reader-fn* takes the rest.
				#expect(try cljEval(##"(binding [*data-readers* {'uuid (fn [s] [:mine s])}] (read-string "#uuid \"x\""))"##) == [kw("mine"), "x"])
				#expect(try cljEval(##"(binding [*data-readers* {'my/tag inc}] (read-string "#my/tag 41"))"##) == 42)
				#expect(try cljEval(##"(binding [*data-readers* {'tg inc} *default-data-reader-fn* (fn [t v] [t v])] (read-string "[#tg 1 #other {:a 1}]"))"##) == [2, [sym("other"), [kw("a"): 1]]])
				#expect(try cljEval(##"(binding [*default-data-reader-fn* (fn [t v] (list t v))] (read-string "#foo/bar baz"))"##) == Value(list: [sym("foo/bar"), sym("baz")]))
				#expect(try cljEval("(get default-data-readers 'inst)").description == "#object[fn clojure.core/read-inst*]")
				#expect(try cljEval("(map? *data-readers*)") == true)
				// A tag fn's throw is a reader error at the literal; an unknown tag likewise.
				#expect(message(##"(read-string "#tg-none 1")"##) == "No reader function for tag tg-none")
				#expect(message(##"(binding [*data-readers* {'tg (fn [_] (throw (ex-info "boom" {})))}] (read-string "#tg 1"))"##) == "boom")
				#expect(message(##"(read-string "#tg/a")"##) == "EOF while reading")
				#expect(message(##"(read-string "#1 x")"##) == "Reader tag must be a symbol")
				#expect(readError(##"#uuid"##) == "EOF while reading")
				#expect(readError("# x") == "No dispatch macro for:  ")
				#expect(readError("#)") == "No dispatch macro for: )")
				// Inside an unselected #? branch the tag is skipped, whatever it is.
				#expect(try cljEval(##"#?(:cljs #uuid "nope" :clj #tg-none 7 :default 3)"##) == 3)
				#expect(try cljEval(##"#?(:cljs #uuid "nope" :default 7)"##) == 7)
				// The tag fn sees the value as read, with any inner tag already applied.
				#expect(try cljEval(##"(binding [*data-readers* {'tg (fn [v] [:tg v])}] (read-string "#tg #tg 1"))"##) == [kw("tg"), [kw("tg"), 1]])
				#expect(try cljEval(##"(binding [*data-readers* {'tg (fn [v] (meta v))}] (read-string "#tg ^:a [1]"))"##) == [kw("a"): true])
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func namespacedMaps() throws {
			_ = try cljEval("(alias 'tg-alias 'clojure.core)")
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval("#:foo{:a 1 :b 2}") == [kw("foo/a"): 1, kw("foo/b"): 2])
				#expect(try cljEval("#:foo {:a 1}") == [kw("foo/a"): 1])
				#expect(try cljEval("'#:foo{:a 1 :ns/a 2 :_/b 3 x 4 _/z 5 \"s\" 6 7 8}") == [kw("foo/a"): 1, kw("ns/a"): 2, kw("b"): 3, sym("foo/x"): 4, sym("z"): 5, "s": 6, 7: 8])
				#expect(try cljEval("#::{:a 1 :b/c 2}") == [kw("user/a"): 1, kw("b/c"): 2])
				#expect(try cljEval("#:: {:a 1}") == [kw("user/a"): 1])
				#expect(try cljEval("#::tg-alias{:a 1}") == [kw("clojure.core/a"): 1])
				#expect(try cljEval("#:foo{}") == [:])
				#expect(try cljEval("#:foo{:a #:bar{:x 1}}") == [kw("foo/a"): [kw("bar/x"): 1]])
				#expect(try cljEval("#?(:cljs #:foo{:a 1} :default #:bar{:a 1})") == [kw("bar/a"): 1])
				#expect(try cljEval("(read-string \"#:foo{:a 1}\")") == [kw("foo/a"): 1])
				#expect(readError("#:foo[1]") == "Namespaced map must specify a map")
				#expect(readError("#:foo 1") == "Namespaced map must specify a map")
				#expect(readError("#:foo") == "Namespaced map must specify a map")
				#expect(readError("#: {:a 1}") == "Namespaced map must specify a namespace")
				#expect(readError("#:{:a 1}") == "Namespaced map must specify a namespace")
				#expect(readError("#:a/b{:a 1}") == "Namespaced map must specify a valid namespace: a/b")
				#expect(readError("#::nope{:a 1}") == "Unknown auto-resolved namespace alias: nope")
				#expect(readError("#:foo{:a 1 :foo/a 2}") == "Duplicate key: :foo/a")
			}
			#expect(clj_debug_live_objects() == before)
		}

		@Test func recordLiterals() throws {
			_ = try cljEval("(def TgRec) (def ->TgRec) (def map->TgRec) (defrecord TgRec [a b])")
			let before = clj_debug_live_objects()
			do {
				#expect(try cljEval("(= #user.TgRec{:a 1 :b 2} (->TgRec 1 2))") == true)
				#expect(try cljEval("(= #user.TgRec[1 2] (->TgRec 1 2))") == true)
				#expect(try cljEval("[(record? #user.TgRec{:a 1}) (:a #user.TgRec{:a 1}) (:b #user.TgRec{:a 1}) (:c #user.TgRec{:a 1 :c 3})]") == [true, 1, nil, 3])
				#expect(try cljEval("(= (read-string (pr-str (->TgRec 1 [2]))) (->TgRec 1 [2]))") == true)
				#expect(try cljEval("(pr-str #user.TgRec{:a 1 :b 2})") == "#user.TgRec{:a 1, :b 2}")
				#expect(message("(read-string \"#user.TgNone{:a 1}\")") == "Unable to resolve classname: user.TgNone")
				#expect(message("(read-string \"#nope.TgRec{:a 1}\")") == "Unable to resolve classname: nope.TgRec")
				#expect(message("(read-string \"#user.TgRec[1]\")") == "user.TgRec has 2 fields, got 1")
				#expect(message("(read-string \"#user.TgRec 1\")") == "Unreadable constructor form starting with \\\"#user.TgRec\\\"")
			}
			#expect(clj_debug_live_objects() == before)
			_ = try cljEval("(def TgRec nil) (def ->TgRec nil) (def map->TgRec nil)")
		}
	}
}
