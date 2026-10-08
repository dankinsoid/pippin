;; The repointed words of design §3 «"Наш хост" — это C-ядро» and jvm-hash, in compiled code.
(ns fixture.repoint-model)

(defrecord User [name])
(defprotocol Greet (greet [x]) (-shout [x]))
(deftype Box [v] Greet (greet [_] (str "hi " v)) (-shout [_] (str "HI " v)))

(ns fixture.repoint
  (:import [fixture.repoint_model User Box] (java.util Date)))

(defn run []
  (let [pooled (var-get #'clojure.core/pooled-executor)
        seen (atom 0)
        a (agent 0)]
    (set-agent-send-executor! (fn [run] (swap! seen inc) (pooled run)))
    (send a inc)
    (await a)
    (set-agent-send-executor! pooled)
    [(User. "u") (instance? User (fixture.repoint-model/->User "v")) (try (throw (User. "w")) (catch User u (:name u)))
     (.. (Box. 1) greet) ((memfn shout) (Box. 2)) (= (set (keys (ns-imports 'fixture.repoint))) '#{User Box})
     (munge 'fixture.repoint/run)
     (let [u (parse-uri "https://h:8443/x?y#z")] [(uri? u) (:host u) (:port u) (str u)])
     (with-in-str "(+ 1 2) :k\nrest" [(read) (read+string) (read-line)])
     [@a (pos? @seen)]
     (mapv jvm-hash [nil "héllo" :a/b 'c 1.5M 1/3 -0.0 [1 "x"] #{:k} {:a 1} (fixture.repoint-model/->User "u")])]))
(prn (run))
