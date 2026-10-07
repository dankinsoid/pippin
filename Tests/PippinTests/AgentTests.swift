// @ai-generated(solo)
import CljCore
import Foundation
import Testing
@testable import Pippin

private func eval(_ source: String) throws -> Value { try cljEvalScoped("(in-ns 'agent-tests) " + source) }

private func message(_ source: String) -> String? {
	guard let text = cljEvalErrorScoped("(in-ns 'agent-tests) " + source) else { return nil }
	let prefix = "#error {:message \""
	guard text.hasPrefix(prefix), let end = text.range(of: "\", :data") else { return text }
	return String(text[prefix.endIndex..<end.lowerBound])
}

private func kw(_ s: String) -> Value { Value(keyword: s) }

// Agents (core.clj, design §4 «Агенты и ref'ы»): every expectation is what JVM Clojure 1.12.6 answers.
extension CoreTests {
	@Suite struct AgentTests {
		init() throws {
			clj_init()
			try cljTimingSupport()
			for k in ["bound", "root", "from-future", "ok", "x", "y", "w", "bad", "k", "z", "fail", "continue", "sent", "untouched"] { _ = kw(k) }
			_ = try cljEvalScoped("""
			(ns agent-tests)
			(def ^:dynamic *v* :root)
			(defn wait-error [a] (loop [i 0] (when (and (nil? (agent-error a)) (< i 5000)) (Thread/sleep 1) (recur (inc i)))))
			(defn msg [f] (try (f) (catch :default e (ex-message e))))
			(let [a (agent 0 :validator number? :meta {:x 1})]
			  (add-watch a :w (fn [& _])) (send a inc) (send-off a inc) (await a) (await-for 10 a))
			(doall (seque 2 [1 nil 2]))
			""")
		}

		@Test func actionsRunOneAtATimeInSendOrder() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [a (agent 0)] [(identical? a (send a inc)) (do (await a) @a)])") == [true, 1])
				#expect(try eval("(let [a (agent 0)] (send a + 10) (send-off a * 2) (await a) @a)") == 20)
				#expect(try eval("(let [a (agent [])] (dotimes [i 200] (if (even? i) (send a conj i) (send-off a conj i))) (await a) (= @a (vec (range 200))))") == true)
				// A sum no lost update can reach: two actions never overlap.
				#expect(try eval("(let [a (agent 0) fs (doall (for [_ (range 8)] (future (dotimes [_ 100] (send a inc)))))] (run! deref fs) (await a) @a)") == 800)
				#expect(try eval("[(await) (await-for 10) (await-for 1000 (agent 1))]") == [nil, true, true])
				#expect(try eval("(let [a (agent 0)] (send-off a (fn [_] (Thread/sleep 5) :ok)) (await a) @a)") == kw("ok"))
				#expect(try eval("(let [a (agent 1)] (identical? a (await1 a)))") == true)
				#expect(try eval("(shutdown-agents)") == nil)
			}
			base.check()
		}

		@Test func sendsInsideAnActionWaitForItsEnd() throws {
			let base = CoroBaseline()
			do {
				// The nested send runs after the action, so it reads the state the action left.
				#expect(try eval("(let [c (agent []) d (agent nil)] (send c (fn [s] (send d (fn [_] @c)) (conj s 1))) (await c) (await d) @d)") == [1])
				// Released, the send runs while the action still waits for it, so it reads the state before the action's.
				#expect(try eval("(let [c (agent []) d (agent nil) p (promise)] (send c (fn [s] (send d (fn [_] (deliver p @c) @c)) (let [r (release-pending-sends)] @p [r (release-pending-sends)]))) (await c) (await d) [@c @d])") == [[1, 0], []])
				#expect(try eval("(release-pending-sends)") == 0)
				// A future spawned inside the action is not the action: its send goes out at once.
				#expect(try eval("(let [o (agent 0) p (agent nil)] (send o (fn [s] @(future (send p (fn [_] :from-future))) (Thread/sleep 20) @p)) (await o) @o)") == kw("from-future"))
				// A failed action's sends are dropped.
				#expect(try eval("(let [c (agent 0) d (agent :untouched)] (send c (fn [_] (send d (fn [_] :sent)) (throw (ex-info \"x\" {})))) (wait-error c) (Thread/sleep 20) @d)") == kw("untouched"))
			}
			base.check()
		}

		@Test func aFailedAgentRefusesSendsUntilRestarted() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [f (agent 1)] (send f (fn [_] (throw (ex-info \"boom\" {:k 1})))) (wait-error f) [(ex-message (agent-error f)) (map ex-message (agent-errors f)) (msg #(send f inc)) (msg #(await f))])") == ["boom", ["boom"], "Agent is failed, needs restart", "Agent is failed, needs restart"])
				#expect(try eval("(let [f (agent 1)] (send f (fn [_] (throw (ex-info \"boom\" {})))) (wait-error f) [(restart-agent f 5) @f (agent-error f) (msg #(restart-agent f 6)) (do (send f inc) (await f) @f)])") == [5, 5, nil, "Agent does not need a restart", 6])
				// The held actions go on after a restart, unless :clear-actions drops them.
				#expect(try eval("(let [l (agent 0)] (send l (fn [_] (Thread/sleep 20) (throw (ex-info \"z\" {})))) (send l inc) (send l inc) (wait-error l) (restart-agent l 100) (await l) @l)") == 102)
				#expect(try eval("(let [l (agent 0)] (send l (fn [_] (Thread/sleep 20) (throw (ex-info \"z\" {})))) (send l inc) (send l inc) (wait-error l) (restart-agent l 100 :clear-actions true) (send l inc) (await l) @l)") == 101)
				#expect(try eval("(let [k (agent 1)] (send k (fn [_] (throw (ex-info \"y\" {})))) (wait-error k) [(clear-agent-errors k) (agent-error k)])") == [1, nil])
				// :continue, the default once a handler is given: the handler sees the error and the agent goes on.
				#expect(try eval("(let [errs (atom []) g (agent 0 :error-handler (fn [ag ex] (swap! errs conj [(= 0 @ag) (ex-message ex)])))] [(error-mode g) (do (send g (fn [_] (throw (ex-info \"x\" {})))) (send g inc) (await g) @g) @errs (agent-error g)])") == [kw("continue"), 1, [[true, "x"]], nil])
				#expect(try eval("(let [g (agent 0)] [(error-mode g) (error-handler g) (do (set-error-mode! g :continue) (error-mode g)) (do (set-error-handler! g identity) (= identity (error-handler g)))])") == [kw("fail"), nil, kw("continue"), true])
				#expect(message("(send 1 inc)") == "send-via expects an agent, got: long")
			}
			base.check()
		}

		@Test func validatorsWatchesAndMeta() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [h (agent 1 :validator pos?)] (send h dec) (wait-error h) [(ex-message (agent-error h)) @h (some? (get-validator h)) (msg #(restart-agent h -5)) (some? (agent-error h))])") == ["Invalid reference state", 1, true, "Invalid reference state", true])
				#expect(message("(agent -1 :validator pos?)") == "Invalid reference state")
				#expect(try eval("(msg #(set-validator! (agent -1) pos?))") == "Invalid reference state")
				// A watch runs inside the action, so a throwing watch fails the agent. await's own counting action fires
				// it too, after the latch it delivers, so only the first entry is certain when await returns.
				#expect(try eval("(let [log (atom []) b (agent [])] [(identical? b (add-watch b :w (fn [k r o n] (swap! log conj [k (identical? r b) o n])))) (do (send b conj 1) (await b) (first @log)) (identical? b (remove-watch b :w))])") == [true, [kw("w"), true, [], [1]], true])
				#expect(try eval("(let [n (agent 0)] (add-watch n :bad (fn [& _] (throw (ex-info \"watch\" {})))) (send n inc) (wait-error n) [@n (ex-message (agent-error n))])") == [1, "watch"])
				#expect(try eval("(let [b (agent nil :meta {:x 1})] [(meta b) (alter-meta! b assoc :y 2) (reset-meta! b {:z 3}) (meta b)])") == [[kw("x"): 1], [kw("x"): 1, kw("y"): 2], [kw("z"): 3], [kw("z"): 3]])
			}
			base.check()
		}

		@Test func anActionRunsUnderItsSendersBindings() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("(let [j (agent nil)] (binding [*v* :bound] (send j (fn [_] *v*))) (await j) @j)") == kw("bound"))
				// The action queued behind the bound one sees its own sender's bindings, not the first's.
				#expect(try eval("(let [j (agent []) p (promise)] (binding [*v* :bound] (send j (fn [s] @p (conj s *v*)))) (send j (fn [s] (conj s *v*))) (deliver p 1) (await j) @j)") == [kw("bound"), kw("root")])
				#expect(try eval("(let [i (agent nil)] (send i (fn [_] [(identical? *agent* i) (msg #(await i)) (msg #(await-for 10 i))])) (await i) [@i *agent*])") == [[true, "Can't await in agent action", "Can't await in agent action"], nil])
			}
			base.check()
		}

		@Test func sequeRunsAheadOnAnAgent() throws {
			let base = CoroBaseline()
			do {
				#expect(try eval("[(seque 3 (range 10)) (seque [1 nil 2]) (take 5 (seque 2 (range))) (seque 1 [])]") == [[0, 1, 2, 3, 4, 5, 6, 7, 8, 9], [1, nil, 2], [0, 1, 2, 3, 4], []])
			}
			base.check()
		}
	}
}
