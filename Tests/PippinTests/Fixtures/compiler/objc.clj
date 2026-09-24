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
