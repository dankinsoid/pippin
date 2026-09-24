;; OBJC_SEND nodes: the level-1 bridge through the compiler, next to the interpreter's own answers.
(ns fixture.objc)

(defn show [& xs] (apply println (map pr-str xs)))

(show (objc-object? (objc-class "NSString")) (objc-class "NoSuchClassHere") (objc-object? 1))
(show (objc-kebab* "addTarget:action:forControlEvents:") (objc-kebab* "UTF8String") (objc-kebab* "centerXAnchor"))

;; Values cross as values; an NSMutableString stays a handle so it can still be mutated.
(show (.string-with-utf8-string (objc-class "NSString") "hi")
      (.length (.string-with-utf8-string (objc-class "NSMutableString") "abc"))
      (.length nil))

(let [s (.init (.alloc (objc-class "NSMutableString")))]
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
(let [s (.string-with-utf8-string (objc-class "NSString") "hello world")
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
(show (.utf8-string (.string-by-replacing-characters-in-range (.string-with-utf8-string (objc-class "NSString") "hello world")
                                                              {:location 0 :length 5} :with-string "goodbye")))

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
  (show (.utf8-string (.greet o "world")) (.utf8-string (.description o)))
  (.bump o)
  (.bump o)
  (show @n (.is-kind-of-class o (objc-class "NSObject")) (.responds-to-selector o "bump")))

;; One class per reify shape, not per instance: a reify in a loop must not mint a class per iteration.
(let [os (map (fn [k] (objc-reify {} (["k" "q@:"] [self] k))) (range 4))]
  (show (map (fn [o] (.k o)) os) (count (set (map (fn [o] (.utf8-string (.description (.class o)))) os)))))

;; A real delegate: NSXMLParser drives the protocol's methods, whose encodings come from the protocol.
(let [seen (atom [])
      d (objc-reify {:protocols ["NSXMLParserDelegate"]}
          ("parser:did-start-element:namespace-uri:qualified-name:attributes:" [self p el ns qn attrs]
            (swap! seen conj [el (ns-dictionary->map attrs)]))
          ("parser:did-end-element:namespace-uri:qualified-name:" [self p el ns qn]
            (swap! seen conj el)))
      data (.data-using-encoding (.string-with-utf8-string (objc-class "NSString") "<a x=\"1\"><b/></a>") 4)
      p (.init-with-data (.alloc (objc-class "NSXMLParser")) data)]
  (.set-delegate p d)
  (show (.parse p) @seen))

;; A block: Foundation calls it once per comparison, and once per element with a pointer it only reads.
(let [desc (objc-block "q@?@@" [x y] (cond (< x y) 1 (> x y) -1 :else 0))
      seen (atom [])
      each (objc-block "v@?@Q^v" [x i stop] (swap! seen conj [x i (objc-object? stop)]))]
  (show (ns-array->vec (.sorted-array-using-comparator (ns-array [3 1 2]) desc)))
  (.enumerate-objects-using-block (ns-array ["a" "b"]) each)
  (show @seen))

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
  (show (.utf8-string (.copy o))))

(show (try (objc-reify {} ("no-such-selector-anywhere" [self] 1)) (catch :default e (ex-message e))))
(show (try (objc-reify {:protocols ["NoSuchProtocol"]} (["x" "v@:"] [self] 1)) (catch :default e (ex-message e))))
(show (try (objc-reify {:superclass "NoSuchClass"} (["x" "v@:"] [self] 1)) (catch :default e (ex-message e))))
(show (try (objc-reify {} (["big" "{big=ddddddd}@:"] [self] 1)) (catch :default e (ex-message e))))
(show (try (objc-block "v@?[4i]" [x] x) (catch :default e (ex-message e))))
