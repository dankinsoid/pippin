;; (.-f x) sites: a cell per site keyed by the type's serial, which a redefinition or a reused address cannot alias.
(ns fixture.field-reads)

(defprotocol Named
  (-name [x]))

(deftype P [a b])
(deftype Q [b a c])
(defrecord R [a b])

(defn read-a [x] (.-a x))
(defn read-b [x] (.-b x))
(defn read-name [x] (.-name x))

;; one site, three layouts: P's a is field 0, Q's field 1, R's basis field 0
(defn mixed [] (mapv read-a [(->P 1 2) (->Q 3 4 5) (->R 6 7) (->P 8 9) (->Q 10 11 12) (->R 13 14)]))
(defn repeated [] (let [p (->P :x :y)] (reduce (fn [acc _] (conj acc (read-b p))) [] (range 3))))
;; an extmap key is no field: the send goes on to the methods
(defn extmap [] (let [r (assoc (->R 1 2) :c 3)] [(read-a r) (:c r) (try (.-c r) (catch :default _ :no-field))]))
;; a dashed name without a field is the protocol method, a reify's slots are no fields
(deftype N [n]
  Named
  (-name [_] (str "n" n)))
(defn methods-and-reify [] [(read-name (->N 1)) (read-name (reify Named (-name [_] "r"))) (read-name (->N 2))])
(defn nil-reads [] [(read-a nil) (read-b nil) (read-a (->P 1 2))])
(defn host-read [] (try (.-length (ns-string "abc")) (catch :default e (subs (ex-message e) 0 11))))
(defn not-an-instance [] [(try (read-a 5) (catch :default e (subs (ex-message e) 0 14))) (read-a (->P :after 0))])

(println (mixed) (repeated) (extmap))
(println (methods-and-reify) (nil-reads) (host-read) (not-an-instance))

;; the old P stays alive in `old`; the new P keeps a where b was: each instance reads its own type's field
(def old (->P :old-a :old-b))
(def before [(read-a old) (read-b old)])
(deftype P [b a])
(println before [(read-a old) (read-b old)] [(read-a (->P :new-b :new-a)) (read-b (->P :new-b :new-a))] [(read-a old) (read-b old)])
