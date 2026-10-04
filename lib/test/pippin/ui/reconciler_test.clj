;; @ai-generated(solo)
(ns pippin.ui.reconciler-test
  (:require [clojure.test :refer [deftest is testing]]
            [pippin.ui.mock :as mock]
            [pippin.ui.reconciler :as r]))

(defn- fresh []
  (let [mk (mock/recording)]
    [mk (:backend mk) (:root mk)]))

;; An update of the leaf label is the diff having walked all `depth` levels down.
(defn- nested [depth stamp]
  (if (zero? depth)
    [:label {:text (str "leaf " stamp)}]
    [:stack {:depth depth} (nested (dec depth) stamp)]))

(defn- rows [n]
  (mapv (fn [i] [:label {:key i :text (str "row " i)}]) (range n)))

(defn- screen [rs]
  (into [:stack {:axis :vertical}] rs))

(deftest a-mount-creates-every-node-and-places-it
  (let [[mk b root] (fresh)
        m (r/mount b root [:stack {:axis :vertical} [:label {:text "hi"}]])]
    (is (= [[:create [:stack 1] {:axis :vertical}]
            [:create [:label 2] {:text "hi"}]
            [:insert [:stack 1] [:label 2] 0]
            [:insert [:root 0] [:stack 1] 0]]
           (mock/calls mk)))
    (is (= [:stack 1] (r/host-node m)))))

(deftest an-unchanged-subtree-costs-no-backend-call
  (let [[mk b root] (fresh)
        kept (nested 8 "a")
        m (r/mount b root [:stack {:title "before"} kept])
        _ (mock/taken mk)
        m' (r/patch b root m [:stack {:title "after"} kept])]
    (testing "the parent's own attributes, and not one call about the subtree"
      (is (= [[:update [:stack 1] {:title "after"}]] (mock/calls mk))))
    (testing "the mount node comes back as it was, so nothing below it was even traversed"
      (is (identical? (first (:children m)) (first (:children m')))))))

(deftest an-equal-subtree-with-a-new-pointer-is-walked
  (let [[mk b root] (fresh)
        m (r/mount b root [:stack {:title "before"} (nested 8 "a")])
        _ (mock/taken mk)
        m' (r/patch b root m [:stack {:title "after"} (nested 8 "a")])]
    (testing "a props diff finds nothing either, so the empty log is not what tells the two paths apart"
      (is (= [[:update [:stack 1] {:title "after"}]] (mock/calls mk))))
    (testing "the subtree was rebuilt level by level: this is the walk the pointer check skips"
      (is (not (identical? (first (:children m)) (first (:children m'))))))))

(deftest a-walk-that-reaches-the-bottom-says-so
  (let [[mk b root] (fresh)
        m (r/mount b root (nested 8 "a"))
        _ (mock/taken mk)]
    (r/patch b root m (nested 8 "b"))
    (is (= [[:update [:label 9] {:text "leaf b"}]] (mock/calls mk)))))

(deftest a-list-screen-updates-only-the-changed-row
  (let [[mk b root] (fresh)
        rs (rows 50)
        m (r/mount b root (screen rs))
        _ (mock/taken mk)
        rs' (assoc rs 23 [:label {:key 23 :text "edited"}])]
    (r/patch b root m (screen rs'))
    (is (= [[:update [:label 25] {:key 23 :text "edited"}]] (mock/calls mk)))))

(deftest a-keyed-insert-at-the-head-touches-no-other-row
  (let [[mk b root] (fresh)
        rs (rows 3)
        m (r/mount b root (screen rs))
        _ (mock/taken mk)]
    (r/patch b root m (screen (into [[:label {:key :new :text "new"}]] rs)))
    (is (= [[:create [:label 5] {:key :new :text "new"}]
            [:insert [:stack 1] [:label 5] 0]]
           (mock/calls mk)))))

(deftest a-keyed-removal-destroys-only-that-row
  (let [[mk b root] (fresh)
        rs (rows 3)
        m (r/mount b root (screen rs))
        _ (mock/taken mk)
        m' (r/patch b root m (screen [(nth rs 0) (nth rs 2)]))]
    (is (= [[:detach [:stack 1] [:label 3]]
            [:remove [:label 3]]]
           (mock/calls mk)))
    (is (= [[:label 2] [:label 4]] (mapv r/host-node (:children m'))))))

(deftest a-keyed-reorder-moves-and-updates-nothing
  (let [[mk b root] (fresh)
        rs (rows 3)
        m (r/mount b root (screen rs))
        _ (mock/taken mk)
        m' (r/patch b root m (screen [(nth rs 2) (nth rs 0) (nth rs 1)]))]
    (testing "one unlink and one placement for the row that moved; the rows it passed stay put"
      (is (= [[:detach [:stack 1] [:label 4]]
              [:insert [:stack 1] [:label 4] 0]]
             (mock/calls mk))))
    (is (= [[:label 4] [:label 2] [:label 3]] (mapv r/host-node (:children m'))))))

(deftest positional-children-are-matched-by-index
  (let [[mk b root] (fresh)
        m (r/mount b root [:stack {} [:label {:text "a"}] [:label {:text "b"}]])
        _ (mock/taken mk)
        m' (r/patch b root m [:stack {} [:label {:text "b"}] [:label {:text "a"}]])]
    (testing "without keys a swap is two attribute writes, not a move"
      (is (= [[:update [:label 2] {:text "b"}]
              [:update [:label 3] {:text "a"}]]
             (mock/calls mk))))
    (mock/taken mk)
    (r/patch b root m' [:stack {} [:label {:text "b"}]])
    (is (= [[:detach [:stack 1] [:label 3]]
            [:remove [:label 3]]]
           (mock/calls mk)))))

(deftest a-changed-tag-replaces-the-node-at-its-index
  (let [[mk b root] (fresh)
        m (r/mount b root [:stack {} [:label {:text "a"}] [:label {:text "b"}]])
        _ (mock/taken mk)]
    (r/patch b root m [:stack {} [:uikit/blur {:style :dark}] [:label {:text "b"}]])
    (is (= [[:detach [:stack 1] [:label 2]]
            [:remove [:label 2]]
            [:create [:uikit/blur 4] {:style :dark}]
            [:insert [:stack 1] [:uikit/blur 4] 0]]
           (mock/calls mk)))))

(deftest a-native-node-is-placed-and-never-inspected
  (let [[mk b root] (fresh)
        view (fn [] :some-uiview)
        m (r/mount b root [:stack {} [:native view]])
        _ (mock/taken mk)]
    (testing "the thunk makes the handle, so the backend is asked to place it and nothing more"
      (is (= [:some-uiview] (mapv r/host-node (:children m)))))
    (testing "the same thunk is the same node"
      (r/patch b root m [:stack {} [:native view]])
      (is (= [] (mock/calls mk))))
    (testing "another thunk is another node: a native node is replaced, never updated"
      (r/patch b root m [:stack {} [:native (fn [] :other-uiview)]])
      (is (= [[:detach [:stack 1] :some-uiview]
              [:remove :some-uiview]
              [:insert [:stack 1] :other-uiview 0]]
             (mock/calls mk))))))

(deftest a-seq-of-children-is-spliced
  (let [[mk b root] (fresh)
        m (r/mount b root [:stack {} (map (fn [i] [:label {:key i :text (str i)}]) (range 2)) nil])]
    (is (= [[:create [:stack 1] {}]
            [:create [:label 2] {:key 0 :text "0"}]
            [:create [:label 3] {:key 1 :text "1"}]
            [:insert [:stack 1] [:label 2] 0]
            [:insert [:stack 1] [:label 3] 1]
            [:insert [:root 0] [:stack 1] 0]]
           (mock/calls mk)))
    (is (= 2 (count (:children m))))))

(deftest unmounting-destroys-bottom-up-and-leaves-nothing-live
  (let [[mk b root] (fresh)
        m (r/mount b root [:stack {} [:stack {} [:label {:text "a"}]]])
        _ (mock/taken mk)]
    (is (nil? (r/unmount b root m)))
    (is (= [[:detach [:root 0] [:stack 1]]
            [:remove [:label 3]]
            [:remove [:stack 2]]
            [:remove [:stack 1]]]
           (mock/calls mk)))
    (is (= #{} (mock/live mk)))))

(deftest a-component-re-renders-alone
  (let [[mk b root] (fresh)
        rs (rows 3)
        m (r/mount b root (screen rs))
        _ (mock/taken mk)
        m' (r/patch-in b root m [1] [:label {:key 1 :text "its own render"}])]
    (testing "the root tree is never consulted, so the siblings cost nothing"
      (is (= [[:update [:label 3] {:key 1 :text "its own render"}]] (mock/calls mk))))
    (is (identical? (nth (:children m) 0) (nth (:children m') 0)))
    (is (identical? (nth (:children m) 2) (nth (:children m') 2)))
    (testing "a replacement at a path lands at that child's index"
      (mock/taken mk)
      (r/patch-in b root m' [2] [:uikit/blur {:key 2}])
      (is (= [[:detach [:stack 1] [:label 4]]
              [:remove [:label 4]]
              [:create [:uikit/blur 5] {:key 2}]
              [:insert [:stack 1] [:uikit/blur 5] 2]]
             (mock/calls mk))))))

(deftest an-identical-root-is-the-whole-frame-skipped
  (let [[mk b root] (fresh)
        tree [:stack {} [:label {:text "a"}]]
        m (r/mount b root tree)
        _ (mock/taken mk)
        m' (r/patch b root m tree)]
    (is (= [] (mock/calls mk)))
    (is (identical? m m'))))

(deftest attributes-are-compared-by-pointer-first
  (let [[mk b root] (fresh)
        attrs {:text "a"}
        m (r/mount b root [:label attrs])
        _ (mock/taken mk)]
    (r/patch b root m [:label attrs])
    (is (= [] (mock/calls mk)))
    (r/patch b root m [:label {:text "a"}])
    (is (= [] (mock/calls mk)))
    (r/patch b root m [:label (assoc attrs :text "b")])
    (is (= [[:update [:label 1] {:text "b"}]] (mock/calls mk)))))

(deftest a-child-list-is-keyed-or-not-and-never-half
  (let [[_ b root] (fresh)]
    (is (thrown-with-msg? :default #"every child"
                          (r/mount b root [:stack {} [:label {:key 1}] [:label {}]])))
    (is (thrown-with-msg? :default #"duplicate :key"
                          (r/mount b root [:stack {} [:label {:key 1}] [:label {:key 1}]])))
    (is (thrown-with-msg? :default #"must be a vector"
                          (r/mount b root [:stack {} "text"])))
    (testing "a map is callable, so it would fail inside the thunk call instead of here"
      (is (thrown-with-msg? :default #"thunk returning a host node"
                            (r/mount b root [:native {}]))))))
