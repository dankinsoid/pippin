;; Protocol call sites: direct arms from the receiver fact (closed), the per-site inline cache, satisfies? folded.
(ns fixture.protocols)

(defprotocol Shape
  (area [s])
  (scale [s k]))

;; one deftype and one record implement Shape: a `host` or `record` fact names each without a descriptor
(deftype Sq [a]
  Shape
  (area [_] (* a a))
  (scale [_ k] (->Sq (* a k))))

(defrecord Rect [w h]
  Shape
  (area [_] (* w h))
  (scale [_ k] (->Rect (* w k) (* h k))))

;; monomorphic: every recorded caller passes a Sq
(defn sq-area [s] (area s))
(defn sq-twice [] [(sq-area (->Sq 3)) (sq-area (->Sq 4))])

;; bi-morphic: Sq or Rect from the callers
(defn either-area [s] (area s))
(defn both [] [(either-area (->Sq 2)) (either-area (->Rect 1 2))])

;; the receiver is the result of a constructor: the fact carries the descriptor
(defn from-ctor [a] (area (->Sq a)))
(defn record-area [w h] (area (->Rect w h)))

;; a core kind with several implementations: vector and nil are one descriptor each, a seq is many
(defprotocol Firstish
  (firstish [c]))
(extend-type PersistentVector Firstish (firstish [v] (nth v 0)))
(extend-type nil Firstish (firstish [_] :nil))
(extend-type Cons Firstish (firstish [c] (first c)))
(extend-type EmptyList Firstish (firstish [_] :empty))
(extend-type PersistentList Firstish (firstish [l] (first l)))
(extend-type LazySeq Firstish (firstish [l] (first l)))
(extend-type Keyword Firstish (firstish [k] (name k)))
(extend-type Long Firstish (firstish [n] (inc n)))

(defn first-of-vec [v] (firstish v))
(defn first-of-seq [s] (firstish s))
(defn first-of-nil [x] (firstish x))
(defn first-of-kw [k] (firstish k))
(defn first-of-num [n] (firstish n))
(defn firsts []
  [(first-of-vec [1 2]) (first-of-vec [:a])
   (first-of-seq (cons 1 '(2))) (first-of-seq '(3 4)) (first-of-seq (map inc [4])) (first-of-seq '())
   (first-of-nil nil) (first-of-kw :k) (first-of-num 41)])

;; a receiver the facts cannot name: the cache alone, cycling through more receivers than it holds
(defn any-first [x] (firstish x))
(defn cycle-all [] (mapv any-first [[1] nil :b 7 '(8) (cons 9 nil)]))

;; satisfies? and extends? with a known receiver and a type var
(defn is-shape? [s] (satisfies? Shape s))
(defn shapes? [] [(is-shape? (->Sq 1)) (satisfies? Shape (->Rect 1 1)) (satisfies? Shape [1]) (satisfies? Firstish [1]) (satisfies? Firstish nil) (satisfies? Firstish 1) (satisfies? Firstish :k) (satisfies? Firstish 1.5)])
(defn extends-all? [] [(extends? Shape Sq) (extends? Shape Rect) (extends? Firstish PersistentVector) (extends? Firstish nil) (extends? Shape PersistentVector) (extends? Firstish Sq)])

;; the method fn of a type calling another method on this: the callee's symbol comes after the caller's in the unit
(defprotocol Twice
  (once [x])
  (twice [x]))
(deftype Tw [n]
  Twice
  (once [_] n)
  (twice [this] (+ (once this) (once this))))
(defn tw [] (twice (->Tw 21)))

;; a caller the join records with a kind the arms cannot cover: the cache answers, a missing impl still throws
(defn wrong-guess [] [(first-of-vec '(9)) (first-of-nil [5])
                      (try (first-of-kw 'sym) (catch :default e (ex-message e)))
                      (try (sq-area "s") (catch :default e (ex-message e)))
                      (try (first-of-vec 1.5) (catch :default e (ex-message e)))
                      (try (area 1) (catch :default e (ex-message e)))
                      (try (area) (catch :default e (ex-message e)))])

;; extend-type after the sites ran: every cache and arm must miss and refill (the arms name the final impls)
(defn re-extend []
  (let [before [(sq-area (->Sq 2)) (first-of-vec [7]) (is-shape? (->Sq 1)) (extends? Shape Sq)]]
    (extend-type Sq Shape (area [_] :sq-again) (scale [this _] this))
    (extend-type PersistentVector Firstish (firstish [v] (peek v)))
    [before (sq-area (->Sq 2)) (either-area (->Sq 2)) (from-ctor 3) (first-of-vec [7 8]) (is-shape? (->Sq 1)) (extends? Shape Sq)]))

(println (sq-twice) (both) (from-ctor 5) (record-area 2 3) (firsts) (cycle-all) (cycle-all))
(println (shapes?) (extends-all?) (tw) (wrong-guess))
(println (re-extend) (sq-twice) (area (scale (->Sq 2) 3)) (area (scale (->Rect 1 2) 2)))
