(ns throwing-puts.core-test
  (:require
   [clojure.core.async :as async :refer [go chan]]
   [clojure.test :refer :all]
   [throwing-puts.core :as throwing])
  (:import
   [clojure.lang ExceptionInfo]))

(def timeout 100)

(def message 42)

(defn blocking-chan []
  (chan))

(defn nonblocking-chan []
  (chan 1))

(deftest >!!
  (testing "Green path"
    (is (true? (async/<!! (go
                            (throwing/>! (nonblocking-chan) message timeout))))))
  (testing "Red path"
    (is (= :error (async/<!! (go
                               (try
                                 (throwing/>! (blocking-chan) message timeout)
                                 (catch Exception e
                                   :error))))))))

(deftest >!!
  (testing "Green path"
    (is (true? (throwing/>!! (nonblocking-chan) message timeout))))
  (testing "Red path"
    (is (thrown? ExceptionInfo
                 (throwing/>!! (blocking-chan) message timeout)))))

(deftest dynamic-var
  (testing "It is taken into account"
    (let [started-at (System/currentTimeMillis)
          timeout (- throwing/default-timeout-ms 100)
          finished-at (binding [throwing/*default-timeout-ms* timeout]
                        (try
                          (throwing/>!! (blocking-chan) 42)
                          (catch ExceptionInfo e
                            (System/currentTimeMillis))))
          ellapsed (- finished-at started-at)]
      (is (>= ellapsed timeout))
      (is (< ellapsed throwing/default-timeout-ms)))))
