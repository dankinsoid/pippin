;; @ai-generated(guided)
;; Merry sketch (docs/merry.md), not runnable: `;; =>` contract, `;; ?` friction, `;; variant:` one doc option.


;;; ── Attributes: the domain vocabulary ──────────────────────────────────────

(ns shop.person)

;; variant: attributes are declared only explicitly, their namespace is this module; types select them.
(defattrs
  {:id    [:uuid {:tagged true}]          ; its own nominal type: an order id is not a person id
   :name  :string
   :email [:string {:pii true}]})         ; schema props belong to the attribute

(deftype Person {:eq-by [::id]}           ; equal by id; hash from the same field
  [::id ::name [::email {:optional true}]])   ; entry props belong to the selection

(def PublicProfile (s/select Person [::id ::name]))


(ns shop.catalog)

(deftype Sku [:re #"[A-Z]{3}-\d{4}"])     ; a refinement is nominal: checked once, in the constructor
(deftype Qty [:int {:min 1 :max 999}])

(deftype (Money cur) [[:cents :int]])     ; a value type: local fields; the currency is a type argument
(deftype Usd (Money :usd))

(extend (Money cur) IAdd
        (add [a b] {:cents (+ (:cents a) (:cents b))}))
;; (+ usd eur) does not check: IAdd is Self, & Self -> Self, there is no mixed arithmetic

(defn scale [m n] {:cents (* (:cents m) n)})
;; => [:=> [[:map [:cents :int] & r] :int] [:map [:cents :int] & r] {:eff #{}}]


(ns shop.order
  (:require [shop.person :as p]
            [shop.catalog :refer [Sku Qty Usd scale]]))

(deftype Line [[:sku Sku] [:qty Qty] [:price Usd]])   ; unqualified: local fields, not attributes

(defattrs
  {:id     [:uuid {:tagged true}]
   :lines  [:vector Line]
   :coupon :string
   :status [:enum :new :paid :cancelled]
   :total  Usd})

(deftype Cart  [::id ::lines [::coupon {:optional true}] ::p/id])
(deftype Order [::id ::lines ::status ::total ::p/id])

(deftype CheckoutError
         [:enum
          [:out-of-stock [:map [:sku Sku] [:left :int]]]
          [:card-declined :string]
          [:empty-cart]])


;;; ── Pure core: rows, modes, refinements ────────────────────────────────────

(defn line-total [{& [qty price]}]
  (scale price qty))
;; => [:=> [[:map [:qty Qty] [:price Usd] & r]] Usd {:eff #{}}]

(defn total [cart]
  (transduce (map line-total) + (zero Usd) (::lines cart)))
;; => [:=> [[:map ::lines & r]] Usd {:eff #{}}]     (zero Usd): the neutral element comes from a type, not context

(defn add-line [cart line]
  (update cart ::lines
          (fn [lines]
            (if-let [i (index-where #(= (:sku %) (:sku line)) lines)]
              (update-in lines [i :qty] #(Qty (+ % (:qty line))))
              (conj lines line)))))
;; => [:=> [[:map ::lines & r] Line] [:map ::lines & r]
;;     {:eff #{[Fail [:invalid Qty]]} :modes {cart :unique, line :borrowed}}]

(defn receipt [cart]
  (let [sb (string-builder)]
    (doseq [{& [sku qty price]} (::lines cart)]
      (sb-append! sb sku " x" qty " " (:cents price) "\n"))
    (str sb)))
;; => [:=> [[:map ::lines & r]] :string {:eff #{} :modes {cart :borrowed, sb :local}}]

(defn customer-of [::p/id] (p/find-name id))
;; the parameter takes its type from the attribute: neither inferred nor written


;;; ── Protocols ──────────────────────────────────────────────────────────────

;; std: (defprotocol IAdd (add [self other :- Self] :- Self) (add* [self & more :- Self] :- Self :default ...))

(defprotocol Describe
  (describe [x] {:eff #{}})               ; the effect bound every implementation must fit
  (title    [x] {:eff #{} :default (fn [x] (str (type-name x)))}))

(extend Cart Describe
        (describe [c] (str (title c) ": " (count (::lines c)) " lines"))
        (title    [c] (str "cart, " ((default title) c))))   ; the default stays callable: it is a value

(deftype CouponCode {:opaque true} [[:code :string]])
(defn coupon [s] (CouponCode/make {:code (str/upper-case s)}))
;; opaque: no map view, no `as`; outside this module a CouponCode exists only through `coupon`

(deftype AuditedCart (s/merge Cart [[:by ::p/id] [:at :instant]])
  {:delegate {Describe Cart}})
;; variant: explicit delegation list. IAdd is refused: two Self in, Self out cannot merge two audit trails.

;; =, hash, printing, codecs come from the schema; equality crosses types unless a type sets :eq-by.


;;; ── Multimethods: dispatch on a sum's tag ──────────────────────────────────

(deftype OrderEvent
         [:enum
          [:order/created Order]
          [:order/paid    [:map [:payment-id :string] ::total]]
          [:order/cancelled [:map [:reason :string]]]])

;; variant: two explicit forms. defmulti is static: its methods see the narrowed payload,
;; completeness is checked, it compiles to a switch on the tag.
(defmulti on-event {:eff #{Log}} [e :- OrderEvent] :- :unit)
(defmethod on-event :order/created   [[_ order]] (Log/info "created" (::id order)))
(defmethod on-event :order/paid      [[_ paid]]  (Log/info "paid" (::total paid)))
(defmethod on-event :order/cancelled [[_ c]]     (Log/warn "cancelled" (:reason c)))

;; defdispatch is dynamic: any dispatch function over Any, a missing method fails at runtime.
(defdispatch webhook (fn [d] (get d "type")))


;;; ── Patterns: destructuring, match, case ───────────────────────────────────

(defn shipping-label [{:address {:city city :zip zip} & [name :or {name "customer"}]}]
  (str name ", " zip " " city))
;; key → pattern, as in a data literal; `& [...]` names fields, the rest, :as and defaults

(defn paid-amount [e :- OrderEvent] :- [:maybe Usd]
  (match e
    (or [:order/created {::status :paid ::total t}]   ; a literal inside compares
        [:order/paid {::total t}])        t           ; both alternatives bind t of one type
    (or [:order/created _]
        [:order/cancelled _])             nil))
;; without the last branch: error, not covered: [:order/cancelled {:reason _}]
;; {& [::total]} would bind `total` and shadow the function above: a warning
;; [:order/paid p extra] is an error, not a fall to `_`: the variant has one field

(defn classify [line :- Line, limit :- Qty]
  (match line
    {:qty (= limit)}                              :at-limit   ; compare with a name in scope
    {:qty (> 100) :sku (re-matches #"VIP-\d{4}" $)} :bulk-vip ; the value goes first, `$` elsewhere
    {:qty q :price {:cents c}} :when (> (* q c) 100000) :expensive
    _                                             :regular))
;; {:qty limit} binds a fresh name; the shadowing warning suggests (= limit)

(defn merge-pair [a :- Line, b :- Line] :- [:maybe Line]
  (match [a b]
    [{:sku s :qty x} {:sku s :qty y}] (assoc a :qty (Qty (+ x y)))   ; a repeated name is equality
    _                                 nil))
;; => [:=> [Line Line] [:maybe Line] {:eff #{[Fail [:invalid Qty]]}}]

(defn clamp-qty [n :- :int] :- Qty
  (match n
    (Qty q) q                             ; the checking constructor: a failed check goes to the next branch
    (< 1)   (Qty 1)
    _       (Qty 999)))
;; => [:=> [:int] Qty {:eff #{}}]     the literals are checked in phase 1, so no Fail

(defn parse-line [s :- :string] :- Line
  (let [[sku qty cents :or (fail [:bad-line s])] (str/split s #",")]   ; let-else
    {:sku (Sku sku) :qty (Qty (parse-long qty)) :price {:cents (parse-long cents)}}))
;; without :or it does not compile: [a b c] does not cover [:vector :string], use match or let-else
;; ? :or {cents "0"} beside :or (fail ...): one key, two meanings

(def max-lines 50)

(defn cart-state [cart]
  (case (count (::lines cart))
    0         :empty
    max-lines :full                       ; a phase-1 constant: compared by value, not as a symbol
    _         :open))

(defn status-code [o]
  (case (::status o) :new 202 :paid 200 :cancelled 410))
;; a closed enum needs no `_`; a new status is an error here, and an extra `_` a warning

;; variant: defpattern expands to structure, so exhaustiveness sees through it
(defpattern (Priced c) {:price {:cents c}})   ; capitalized: its arguments are patterns, not expressions
(defn cheap? [line] (match line (Priced (< 100)) true _ false))


;;; ── Types as data, phase 1 ─────────────────────────────────────────────────

(deftype Refund (openapi/schema "payments.yaml" "#/components/schemas/Refund"))
;; a phase-1 function; the yaml is a declared input, so the result is cached by hash

(defn audited [t] (s/merge t [[:by ::p/id] [:at :instant]]))

(defn strip [x :- (audited a)] :- a
  (dissoc x :by :at))
;; audited is a row merge, so a is inferred from the argument; an opaque type function would need it written

(def CartPatch (s/optional-keys (s/dissoc Cart ::id)))


;;; ── Fitting data: as, open types, entries ──────────────────────────────────

(defn draft->cart [draft]
  (-> draft (dissoc :debug) (assoc ::coupon "WELCOME") (as Cart)))
;; as is strict: a leftover or missing field is a compile error that names it; the chain is one copy

(deftype ApiCart {:open true} [::id ::lines])   ; unknown keys from the wire are kept in ext

(defn redact [person]
  (into (empty person)
        (map (fn [[a v]]
               (case a
                 ::p/email [a "***"]      ; here v is a string: the tag fixes the value's type
                 [a v])))
        person))
;; ? a generic redact over all :pii attributes needs a case per attribute, generated from (attrs T) in phase 1.


;;; ── Effects: ports and adapters ────────────────────────────────────────────

(ns shop.checkout
  {:effects-bound #{Inventory Payments Async Fail Log}}   ; inside the bound, the interface does not change
  (:require [shop.order :as o :refer [Cart Line Order CheckoutError total]]
            [shop.catalog :refer [Usd]]
            [std.http :as http]
            [std.json :as json]))

;; std: (defeffect (Fail e) (fail [err :- e] :- :never)), Async with spawn/await; the scheduler is a handler.

(defeffect Inventory                      ; a port: the caller's environment decides the implementation
  (stock    [sku :- Sku] :- :int)
  (reserve! [lines :- [:vector Line]] :- :uuid)
  (release! [reservation :- :uuid] :- :unit))

(defeffect Payments
  (charge! [cart :- ::o/id, amount :- Usd] :- [:enum [:ok :string] [:declined :string]]))

(defn- check-stock [lines]
  (let [left (par-map #(Inventory/stock (:sku %)) lines)]
    (doseq [[line n] (zip lines left)
            :when (< n (:qty line))]
      (fail [:out-of-stock {:sku (:sku line) :left n}]))))
;; => [:=> [[:vector Line]] :unit {:eff #{Inventory Async [Fail [:out-of-stock ...]]}}]

(defn checkout
  "Reserves stock and charges the card; returns the reservation id."
  {:example '[(with [(fake-inventory {"ABC-0001" 5}) (fake-payments :ok)]
                    (checkout (cart-of ["ABC-0001" 2 1500])))
              :=> #uuid "00000000-0000-0000-0000-000000000001"]}
  [cart]
  (when (empty? (::o/lines cart))
    (fail [:empty-cart]))
  (check-stock (::o/lines cart))
  (let [reservation (Inventory/reserve! (::o/lines cart))]
    (match (Payments/charge! (::o/id cart) (total cart))
      [:ok _]         reservation
      [:declined why] (do (Inventory/release! reservation)   ; a compensation, not a rollback
                          (fail [:card-declined why])))))
;; => [:=> [[:map ::o/id ::o/lines & r]] :uuid {:eff #{Inventory Payments Async [Fail CheckoutError]}}]


;;; ── Handlers: adapters, installed by scope ─────────────────────────────────

(defhandler http-inventory [base-url] Inventory   ; SQL or HTTP lives here, domain rules do not
  (stock [sku]
         (-> (http/get (str base-url "/stock/" sku)) :body (json/decode [:map [:left :int]]) :left))
  (reserve! [lines]
            (-> (http/post (str base-url "/reservations") {:body (json/encode lines)})
                :body (json/decode [:map [:id :uuid]]) :id))
  (release! [id]
            (http/delete (str base-url "/reservations/" id))
            nil))
;; => [:=> [:string] (Handler Inventory) {:eff #{Net Async [Fail [:or http/Error [:invalid ...]]]}}]

(defhandler fake-inventory [stock] Inventory
  (stock    [sku] (get stock sku 0))
  (reserve! [_]   #uuid "00000000-0000-0000-0000-000000000001")
  (release! [_]   nil))

(defhandler fake-payments [answer] Payments
  (charge! [_ _] (if (= answer :ok) [:ok "txn-1"] [:declined "test card"])))

(def cart-json (json/codec ApiCart {:keys :camel :rename {::p/id "customerId"}}))
;; derived from the schema; two attributes mapped to one key would be an error here

(defn handle-checkout [req]
  (handle (checkout (as Cart (json/read cart-json (:body req))))
          (return [id] {:status 201 :body {:reservation id}})
          (fail [e]    {:status (match e [:invalid & _] 400 _ 409) :body e})))
;; => [:=> [[:map [:body :string] & r]] Response {:eff #{Inventory Payments Async}}]

(defn schedule-sync [cart-id]
  (Scheduler/later 5000
                   (capture [Inventory Log] (fn [] (sync-cart cart-id)))))
;; variant: handlers resolve at call time by default; capture binds service handlers at creation.
;; Capturing a control effect (Fail, Async) is a compile error.


;;; ── Static DI: functors ────────────────────────────────────────────────────

(defeffect (KV k v)
  (get  [key :- k] :- [:maybe v])
  (put! [key :- k, val :- v] :- :unit))

(defmodule Memo [Store :- (KV Sku :int)]
  (defn memo [f sku]
    (or (Store/get sku)
        (doto (f sku) (->> (Store/put! sku))))))

(def StockCache (Memo (ttl-kv {:ttl-ms 5000})))
;; applied at compile time: Store/get is a direct call and KV is absent from the contracts
;; ? the same defeffect is a scope-bound port and a functor argument; one concept or two is open


;;; ── Sequences: one transducer for a collection and a channel ───────────────

(def paid-totals
  (comp (filter #(= (::o/status %) :paid)) (map ::o/total)))

(defn revenue [orders :- [:vector Order {:layout :columns}]]
  (transduce paid-totals + (zero Usd) orders))
;; => [:=> [[:vector Order]] Usd {:eff #{}}]     columns: the pass reads ::status and ::total only

(defn revenue-feed [in]
  (pipe in (chan 64 paid-totals)))
;; => [:=> [(Chan Order)] (Chan Usd) {:eff #{Async}}]     the same xf; Async comes from the channel


;;; ── Memory: a guarantee on a hot path ──────────────────────────────────────

(defn normalize [^:unique xs :- [:array :double]]
  (let [m (reduce max 0.0 xs)]
    (map-into xs #(/ % m))))
;; ^:unique turns the in-place update into a contract: a caller passing a shared array fails to compile


;;; ── Live state and reload ──────────────────────────────────────────────────

(defonce carts (atom {} :- [:map-of ::o/id Cart]))

(defn add-to-cart! [id line]
  (swap! carts update id o/add-line line))
;; => [:=> [::o/id Line] :unit {:eff #{[Ref carts] [Fail [:invalid Qty]]}}]
;; ? swap! keeps the old value for its CAS, so add-line copies here.

(defmigration Cart [old]
  (assoc old ::o/coupon "NONE"))
;; a changed Cart type refuses the reload of `carts` until a migration exists


;;; ── Properties: generators from the schemas ────────────────────────────────

(defprop add-line-keeps-total
  [cart Cart, line Line]
  (= (total (o/add-line cart line))
     (+ (total cart) (o/line-total line))))
;; finds qty 999 + 1: add-line is partial, and its contract says so


;;; ── Entry point ────────────────────────────────────────────────────────────

(defn -main {:eff #{Env Net}} [& _]     ; a closed row: the program's capability policy
  (with [(scheduler {:threads 8})
         (http-inventory (env/get "INVENTORY_URL"))
         (stripe-payments (env/get "STRIPE_KEY"))
         (stdout-log)]
        (http/serve {:port 8080} handle-checkout)))
;; a dependency that starts writing files is an error naming the chain that requires it


(comment
  ;; REPL session

  user=> (contract checkout)
  [:=> [[:map ::o/id ::o/lines & r]] :uuid
   {:eff   #{Inventory Payments Async [Fail CheckoutError]}
    :modes {cart :borrowed}
    :doc   "Reserves stock and charges the card; returns the reservation id."}]

  user=> (checkout c)
  ;; error: Inventory, Payments are not handled
  ;; the REPL installs only Async (a seeded scheduler) and Fail (prints the error)

  user=> (with [(fake-inventory {"ABC-0001" 1}) (fake-payments :ok)] (checkout c))
  ;; Fail: [:out-of-stock {:sku "ABC-0001" :left 1}]

  user=> (def d (json/read-any (slurp "cart.json")))
  user=> (type-of d)
  ;; => Any
  user=> (as Cart d)
  ;; Fail: [:invalid Cart {:missing [::o/id]}]

  user=> (entry person ::p/email)
  ;; => [::p/email "bob@x.com"]

  user=> (attr ::p/email)
  ;; => {:ident :shop.person/email, :schema [:string {:pii true}], :module shop.person}

  user=> (as? Describe d)
  ;; => nil

  user=> (compile-profile 'shop.order)
  ;; => {:phase-1-ms 41, :by {openapi/schema 30, s/merge 6}, :check-ms 12}

  user=> (contract eval)
  ;; => [:=> [Any] Any {:eff #{Eval}}]   a release binary without the compiler rejects Eval at link time
  )
