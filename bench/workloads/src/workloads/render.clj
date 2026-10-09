;; @ai-generated(solo)
;; A screen as hiccup, rendered to HTML text: the rendering half of design §10's app, without a UI.
;; Input: n = 4000 list items, each [:li {:class :data-id} [:span.title text] [:div {:style} 0-3 links]], under
;; html/head/body/h1/ul/footer. Work: render recursively to one string, escaping text and attribute values and
;; sorting attributes by name; then render every item on its own. Output: the page length, the summed item lengths
;; and the first 200 characters of the page, `expected` below.
(ns workloads.render
  (:require [clojure.string :as str]))

(defn- escape [s]
  (str/escape s {\< "&lt;" \> "&gt;" \& "&amp;" \" "&quot;"}))

(defn- attrs [m]
  (apply str (map (fn [[k v]] (str " " (name k) "=\"" (escape (str v)) "\"")) (sort-by key m))))

(declare render)

(defn- render-children [xs]
  (apply str (map render xs)))

(defn render [node]
  (cond
    (string? node) (escape node)
    (number? node) (str node)
    (nil? node) ""
    (seq? node) (render-children node)
    (vector? node)
    (let [[tag & more] node
          [opts children] (if (map? (first more)) [(first more) (rest more)] [{} more])]
      (str "<" (name tag) (attrs opts) ">" (render-children children) "</" (name tag) ">"))
    :else (escape (str node))))

(defn- item [i]
  [:li {:class (if (even? i) "even" "odd") :data-id i}
   [:span.title (str "Item <" i "> & co")]
   [:div {:style "color: red"}
    (for [j (range (mod i 4))]
      [:a {:href (str "/items/" i "/" j)} "link " j])]])

(defn- page [items]
  [:html
   [:head [:title "List"]]
   [:body
    [:h1 "Items"]
    [:ul {:id "items"} (map item items)]
    [:footer (str (count items) " items")]]])

(defn run* [n]
  (let [html (render (page (range n)))
        small (reduce + (map #(count (render (item %))) (range n)))]
    [(count html) small (subs html 0 200)]))

(defn run [] (run* 4000))

;; What run returns on JVM Clojure 1.12.6; clj-corpus-bench and jvm.clj check every run against it.
(def expected '[670236 670117 "<html><head><title>List</title></head><body><h1>Items</h1><ul id=\"items\"><li class=\"even\" data-id=\"0\"><span.title>Item &lt;0&gt; &amp; co</span.title><div style=\"color: red\"></div></li><li class=\"odd\""])
