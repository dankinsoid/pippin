;; Closed sites whose receiver fact names no descriptor take an arm per implementor when the protocol has few.
(ns fixture.protocols-poly)

;; a tree whose levels alternate the two implementors, as persistent-sorted-set's Node and Leaf do
(defprotocol INode
  (total [n])
  (depth [n]))

(deftype Leaf [v]
  INode
  (total [_] v)
  (depth [_] 1))

(deftype Node [kids]
  INode
  (total [_] (reduce + (map total kids)))
  (depth [_] (inc (reduce max 0 (map depth kids)))))

(defn tree [d] (if (zero? d) (->Leaf d) (->Node [(tree (dec d)) (->Leaf 10) (tree (dec d))])))
(defn tot [n] (total n))
(defn walk [] (let [t (tree 4)] [(tot t) (depth t) (tot (->Leaf 3))]))

;; builtins and a record beside a deftype: nil, Long and the record get arms of their own
(defprotocol Desc
  (desc [x]))
(defrecord Rec [a])
(extend-type nil Desc (desc [_] :nil))
(extend-type Long Desc (desc [n] [:long n]))
(extend-type Rec Desc (desc [r] [:rec (:a r)]))
(extend-type Leaf Desc (desc [l] [:leaf (.-v l)]))
(defn describe [x] (desc x))
(defn descs [] (mapv describe [nil 1 (->Rec 2) (->Leaf 3) nil 4]))

;; five implementors: past the arms' cap, the cache alone
(defprotocol Wide
  (wide [x]))
(extend-protocol Wide
  nil (wide [_] 0)
  Long (wide [_] 1)
  String (wide [_] 2)
  Keyword (wide [_] 3)
  Leaf (wide [_] 4))
(defn w [x] (wide x))
(defn wides [] (mapv w [nil 1 "s" :k (->Leaf 0) 1 nil]))

;; a reify implementor has no var to guard on: the cache serves it beside the deftype's arm
(defprotocol Greet
  (greet [x]))
(deftype Hello []
  Greet
  (greet [_] :hello))
(defn g [x] (greet x))
(defn greets [] [(g (->Hello)) (g (reify Greet (greet [_] :reified))) (g (->Hello))])

;; a receiver no implementor covers still throws through the tables
(defn missing [] [(try (tot "s") (catch :default e (ex-message e))) (try (describe 1.5) (catch :default e (ex-message e)))])

(println (walk) (descs) (wides) (greets))
(println (missing))

;; extended after the sites ran: a new type is the cache's, a replaced impl fails its arm's fill
(extend-type String INode (total [s] (count s)) (depth [_] 0))
(extend-type Leaf INode (total [l] (* 100 (.-v l))) (depth [_] 0))
(println (tot "abcd") (tot (->Leaf 1)) (walk) (g (->Hello)))
