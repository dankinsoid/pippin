;; Level 2's production path (design §5): make swift-closed links only app.stubs; closed or interpreted, -main prints app.out.
(ns fixture.swift-app
  (:require-swift [PippinFixture :as fx :refer [make-point describe]]))

(defn -main []
  (let [p (make-point :x 1 :y 2)]
    ;; @MainActor off the main thread hops, from the host thread and from a pool coroutine alike.
    (prn (describe p) (describe (fx/moved p :by 3)) @(future (describe (fx/moved p :by 10))))
    ;; Every overload of a named var is linked: Point. has three.
    (prn (describe (fx/Point. :x 3 :y 4)) (fx/Point.sum p) (describe (fx/Point. :validating 2)))
    (prn (fx/turn :north) (:origin (fx/frame 2)) (fx/twice inc 1))
    ;; The Swift error type is found by name in a binary that links the module statically.
    (prn (try (fx/risky 30) (catch PippinFixture/FixtureError e (ex-message e))) (fx/later 21))
    (prn (= (fx/Counter.shared) (fx/Counter.shared)))))
