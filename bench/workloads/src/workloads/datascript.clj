;; @ai-generated(solo)
;; Datascript 1.8.1 (corpus/datascript): an in-memory database built, changed and queried the way an app does.
;; Input: n = 400 users with a unique email, a city, 1-3 tags, a manager among the earlier users and up to 3
;; friends; n/4 products; 3n orders of 1-3 component items, naming user and product by lookup ref; every choice an
;; util/lcg state. Work: the database in batches of 50 entity maps, then upserts by email, tag retractions, status
;; changes and retractEntity of every 9th order; 11 queries (3-7 clause joins, predicates, fn bindings, not,
;; or-join, aggregates, a collection input, a recursive rule), nested and reverse pulls, entity navigation and index
;; access. Output: a vector of counts, sums and short sorted samples, `expected` below.
;; datascript.core's own namespace also loads conn, storage and serialize, JVM-only and off this path: the workload
;; calls the namespaces core delegates to, and `with` is datascript.conn/with without its FilteredDB check.
(ns workloads.datascript
  (:require [datascript.db :as db]
            [datascript.impl.entity :as de]
            [datascript.pull-api :as dp]
            [datascript.query :as dq]
            [workloads.util :refer [lcg]]))

(def schema
  {:user/email     {:db/unique :db.unique/identity}
   :user/city      {:db/index true}
   :user/tags      {:db/cardinality :db.cardinality/many}
   :user/manager   {:db/valueType :db.type/ref}
   :user/friends   {:db/valueType :db.type/ref :db/cardinality :db.cardinality/many}
   :product/sku    {:db/unique :db.unique/identity}
   :product/price  {:db/index true}
   :order/id       {:db/unique :db.unique/identity}
   :order/user     {:db/valueType :db.type/ref}
   :order/items    {:db/valueType :db.type/ref :db/cardinality :db.cardinality/many :db/isComponent true}
   :item/product   {:db/valueType :db.type/ref}})

(def cities ["Lisbon" "Porto" "Faro" "Braga" "Coimbra" "Evora" "Aveiro" "Leiria"])
(def tags [:new :vip :beta :mobile :web :b2b :trial :churn :promo :ref])
(def categories [:books :games :tools :food :music :garden])
(def statuses [:open :paid :shipped])

(defn- with [db tx-data]
  (db/transact-tx-data (db/->TxReport db db [] {} nil) tx-data))

(defn- db-with [db tx-data]
  (:db-after (with db tx-data)))

(defn- email [i] (str "user" i "@example.com"))
(defn- sku [i] (str "SKU-" i))

(defn- states
  "n successive generator states after seed, as a vector."
  [seed n]
  (loop [i 0 x seed acc []]
    (if (< i n)
      (let [x (lcg x)] (recur (inc i) x (conj acc x)))
      acc)))

(defn- user-tx [i x]
  (let [ntags (inc (mod x 3))]
    (cond-> {:db/id        (str "u" i)
             :user/email   (email i)
             :user/name    (str "User " i)
             :user/age     (+ 18 (mod (quot x 7) 50))
             :user/city    (nth cities (mod (quot x 11) (count cities)))
             :user/tags    (vec (distinct (map #(nth tags (mod (+ (quot x 13) (* % 3)) (count tags))) (range ntags))))}
      ;; the first ten are the roots of the management tree
      (>= i 10) (assoc :user/manager [:user/email (email (mod (quot x 17) i))])
      (>= i 3)  (assoc :user/friends (vec (distinct (map #(vector :user/email (email (mod (quot x %) i))) [19 23 29])))))))

(defn- product-tx [i x]
  {:db/id            (str "p" i)
   :product/sku      (sku i)
   :product/category (nth categories (mod x (count categories)))
   :product/price    (+ 100 (mod (quot x 3) 4900))})

(defn- order-tx [i x nusers nproducts]
  {:order/id     i
   :order/user   [:user/email (email (mod (quot x 3) nusers))]
   :order/status (nth statuses (mod (quot x 5) (count statuses)))
   :order/items  (mapv (fn [k] {:item/product [:product/sku (sku (mod (quot x (+ 7 k)) nproducts))]
                                :item/qty     (inc (mod (quot x (+ 11 k)) 5))})
                       (range (inc (mod x 3))))})

(defn- build [n]
  (let [nproducts (quot n 4)
        norders (* 3 n)
        db0 (db/empty-db schema {})
        users (map user-tx (range n) (states 11 n))
        products (map product-tx (range nproducts) (states 13 nproducts))
        orders (map #(order-tx %1 %2 n nproducts) (range norders) (states 17 norders))]
    ;; products before orders, which name them by lookup ref
    (reduce db-with db0 (partition-all 50 (concat users products orders)))))

(defn- change [db n]
  (let [xs (states 19 n)
        upserts (map (fn [i x] {:user/email (email i)
                                :user/age   (+ 20 (mod x 40))
                                :user/city  (nth cities (mod (quot x 3) (count cities)))
                                :user/tags  :promo})
                     (range 0 n 4) xs)
        untag (for [i (range 1 n 5)
                    :let [e (db/entid db [:user/email (email i)])]
                    t (:user/tags (de/entity db e))
                    :when (#{:new :trial} t)]
                [:db/retract e :user/tags t])
        statuses (for [i (range 0 (* 3 n) 7)]
                   [:db/add [:order/id i] :order/status :shipped])
        gone (for [i (range 0 (* 3 n) 9)]
               [:db/retractEntity [:order/id i]])
        report (with db (concat upserts untag statuses))]
    [(count (:tx-data report))
     (db-with (:db-after report) gone)]))

(def rules
  '[[(reports-to ?e ?m) [?e :user/manager ?m]]
    [(reports-to ?e ?m) [?e :user/manager ?x] (reports-to ?x ?m)]])

(defn- queries [db]
  (let [line-totals (dq/q '[:find ?city (sum ?line)
                            :with ?o ?it
                            :where [?u :user/city ?city]
                                   [?o :order/user ?u]
                                   [?o :order/items ?it]
                                   [?it :item/qty ?q]
                                   [?it :item/product ?p]
                                   [?p :product/price ?price]
                                   [(* ?q ?price) ?line]]
                          db)
        older-paid (dq/q '[:find (count-distinct ?u) .
                           :where [?u :user/age ?a]
                                  [(> ?a 45)]
                                  [?o :order/user ?u]
                                  [?o :order/status :paid]]
                         db)
        no-orders (dq/q '[:find [?email ...]
                          :where [?u :user/email ?email]
                                 (not [_ :order/user ?u])]
                        db)
        by-tags (dq/q '[:find ?t (count ?u)
                        :in $ [?t ...]
                        :where [?u :user/tags ?t]]
                      db [:vip :b2b :promo])
        reports (dq/q '[:find ?m (count ?e)
                        :in $ %
                        :where [?m :user/email]
                               [(< ?m 6)]
                               (reports-to ?e ?m)]
                      db rules)
        category-stats (dq/q '[:find ?c (min ?price) (max ?price) (count ?p)
                               :where [?p :product/category ?c]
                                      [?p :product/price ?price]]
                             db)
        friends-same-city (dq/q '[:find (count ?u) .
                                  :where [?u :user/friends ?f]
                                         [?u :user/city ?c]
                                         [?f :user/city ?c]]
                                db)
        either (dq/q '[:find (count ?o) .
                       :where [?o :order/status ?s]
                              (or-join [?o ?s]
                                (and [?o :order/user ?u] [?u :user/tags :vip])
                                [(contains? #{:open} ?s)])]
                     db)
        books (dq/q '[:find ?sku
                      :where [?p :product/category :books]
                             [?p :product/sku ?sku]
                             [?p :product/price ?price]
                             [(>= ?price 2500)]]
                    db)
        busiest (dq/q '[:find ?email (count ?o)
                        :where [?o :order/user ?u]
                               [?u :user/email ?email]]
                      db)
        managers (dq/q '[:find ?me ?mm
                         :where [?e :user/manager ?m]
                                [?m :user/manager ?mm]
                                [?e :user/email ?me]
                                [(clojure.string/ends-with? ?me "7@example.com")]]
                       db)]
    [(vec (sort line-totals))
     older-paid
     [(count no-orders) (vec (take 3 (sort no-orders)))]
     (vec (sort by-tags))
     (vec (sort reports))
     (vec (sort category-stats))
     friends-same-city
     either
     (count books)
     (vec (take 3 (sort-by (fn [[e c]] [(- c) e]) busiest)))
     (count managers)]))

(def order-pattern
  '[:order/id :order/status
    {:order/user [:user/email {:user/manager [:user/email]}]}
    {:order/items [:item/qty {:item/product [:product/sku :product/price]}]}])

(defn- pulls [db n]
  (let [orders (dp/pull-many db order-pattern (map #(vector :order/id %) (range 1 (* 3 n) 10)))
        revenue (reduce + (for [o orders
                                it (:order/items o)]
                            (* (:item/qty it) (:product/price (:item/product it)))))
        managed (count (filter #(get-in % [:order/user :user/manager]) orders))
        users (dp/pull-many db '[:user/email {:order/_user [:order/id]} {:user/_manager [:user/email]}]
                            (map #(vector :user/email (email %)) (range 0 n 13)))]
    [(count orders) revenue managed
     (reduce + (map #(count (:order/_user %)) users))
     (reduce + (map #(count (:user/_manager %)) users))]))

(defn- depth [e]
  (loop [e e d 0]
    (if-some [m (:user/manager e)] (recur m (inc d)) d)))

(defn- entities [db n]
  (let [es (map #(de/entity db [:user/email (email %)]) (range 0 n 3))
        chain (reduce + (map depth es))
        spend (reduce + (for [e es
                              o (:order/_user e)
                              it (:order/items o)]
                          (:item/qty it)))
        touched (reduce + (map #(count (de/touch %)) (take 50 es)))
        friends (reduce + (map #(count (:user/friends %)) es))]
    [chain spend touched friends]))

(defn- indexes [db]
  (let [lisbon (db/-datoms db :avet :user/city "Lisbon" nil nil)
        cheap (db/-index-range db :product/price 100 1000)
        user0 (db/-datoms db :eavt (db/entid db [:user/email (email 0)]) nil nil nil)]
    [(count lisbon) (count cheap) (count user0)
     (vec (take 3 (map :v (db/-datoms db :aevt :product/sku nil nil nil))))]))

(defn run* [n]
  (let [db (build n)
        [changed db] (change db n)]
    [(count (:eavt db)) (:max-eid db) changed
     (queries db) (pulls db n) (entities db n) (indexes db)]))

(defn run [] (run* 400))

;; What run returns on JVM Clojure 1.12.6 with datascript 1.8.1 and persistent-sorted-set 0.3.2.
(def expected
  '[13950 4113 711
    [[["Aveiro" 2232845] ["Braga" 2310369] ["Coimbra" 2007543] ["Evora" 1548590] ["Faro" 2131219] ["Leiria" 2503392]
      ["Lisbon" 1424415] ["Porto" 2233734]]
     69
     [26 ["user129@example.com" "user135@example.com" "user144@example.com"]]
     [[:b2b 72] [:promo 155] [:vip 72]]
     [[1 64] [2 2] [3 39] [4 24] [5 66]]
     [[:books 620 4884 17] [:food 197 4991 19] [:games 102 4848 11] [:garden 147 4501 20] [:music 249 4509 20]
      [:tools 144 4862 13]]
     142 440 11
     [["user118@example.com" 9] ["user39@example.com" 9] ["user134@example.com" 7]]
     36]
    [120 1508422 107 100 23]
    [493 1959 345 395]
    [41 24 8 ["SKU-0" "SKU-1" "SKU-2"]]])
