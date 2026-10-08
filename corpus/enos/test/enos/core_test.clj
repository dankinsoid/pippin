(ns enos.core-test
  (:require [clojure.test :refer :all]
            [enos.core :as enos :refer [pause! pause!!
                                        arange
                                        dochan! dochan!!
                                        pmap<
                                        chan->seq
                                        generator defgenerator
                                        fib poisson
                                        <!+
                                        ]]
            [clojure.core.async :as async :refer [go thread <!! >!! close!]]))


(defn now []
  (System/currentTimeMillis))

(defn slow
  "... and low, that is the tempo."
  [f ms]
  (if (and ms (pos? ms))
    (fn [& args]
      (pause!! ms)
      (apply f args))
    f))

(defmacro timed [& body]
  `(let [start# (System/nanoTime)
         ret#   (do ~@body)
         ms#     (/ (double (- (System/nanoTime) start#)) 1000000.0)]
     [ms# ret#]))

(deftest test-pause
  (let [ch (go (pause! 20))
        t1 (now)]
    (<!! ch)
    (is (<= 20 (- (now) t1)))))

(deftest test-dochan
  (testing "double-bang dochan executes synchronously"
    (let [a (atom 0)]
      (dochan!! [i (arange 10 10)]
        (swap! a + i))
      (is (= 45 @a))))
  
  (testing "single-bang dochan executes asyncronously"
    (let [a (atom 0)]
      (dochan! [i (arange 10 10)]
        (swap! a + i))
      ;; First iteration of the body is delayed 10 ms, so still 0
      (is (zero? @a))
      (pause!! 150)
      ;; Give the body time to finish executing (plus a generous
      ;; amount, else this gets flakey).
      (is (= 45 @a)))))

(deftest test-fork
  (let [c (arange 10 10)
        [c1 c2 c3] (enos/fork c 3 10)
        [s1 s2 s3] (map #(chan->seq % 20) [c1 c2 c3])]
    (is (= (range 10) s1 s2 s3))))

(deftest test-pmap-makes-it-faster
  ;; Scenario: channel produces items every 20 ms, but they take 50
  ;; ms to process. Keep up.
  (let [c-in  (arange 10 20)
        c-out (pmap< (slow identity 50) c-in)]
    (let [[t result] (timed (<!! (async/into [] c-out)))]
      (is (= (range 10) result))
      (is (< t 500))                  ; It would take at least 500
                                        ; ms to process sequentially
      )))

(deftest test-pmap-retains-order
  (let [in-ch (arange 20 100)
        f     (fn [i]
                ;; Blocking function that is usually a bit slow, but
                ;; sometimes very slow
                (pause!! (if (#{1 4 5 11 15} i) 400 50))
                i)]
    (is (= (range 20) (chan->seq (pmap< f in-ch))))))

(deftest test-lazy-seq
  (is (= (range 10) (chan->seq (arange 10))))
  (is (= (range 10) (chan->seq (arange 10 10) 20)))
  (is (= ()         (chan->seq (arange 10 20) 10)))
  (let [c (arange 10 100 10)]
    (pause!! 310)
    ;; c should contain [0 1 2] by now...
    (is (= (range 3) (chan->seq c 50)))
    ))

(defgenerator foo [n]
  (loop [i n]
    (when (pos? i)
      (yield i)
      (recur (dec i)))))

(deftest test-generator
  (let [g (generator (yield 1) (yield 2) (yield 3))]
    (is (= [1 2 3] (chan->seq g))))

  (is (= [3 2 1] (chan->seq (foo 3))))

  (is (= [1 1 2 3 5 8 13 21] (take 8 (chan->seq (fib))))))


(deftest test-multitake
  (let [chs (for [ms [50 20 100]] (arange 100 ms))]
    (let [[v1 v2 v3] (<!! (go (<!+ chs)))]
      (is (= 0 v1 v2 v3)))
    (let [[v1 v2 v3] (<!! (go (<!+ chs)))]
      (is (= 1 v1 v2 v3)))))


;; Local Variables:
;; mode: clojure
;; eval: (define-clojure-indent (dochan! (quote defun))
;;                              (dochan!! (quote defun)))
;; End:
