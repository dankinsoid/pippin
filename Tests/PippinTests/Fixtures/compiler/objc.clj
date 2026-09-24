;; OBJC_SEND nodes: the level-1 bridge through the compiler, next to the interpreter's own answers.
(ns fixture.objc)

(defn show [& xs] (apply println (map pr-str xs)))

(show (objc-object? (objc-class "NSString")) (objc-class "NoSuchClassHere") (objc-object? 1))
(show (objc-kebab* "addTarget:action:forControlEvents:") (objc-kebab* "UTF8String") (objc-kebab* "centerXAnchor"))

;; Every NSString crosses as a value; ns-string and ns-mutable-string hand back the object itself.
(show (.string-with-utf8-string (objc-class "NSString") "hi")
      (.length (ns-string "abc"))
      (ns-string->str (ns-string "abc"))
      (.length nil))

(let [s (ns-mutable-string "")]
  (.append-string s "ab")
  (.append-string s "cd")
  (show (.utf8-string s) (.length s) (.retain-count s)))

;; A double argument in a v register beside a pointer one in an x register.
(let [epoch (.date-with-time-interval-since1970 (objc-class "NSDate") 0.0)
      d (.init-with-time-interval (.alloc (objc-class "NSDate")) 1.5 :since-date epoch)]
  (show (.time-interval-since1970 d) (.is-equal-to-date d epoch)))

(show (objc-send (objc-class "NSNumber") "numberWithDouble:" 2.5)
      (objc-send (objc-class "NSNumber") "numberWithBool:" true)
      (objc-send (objc-class "NSNumber") "numberWithLongLong:" -7))

(show (try (.no-such-method (objc-class "NSString")) (catch :default e (ex-message e))))
(show (try (objc-send 1 "length") (catch :default e (ex-message e))))

;; A struct crosses as a map of the field names the type encoding does not carry.
(let [s (ns-string "hello world")
      r (.range-of-string s "o w")]
  (show r (:location r) (:length r)))

(show (.point-value (.value-with-point (objc-class "NSValue") {:x 1.5 :y -2.5}))
      (.size-value (.value-with-size (objc-class "NSValue") {:width 10 :height 20}))
      (.range-value (.value-with-range (objc-class "NSValue") {:location 3 :length 4})))

;; CGRect nests, and its four doubles are an HFA: v0-v3 both ways, not memory.
(show (.rect-value (.value-with-rect (objc-class "NSValue") {:origin {:x 1.0 :y 2.0} :size {:width 3.0 :height 4.0}})))

;; An anonymous struct names no members, so it crosses positionally; six doubles are neither an HFA nor
;; small, so this one travels by a pointer and returns through x8.
(let [t (.init (.alloc (objc-class "NSAffineTransform")))]
  (.set-transform-struct t [2.0 0.0 0.0 3.0 5.0 6.0])
  (show (.transform-struct t) (.transform-point t {:x 1.0 :y 1.0})))

(show (try (.value-with-point (objc-class "NSValue") {:x 1.0}) (catch :default e (ex-message e))))
(show (try (.value-with-range (objc-class "NSValue") [1 2 3]) (catch :default e (ex-message e))))
(show (try (.string-with-format (objc-class "NSString") "x") (catch :default e (ex-message e))))

;; A struct argument beside a pointer one: each register class is filled in the order of its own arguments.
(show (.string-by-replacing-characters-in-range (ns-string "hello world")
                                               {:location 0 :length 5} :with-string "goodbye"))

;; Collections cross by hand only, deeply, and lossily: nil is NSNull and a keyword key comes back a string.
(let [a (ns-array [1 "two" :three nil true 2.5 [7]])]
  (show (.count a) (ns-array->vec a)))

(let [d (ns-dictionary {:a 1 "b" [1 2] :c {:d "e"}})
      m (ns-dictionary->map d)]
  (show (.count d) (sort (keys m)) (get m "a") (get m "b") (get m "c")))

(show (ns-array->vec (ns-array (map inc (range 5)))) (ns-array->vec (ns-array '())) (ns-array->vec nil))
(show (try (ns-array {:a 1}) (catch :default e (ex-message e))))
(show (try (ns-array [(fn [])]) (catch :default e (ex-message e))))
(show (try (ns-dictionary->map (ns-array [])) (catch :default e (ex-message e))))

;; Calling in: an object of our own, with a method per return shape the trampolines cover.
(let [n (atom 0)
      o (objc-reify {}
          (["twice:" "q@:q"] [self x] (* 2 x))
          (["hypot:with:" "d@:dd"] [self a b] (+ (* a a) (* b b)))
          (["mid:" "{CGPoint=dd}@:{CGRect={CGPoint=dd}{CGSize=dd}}"] [self r]
            {:x (+ (:x (:origin r)) (/ (:width (:size r)) 2))
             :y (+ (:y (:origin r)) (/ (:height (:size r)) 2))})
          (["greet:" "@@:@"] [self who] (str "hello " who))
          (["bump" "v@:"] [self] (swap! n inc))
          ("description" [self] "a reified thing"))]
  (show (.twice o 21) (.hypot o 3.0 :with 4.0))
  (show (.mid o {:origin {:x 1.0 :y 2.0} :size {:width 10.0 :height 4.0}}))
  (show (.greet o "world") (.description o))
  (.bump o)
  (.bump o)
  (show @n (.is-kind-of-class o (objc-class "NSObject")) (.responds-to-selector o "bump")))

;; One class per reify shape, not per instance: a reify in a loop must not mint a class per iteration.
(let [os (map (fn [k] (objc-reify {} (["k" "q@:"] [self] k))) (range 4))]
  (show (map (fn [o] (.k o)) os) (count (set (map (fn [o] (.description (.class o))) os)))))

;; A real delegate: NSXMLParser drives the protocol's methods, whose encodings come from the protocol.
(let [seen (atom [])
      d (objc-reify {:protocols ["NSXMLParserDelegate"]}
          ("parser:did-start-element:namespace-uri:qualified-name:attributes:" [self p el ns qn attrs]
            (swap! seen conj [el (ns-dictionary->map attrs)]))
          ("parser:did-end-element:namespace-uri:qualified-name:" [self p el ns qn]
            (swap! seen conj el)))
      data (.data-using-encoding (ns-string "<a x=\"1\"><b/></a>") 4)
      p (.init-with-data (.alloc (objc-class "NSXMLParser")) data)]
  (.set-delegate p d)
  (show (.parse p) @seen))

;; A block: Foundation calls it once per comparison, and once per element with a BOOL * it reads back.
(let [desc (objc-block "q@?@@" [x y] (cond (< x y) 1 (> x y) -1 :else 0))
      seen (atom [])
      each (objc-block "v@?@Q^B" [x i stop] (swap! seen conj [x i (objc-object? stop)]))]
  (show (ns-array->vec (.sorted-array-using-comparator (ns-array [3 1 2]) desc)))
  (.enumerate-objects-using-block (ns-array ["a" "b"]) each)
  (show @seen))

;; Writing through that pointer stops the enumeration, which is Foundation reading the flag back.
(let [seen (atom [])
      stop-at-b (objc-block "v@?@Q^B" [x i stop]
                  (swap! seen conj x)
                  (objc-write! stop (= x "b")))]
  (.enumerate-objects-using-block (ns-array ["a" "b" "c"]) stop-at-b)
  (show @seen))

;; The pointee's encoding is what says how wide a write is, so an unknown one is a refusal.
(let [err (atom nil)
      opaque (objc-block "v@?@Q^v" [x i stop]
               (try (objc-write! stop true) (catch :default e (reset! err (ex-message e)))))]
  (.enumerate-objects-using-block (ns-array ["a"]) opaque)
  (show @err))
(show (try (objc-write! (ns-string "x") 1) (catch :default e (ex-message e))))

;; Calling a block: its own invoke pointer and the signature its descriptor carries, ours or the host's.
(let [add (objc-block "q@?qq" [a b] (+ a b))
      mid (objc-block "{CGPoint=dd}@?d" [d] {:x d :y (* 2 d)})
      back (get (ns-dictionary->map (ns-dictionary {"add" add})) "add")]
  (show (objc-invoke add 20 22) (objc-invoke back 1 2) (objc-invoke mid 1.5)))
(show (try (objc-invoke (ns-string "x") 1) (catch :default e (ex-message e))))
(show (try (objc-invoke (objc-block "v@?" [])) (catch :default e (ex-message e))))
(show (try (objc-invoke (objc-block "v@?q" [x]) 1 2) (catch :default e (ex-message e))))

;; A callback can arrive on a thread the runtime has never seen: clj_coro_current gives it one.
(let [done (atom nil)
      o (objc-reify {} (["run:" "v@:@"] [self arg] (reset! done (ns-array->vec (ns-array [1 2])))))]
  (.detach-new-thread-selector (objc-class "NSThread") "run:" :to-target o :with-object nil)
  (loop [i 0]
    (when (and (nil? @done) (< i 500))
      (.sleep-for-time-interval (objc-class "NSThread") 0.01)
      (recur (inc i))))
  (show @done))

;; An owned family hands the caller +1, so the copy outlives the callback's own pool.
(let [o (objc-reify {} (["copyWithZone:" "@@:^v"] [self z] (.string-with-utf8-string (objc-class "NSString") "a copy of me")))]
  (show (.copy o)))

(show (try (objc-reify {} ("no-such-selector-anywhere" [self] 1)) (catch :default e (ex-message e))))
(show (try (objc-reify {:protocols ["NoSuchProtocol"]} (["x" "v@:"] [self] 1)) (catch :default e (ex-message e))))
(show (try (objc-reify {:superclass "NoSuchClass"} (["x" "v@:"] [self] 1)) (catch :default e (ex-message e))))
;; A struct through x8: the shape's size is the real one, so it fits the buffer -transformStruct laid out.
(let [t (objc-reify {:superclass "NSAffineTransform"} ("transform-struct" [self] [1.0 2.0 3.0 4.0 5.0 6.0]))]
  (show (.transform-struct t)))
(show (try (objc-reify {} (["odd" "{odd=sssssssss}@:"] [self] 1)) (catch :default e (ex-message e))))
(show (try (objc-block "{odd=sssssssss}@?" [] 1) (catch :default e (ex-message e))))
(show (try (objc-reify {} (["big" "{big=ddddddddddddddddd}@:"] [self] 1)) (catch :default e (ex-message e))))
(show (try (objc-block "v@?[4i]" [x] x) (catch :default e (ex-message e))))
