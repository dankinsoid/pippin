;; Mutable deftype fields: set! in the type's methods, a fn made in one reads the field as it was then, hints admit stores.
(ns fixture.mutable-fields)

(defprotocol Counter
  (tick [c])
  (tick-by [c k])
  (total [c])
  (snapshot [c]))

(deftype LongCounter [^:unsynchronized-mutable ^long n step]
  Counter
  (tick [_] (set! n (+ n step)) n)
  (tick-by [_ k] (let [before n] (set! n (+ n k)) [before n]))
  (total [_] n)
  (snapshot [_] (let [f (fn [] n)] (set! n (inc n)) [(f) n])))

(deftype Acc [^:volatile-mutable ^double sum ^:unsynchronized-mutable ^float f ^:volatile-mutable items]
  Counter
  (tick [_] (set! sum (+ sum 0.5)) (set! f (+ f 0.25)) (set! items (conj items sum)) sum)
  (tick-by [this k] (loop [i 0] (when (< i k) (tick this) (recur (inc i)))) this)
  (total [_] [sum f items])
  (snapshot [_] (set! f 0.1) (= f (float 0.1))))

(defprotocol Cell
  (get-v [c])
  (put-v [c v]))

(deftype Node [^:unsynchronized-mutable next]
  Cell
  (get-v [_] next)
  (put-v [this v] (set! next v) this))

(deftype Named [^:unsynchronized-mutable ^String s ^:unsynchronized-mutable ^long k]
  Cell
  (get-v [_] [s k])
  (put-v [_ v] (if (string? v) (set! s v) (set! k v))))

(deftype Hashed [x ^:unsynchronized-mutable ^int h]
  Cell
  (get-v [_] (if (zero? h) (let [v (int (count x))] (set! h v) [:computed v]) [:cached h]))
  (put-v [_ v] nil))

(defprotocol Pos
  (read-char [r])
  (unread [r])
  (pos [r]))

(deftype Reader [^String s ^:unsynchronized-mutable ^long p ^long len]
  Pos
  (read-char [_] (if (< p len) (let [c (nth s p)] (set! p (unchecked-inc p)) c) nil))
  (unread [_] (set! p (unchecked-dec p)) nil)
  (pos [_] p))

(defn read-all [r] (loop [acc []] (if-let [c (read-char r)] (recur (conj acc c)) acc)))
(defn err [f] (try (f) (catch :default e (ex-message e))))

(let [c (->LongCounter 0 2)]
  (println (tick c) (tick c) (tick-by c 10) (total c) (.-n c) (snapshot c) (.-step c))
  (println (err #(field* c 0)) (total c)))

(let [a (->Acc 0 0 [])]
  (tick-by a 3)
  (println (total a) (snapshot a) (.-sum a) (.-items a)))

(let [a (->Node nil) b (->Node [1])]
  (put-v a b)
  (println (= [1] (get-v (get-v a))) (identical? b (get-v a)) (get-v (put-v b :x)) (identical? b (.-next a)))
  (put-v a nil))

(let [nm (->Named "a" 1)]
  (println (put-v nm "b") (put-v nm 2.9) (put-v nm 9223372036854775807) (get-v nm) (err #(put-v nm :q))
           (err #(Named. :k 1))))

(let [h (->Hashed "four" 0)]
  (println (get-v h) (get-v h) (.-h h)))

(let [r (->Reader "héllo" 0 5)]
  (println (read-char r) (read-char r) (unread r) (pos r) (apply str (read-all r)) (pos r) (read-char r)))
