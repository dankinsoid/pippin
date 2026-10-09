;; @ai-generated(solo)
;; core.async mult/tap/merge. Input: the integers 0..n-1, n = 20000. Work: an unbuffered source multed to two
;; 32-slot taps, one with (filter even?), one with (map square), merged and summed by a/reduce. Output: the sum of
;; the even inputs plus the sum of all squares. Apart from async-pipeline because it deadlocks here under load
;; (docs/notes/channels.md), and a hung run reports nothing else.
(ns workloads.async-broadcast
  (:require [clojure.core.async :as a]))

(defn- broadcast [n]
  (let [src (a/chan)
        m (a/mult src)
        evens (a/chan 32 (filter even?))
        squares (a/chan 32 (map #(* % %)))]
    (a/tap m evens)
    (a/tap m squares)
    ;; A mult drops what arrives before its first tap, so the source fills only now.
    (a/onto-chan! src (range n))
    (a/<!! (a/reduce + 0 (a/merge [evens squares])))))

(defn run* [n] (broadcast n))

(defn run [] (run* 20000))

;; What run returns on JVM Clojure 1.12.6; clj-corpus-bench and jvm.clj check every run against it.
(def expected '2666566660000)
