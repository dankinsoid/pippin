;; @ai-generated(solo)
(ns clojure.core.async
  "core.async over the runtime's coroutines (design §4): go is a spawn, <! and >! are parks with a direct
  hand-off, and any function may park — not only the body of a go block. The blocking variants (<!!, >!!,
  alts!!, alt!!) are the same functions: a wait from a bare thread blocks that thread, a wait from a coroutine
  parks it. See NOTES.md, \"Channels\", and docs/jvm-differences.md.")

(defn buffer
  "Returns a fixed buffer of size n. When full, puts will block/park."
  [n]
  (buffer* n))

(defn dropping-buffer
  "Returns a buffer of size n. When full, puts will complete but val will be dropped (no transfer)."
  [n]
  (dropping-buffer* n))

(defn sliding-buffer
  "Returns a buffer of size n. When full, puts will complete, and be buffered, but oldest elements in buffer
  will be dropped (not transferred)."
  [n]
  (sliding-buffer* n))

(defn chan
  "Creates a channel with an optional buffer. If buf-or-n is a number, will create and use a fixed buffer of
  that size. Transducers are not supported yet (NOTES.md, \"Channels\")."
  ([] (chan* nil))
  ([buf-or-n] (chan* buf-or-n))
  ([buf-or-n xform] (chan buf-or-n xform nil))
  ([buf-or-n xform ex-handler]
   (if xform
     (throw (ex-info "chan: transducers on channels are not supported yet" {:xform xform}))
     (chan* buf-or-n))))

(defn timeout
  "Returns a channel that will close after msecs."
  [msecs]
  (chan-timeout* msecs))

(defn <!
  "Takes a val from port. Parks until a value is available; returns nil if closed. Any function may call
  it, not only a go block; from a bare thread it blocks the thread."
  [port]
  (chan-take* port))

(defn >!
  "Puts a val into port. nil values are not allowed. Parks until the put completes; returns true unless
  port is already closed. Any function may call it."
  [port val]
  (chan-put* port val))

(def <!! "The same as <!: a wait from a bare thread blocks it, from a coroutine parks it." <!)
(def >!! "The same as >!: a wait from a bare thread blocks it, from a coroutine parks it." >!)

(defn take!
  "Asynchronously takes a val from port, passing to fn1. Will pass nil if closed. If on-caller? (default
  true) is true, and value is immediately available, will call fn1 on calling thread. Returns nil."
  ([port fn1] (chan-take-cb* port fn1 true))
  ([port fn1 on-caller?] (chan-take-cb* port fn1 on-caller?)))

(defn put!
  "Asynchronously puts a val into port, calling fn1 (if supplied) when complete, passing false iff port is
  already closed. nil values are not allowed. Returns true unless port is already closed."
  ([port val] (chan-put-cb* port val nil true))
  ([port val fn1] (chan-put-cb* port val fn1 true))
  ([port val fn1 on-caller?] (chan-put-cb* port val fn1 on-caller?)))

(defn close!
  "Closes a channel. The channel will no longer accept any puts (they will be ignored). Data in the channel
  remains available for taking, until exhausted, after which takes will return nil."
  [chan]
  (chan-close* chan))

(defn offer!
  "Puts a val into port if it's possible to do so immediately. nil values are not allowed. Never parks.
  Returns true if offer succeeds, false on a closed channel, nil otherwise."
  [port val]
  (chan-offer* port val))

(defn poll!
  "Takes a val from port if it's possible to do so immediately. Never parks. Returns value if successful, nil
  otherwise."
  [port]
  (chan-poll* port))

(defn alts!
  "Completes at most one of several channel operations. Must be called in a go block. ports is a vector of
  channel endpoints, which can be either a channel to take from or a vector of [channel-to-put-to val-to-put],
  in any combination. Takes will be made as if by <!, and puts will be made as if by >!. Unless the :priority
  option is true, if more than one port operation is ready a non-deterministic choice will be made. If no
  operation is ready and a :default value is supplied, [default-val :default] will be returned, otherwise
  alts! will park until the first operation to become ready completes. Returns [val port] of the completed
  operation, where val is the value taken for takes, and a boolean (true unless already closed, as per put!)
  for puts."
  [ports & {:as opts}]
  (chan-alts* ports opts))

(def alts!! "The same as alts!: a wait from a bare thread blocks it, from a coroutine parks it." alts!)

(defn- do-alt
  "The JVM's expansion: bindings for every port and value, one alts!, a cond on the completed port."
  [alts clauses]
  (let [clauses (partition 2 clauses)
        opt? #(keyword? (first %))
        opts (filter opt? clauses)
        clauses (remove opt? clauses)
        [clauses bindings]
        (reduce
         (fn [[clauses bindings] [ports expr]]
           (let [ports (if (vector? ports) ports [ports])
                 [ports bindings]
                 (reduce
                  (fn [[ports bindings] port]
                    (if (vector? port)
                      (let [[port val] port
                            gp (gensym)
                            gv (gensym)]
                        [(conj ports [gp gv]) (conj bindings [gp port] [gv val])])
                      (let [gp (gensym)]
                        [(conj ports gp) (conj bindings [gp port])])))
                  [[] bindings] ports)]
             [(conj clauses [ports expr]) bindings]))
         [[] []] clauses)
        gch (gensym "ch")
        gret (gensym "ret")]
    `(let [~@(apply concat bindings)
           [val# ~gch :as ~gret] (~alts [~@(apply concat (map first clauses))] ~@(apply concat opts))]
       (cond
         ~@(mapcat (fn [[ports expr]]
                     [`(or ~@(map (fn [port]
                                    `(= ~gch ~(if (vector? port) (first port) port)))
                                  ports))
                      (if (and (seq? expr) (vector? (first expr)))
                        `(let [~(first expr) ~gret] ~@(rest expr))
                        expr)])
                   clauses)
         (= ~gch :default) val#))))

(defmacro alt!
  "Makes a single choice between one of several channel operations, as if by alts!, returning the value of the
  result expr corresponding to the operation completed. Each clause takes the form of:

  channel-op[s] result-expr

  where channel-ops is one of: a take-port, [put-port put-val], or a vector of those; result-expr is either a
  list beginning with a vector, whereupon that vector will be treated as a binding for the [val port] return of
  the operation, else any other expression. A :default clause and a :priority true clause are honoured as in
  alts!. Each option may appear at most once."
  [& clauses]
  (do-alt `alts! clauses))

(defmacro alt!!
  "The same as alt!: a wait from a bare thread blocks it, from a coroutine parks it."
  [& clauses]
  `(alt! ~@clauses))

(defmacro go
  "Asynchronously executes the body on a coroutine of the pool, returning immediately to the calling thread.
  The body is an ordinary fn: <! and >! may be called from any function it invokes. Returns a channel which
  will receive the result of the body when completed; the channel is then closed. Dynamic bindings are
  conveyed. (cancel! ch) cancels the coroutine at its next park or loop tick."
  [& body]
  `(go* (fn [] ~@body)))

(defmacro go-main
  "As go, on the main carrier: the body runs when the main thread's run loop turns (clj_sched_main_install)."
  [& body]
  `(go-main* (fn [] ~@body)))

(defmacro go-loop
  "Like (go (loop ...))"
  [bindings & body]
  `(go (loop ~bindings ~@body)))

(defn thread-call
  "Executes f in another thread, returning immediately to the calling thread. Returns a channel which will
  receive the result of calling f when completed, then close. Dynamic bindings are conveyed."
  [f]
  (thread* f))

(defmacro thread
  "Executes the body in another thread, returning immediately to the calling thread. Returns a channel which
  will receive the result of the body when completed, then close."
  [& body]
  `(thread-call (fn [] ~@body)))

(defn cancel!
  "Cancels the go block behind a channel returned by go: its next park or loop tick throws. Returns true when
  the channel had a go block, false otherwise. Not in the JVM's core.async."
  [ch]
  (chan-cancel* ch))
