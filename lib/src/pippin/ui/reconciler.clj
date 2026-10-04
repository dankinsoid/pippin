;; @ai-generated(solo)
;; design §5b: the language knows nothing about UI, so this is a plain library on the load path.
(ns pippin.ui.reconciler)

;; Three functions per tag, plus the two structural calls no single tag owns (design §5b, «Размещение»).
(defprotocol Backend
  (create! [b tag attrs] "The host node for tag with attrs applied; the handle is opaque to the reconciler.")
  (update! [b tag node prev attrs] "prev and attrs both: the backend diffs the properties it knows.")
  (remove! [b tag node] "Destroys node; its children arrive in their own calls, bottom-up.")
  (insert! [b parent node index] "Places node as parent's child at index, shifting the later ones right.")
  (detach! [b parent node] "Unlinks node from parent without destroying it; a move is detach! then insert!."))

(defn- fail [msg data] (throw (ex-info msg data)))

(defn- hiccup? [h] (and (vector? h) (pos? (count h)) (keyword? (nth h 0))))

(defn- check-hiccup [h]
  (when-not (hiccup? h)
    (fail "the reconciler takes a hiccup vector [tag attrs? & children]" {:value h}))
  h)

(defn- attrs-of [h]
  (let [a (if (> (count h) 1) (nth h 1) nil)]
    (if (map? a) a nil)))

(defn- append-children [acc xs]
  (reduce (fn [acc c]
            (cond
              (nil? c) acc
              (vector? c) (conj acc (check-hiccup c))
              (seq? c) (append-children acc c)
              :else (fail "a hiccup child must be a vector, a seq of them, or nil" {:child c})))
          acc xs))

;; Splicing a seq reallocates the list but keeps each element's pointer, so the per-child identical? holds.
(defn- children-of [h]
  (let [from (if (map? (if (> (count h) 1) (nth h 1) nil)) 2 1)]
    (if (<= (count h) from)
      []
      (append-children [] (subvec h from)))))

(def ^:private native :native)

;; §5b's escape hatch: an opaque host node, identified by its thunk, with no attrs and no children.
(defn- native-thunk [h]
  (when-not (= (count h) 2)
    (fail "[:native thunk] takes the thunk and nothing else" {:node h}))
  (let [t (nth h 1)]
    (when-not (ifn? t) (fail "[:native thunk] takes a thunk returning a host node" {:thunk t}))
    t))

(defn- child-key [h]
  (if (= native (nth h 0)) nil (:key (attrs-of h))))

;; All or nothing: a mixed list is a mistake the reconciler cannot paper over (§5b, «Идентичность строки»).
(defn- child-keys [news]
  (let [keys (mapv child-key news)]
    (cond
      (every? nil? keys) nil
      (not (every? some? keys))
      (fail "either every child of a node carries :key or none does"
            {:keys keys :tags (mapv (fn [h] (nth h 0)) news)})
      (not= (count keys) (count (set keys)))
      (fail "the children of a node carry duplicate :key" {:keys keys})
      :else keys)))

(declare ^:private patch-children)

(defn- mount-node [b h]
  (check-hiccup h)
  (if (= native (nth h 0))
    (let [t (native-thunk h)]
      {:tag native :key nil :attrs nil :hiccup h :thunk t :node (t) :children []})
    (let [tag (nth h 0)
          attrs (attrs-of h)
          kids (children-of h)
          _ (child-keys kids)
          node (create! b tag attrs)
          children (mapv (fn [c] (mount-node b c)) kids)]
      (loop [i 0]
        (when (< i (count children))
          (insert! b node (:node (nth children i)) i)
          (recur (inc i))))
      {:tag tag :key (:key attrs) :attrs attrs :hiccup h :node node :children children})))

;; Bottom-up, so a backend may free a node the moment it is told to.
(defn- destroy-subtree! [b m]
  (doseq [c (:children m)] (destroy-subtree! b c))
  (remove! b (:tag m) (:node m)))

;; Only the top of a dropped subtree is unlinked; what is under it leaves with its parent.
(defn- drop-child! [b parent m]
  (detach! b parent (:node m))
  (destroy-subtree! b m))

(defn- replace-child! [b parent index old h]
  (drop-child! b parent old)
  (let [m (mount-node b h)]
    (insert! b parent (:node m) index)
    m))

(defn- patch-node [b parent index old h]
  (cond
    ;; Persistence makes this sound: a changed subtree cannot be the pointer the last render left (§5b).
    (identical? (:hiccup old) h) old

    (not (and (hiccup? h) (= (:tag old) (nth h 0)))) (replace-child! b parent index old h)

    (= native (:tag old))
    (if (= (:thunk old) (native-thunk h))
      (assoc old :hiccup h)
      (replace-child! b parent index old h))

    :else
    (let [attrs (attrs-of h)
          prev (:attrs old)]
      ;; The props diff only when the pointer differs: that comparison is React's whole cost.
      (when-not (or (identical? prev attrs) (= prev attrs))
        (update! b (:tag old) (:node old) prev attrs))
      (assoc old
             :attrs attrs
             :key (:key attrs)
             :hiccup h
             :children (patch-children b (:node old) (:children old) (children-of h))))))

(defn- vec-remove [v i]
  (into (subvec v 0 i) (subvec v (inc i))))

(defn- vec-insert [v i x]
  (into (conj (subvec v 0 i) x) (subvec v i)))

;; Never before `from`: the positions before it already hold their final child.
(defn- pos-of [v x from]
  (loop [i from]
    (cond
      (>= i (count v)) nil
      (identical? (nth v i) x) i
      :else (recur (inc i)))))

(defn- patch-positional [b parent olds news]
  (let [no (count olds)
        nn (count news)]
    ;; The tail goes first, so the indices of what stays do not move under the loop.
    (loop [i (dec no)]
      (when (>= i nn)
        (drop-child! b parent (nth olds i))
        (recur (dec i))))
    (loop [i 0 acc []]
      (if (= i nn)
        acc
        (recur (inc i)
               (conj acc (if (< i no)
                           (patch-node b parent i (nth olds i) (nth news i))
                           (let [m (mount-node b (nth news i))]
                             (insert! b parent (:node m) i)
                             m))))))))

(defn- patch-keyed [b parent olds news keys]
  (let [by-key (reduce (fn [m c] (assoc m (:key c) c)) {} olds)
        live (set keys)
        kept (reduce (fn [acc c]
                       (if (and (contains? live (:key c)) (identical? c (get by-key (:key c))))
                         (conj acc c)
                         (do (drop-child! b parent c) acc)))
                     [] olds)]
    (loop [i 0 cur kept acc []]
      (if (= i (count news))
        acc
        (let [h (nth news i)
              old (get by-key (nth keys i))]
          (if (or (nil? old) (nil? (pos-of cur old i)))
            (let [m (mount-node b h)]
              (insert! b parent (:node m) i)
              (recur (inc i) (vec-insert cur i m) (conj acc m)))
            ;; A row matched by key costs one unlink and one placement, and no attribute diff at all.
            (let [p (pos-of cur old i)
                  cur (if (= p i)
                        cur
                        (do (detach! b parent (:node old))
                            (insert! b parent (:node old) i)
                            (vec-insert (vec-remove cur p) i old)))
                  m (patch-node b parent i old h)]
              (recur (inc i) (assoc cur i m) (conj acc m)))))))))

(defn- patch-children [b parent olds news]
  (let [keys (child-keys news)]
    (if (nil? keys)
      (patch-positional b parent olds news)
      (patch-keyed b parent olds news keys))))

(defn patch
  "Diffs `old` (what mount/patch returned, or nil) against the hiccup `h` (or nil to unmount), emitting the
   backend calls that bring `container`'s subtree to `h`. Returns the new mount tree. `container` is an
   opaque backend handle the host owns."
  [b container old h]
  (cond
    (nil? old) (when (some? h)
                 (let [m (mount-node b h)]
                   (insert! b container (:node m) 0)
                   m))
    (nil? h) (do (drop-child! b container old) nil)
    :else (patch-node b container 0 old h)))

(defn- patch-in* [b parent index m path h]
  (if (zero? (count path))
    (patch-node b parent index m h)
    (let [i (nth path 0)
          kids (:children m)]
      (assoc m :children (assoc kids i (patch-in* b (:node m) i (nth kids i) (subvec path 1) h))))))

(defn patch-in
  "Re-renders the node at `path` (child indices from the root) against `h` and returns the whole mount tree.
   What the frame costs is that subtree, not the root's — this is how a component re-renders alone. The
   enclosing tree must not re-describe that node: it is a component boundary (design §5b)."
  [b container m path h]
  (patch-in* b container 0 m path h))

(defn mount
  "Builds h under container and returns the mount tree."
  [b container h]
  (patch b container nil h))

(defn unmount
  "Destroys the mount tree under container."
  [b container m]
  (patch b container m nil))

(defn host-node
  "The backend handle of a mount tree's root: what a host places, measures or hands to another API."
  [m]
  (:node m))
