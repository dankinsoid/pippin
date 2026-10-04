;; @ai-generated(solo)
;; Ours, not Clojure's: a stand-in for org.clojure/data.generators, whose own *rnd* is a java.util.Random.
;; Only the names the vendored files use, with data.generators' signatures (corpus/clojure-core-tests/SOURCE).
(ns clojure.data.generators
  (:refer-clojure :exclude [bigdec bigint boolean byte hash-map int keyword long
                            rand-nth set short string symbol vec]))

(def ^:dynamic *rnd* nil)

;; Own xorshift64, not core's rand: the corpus runs every test twice and compares the two verdicts.
(def ^:private state (atom 42))

(defn set-seed! [n] (reset! state (bit-or 1 n)))

(defn- next-long []
  (swap! state (fn [s]
                 (let [s (bit-xor s (bit-shift-left s 13))
                       s (bit-xor s (unsigned-bit-shift-right s 7))]
                   (bit-xor s (bit-shift-left s 17))))))

(defn- next-double []
  (/ (double (bit-and (unsigned-bit-shift-right (next-long) 11) 9007199254740991))
     9007199254740992.0))

(defn call-through
  "Recursively call x until it is not a function."
  [x]
  (if (fn? x) (recur (x)) x))

(defn pick [coll] (let [v (clojure.core/vec coll)] (nth v (clojure.core/long (* (count v) (next-double))))))
(defn rand-nth [coll] (pick coll))

(defn uniform
  "A long in [lo hi)."
  [lo hi]
  (+ lo (clojure.core/long (* (- hi lo) (next-double)))))

(def ^:private default-size-fn #(uniform 1 9))

;; rand is a double, so a full-range long is built from two halves.
(defn long []
  (+ (* (clojure.core/long (uniform -2147483648 2147483648)) 4294967296)
     (clojure.core/long (uniform 0 4294967296))))

(defn int [] (uniform -2147483648 2147483648))
(defn short [] (uniform -32768 32768))
(defn byte [] (uniform -128 128))
(defn boolean [] (< (next-double) 0.5))
(defn printable-ascii-char [] (char (uniform 32 127)))

(defn geometric
  "The number of failures before the first success at probability p."
  [p]
  (loop [n 0] (if (< (next-double) p) n (recur (inc n)))))

(defn reps [size-fn f] (repeatedly (call-through size-fn) f))

(defn vec
  ([f] (vec f default-size-fn))
  ([f size-fn] (into [] (reps size-fn f))))

(defn set
  ([f] (set f default-size-fn))
  ([f size-fn] (into #{} (reps size-fn f))))

(defn hash-map
  ([kf vf] (hash-map kf vf default-size-fn))
  ([kf vf size-fn] (zipmap (reps size-fn kf) (reps size-fn vf))))

(defn string
  ([] (string printable-ascii-char))
  ([f] (string f default-size-fn))
  ([f size-fn] (apply str (reps size-fn f))))

(defn symbol
  ([] (symbol default-size-fn))
  ([_] (clojure.core/symbol (str "s" (uniform 0 100000)))))

(defn keyword
  ([] (keyword default-size-fn))
  ([_] (clojure.core/keyword (str "k" (uniform 0 100000)))))

(defn ratio []
  (let [d (long)]
    (if (zero? d) (ratio) (/ (long) d))))

(defn bigint [] (clojure.core/bigint (*' (long) (long))))
(defn bigdec [] (clojure.core/bigdec (/ (clojure.core/bigint (long)) 100)))
(defn- hex [n] (apply str (repeatedly n #(nth "0123456789abcdef" (uniform 0 16)))))
(defn uuid [] (parse-uuid (str (hex 8) "-" (hex 4) "-" (hex 4) "-" (hex 4) "-" (hex 12))))

;; No constructor for an inst here, so a fixed pool of literals stands in for data.generators' Date.
(def ^:private insts [#inst "1970-01-01T00:00:00.000-00:00" #inst "2001-09-09T01:46:40.000-00:00"
                      #inst "2026-10-05T12:00:00.000-00:00"])
(defn date [] (pick insts))

(defn one-of [& specs] (call-through (pick specs)))

(def scalars [(constantly nil) byte short int long boolean printable-ascii-char
              string symbol keyword ratio bigint bigdec uuid date])

(defn scalar [] (call-through (pick scalars)))

(def collections [[vec [scalars]] [set [scalars]] [hash-map [scalars scalars]]])

(defn collection []
  (let [[coll args] (pick collections)]
    (apply coll (map pick args))))

(defn anything [] (one-of scalar collection))
