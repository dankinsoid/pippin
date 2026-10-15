;; A protocol's one implementor runs in place under a guard asking for the receiver's impl; anything else runs the call.
(ns fixture.proto-inline)

(defprotocol Cursor
  (step [c])
  (peek-at [c i])
  (span [c a b]))

(deftype Text [^String s ^longs pos ^long n]
  Cursor
  (step [_] (let [p (aget pos 0)] (if (< p n) (do (aset pos 0 (unchecked-inc p)) (int (nth s p))) -1)))
  (peek-at [_ i] (nth s i nil))
  (span [_ a b] (subs s a b)))

(defn text [s] (->Text s (long-array 1) (count s)))
(defn drain [c] (loop [acc []] (let [x (step c)] (if (neg? x) acc (recur (conj acc x))))))
(defn peeks [c k] [(peek-at c 0) (peek-at c (inc k)) (peek-at c 99) (span c 1 (dec 3))])

(prn (drain (text "héllo")) (peeks (text "abcd") 1))
(extend-protocol Cursor
  String
  (step [_] -1)
  (peek-at [_ i] (str "str" i))
  (span [_ _ _] :string))
(prn (drain "x") (peeks "abcd" 1) (peeks (text "wxyz") 1))
(extend-type Text
  Cursor
  (step [_] -1)
  (peek-at [_ i] [:replaced i])
  (span [_ a b] [:replaced a b]))
(prn (drain (text "abc")) (peeks (text "abc") 1))
