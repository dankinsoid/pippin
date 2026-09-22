;; @ai-generated(solo)
(ns clojure.core.async
  "core.async over the runtime's coroutines (design §4): go is a spawn, <! and >! are parks with a direct
  hand-off, and any function may park — not only the body of a go block. The blocking variants (<!!, >!!,
  alts!!, alt!!) are the same functions: a wait from a bare thread blocks that thread, a wait from a coroutine
  parks it. The library layer (pipe, mult, pub, mix, pipeline, …) is core.async's own shape over these
  primitives. See NOTES.md, \"Channels\" and \"Futures and scopes\", and docs/jvm-differences.md."
  (:refer-clojure :exclude [reduce transduce into merge map take partition partition-by]))

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

(defn unblocking-buffer?
  "Returns true if a channel created with buff will never block. That is to say, puts into this buffer will
  never cause the buffer to be full."
  [buff]
  (unblocking-buffer?* buff))

(defn chan
  "Creates a channel with an optional buffer, an optional transducer (like (map f), (filter p) etc or a
  composition thereof), and an optional exception-handler. If buf-or-n is a number, will create and use a
  fixed buffer of that size. If a transducer is supplied a buffer must be specified. ex-handler must be a fn
  of one argument - if an exception occurs during transformation it will be called with the exception as an
  argument, and any non-nil return value will be placed in the channel. The transducer's step runs under the
  channel's coroutine mutex, so it may park (NOTES.md, \"Channels\")."
  ([] (chan* nil))
  ([buf-or-n] (chan* buf-or-n))
  ([buf-or-n xform] (chan buf-or-n xform nil))
  ([buf-or-n xform ex-handler]
   (if xform
     (chan* buf-or-n xform ex-handler)
     (chan* buf-or-n))))

(defn promise-chan
  "Creates a promise channel with an optional transducer, and an optional exception-handler. A promise channel
  can take exactly one value that consumers will receive. Once full, puts complete but val is dropped (no
  transfer). Consumers will block until either a value is placed in the channel or the channel is closed,
  then return the value (or nil) forever. See chan for the semantics of xform and ex-handler."
  ([] (promise-chan nil))
  ([xform] (promise-chan xform nil))
  ([xform ex-handler] (chan (promise-buffer*) xform ex-handler)))

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

(defn do-alt
  "The JVM's expansion: bindings for every port and value, one alts!, a cond on the completed port."
  [alts clauses]
  (let [clauses (clojure.core/partition 2 clauses)
        opt? #(keyword? (first %))
        opts (filter opt? clauses)
        clauses (remove opt? clauses)
        [clauses bindings]
        (clojure.core/reduce
         (fn [[clauses bindings] [ports expr]]
           (let [ports (if (vector? ports) ports [ports])
                 [ports bindings]
                 (clojure.core/reduce
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
           [val# ~gch :as ~gret] (~alts [~@(apply concat (clojure.core/map first clauses))] ~@(apply concat opts))]
       (cond
         ~@(mapcat (fn [[ports expr]]
                     [`(or ~@(clojure.core/map (fn [port]
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

;;;; go and go-scoped: a scope conveyed through the binding chain makes every go in its extent a child

(def ^:dynamic *scope*
  "The go-scoped scope of the dynamic extent, nil outside any: conveyed to spawned coroutines like every
  binding, so their go blocks are children of the same scope (NOTES.md, \"Futures and scopes\")."
  nil)

(defn- scope-new []
  {:pending (atom 0)
   :done (chan (sliding-buffer 1))
   :children (atom {}) ; token -> channel, nil until the spawn returned
   :state (atom {:error nil :failing false})
   :body (coro-current*)})

;; The cause rides with the cancellation: a sibling reads the failure that killed it through ex-cause.
(defn- scope-cancel-children! [s cause]
  (doseq [c (vals @(:children s)) :when c] (chan-cancel-cause* c cause)))

;; The first failure is the scope's error: it cancels the siblings and the body; later ones are its consequences.
(defn- scope-child-failed! [s e]
  (let [st (swap! (:state s) (fn [st] (if (:failing st) st (assoc st :error e :failing true))))]
    (when (identical? (:error st) e)
      (scope-cancel-children! s e)
      (coro-cancel-scope* (:body s) e))))

(defn- scope-child-done! [s tok]
  (swap! (:children s) dissoc tok)
  (when (zero? (swap! (:pending s) dec))
    (put! (:done s) true)))

;; The count is raised before the spawn, so a join that finds it zero has nothing left to wait for.
(defn- scope-spawn [s spawn f]
  (swap! (:pending s) inc)
  (let [tok (volatile! nil)
        _ (swap! (:children s) assoc tok nil)
        c (spawn (fn []
                   (try
                     (f)
                     ;; Quietly only when this child was the one cancelled; someone else's cancellation is a failure.
                     (catch :cancelled e (when-not (cancelled?*) (scope-child-failed! s e)) nil)
                     (catch :default e (scope-child-failed! s e) nil)
                     (finally (scope-child-done! s tok)))))]
    ;; A child done before the spawn returned has left already: its entry is not put back.
    (swap! (:children s) (fn [m] (if (contains? m tok) (assoc m tok c) m)))
    c))

(defn- spawn* [spawn f]
  (if-let [s *scope*]
    (scope-spawn s spawn f)
    (spawn f)))

(defn go*
  "The function behind go: spawns f on a pool coroutine, as a child of the enclosing go-scoped scope when there is one."
  [f]
  (spawn* coro-go* f))

(defn go-main*
  "The function behind go-main: as go*, on the main carrier."
  [f]
  (spawn* coro-go-main* f))

(defmacro go
  "Asynchronously executes the body on a coroutine of the pool, returning immediately to the calling thread.
  The body is an ordinary fn: <! and >! may be called from any function it invokes. Returns a channel which
  will receive the result of the body when completed; the channel is then closed. Dynamic bindings are
  conveyed. (cancel! ch) cancels the coroutine at its next park or loop tick. Inside a go-scoped scope the
  block is a child of the scope."
  [& body]
  ;; The unscoped spawn goes straight to the primitive: a trace through the block shows no frame of ours (the JVM's
  ;; go shows nothing of the spawner either).
  `(let [f# (fn [] ~@body)] (if *scope* (go* f#) (coro-go* f#))))

(defmacro go-main
  "As go, on the main carrier: the body runs when the main thread's run loop turns (clj_sched_main_install)."
  [& body]
  `(let [f# (fn [] ~@body)] (if *scope* (go-main* f#) (coro-go-main* f#))))

(defmacro go-loop
  "Like (go (loop ...))"
  [bindings & body]
  `(go (loop ~bindings ~@body)))

;; The join outlasts a cancellation of the body: a scope never returns while a child runs.
(defn- scope-join! [s]
  (loop []
    (when (pos? @(:pending s))
      (chan-take* (:done s) true)
      (recur))))

(defn scoped*
  "The function behind go-scoped: runs f inline under a fresh scope, joins the children on the way out."
  [f]
  (let [s (scope-new)]
    (binding [*scope* s]
      ;; :cancelled reaches here too: the body's own cancellation must still cancel and join the children.
      (let [r (try {:value (f)} (catch :cancelled e {:error e}) (catch :default e {:error e}))]
        (when (contains? r :error)
          (swap! (:state s) assoc :failing true)
          (scope-cancel-children! s (:error r)))
        (scope-join! s)
        (coro-uncancel-scope* (:body s))
        (if-let [e (or (:error @(:state s)) (:error r))]
          (throw e)
          (:value r))))))

(defmacro go-scoped
  "Runs the body inline under a scope (design §4, \"Контекст go\"): every go spawned in its dynamic extent —
  including from functions it calls and from coroutines those spawn — is a child of the scope. On exit the
  children are joined. A child's uncaught error cancels the siblings and the body and is rethrown from
  go-scoped; an error of the body cancels the children first. Cancelling the coroutine running the scope
  cancels the children transitively. Returns the body's value. Not in the JVM's core.async."
  [& body]
  `(scoped* (fn [] ~@body)))

(defmacro plet
  "Like let, but every init expression runs in its own go block under one go-scoped scope, and the bindings
  are their values: Swift's async let. A failing init cancels the others and rethrows. Not in the JVM's core.async."
  [bindings & body]
  (let [pairs (clojure.core/partition 2 bindings)
        chans (clojure.core/map (fn [_] (gensym "plet")) pairs)]
    `(go-scoped
      (let [~@(mapcat (fn [c [_ e]] [c `(go ~e)]) chans pairs)]
        (let [~@(mapcat (fn [[s _] c] [s `(<! ~c)]) pairs chans)]
          ~@body)))))

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
  "Cancels the go block, future or thread body behind a channel: its next park or loop tick throws. Returns
  true when the body was still running, false otherwise. Not in the JVM's core.async."
  [ch]
  (chan-cancel* ch))

(defn cancelled?
  "True once the running coroutine's cancel flag is set: its next park or loop tick throws :cancelled.
  A cooperative poll, not a park point itself. Not in the JVM's core.async."
  []
  (cancelled?*))

;;;;;;;;;;;;;;;;;;;; ops ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(defn pipe
  "Takes elements from the from channel and supplies them to the to channel. By default, the to channel will
  be closed when the from channel closes, but can be determined by the close? parameter. Will stop consuming
  the from channel if the to channel closes"
  ([from to] (pipe from to true))
  ([from to close?]
   (go-loop []
     (let [v (<! from)]
       (if (nil? v)
         (when close? (close! to))
         (when (>! to v)
           (recur)))))
   to))

(defn- pipeline*
  ([n to xf from close? ex-handler type]
   (assert (pos? n))
   (let [ex-handler (or ex-handler (fn [ex] (uncaught-report* ex) nil))
         jobs (chan n)
         results (chan n)
         process (fn [[v p :as job]]
                   (if (nil? job)
                     (do (close! results) nil)
                     (let [res (chan 1 xf ex-handler)]
                       (>!! res v)
                       (close! res)
                       (put! p res)
                       true)))
         async (fn [[v p :as job]]
                 (if (nil? job)
                   (do (close! results) nil)
                   (let [res (chan 1)]
                     (xf v res)
                     (put! p res)
                     true)))]
     (dotimes [_ n]
       (case type
         (:blocking :compute) (thread
                                (let [job (<!! jobs)]
                                  (when (process job)
                                    (recur))))
         :async (go-loop []
                  (let [job (<! jobs)]
                    (when (async job)
                      (recur))))))
     (go-loop []
       (let [v (<! from)]
         (if (nil? v)
           (close! jobs)
           (let [p (chan 1)]
             (>! jobs [v p])
             (>! results p)
             (recur)))))
     (go-loop []
       (let [p (<! results)]
         (if (nil? p)
           (when close? (close! to))
           (let [res (<! p)]
             (loop []
               (let [v (<! res)]
                 (when (and (not (nil? v)) (>! to v))
                   (recur))))
             (recur))))))))

(defn pipeline
  "Takes elements from the from channel and supplies them to the to channel, subject to the transducer xf,
  with parallelism n. Because it is parallel, the transducer will be applied independently to each element,
  not across elements, and may produce zero or more outputs per input. Outputs will be returned in order
  relative to the inputs. By default, the to channel will be closed when the from channel closes, but can be
  determined by the close? parameter. Will stop consuming the from channel if the to channel closes. Note this
  should be used for computational parallelism. If you have multiple blocking operations to put in flight, use
  pipeline-blocking instead. If you have multiple asynchronous operations to put in flight, use pipeline-async
  instead. See chan for semantics of ex-handler."
  ([n to xf from] (pipeline n to xf from true))
  ([n to xf from close?] (pipeline n to xf from close? nil))
  ([n to xf from close? ex-handler] (pipeline* n to xf from close? ex-handler :compute)))

(defn pipeline-blocking
  "Like pipeline, for blocking operations."
  ([n to xf from] (pipeline-blocking n to xf from true))
  ([n to xf from close?] (pipeline-blocking n to xf from close? nil))
  ([n to xf from close? ex-handler] (pipeline* n to xf from close? ex-handler :blocking)))

(defn pipeline-async
  "Takes elements from the from channel and supplies them to the to channel, subject to the async function af,
  with parallelism n. af must be a function of two arguments, the first an input value and the second a channel
  on which to place the result(s). The presumption is that af will return immediately, having launched some
  asynchronous operation whose completion/callback will put results on the channel, then close! it. Outputs
  will be returned in order relative to the inputs. By default, the to channel will be closed when the from
  channel closes, but can be determined by the close? parameter. Will stop consuming the from channel if the
  to channel closes. See also pipeline, pipeline-blocking."
  ([n to af from] (pipeline-async n to af from true))
  ([n to af from close?] (pipeline* n to af from close? nil :async)))

(defn split
  "Takes a predicate and a source channel and returns a vector of two channels, the first of which will contain
  the values for which the predicate returned true, the second those for which it returned false.

  The out channels will be unbuffered by default, or two buf-or-ns can be supplied. The channels will close
  after the source channel has closed."
  ([p ch] (split p ch nil nil))
  ([p ch t-buf-or-n f-buf-or-n]
   (let [tc (chan t-buf-or-n)
         fc (chan f-buf-or-n)]
     (go-loop []
       (let [v (<! ch)]
         (if (nil? v)
           (do (close! tc) (close! fc))
           (when (>! (if (p v) tc fc) v)
             (recur)))))
     [tc fc])))

(defn reduce
  "f should be a function of 2 arguments. Returns a channel containing the single result of applying f to init
  and the first item from the channel, then applying f to that result and the 2nd item, etc. If the channel
  closes without yielding items, returns init and f is not called. ch must close before reduce produces a
  result."
  [f init ch]
  (go-loop [ret init]
    (let [v (<! ch)]
      (if (nil? v)
        ret
        (let [ret' (f ret v)]
          (if (reduced? ret')
            @ret'
            (recur ret')))))))

(defn transduce
  "async/reduces a channel with a transformation (xform f). Returns a channel containing the result. ch must
  close before transduce produces a result."
  [xform f init ch]
  (let [f (xform f)]
    (go
      (let [ret (<! (reduce f init ch))]
        (f ret)))))

(defn- bounded-count
  "Returns the smaller of n or the count of coll, without examining more than n items if coll is not counted"
  [n coll]
  (if (counted? coll)
    (min n (count coll))
    (loop [i 0 s (seq coll)]
      (if (and s (< i n))
        (recur (inc i) (next s))
        i))))

(defn onto-chan!
  "Puts the contents of coll into the supplied channel.

  By default the channel will be closed after the items are copied, but can be determined by the close?
  parameter.

  Returns a channel which will close after the items are copied.

  If accessing coll might block, use onto-chan!! instead"
  ([ch coll] (onto-chan! ch coll true))
  ([ch coll close?]
   (go-loop [vs (seq coll)]
     (if (and vs (>! ch (first vs)))
       (recur (next vs))
       (when close?
         (close! ch))))))

(defn to-chan!
  "Creates and returns a channel which contains the contents of coll, closing when exhausted.

  If accessing coll might block, use to-chan!! instead"
  [coll]
  (let [c (bounded-count 100 coll)]
    (if (pos? c)
      (let [ch (chan c)]
        (onto-chan! ch coll)
        ch)
      (let [ch (chan)]
        (close! ch)
        ch))))

(defn onto-chan
  "Deprecated - use onto-chan! or onto-chan!!"
  {:deprecated "1.2"}
  ([ch coll] (onto-chan! ch coll true))
  ([ch coll close?] (onto-chan! ch coll close?)))

(defn to-chan
  "Deprecated - use to-chan! or to-chan!!"
  {:deprecated "1.2"}
  [coll]
  (to-chan! coll))

(defn onto-chan!!
  "Like onto-chan! for use when accessing coll might block, e.g. a lazy seq of blocking operations"
  ([ch coll] (onto-chan!! ch coll true))
  ([ch coll close?]
   (thread
     (loop [vs (seq coll)]
       (if (and vs (>!! ch (first vs)))
         (recur (next vs))
         (when close?
           (close! ch)))))))

(defn to-chan!!
  "Like to-chan! for use when accessing coll might block, e.g. a lazy seq of blocking operations"
  [coll]
  (let [c (bounded-count 100 coll)]
    (if (pos? c)
      (let [ch (chan c)]
        (onto-chan!! ch coll)
        ch)
      (let [ch (chan)]
        (close! ch)
        ch))))

(defprotocol Mux
  (muxch* [_]))

(defprotocol Mult
  (tap* [m ch close?])
  (untap* [m ch])
  (untap-all* [m]))

(defn mult
  "Creates and returns a mult(iple) of the supplied channel. Channels containing copies of the channel can be
  created with 'tap', and detached with 'untap'.

  Each item is distributed to all taps in parallel and synchronously, i.e. each tap must accept before the
  next item is distributed. Use buffering/windowing to prevent slow taps from holding up the mult.

  Items received when there are no taps get dropped.

  If a tap puts to a closed channel, it will be removed from the mult."
  [ch]
  (let [cs (atom {}) ;;ch->close?
        m (reify
            Mux
            (muxch* [_] ch)

            Mult
            (tap* [_ ch close?] (swap! cs assoc ch close?) nil)
            (untap* [_ ch] (swap! cs dissoc ch) nil)
            (untap-all* [_] (reset! cs {}) nil))
        dchan (chan 1)
        dctr (atom nil)
        done (fn [_] (when (zero? (swap! dctr dec))
                       (put! dchan true)))]
    (go-loop []
      (let [val (<! ch)]
        (if (nil? val)
          (doseq [[c close?] @cs]
            (when close? (close! c)))
          (let [chs (keys @cs)]
            (reset! dctr (count chs))
            (doseq [c chs]
              (when-not (put! c val done)
                (untap* m c)))
            ;;wait for all
            (when (seq chs)
              (<! dchan))
            (recur)))))
    m))

(defn tap
  "Copies the mult source onto the supplied channel.

  By default the channel will be closed when the source closes, but can be determined by the close? parameter."
  ([mult ch] (tap mult ch true))
  ([mult ch close?] (tap* mult ch close?) ch))

(defn untap
  "Disconnects a target channel from a mult"
  [mult ch]
  (untap* mult ch))

(defn untap-all
  "Disconnects all target channels from a mult"
  [mult] (untap-all* mult))

(defprotocol Mix
  (admix* [m ch])
  (unmix* [m ch])
  (unmix-all* [m])
  (toggle* [m state-map])
  (solo-mode* [m mode]))

(defn mix
  "Creates and returns a mix of one or more input channels which will be put on the supplied out channel.
  Input sources can be added to the mix with 'admix', and removed with 'unmix'. A mix supports soloing, muting
  and pausing multiple inputs atomically using 'toggle', and can solo using either muting or pausing as
  determined by 'solo-mode'.

  Each channel can have zero or more boolean modes set via 'toggle':

  :solo - when true, only this (ond other soloed) channel(s) will appear in the mix output channel. :mute and
          :pause states of soloed channels are ignored. If solo-mode is :mute, non-soloed channels are muted,
          if :pause, non-soloed channels are paused.

  :mute - muted channels will have their contents consumed but not included in the mix
  :pause - paused channels will not have their contents consumed (and thus also not included in the mix)"
  [out]
  (let [cs (atom {}) ;;ch->attrs-map
        solo-modes #{:mute :pause}
        attrs (conj solo-modes :solo)
        solo-mode (atom :mute)
        change (chan (sliding-buffer 1))
        changed #(put! change true)
        pick (fn [attr chs]
               (reduce-kv
                (fn [ret c v]
                  (if (attr v)
                    (conj ret c)
                    ret))
                #{} chs))
        calc-state (fn []
                     (let [chs @cs
                           mode @solo-mode
                           solos (pick :solo chs)
                           pauses (pick :pause chs)]
                       {:solos solos
                        :mutes (pick :mute chs)
                        :reads (conj
                                (if (and (= mode :pause) (seq solos))
                                  (vec solos)
                                  (vec (remove pauses (keys chs))))
                                change)}))
        m (reify
            Mux
            (muxch* [_] out)
            Mix
            (admix* [_ ch] (swap! cs assoc ch {}) (changed))
            (unmix* [_ ch] (swap! cs dissoc ch) (changed))
            (unmix-all* [_] (reset! cs {}) (changed))
            (toggle* [_ state-map] (swap! cs (partial merge-with clojure.core/merge) state-map) (changed))
            (solo-mode* [_ mode]
              (assert (solo-modes mode) (str "mode must be one of: " solo-modes))
              (reset! solo-mode mode)
              (changed)))]
    (go-loop [{:keys [solos mutes reads] :as state} (calc-state)]
      (let [[v c] (alts! reads)]
        (if (or (nil? v) (= c change))
          (do (when (nil? v)
                (swap! cs dissoc c))
              (recur (calc-state)))
          (if (or (solos c)
                  (and (empty? solos) (not (mutes c))))
            (when (>! out v)
              (recur state))
            (recur state)))))
    m))

(defn admix
  "Adds ch as an input to the mix"
  [mix ch]
  (admix* mix ch))

(defn unmix
  "Removes ch as an input to the mix"
  [mix ch]
  (unmix* mix ch))

(defn unmix-all
  "removes all inputs from the mix"
  [mix]
  (unmix-all* mix))

(defn toggle
  "Atomically sets the state(s) of one or more channels in a mix. The state map is a map of channels ->
  channel-state-map. A channel-state-map is a map of attrs -> boolean, where attr is one or more of :mute,
  :pause or :solo. Any states supplied are merged with the current state.

  Note that channels can be added to a mix via toggle, which can be used to add channels in a particular
  (e.g. paused) state."
  [mix state-map]
  (toggle* mix state-map))

(defn solo-mode
  "Sets the solo mode of the mix. mode must be one of :mute or :pause"
  [mix mode]
  (solo-mode* mix mode))

(defprotocol Pub
  (sub* [p v ch close?])
  (unsub* [p v ch])
  (unsub-all* [p] [p v]))

(defn pub
  "Creates and returns a pub(lication) of the supplied channel, partitioned into topics by the topic-fn.
  topic-fn will be applied to each value on the channel and the result will determine the 'topic' on which
  that value will be put. Channels can be subscribed to receive copies of topics using 'sub', and unsubscribed
  using 'unsub'. Each topic will be handled by an internal mult on a dedicated channel. By default these
  internal channels are unbuffered, but a buf-fn can be supplied which, given a topic, creates a buffer with
  desired properties.

  Each item is distributed to all subs in parallel and synchronously, i.e. each sub must accept before the
  next item is distributed. Use buffering/windowing to prevent slow subs from holding up the pub.

  Items received when there are no matching subs get dropped.

  Note that if buf-fns are used then each topic is handled asynchronously, i.e. if a channel is subscribed to
  more than one topic it should not expect them to be interleaved identically with the source."
  ([ch topic-fn] (pub ch topic-fn (constantly nil)))
  ([ch topic-fn buf-fn]
   (let [mults (atom {}) ;;topic->mult
         ensure-mult (fn [topic]
                       (or (get @mults topic)
                           (get (swap! mults
                                       #(if (% topic) % (assoc % topic (mult (chan (buf-fn topic))))))
                                topic)))
         p (reify
             Mux
             (muxch* [_] ch)

             Pub
             (sub* [_p topic ch close?]
               (let [m (ensure-mult topic)]
                 (tap m ch close?)))
             (unsub* [_p topic ch]
               (when-let [m (get @mults topic)]
                 (untap m ch)))
             (unsub-all* [_] (reset! mults {}))
             (unsub-all* [_ topic] (swap! mults dissoc topic)))]
     (go-loop []
       (let [val (<! ch)]
         (if (nil? val)
           (doseq [m (vals @mults)]
             (close! (muxch* m)))
           (let [topic (topic-fn val)
                 m (get @mults topic)]
             (when m
               (when-not (>! (muxch* m) val)
                 (swap! mults dissoc topic)))
             (recur)))))
     p)))

(defn sub
  "Subscribes a channel to a topic of a pub.

  By default the channel will be closed when the source closes, but can be determined by the close? parameter."
  ([p topic ch] (sub p topic ch true))
  ([p topic ch close?] (sub* p topic ch close?)))

(defn unsub
  "Unsubscribes a channel from a topic of a pub"
  [p topic ch]
  (unsub* p topic ch))

(defn unsub-all
  "Unsubscribes all channels from a pub, or a topic of a pub"
  ([p] (unsub-all* p))
  ([p topic] (unsub-all* p topic)))

;;; these are down here because they alias core fns, don't want accidents above

(defn map
  "Takes a function and a collection of source channels, and returns a channel which contains the values
  produced by applying f to the set of first items taken from each source channel, followed by applying f to
  the set of second items from each channel, until any one of the channels is closed, at which point the
  output channel will be closed. The returned channel will be unbuffered by default, or a buf-or-n can be
  supplied"
  ([f chs] (map f chs nil))
  ([f chs buf-or-n]
   (let [chs (vec chs)
         out (chan buf-or-n)
         cnt (count chs)
         rets (object-array cnt)
         dchan (chan 1)
         dctr (atom nil)
         done (mapv (fn [i]
                      (fn [ret]
                        (aset rets i ret)
                        (when (zero? (swap! dctr dec))
                          (put! dchan (vec rets)))))
                    (range cnt))]
     (if (zero? cnt)
       (close! out)
       (go-loop []
         (reset! dctr cnt)
         (dotimes [i cnt]
           (try
             (take! (chs i) (done i))
             (catch :default _
               (swap! dctr dec))))
         (let [rets (<! dchan)]
           (if (some nil? rets)
             (close! out)
             (do (>! out (apply f rets))
                 (recur))))))
     out)))

(defn merge
  "Takes a collection of source channels and returns a channel which contains all values taken from them. The
  returned channel will be unbuffered by default, or a buf-or-n can be supplied. The channel will close after
  all the source channels have closed."
  ([chs] (merge chs nil))
  ([chs buf-or-n]
   (let [out (chan buf-or-n)]
     (go-loop [cs (vec chs)]
       (if (pos? (count cs))
         (let [[v c] (alts! cs)]
           (if (nil? v)
             (recur (filterv #(not= c %) cs))
             (do (>! out v)
                 (recur cs))))
         (close! out)))
     out)))

(defn into
  "Returns a channel containing the single (collection) result of the items taken from the channel conjoined
  to the supplied collection. ch must close before into produces a result."
  [coll ch]
  (reduce conj coll ch))

(defn take
  "Returns a channel that will return, at most, n items from ch. After n items have been returned, or ch has
  been closed, the return channel will close.

  The output channel is unbuffered by default, unless buf-or-n is given."
  ([n ch]
   (take n ch nil))
  ([n ch buf-or-n]
   (let [out (chan buf-or-n)]
     (go (loop [x 0]
           (when (< x n)
             (let [v (<! ch)]
               (when (not (nil? v))
                 (>! out v)
                 (recur (inc x))))))
         (close! out))
     out)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; deprecated - do not use ;;;;;;;;;;;;;;;;;;;;;;;;;

(defn map<
  "Deprecated - this function will be removed. Use transducer instead"
  {:deprecated "0.1.319.0-6b1aca-alpha", :skip-wiki true}
  [f ch]
  (let [out (chan)]
    (go-loop []
      (let [v (<! ch)]
        (if (nil? v)
          (close! out)
          (when (>! out (f v))
            (recur)))))
    out))

;; The JVM wraps the channel in a write port; here the front channel is piped through f into ch.
(defn map>
  "Deprecated - this function will be removed. Use transducer instead"
  {:deprecated "0.1.319.0-6b1aca-alpha", :skip-wiki true}
  [f ch]
  (let [in (chan)]
    (go-loop []
      (let [v (<! in)]
        (if (nil? v)
          (close! ch)
          (when (>! ch (f v))
            (recur)))))
    in))

(defn filter>
  "Deprecated - this function will be removed. Use transducer instead"
  {:deprecated "0.1.319.0-6b1aca-alpha", :skip-wiki true}
  [p ch]
  (let [in (chan)]
    (go-loop []
      (let [v (<! in)]
        (if (nil? v)
          (close! ch)
          (if (p v)
            (when (>! ch v)
              (recur))
            (when-not (chan-closed?* ch)
              (recur))))))
    in))

(defn remove>
  "Deprecated - this function will be removed. Use transducer instead"
  {:deprecated "0.1.319.0-6b1aca-alpha", :skip-wiki true}
  [p ch]
  (filter> (complement p) ch))

(defn filter<
  "Deprecated - this function will be removed. Use transducer instead"
  {:deprecated "0.1.319.0-6b1aca-alpha", :skip-wiki true}
  ([p ch] (filter< p ch nil))
  ([p ch buf-or-n]
   (let [out (chan buf-or-n)]
     (go-loop []
       (let [val (<! ch)]
         (if (nil? val)
           (close! out)
           (do (when (p val)
                 (>! out val))
               (recur)))))
     out)))

(defn remove<
  "Deprecated - this function will be removed. Use transducer instead"
  {:deprecated "0.1.319.0-6b1aca-alpha", :skip-wiki true}
  ([p ch] (remove< p ch nil))
  ([p ch buf-or-n] (filter< (complement p) ch buf-or-n)))

(defn- mapcat* [f in out]
  (go-loop []
    (let [val (<! in)]
      (if (nil? val)
        (close! out)
        (do (doseq [v (f val)]
              (>! out v))
            (when-not (chan-closed?* out)
              (recur)))))))

(defn mapcat<
  "Deprecated - this function will be removed. Use transducer instead"
  {:deprecated "0.1.319.0-6b1aca-alpha", :skip-wiki true}
  ([f in] (mapcat< f in nil))
  ([f in buf-or-n]
   (let [out (chan buf-or-n)]
     (mapcat* f in out)
     out)))

(defn mapcat>
  "Deprecated - this function will be removed. Use transducer instead"
  {:deprecated "0.1.319.0-6b1aca-alpha", :skip-wiki true}
  ([f out] (mapcat> f out nil))
  ([f out buf-or-n]
   (let [in (chan buf-or-n)]
     (mapcat* f in out)
     in)))

(defn unique
  "Deprecated - this function will be removed. Use transducer instead"
  {:deprecated "0.1.319.0-6b1aca-alpha", :skip-wiki true}
  ([ch]
   (unique ch nil))
  ([ch buf-or-n]
   (let [out (chan buf-or-n)]
     (go (loop [last nil]
           (let [v (<! ch)]
             (when (not (nil? v))
               (if (= v last)
                 (recur last)
                 (do (>! out v)
                     (recur v))))))
         (close! out))
     out)))

(defn partition
  "Deprecated - this function will be removed. Use transducer instead"
  {:deprecated "0.1.319.0-6b1aca-alpha", :skip-wiki true}
  ([n ch]
   (partition n ch nil))
  ([n ch buf-or-n]
   (let [out (chan buf-or-n)]
     (go (loop [acc []]
           (let [v (<! ch)]
             (if (not (nil? v))
               (let [acc (conj acc v)]
                 (if (< (count acc) n)
                   (recur acc)
                   (do (>! out acc)
                       (recur []))))
               (do (when (pos? (count acc))
                     (>! out acc))
                   (close! out))))))
     out)))

(defn partition-by
  "Deprecated - this function will be removed. Use transducer instead"
  {:deprecated "0.1.319.0-6b1aca-alpha", :skip-wiki true}
  ([f ch]
   (partition-by f ch nil))
  ([f ch buf-or-n]
   (let [out (chan buf-or-n)]
     (go (loop [acc []
                last ::nothing]
           (let [v (<! ch)]
             (if (not (nil? v))
               (let [new-itm (f v)]
                 (if (or (= new-itm last)
                         (identical? last ::nothing))
                   (recur (conj acc v) new-itm)
                   (do (>! out acc)
                       (recur [v] new-itm))))
               (do (when (pos? (count acc))
                     (>! out acc))
                   (close! out))))))
     out)))
