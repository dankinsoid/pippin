;; @ai-generated(solo)
;; A screen as hiccup, rendered to HTML text: the rendering half of design §10's app, without a UI.
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
