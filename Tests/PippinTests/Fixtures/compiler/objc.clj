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
