;; @ai-generated(guided)
;; Merry sketch (docs/merry.md), not runnable: `;; =>` contract, `;; ?` friction, `;; variant:` one doc option.


;;; ── Attributes: the domain vocabulary ──────────────────────────────────────

(ns shop.person)

(deftype PersonId (Uuid {:tagged true}))  ; its own nominal type: an order id is not a person id

;; variant: attributes are declared only explicitly, their namespace is this module; types select them.
(defattrs
  {:id    PersonId
   :name  String
   :email (String {:pii true})})         ; type props belong to the attribute

(deftype ^{:eq-by [::id]} Person          ; equal by id; hash from the same field
  {::id _ ::name _ ::email (or nil _)})   ; a selection: `_` is the attribute's declared type

(def PublicProfile (s/select Person [::id ::name]))


(ns shop.catalog)

(deftype Sku (String {:re #"[A-Z]{3}-\d{4}"}))   ; a refinement is nominal: checked once, in the constructor
(deftype Qty (Int {:min 1 :max 999}))

(deftype (Money cur) {:cents Int})        ; a value type: local fields; the currency is a type argument
(deftype Usd (Money :usd))

;; variant: arithmetic is a given; its key (Add (Money cur)) has one cur, so (+ usd eur) does not check
(def ^:given money-add :- (Add (Money cur))
  (make-add {:add (fn [a b] {:cents (+ (:cents a) (:cents b))})}))

(defn scale [m n] (update m :cents * n))
;; => (Fn [{:cents Int & r} Int] {:cents Int & r} {:eff #{}})     update keeps the row: a Usd comes back a Usd


(ns shop.order
  (:require [shop.person :as p]
            [shop.catalog :refer [Sku Qty Usd scale]]))

(deftype Line {:sku Sku :qty Qty :price Usd})   ; unqualified: local fields, not attributes
(deftype OrderId (Uuid {:tagged true}))

(defattrs
  {:id     OrderId
   :lines  [Line]
   :coupon String
   :status (or :new :paid :cancelled)
   :total  Usd})

(deftype Cart  {::id _ ::lines _ ::coupon (or nil _) ::p/id _})
(deftype Order {::id _ ::lines _ ::status _ ::total _ ::p/id _})

(deftype CheckoutError                    ; a nominal sum: domain variants are declared, not inferred
  (or [:out-of-stock {:sku Sku :left Int}]
      [:card-declined String]
      [:empty-cart]))


;;; ── Pure core: rows, modes, refinements ────────────────────────────────────

(defn line-total [{& [qty price]}]
  (scale price qty))
;; => (Fn [{:qty Int :price {:cents Int & s} & r}] {:cents Int & s} {:eff #{}})
;; a Line fits without conversion: Qty is used as Int, Usd as its row

(defn total [cart]
  (transduce (map line-total) + (zero Usd) (::lines cart)))
;; => (Fn [{::lines [Line] & r}] Usd {:eff #{}})     (zero Usd): the neutral element comes from a type, not context

(defn add-line [cart line]
  (update cart ::lines
          (fn [lines]
            (if-let [i (index-where #(= (:sku %) (:sku line)) lines)]
              (update-in lines [i :qty] #(coerce Qty (+ % (:qty line))))
              (conj lines line)))))
;; => (Fn [^:unique {::lines [Line] & r} ^:borrowed Line] {::lines [Line] & r} {:eff #{[Fail [:invalid Qty]]}})
;; modes sit on the parameters: they are coeffects of the inputs, not effects of the function

(defn receipt [cart]
  (let [sb (string-builder)]
    (doseq [{& [sku qty price]} (::lines cart)]
      (sb-append! sb sku " x" qty " " (:cents price) "\n"))
    (str sb)))
;; => (Fn [^:borrowed {::lines [Line] & r}] String {:eff #{}})     sb is local: a builder that never escapes

(defn customer-of [::p/id] (p/find-name id))
;; the parameter takes its type from the attribute: neither inferred nor written


;;; ── Givens: dictionaries chosen by type ────────────────────────────────────

;; variant: protocols are givens (docs «Кандидат: given'ы»); defprotocol is sugar over this.
;; std: (deftype ^:witness (Add T) {:add (Fn [T T] T) :add-all (Fn [T [T]] T)}), + takes ^:using (Add a)

(deftype ^:witness (Describe T)
  {:describe (Fn [T] String {:eff #{}})   ; the effect bound every implementation must fit
   :title    (Fn [T] String {:eff #{}})})

(defn default-title [x] (str (type-name x)))

(defn make-describe [{:keys [describe title]}]   ; defaults live in the constructor, so they stay callable
  {:describe describe
   :title    (or-some title default-title)})

(def ^:given cart-describe :- (Describe Cart)
  (make-describe {:describe #(str ((:title cart-describe) %) ": " (count (::lines %)) " lines")
                  :title    #(str "cart, " (default-title %))}))

(defn ^:given vector-describe [d :- (Describe a)] :- (Describe [a])
  (make-describe {:describe #(str/join ", " (map (:describe d) %))}))
;; a given-function whose premise is smaller than its key (Paterson conditions), so the search ends

(deftype ^:opaque CouponCode {:code String})
(defn coupon [s] (as CouponCode {:code (str/upper-case s)}))
;; opaque: no map view, no `as` or `coerce`; outside this module a CouponCode exists only through `coupon`

(deftype ^{:delegate {Describe Cart}} AuditedCart (s/merge Cart {:by p/PersonId :at Instant}))
;; variant: explicit delegation builds (Describe AuditedCart) from Cart's; Add is refused: two audit trails do not merge

;; =, hash, printing, codecs come from the schema; equality crosses types unless a type sets :eq-by.


;;; ── Multimethods: dispatch on a sum's tag ──────────────────────────────────

(deftype OrderEvent
  (or [:order/created Order]
      [:order/paid {:payment-id String ::total _}]
      [:order/cancelled {:reason String}]))

;; variant: two explicit forms. defmulti is static: its methods see the narrowed payload,
;; completeness is checked, it compiles to a switch on the tag.
(defmulti on-event [e :- OrderEvent] :- nil)
(defmethod on-event :order/created   [[_ order]] (Log/info "created" (::id order)))
(defmethod on-event :order/paid      [[_ paid]]  (Log/info "paid" (::total paid)))
(defmethod on-event :order/cancelled [[_ c]]     (Log/warn "cancelled" (:reason c)))
;; => (Fn [OrderEvent] nil {:eff #{} :using #{Log}})

;; defdispatch is dynamic: any dispatch function over data read at runtime, a missing method fails at runtime.
(defdispatch webhook (fn [d :- Json] (get d "type")))


;;; ── Patterns: destructuring, match, case ───────────────────────────────────

(defn shipping-label [{:address {:city city :zip zip} & [name :or {name "customer"}]}]
  (str name ", " zip " " city))
;; key → pattern, as in a data literal; `& [...]` names fields, the rest, :as and defaults

(defn paid-amount [e :- OrderEvent] :- (or nil Usd)
  (match e
    (or [:order/created {::status :paid ::total t}]   ; a literal inside compares
        [:order/paid {::total t}])        t           ; both alternatives bind t of one type
    (or [:order/created _]
        [:order/cancelled _])             nil))
;; without the last branch: error, not covered: [:order/cancelled {:reason _}]
;; {& [::total]} would bind `total` and shadow the function above: a warning
;; [:order/paid p extra] is an error, not a fall to `_`: the variant has one field

(defn payment-label [p]
  (match p
    [:card last4] (str "card •••• " last4)
    [:cash]       "cash"))
;; => (Fn [(or [:card a] [:cash])] String {:eff #{}})     no `_`: closed; a caller passing [:voucher] fails at its call

(defn region [code]
  (match code :eu "Europe" :us "United States" _ "elsewhere"))
;; => (Fn [(or :eu :us & r)] String {:eff #{}})     `_` opens it; without `_` the literals give the enum (or :eu :us)

(defn classify [line :- Line, limit :- Qty]
  (match line
    {:qty (= limit)}                              :at-limit   ; compare with a name in scope
    {:qty (> 100) :sku (re-matches #"VIP-\d{4}" $)} :bulk-vip ; the value goes first, `$` elsewhere
    {:qty q :price {:cents c}} :when (> (* q c) 100000) :expensive
    _                                             :regular))
;; {:qty limit} binds a fresh name; the shadowing warning suggests (= limit)

(defn merge-pair [a :- Line, b :- Line] :- (or nil Line)
  (match [a b]
    [{:sku s :qty x} {:sku s :qty y}] (assoc a :qty (coerce Qty (+ x y)))   ; a repeated name is equality
    _                                 nil))
;; => (Fn [Line Line] (or nil Line) {:eff #{[Fail [:invalid Qty]]}})

(defn clamp-qty [n :- Int] :- Qty
  (match n
    (Qty q) q                             ; the checking constructor: a failed check goes to the next branch
    (< 1)   1
    _       999))
;; => (Fn [Int] Qty {:eff #{}})     the literals meet :- Qty and are checked in phase 1, so no Fail

(defn parse-line [s :- String] :- Line
  (let [[sku qty cents :or (fail [:bad-line s])] (str/split s #",")]   ; let-else
    {:sku (coerce Sku sku) :qty (coerce Qty (parse-long qty)) :price {:cents (parse-long cents)}}))
;; without :or it does not compile: [a b c] does not cover [String], use match or let-else
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
;; a phase-1 function over known arguments; the yaml is a declared input, so the result is cached by hash

(defn audited [t] (s/merge t {:by p/PersonId :at Instant}))

(defn strip [x :- (audited a)] :- a
  (dissoc x :by :at))
;; variant: s/merge, s/dissoc, s/optional-keys are the checker's row algebra, so a is inferred from the argument;
;; any other function over types runs only on known arguments and never during inference

(def CartPatch (s/optional-keys (s/dissoc Cart ::id)))


;;; ── Fitting data: as, open types, entries ──────────────────────────────────

(defn draft->cart [draft]
  (-> draft (dissoc :debug) (assoc ::coupon "WELCOME") (as Cart $)))
;; as is strict: a leftover or missing field is a compile error that names it; the chain is one copy

(deftype ApiCart {::id _ ::lines _ & _})  ; open: unknown keys from the wire are kept in ext

(defn redact [person]
  (into (empty person)
        (map (fn [[a v]]
               (case a
                 ::p/email [a "***"]      ; here v is a string: the tag fixes the value's type
                 [a v])))
        person))
;; ? a generic redact over all :pii attributes needs a case per attribute, generated from (attrs T) in phase 1.


;;; ── Services: ports as contextual givens ───────────────────────────────────

(ns shop.checkout
  {:effects-bound #{Async Fail}}          ; inside the bound, the interface does not change
  (:require [shop.order :as o :refer [Cart Line OrderId CheckoutError ApiCart total]]
            [shop.catalog :refer [Sku Usd]]
            [std.http :as http]
            [std.json :as json]))

;; variant (docs «сервисы — контекстные given'ы»): defeffect is only for control effects, which need a continuation:
;; std: (defeffect (Fail e) (fail [err :- e] :- Never)), Async with spawn/await.
;; A service is a witness type, an adapter a function returning its value; `Inventory/stock` is ((:stock inv) sku).

(deftype ^:witness (Inventory e)          ; e: what an adapter may do; the fake does nothing, HTTP has Net
  {:stock    (Fn [Sku] Int {:eff e})
   :reserve! (Fn [[Line]] Uuid {:eff e})
   :release! (Fn [Uuid] nil {:eff e})})

(deftype ^:witness (Payments e)
  {:charge! (Fn [OrderId Usd] (or [:ok String] [:declined String]) {:eff e})})

(defn- check-stock [lines]
  (let [left (par-map #(Inventory/stock (:sku %)) lines)]
    (doseq [[line n] (zip lines left)
            :when (< n (:qty line))]
      (fail [:out-of-stock {:sku (:sku line) :left n}]))))
;; => (Fn [^:borrowed [Line]] nil {:eff #{Async [Fail [:out-of-stock {:sku Sku :left Int}]] & e} :using #{(Inventory e)}})

(defn checkout
  "Reserves stock and charges the card; returns the reservation id."
  {:example '[(with {:inventory (fake-inventory {"ABC-0001" 5}) :payments (fake-payments :ok)}
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
;; => (Fn [^:borrowed {::o/id OrderId ::o/lines [Line] & r}] Uuid
;;     {:eff #{Async [Fail CheckoutError] & e} :using #{(Inventory e) (Payments e)}})
;; requirements are inferred along the chain, like effects; the caller writes none of them


;;; ── Adapters: plain functions, installed once at the root ──────────────────

(defn http-inventory [base-url]           ; SQL or HTTP lives here, domain rules do not
  {:stock    (fn [sku]
               (-> (http/get (str base-url "/stock/" sku)) :body (json/decode {:left Int}) :left))
   :reserve! (fn [lines]
               (-> (http/post (str base-url "/reservations") {:body (json/encode lines)})
                   :body (json/decode {:id Uuid}) :id))
   :release! (fn [id]
               (http/delete (str base-url "/reservations/" id))
               nil)})
;; => (Fn [String] (Inventory #{Net Async [Fail (or http/Error [:invalid ...])]}) {:eff #{}})
;; no defhandler: an adapter is a value; a control handler is `handle`, a form whose body is a Thunk

(defn fake-inventory [stock]
  {:stock    (fn [sku] (get stock sku 0))
   :reserve! (fn [_] #uuid "00000000-0000-0000-0000-000000000001")
   :release! (fn [_] nil)})
;; => (Fn [{Sku Int}] (Inventory #{}) {:eff #{}})

(defn fake-payments [answer]
  {:charge! (fn [_ _] (if (= answer :ok) [:ok "txn-1"] [:declined "test card"]))})

(def cart-json (json/codec ApiCart {:keys :camel :rename {::p/id "customerId"}}))
;; derived from the schema; two attributes mapped to one key would be an error here

(defn handle-checkout [req]
  (handle (checkout (coerce Cart (json/read cart-json (:body req))))
          (return [id] {:status 201 :body {:reservation id}})
          (fail [e]    {:status (match e [:invalid & _] 400 _ 409) :body e})))
;; => (Fn [{:body String & r}] Response {:eff #{Async & e} :using #{(Inventory e) (Payments e)}})

(defn schedule-sync [cart-id]
  (go (sleep 5000)
      (try-some (sync-cart cart-id))))
;; the closure keeps the givens it was made under, so services need no capture; go spawns into the
;; enclosing scope and adds Tasks; its body has no Fail, so try-some turns a failure into nil


;;; ── Sequences: one transducer for a collection and a channel ───────────────

(def paid-totals
  (comp (filter #(= (::o/status %) :paid)) (map ::o/total)))

(defn revenue [orders :- (Vector Order {:layout :columns})]
  (transduce paid-totals + (zero Usd) orders))
;; => (Fn [[Order]] Usd {:eff #{}})     columns: the pass reads ::status and ::total only

(defn revenue-feed [in]
  (pipe in (chan 64 paid-totals)))
;; => (Fn [(Chan Order)] (Chan Usd) {:eff #{Async}})     the same xf; Async comes from the channel


;;; ── Memory: guarantees on a hot path ───────────────────────────────────────

(defn normalize {:copies :explicit} [^:unique xs :- (Array Double)]
  (let [m (reduce max 0.0 xs)]
    (map-into xs #(/ % m))))
;; ^:unique turns the in-place update into a contract: a caller passing a shared array fails to compile
;; {:copies :explicit} brings the Rust model into this function: an implicit copy is an error with a fix

(defn reprice [cart pct]
  (let [^:flat lines (flat (::o/lines cart))]   ; one O(n) copy if shared, then each update is in place
    (assoc cart ::o/lines
           (reduce (fn [ls i] (update-in ls [i :price :cents] #(quot (* % (- 100 pct)) 100)))
                   lines
                   (range (count lines))))))
;; ^:flat: an error if lines could turn persistent in this scope (stored in an atom, held by a snapshot)


;;; ── Live state and reload ──────────────────────────────────────────────────

(defonce carts (atom {} :- {OrderId Cart}))

(defn add-to-cart! [id line]
  (swap! carts update id o/add-line line))
;; => (Fn [OrderId Line] nil {:eff #{[Ref carts] [Fail [:invalid Qty]]}})
;; carts is global, so shared: swap! is a CAS, and the map turns persistent (CHAMP) at its first write
;; after sharing; add-line then copies a path, not the whole map

(defmigration Cart [old]
  (assoc old ::o/coupon "NONE"))
;; a changed Cart type refuses the reload of `carts` until a migration exists


;;; ── UI state: a store with typed cursors ───────────────────────────────────

(deftype UiState {:cart Cart :query String})

(def ui-state (ui/store (as UiState {:cart (o/empty-cart) :query ""})))
(def cart-lines (ui/cursor ui-state [:cart ::o/lines]))
;; => (Cursor [Line])     the path is checked against UiState: a typo is a compile error

(defn cart-badge [] (str (count @cart-lines)))
;; a deref in render records the path, so a write to :query does not re-render the badge;
;; ui/store is not an atom: one thread, no CAS, notifications batched once per frame


;;; ── Properties: generators from the schemas ────────────────────────────────

(defprop add-line-keeps-total
  [cart Cart, line Line]
  (= (total (o/add-line cart line))
     (+ (total cart) (o/line-total line))))
;; finds qty 999 + 1: add-line is partial, and its contract says so


;;; ── Entry point ────────────────────────────────────────────────────────────

(defn -main {:eff #{Env Net}} [& _]     ; a closed row: the program's capability policy
  (run-async {:threads 8}               ; Async's handler: a function with a Thunk body
    (with {:inventory (http-inventory (env/get "INVENTORY_URL"))
           :payments  (stripe-payments (env/get "STRIPE_KEY"))
           :log       (stdout-log)}     ; the app: one record of services, installed once
      (http/serve {:port 8080} handle-checkout))))
;; in release a given from the root is a constant: Inventory/stock compiles to a direct call
;; a dependency that starts writing files is an error naming the chain that requires it


(comment
  ;; REPL session

  user=> (contract checkout)
  (Fn [^:borrowed {::o/id OrderId ::o/lines [Line] & r}] Uuid
      {:eff   #{Async [Fail CheckoutError] & e}
       :using #{(Inventory e) (Payments e)}
       :doc   "Reserves stock and charges the card; returns the reservation id."})

  user=> (checkout c)
  ;; error: no given for (Inventory e), (Payments e)
  ;; the REPL installs only Async (a seeded scheduler) and Fail (prints the error)

  user=> (with {:inventory (fake-inventory {"ABC-0001" 1}) :payments (fake-payments :ok)} (checkout c))
  ;; Fail: [:out-of-stock {:sku "ABC-0001" :left 1}]

  user=> (def d (json/read (slurp "cart.json")))
  user=> (type-of d)
  ;; => Json     pure data: closed, acyclic, no Ref; Any is left for eval and plugins
  user=> (if (string? (get d "coupon")) (str/upper-case (get d "coupon")) "NONE")
  ;; => "WELCOME"     narrowing by path: the data is immutable, so the second get is a String too
  user=> (coerce Cart d)
  ;; Fail: [:invalid Cart {:missing [::o/id]}]

  user=> (entry person ::p/email)
  ;; => [::p/email "bob@x.com"]

  user=> (attr ::p/email)
  ;; => {:ident :shop.person/email, :type (String {:pii true}), :module shop.person}

  user=> (satisfies? Describe d)
  ;; => false

  user=> (compile-profile 'shop.order)
  ;; => {:phase-1-ms 41, :by {openapi/schema 30, s/merge 6}, :check-ms 12}

  user=> (contract eval)
  ;; => (Fn [Any] Any {:eff #{Eval}})   a release binary without the compiler rejects Eval at link time
  )
