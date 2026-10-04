;; @ai-generated(solo)
;; Builds no host node: a test asserts the recorded call sequence, not a rendered result.
(ns pippin.ui.mock
  (:require [pippin.ui.reconciler :as r]))

;; A handle prints as [tag ordinal], so a logged call names the node it is about.
(defn- handle [tag n] [tag n])

(defn recording
  "{:backend b :root handle :log atom}; the log is a vector of [:create handle attrs], [:update handle attrs],
   [:remove handle], [:insert parent handle index] and [:detach parent handle], in the order they were made."
  []
  (let [log (atom [])
        next-id (atom 0)
        live (atom #{})
        record (fn [call] (swap! log conj call) nil)]
    {:log log
     :live live
     :root (handle :root 0)
     :backend
     (reify r/Backend
       (create! [_ tag attrs]
         (let [h (handle tag (swap! next-id inc))]
           (swap! live conj h)
           (record [:create h attrs])
           h))
       (update! [_ _tag node _prev attrs] (record [:update node attrs]))
       (remove! [_ _tag node]
         (swap! live disj node)
         (record [:remove node]))
       (insert! [_ parent node index] (record [:insert parent node index]))
       (detach! [_ parent node] (record [:detach parent node])))}))

(defn calls
  "The calls recorded so far."
  [mk]
  @(:log mk))

(defn taken
  "The calls recorded so far, clearing the log."
  [mk]
  (let [calls @(:log mk)]
    (reset! (:log mk) [])
    calls))

(defn live
  "The handles created and not yet removed: what a leaked node shows up in."
  [mk]
  @(:live mk))
