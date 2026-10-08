(ns throwing-puts.core
  (:require
   [clojure.core.async :as async]))

(def default-timeout-ms 1000)

(def ^:dynamic *default-timeout-ms* nil)

(defn- impl [& {:keys [channel timeout-ms message alts-fn]}]
  `(let [the-channel# ~channel ;; ensure `channel` is evaluated just once (it can be a symbol or an arbitrary form)
         the-message# ~message ;; same
         the-timeout# (or ~timeout-ms *default-timeout-ms* default-timeout-ms) ;; same, plus config logic
         [v# ch#] (~alts-fn [[the-channel# the-message#] (async/timeout the-timeout#)])]
     (if (= ch# the-channel#)
       v#
       (throw (ex-info (str ::put-or-throw! " - :channel is full / put timed out")
                       {:message the-message#
                        :timed-out-after the-timeout#
                        :channel the-channel#})))))

(defmacro >!!
  "Puts the `message` into `channel`,
  or throws an exception if not possible before `timeout-ms`
  (defaults: `*default-timeout-ms*`, `#'default-timeout-ms`. In that order)"
  [channel message & [timeout-ms]]
  (impl :channel channel :timeout-ms timeout-ms :message message :alts-fn `async/alts!!))

(defmacro >!
  "Puts the `message` into `channel`,
    or throws an exception if not possible before `timeout-ms`
    (defaults: `*default-timeout-ms*`, `#'default-timeout-ms`. In that order)"
  [channel message & [timeout-ms]]
  (impl :channel channel :timeout-ms timeout-ms :message message :alts-fn `async/alts!))
