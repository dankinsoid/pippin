;; @ai-generated(guided)
;; Design sketch, not runnable: a static Clojure-family language on one checkout service.
;; `;; =>` is the inferred contract the REPL prints; `;; ?` marks friction.
;;   args -> result ! <effects>    {k: T | r} open row    ^u unique  ^b borrowed  ^l local  ^1 once

(ns shop.checkout
  (:require [std.http :as http]
            [std.json :as json]
            [std.env :as env]))


;;; ── Shapes ─────────────────────────────────────────────────────────────────

;; A refinement makes a nominal type: checked once in its constructor, carried by the type afterwards.
(deftype Sku   [:re #"[A-Z]{3}-\d{4}"])
(deftype Qty   [:int {:min 1 :max 999}])
(deftype Cents [:int {:min 0}])

(deftype Line
  [:map
   [:sku Sku]
   [:qty Qty]
   [:price Cents]])

(deftype Cart
  [:map
   [:id :uuid]
   [:lines [:vector Line]]
   [:coupon {:optional true} :string]])

(deftype CheckoutError
  [:enum
   [:out-of-stock [:map [:sku Sku] [:left :int]]]
   [:card-declined :string]
   [:empty-cart]])

(deftype (Page a)
  [:map [:items [:vector a]] [:next [:maybe :string]]])

;; Phase 1 function; the yaml is a declared input, so the result is cached by hash.
(deftype Refund (comptime (openapi/schema "payments.yaml" "#/components/schemas/Refund")))

(comment
  (Qty 3)    ; literal: checked at compile time, no effect
  (Qty n)    ; => Qty ! <Fail [:invalid Qty]>
  ;; ? should a failed refinement join the caller's Fail row, or be its own Invalid effect?
  )


;;; ── Pure core: rows and modes ──────────────────────────────────────────────

(defn line-total [{:keys [qty price]}]
  (* qty price))
;; => {qty: Qty, price: Cents | r}^b -> Int ! <>
;; ? arithmetic drops refinements; keeping them means interval inference in the checker.

(defn total [cart]
  (transduce (map line-total) + 0 (:lines cart)))
;; => {lines: [{qty: Qty, price: Cents | r}] | s}^b -> Int ! <>

(defn add-line [cart line]
  (update cart :lines
          (fn [lines]
            (if-let [i (index-where #(= (:sku %) (:sku line)) lines)]
              (update-in lines [i :qty] #(Qty (+ % (:qty line))))
              (conj lines line)))))
;; => Cart^u, Line^b -> Cart ! <Fail [:invalid Qty]>
;; Inferred ^u: in place when the refcount is 1, a copy otherwise; ^:unique makes the copy a compile error.

(defn receipt [cart]
  (let [sb (string-builder)]
    (doseq [{:keys [sku qty price]} (:lines cart)]
      (sb-append! sb sku " x" qty " " price "\n"))
    (str sb)))
;; => {lines: [...] | r}^b -> String ! <>
;; sb is ^l: it never escapes, so it lives in the frame's arena; (str sb) copies out.


;;; ── Effects: dependencies, failure and async are one mechanism ─────────────

;; From std, for reference:
;;   (defeffect (Fail e) (fail [err :- e] :- :never))
;;   (defeffect Async
;;     (spawn [f :- ^1 (fn [] a ! <Async | e>)] :- (Task a))
;;     (await [t :- (Task a)] :- a))
;; There is no async keyword: par-map is spawn + await, and the scheduler is a handler.

(defeffect Inventory
  (stock    [sku :- Sku] :- :int)
  (reserve! [lines :- [:vector Line]] :- :uuid)
  (release! [reservation :- :uuid] :- :unit))

(defeffect Payments
  (charge! [cart-id :- :uuid, amount :- Cents]
           :- [:enum [:ok :string] [:declined :string]]))

(defn- check-stock [lines]
  (let [left (par-map #(Inventory/stock (:sku %)) lines)]
    (doseq [[line n] (zip lines left)
            :when (< n (:qty line))]
      (fail [:out-of-stock {:sku (:sku line) :left n}]))))
;; => [Line]^b -> Unit ! <Inventory, Async, Fail [:out-of-stock {sku: Sku, left: Int}]>
;; ? par-map calls Inventory/stock from several tasks, so the installed handler must be shareable
;;   across tasks: a mode on the handler value (portable), not on this function.

(defn checkout
  "Reserves stock and charges the card; returns the reservation id."
  {:example '[(with [(fake-inventory {"ABC-0001" 5}) (fake-payments :ok)]
                (checkout (cart-of ["ABC-0001" 2 1500])))
              :=> #uuid "00000000-0000-0000-0000-000000000001"]}
  [cart]
  (when (empty? (:lines cart))
    (fail [:empty-cart]))
  (check-stock (:lines cart))
  (let [reservation (Inventory/reserve! (:lines cart))]
    (case (Payments/charge! (:id cart) (Cents/trust (total cart)))
      [:ok _]         reservation
      [:declined why] (do (Inventory/release! reservation)
                          (fail [:card-declined why])))))
;; => Cart^b -> Uuid ! <Inventory, Payments, Async, Fail CheckoutError>
;; The Fail row is inferred as exactly three tags; CheckoutError is checked against it, not required.
;; :example is a test that `check` runs.
;; ? (Cents (total cart)) would add [:invalid Cents] to the row: total is never negative, but the
;;   checker cannot see that (see line-total). Cents/trust is an assertion compiled out in release.


;;; ── Handlers: implementations, installed by scope ──────────────────────────

(defhandler http-inventory [base-url] Inventory
  (stock [sku]
    (->> (http/get (str base-url "/stock/" sku))
         :body
         (json/decode [:map [:left :int]])
         :left))
  (reserve! [lines]
    (->> (http/post (str base-url "/reservations") {:body (json/encode lines)})
         :body
         (json/decode [:map [:id :uuid]])
         :id))
  (release! [id]
    (http/delete (str base-url "/reservations/" id))
    nil))
;; => String -> (Handler Inventory) ! <Http, Async, Fail [:or http/Error [:invalid ...]]>
;; The handler's own effects move into the scope that installs it.

(defhandler fake-inventory [stock] Inventory
  (stock    [sku] (get stock sku 0))
  (reserve! [_]   #uuid "00000000-0000-0000-0000-000000000001")
  (release! [_]   nil))

(defhandler fake-payments [answer] Payments
  (charge! [_ _] (if (= answer :ok) [:ok "txn-1"] [:declined "test card"])))

(defn handle-checkout [req]
  (handle (checkout (json/decode Cart (:body req)))
    (return [id] {:status 201 :body {:reservation id}})
    (fail [e]    {:status (if (= (first e) :invalid) 400 409) :body e})))
;; => {body: String | r}^b -> Response ! <Inventory, Payments, Async>
;; Fail is handled here and leaves the row; the decoder's [:invalid Cart] joins the same union.


;;; ── Static DI: functors over an effect signature ───────────────────────────

(defeffect (KV k v)
  (get  [key :- k] :- [:maybe v])
  (put! [key :- k, val :- v] :- :unit))

;; Applying a functor resolves the handler at compile time: KV/get becomes a direct, inlinable call,
;; and KV is absent from the contracts of the result.
(defmodule Memo [Store :- (KV Sku :int)]
  (defn memo [f sku]
    (or (Store/get sku)
        (doto (f sku) (->> (Store/put! sku))))))

(def StockCache (Memo (ttl-kv {:ttl-ms 5000})))
;; ? the same defeffect serves as a dynamic dependency (handle/with) and as a functor argument.
;;   One concept with two binding times, or two concepts that look alike? Arguments to ttl-kv
;;   must be comptime-known here, and nothing in the syntax says so.


;;; ── Macros and elaborators ─────────────────────────────────────────────────

;; Syntax -> syntax, untyped, before inference: cheap, and the common case.
(defmacro with-retry [n & body]
  `(retry* ~n (fn [] ~@body)))

;; An elaborator sees inferred types (as in Lean 4); it is slower and used rarely.
(defelab dbg [e]
  (let [t (type-of e)]
    `(let [v# ~e] (log/debug '~e :- '~t v#) v#)))
;; ? deriving Eq/Show/generators needs no elaborator: a type is data in phase 1 already,
;;   so (derive Line [:eq :show :gen]) is a comptime function over the schema.


;;; ── Properties: generators come from the schemas ───────────────────────────

(defprop add-line-keeps-total
  [cart Cart, line Line]
  (= (total (add-line cart line))
     (+ (total cart) (line-total line))))
;; The generator finds a counterexample: merging qty 999 + 1 fails the Qty constructor.
;; The property is honest: add-line is partial, and its contract says so (Fail [:invalid Qty]).


;;; ── Live state and reload ──────────────────────────────────────────────────

(defonce carts (atom {} :- [:map-of :uuid Cart]))

(defn add-to-cart! [id line]
  (swap! carts update id add-line line))
;; => Uuid, Line -> Unit ! <Fail [:invalid Qty]>
;; ? swap! keeps the old value for its CAS, so add-line copies the cart here. A single-writer
;;   ^:unique atom could move the value out instead.

;; Redefining Cart with a new required key: the reload refuses `carts` until a migration exists.
(defmigration Cart [old]
  (assoc old :currency :usd))
;; Redefining a function with the same contract swaps it in place (dev builds call through vars).
;; A changed contract rechecks the dependents; until they pass, the old version keeps running.


;;; ── Entry point ────────────────────────────────────────────────────────────

(defn -main [& _]
  (with [(scheduler {:threads 8})
         (http-inventory (env/get "INVENTORY_URL"))
         (stripe-payments (env/get "STRIPE_KEY"))]
    (http/serve {:port 8080} handle-checkout)))
;; => [String] -> Unit ! <Env, Net, Fail http/Error>
;; Everything else is handled here. Whatever is left in the row of -main is what the binary
;; asks of the OS: an audit of the program's capabilities for free.


(comment
  ;; REPL session

  user=> (contract checkout)
  {:in    [Cart]
   :out   :uuid
   :eff   #{Inventory Payments Async [Fail CheckoutError]}
   :modes {cart :borrow}
   :meta  {:doc "Reserves stock and charges the card; returns the reservation id." :example [...]}}

  user=> (checkout c)
  ;; error: unhandled effects Inventory, Payments
  ;; the REPL installs only Async (a seeded scheduler), Fail (prints the error) and Dyn

  user=> (with [(fake-inventory {"ABC-0001" 1}) (fake-payments :ok)]
           (checkout c))
  ;; Fail: [:out-of-stock {:sku "ABC-0001" :left 1}]

  user=> (def r (http/get "http://localhost:9000/stock/ABC-0001"))
  user=> (-> r :body json/read :left)
  ;; => 7 : Dyn   exploration is gradual; a Dyn is checked where it meets a typed position

  user=> (Qty (-> r :body json/read :left))
  ;; => 7 : Qty

  user=> (eval '(+ 1 2))
  ;; => 3   ! <Eval>; a release binary without the compiler rejects Eval at link time
  )
