;; The struct shapes the bridge passes as AAPCS64 does and x86_64 refuses (docs/portability.md): arm64 only.
(ns fixture.objc-arm64)

(defn show [& xs] (apply println (map pr-str xs)))

;; CGRect nests, and its four doubles are an HFA: v0-v3 both ways, not memory.
(show (.rect-value (.value-with-rect (objc-class "NSValue") {:origin {:x 1.0 :y 2.0} :size {:width 3.0 :height 4.0}})))

;; An anonymous struct names no members, so it crosses positionally; six doubles are neither an HFA nor
;; small, so this one travels by a pointer and returns through x8.
(let [t (.init (.alloc (objc-class "NSAffineTransform")))]
  (.set-transform-struct t [2.0 0.0 0.0 3.0 5.0 6.0])
  (show (.transform-struct t) (.transform-point t {:x 1.0 :y 1.0})))

;; Calling in: an HFA argument.
(let [o (objc-reify {}
          (["mid:" "{CGPoint=dd}@:{CGRect={CGPoint=dd}{CGSize=dd}}"] [self r]
            {:x (+ (:x (:origin r)) (/ (:width (:size r)) 2))
             :y (+ (:y (:origin r)) (/ (:height (:size r)) 2))}))]
  (show (.mid o {:origin {:x 1.0 :y 2.0} :size {:width 10.0 :height 4.0}})))

;; A struct through x8: the shape's size is the real one, so it fits the buffer -transformStruct laid out.
(let [t (objc-reify {:superclass "NSAffineTransform"} ("transform-struct" [self] [1.0 2.0 3.0 4.0 5.0 6.0]))]
  (show (.transform-struct t)))
(show (try (objc-reify {} (["odd" "{odd=sssssssss}@:"] [self] 1)) (catch :default e (ex-message e))))
(show (try (objc-block "{odd=sssssssss}@?" [] 1) (catch :default e (ex-message e))))
