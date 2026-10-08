(ns enos.core
  "Utilities for core.async."
  (:require [clojure.core.async :as async
             :refer [go go-loop <! >! <!! >!! thread close!]]))


(def ^:private
  processors
  (delay (-> (Runtime/getRuntime)
             (.availableProcessors))))

(defmacro pause!
  "Used in a go-block, pauses execution for `ms` milliseconds without
  blocking a thread."
  [ms]
  `(<! (async/timeout ~ms)))

(defn pause!!
  "Blocks the calling thread for `ms` milliseconds."
  [ms]
  (<!! (async/timeout ms)))

(defn arange
  "Returns a channel that will produce the numbers from 0 to n-1 and then close.
  If `ms` is provided, pauses that many milliseconds before emitting
  each number."
  ([n]
     (arange n nil))
  ([n ms]
     (arange n ms nil))
  ([n ms buf-or-n]
     (let [ch (async/chan buf-or-n)]
       (go
         (dotimes [i n]
           (when (and ms (pos? ms))
             (pause! ms))
           (>! ch i))
         (close! ch))
       ch)))

(defmacro dochan* [loop-sym take-sym [binding ch] & body]
  `(let [ch# ~ch]
     (~loop-sym []
                (let [v# (~take-sym ch#)]
                  (when-not (nil? v#)
                    (let [~binding v#]
                      ~@body
                      (recur)))))))

(defmacro dochan!
  "Asynchronously executes the body for each value in the channel,
  extracting with <! and binding the result to `binding`."
  [[binding ch] & body]
  `(dochan* go-loop <! [~binding  ~ch] ~@body))

(defmacro dochan!!
  "Synchronously executes the body for each value in the channel,
  extracting with <!! and binding the result to `binding`. Will block
  until the channel is closed."
  [[binding ch] & body]
  `(dochan* loop <!! [~binding ~ch] ~@body))

(defn drain! [ch]
  "Consumes and discards all values in the channel, asynchronously."
  (dochan! [_ ch]))

(defn drain!! [ch]
  "Consumes and discards all values in the channel."
  (dochan!! [_ ch]))

(defn fork
  "Return two or more new channels that tap the given channel."
  ([ch]
     (fork ch 2))
  ([ch n]
     (fork ch 2 nil))
  ([ch n buf-or-n]
     (let [m   (async/mult ch)
           chs (repeatedly n #(async/chan buf-or-n))]
       (doseq [ch chs]
         (async/tap m ch))
       chs)))

(defn pmap<
  "Parallel map over a channel. Executes `n` worker threads,
  each of which applies `f` to values from the channel. Returns a new
  channel containing the return valus of `f`, in the same order as the
  input channel."
  ;; TODO - support more than one input channel
  ([f ch]
     (pmap< f ch (* 2 @processors)))
  ([f ch n]
     (let [[c1 c2] (fork (async/map< #(vector % (promise)) ch) 2 n)]
       (dotimes [_ n]
         (thread
          (dochan! [[v p] c1]
            (deliver p (f v)))))
       (async/map< (comp deref second) c2))))

(defmacro pdochan! [n [binding ch] & body]
  "WIP - Execute the body in `n` threads."
  `(drain! (pmap< (fn [~binding] ~@body nil) ~ch ~n)))


(defn chan->seq
  "Returns a lazy-seq of the values from a channel. Ends when either:
   - the channel is closed, or
   - it takes more than `timeout` milliseconds to get the next value."
  ([ch]
     (chan->seq ch nil))
  ([ch timeout]
     (lazy-seq
      (let [chs   (vec (cons ch (when timeout (list (async/timeout timeout)))))
            [v _] (async/alts!! chs)]
        (when-not (nil? v)
          (cons v (chan->seq ch timeout)))))))


(defmacro <!+
  "Returns a vector containing one value taken from each of a sequence
  of channels, in the same order as the channels. Must be used in a go
  block."
  [chs]
  `(let [chs# (zipmap ~chs (range))
         res# (object-array (count chs#))]
     (loop [chs# chs#]
       (if (empty? chs#)
         (vec res#)
         (let [[v# ch#] (async/alts! (keys chs#))
               idx#     (get chs# ch#)]
           (aset res# idx# v#)
           (recur (dissoc chs# ch#)))))))


(defmacro generator
  "Emulates, more-or-less, a Python / JavaScript generator. Returns an
  unbuffered channel whose values are produced by calls to `yield`
  within the body."
  [& body]
  (let [ch   (gensym "ch")
        body (clojure.walk/prewalk (fn [f]
                                     (if (and (list? f) (= 'yield (first f)))
                                       (list* `>! ch (rest f))
                                       f))
                                   body)]
    `(let [~ch (async/chan)]
       (go ~@body
           (async/close! ~ch))
       ~ch)))

(defmacro defgenerator
  "Defines a function that returns a channel. The channel's value's
  are produced by calls to `yield` with the body."
  ;; TODO - support multi-arity
  [name arglist & body]
  `(defn ~name ~arglist
     (generator ~@body)))

(defn poisson
  "Generate values via a Poisson process.
   - interval - tick interval in milliseconds
   - prob - number > 0.0 and < 1.0 - probability of emitting an event at each tick
   - fn0 - function to generate the next value
   - iterations - optional - end after this many ticks. If nil, it goes forever.
  "
  ([interval prob fn0]
     (poisson interval prob fn0 nil))
  ([interval prob fn0 iterations]
     (generator
      (loop [remaining iterations]
        (when (and iterations (pos? remaining))
          (pause! interval)
          (if (< (rand) prob)
            (yield (fn0)))
          (recur (dec remaining)))))))

(defgenerator fib []
  (loop [a 1 b 1]
    (yield a)
    (recur b (+ a b))))


