;; @ai-generated(solo)
;; A feed screen's JSON round trip: what a mobile app does with every API response it shows.
;; Input: a feed response of n = 800 posts, ~0.9 MB of JSON from util/lcg states, made once per process (a response
;; that has arrived): nested objects and arrays, strings with escapes, \u escapes and raw non-ASCII (BMP only), ints,
;; decimals, nulls, booleans, fields the app does not know. Work: parse with keyword keys (workloads.json-host:
;; clojure.data.json, or the platform's JSON on ClojureScript and ClojureDart); coerce each post into the app's model
;; (unknown keys dropped, ids and prices parsed, enums as keywords, no nulls); drop hidden posts, score and sort the
;; rest, count tags and authors; write the screen's state back to JSON.
;; Output: counts and sums over the model, the top post, and the written JSON's length (key order is each runtime's
;; own), `expected` below.
(ns workloads.json
  (:require [clojure.string :as str]
            [workloads.json-host :as host]
            [workloads.util :as u]))

;; ---- the response's text

(def ^:private first-names ["Anna" "Борис" "Chloé" "Dmitri" "Émile" "Фаина" "Grace" "Hiroshi" "Иван" "Jürgen"
                            "Kateřina" "Lucía"])
(def ^:private last-names ["Smith" "Петрова" "Müller" "García" "Новиков" "Łukasiewicz" "O'Brien" "Søren" "Yamada"
                           "Çelik"])
(def ^:private words ["the" "quick" "brown" "fox" "café" "naïve" "über" "привет" "мир" "東京" "data" "screen" "feed"
                      "today" "день" "résumé" "jalapeño" "smörgåsbord" "news" "Ереван"])
;; String contents as a server escapes them: \" \n \t \\, a URL's slashes, \u escapes of non-ASCII.
(def ^:private fragments ["\\\"quoted\\\"" "\\n" "\\t" "C:\\\\Users" "https://example.com/a/b?c=1"
                          "caf\\u00e9 \\u043f\\u0440\\u0438\\u0432\\u0435\\u0442"])
(def ^:private tag-pool ["news" "tech" "food" "travel" "music" "sport" "art" "science" "games" "books" "film" "photo"])
(def ^:private kinds ["post" "photo" "video" "repost" "ad"])
(def ^:private currencies ["USD" "EUR" "AMD" "RUB"])
(def ^:private cities ["Yerevan" "Lisbon" "Tbilisi" "Berlin" "Kyoto" "São Paulo" "Montréal" "Москва"])

(defn- pick [v x] (nth v (mod x (count v))))

(defn- q [s] (str "\"" s "\""))

(defn- pad [n width]
  (let [s (str n)]
    (str (subs "000" 0 (- width (count s))) s)))

(defn- title [r]
  (str/join " " (map #(pick words (+ (quot r 7) (* % 5))) (range (+ 3 (mod r 4))))))

(defn- body [r i]
  (let [nw (count words)]
    (str/join " " (map (fn [k]
                         (let [t (mod (+ (quot r 3) (* k 11) i) (+ nw (count fragments)))]
                           (if (< t nw) (nth words t) (nth fragments (- t nw)))))
                       (range (+ 12 (mod r 20)))))))

;; Decimals whose last digit is not 0, so every runtime prints the double it reads back as the same text.
(defn- rating [r]
  (str (+ 1 (mod r 4)) "." (mod (quot r 4) 10) (+ 1 (mod (quot r 40) 9))))

(defn- coordinate [r sign? whole]
  (str (if sign? "-" "") (+ 1 (mod (quot r 3) whole)) "." (pad (mod (quot r 240) 1000) 3) (+ 1 (mod (quot r 240000) 9))))

(defn- author-json [r4 r5]
  (let [u (mod r4 400)]
    (str "{\"id\":" (q (str "u-" u))
         ",\"name\":" (q (str (pick first-names r4) " " (pick last-names (quot r4 12))))
         ",\"handle\":" (q (str "@user" u))
         ",\"avatarUrl\":" (q (str "https://cdn.example.com/avatars/" u ".jpg"))
         ",\"verified\":" (zero? (mod r5 5))
         ",\"followers\":" (mod r5 100000)
         ",\"bio\":" (if (zero? (mod r5 3)) "null" (q (pick fragments r5)))
         ",\"_links\":{\"self\":" (q (str "/v2/users/" u)) ",\"posts\":" (q (str "/v2/users/" u "/posts")) "}}")))

(defn- media-json [r i k]
  (str "{\"type\":" (q (if (even? k) "image" "video"))
       ",\"url\":" (q (str "https://cdn.example.com/m/" i "/" k ".jpg"))
       ",\"width\":" (+ 640 (* 320 (mod (+ (quot r 3) k) 4)))
       ",\"height\":" (+ 480 (* 240 (mod (+ (quot r 12) k) 3)))
       ",\"alt\":" (if (even? (+ r k)) "null" (q (pick words (+ r k))))
       ",\"blurhash\":\"LEHV6nWB2yk8pyo0adR*.7kCMdnj\"}"))

(defn- comment-json [r i k]
  (str "{\"id\":" (q (str "c-" i "-" k)) ",\"author\":" (q (pick first-names (+ r k)))
       ",\"text\":" (q (title (+ r k))) ",\"likes\":" (mod (+ r k) 50) "}"))

(defn- post-json [i r]
  (let [[r0 r1 r2 r3 r4 r5 r6 r7 r8 r9 r10 r11 r12 r13 r14 r15] r
        kind (pick kinds r0)]
    (str "{\"id\":" (q (str "p-" (+ 100000 (* i 7))))
         ",\"kind\":" (q kind)
         ",\"createdAt\":" (q (str "2026-" (pad (+ 1 (mod r1 12)) 2) "-" (pad (+ 1 (mod (quot r1 12) 28)) 2) "T"
                                   (pad (mod r1 24) 2) ":" (pad (mod (quot r1 24) 60) 2) ":00Z"))
         ",\"title\":" (q (title r2))
         ",\"body\":" (q (body r3 i))
         ",\"author\":" (author-json r4 r5)
         ",\"stats\":{\"likes\":" (mod r6 5000) ",\"comments\":" (mod (quot r6 5000) 300)
         ",\"shares\":" (mod r7 100) ",\"views\":" (+ 100 (mod r7 100000)) ",\"rating\":" (rating r8) "}"
         ",\"tags\":[" (str/join "," (map #(q (pick tag-pool (+ (quot r9 4) (* % 3)))) (range (mod r9 4)))) "]"
         ",\"media\":[" (str/join "," (map #(media-json r10 i %) (range (mod r10 3)))) "]"
         ",\"price\":" (if (zero? (mod r11 4))
                         (str "{\"amount\":" (q (str (mod (quot r11 8) 200) "." (pad (mod (quot r11 1600) 100) 2)))
                              ",\"currency\":" (q (pick currencies (quot r11 160000))) "}")
                         "null")
         ",\"location\":" (if (zero? (mod r12 3))
                            (str "{\"lat\":" (coordinate r12 false 80)
                                 ",\"lon\":" (coordinate (quot r12 7) (even? (quot r12 5)) 170)
                                 ",\"name\":" (q (pick cities (quot r12 11))) "}")
                            "null")
         ",\"flags\":{\"pinned\":" (zero? (mod r13 50)) ",\"hidden\":" (zero? (mod r13 17))
         ",\"sponsored\":" (= kind "ad") "}"
         ",\"tracking\":{\"impressionId\":" (q (str "imp-" r14)) ",\"position\":" i
         ",\"experiment\":" (q (str "exp-" (mod r14 7))) ",\"variant\":\"b\"}"
         ",\"commentsPreview\":[" (str/join "," (map #(comment-json r15 i %) (range (mod r15 3)))) "]}")))

(defn feed-text
  "The response for n posts: its JSON text."
  [n]
  ;; The generator's low bits cycle with a short period: a post's draws are its states' top 24 bits.
  (let [rs (mapv #(quot % 128) (u/lcg-vec 42 (* 16 n)))]
    (str "{\"status\":\"ok\",\"generatedAt\":\"2026-10-15T12:00:00Z\",\"items\":["
         (str/join "," (map #(post-json % (subvec rs (* 16 %) (* 16 (inc %)))) (range n)))
         "],\"paging\":{\"cursor\":" (q (str "c-" n)) ",\"hasMore\":true,\"limit\":" n "}"
         ",\"experiments\":{\"feedRanking\":\"v3\",\"inlineVideo\":false}}")))

;; ---- the app's model of it

(defn- parse-id [s] (parse-long (subs s (inc (str/index-of s "-")))))

(defn- cents [amount]
  (let [[whole frac] (str/split amount #"\.")]
    (+ (* 100 (parse-long whole)) (parse-long frac))))

(defn- ->author [a]
  (-> (select-keys a [:id :name :handle :avatarUrl :verified :followers])
      (update :id parse-id)))

(defn- ->media [m]
  (-> (select-keys m [:type :url :width :height])
      (update :type keyword)))

(defn- ->post [p]
  (let [price (:price p)
        place (:location p)]
    (cond-> (-> (select-keys p [:id :kind :createdAt :title :body :author :stats :tags :media])
                (update :id parse-id)
                (update :kind keyword)
                (update :author ->author)
                (update :stats select-keys [:likes :comments :shares :views :rating])
                (update :media #(mapv ->media %))
                (assoc :day (subs (:createdAt p) 0 10)
                       :pinned (get-in p [:flags :pinned])
                       :hidden (get-in p [:flags :hidden])))
      price (assoc :priceCents (cents (:amount price)) :currency (keyword (:currency price)))
      place (assoc :place (select-keys place [:lat :lon :name])))))

(defn- score [{:keys [stats pinned]}]
  (+ (:likes stats) (* 2 (:comments stats)) (* 3 (:shares stats)) (if pinned 1000 0)))

(defn- by-score [a b]
  (let [c (compare (:score b) (:score a))]
    (if (zero? c) (compare (:id a) (:id b)) c)))

(defn- screen [feed]
  (let [posts (->> (:items feed)
                   (map ->post)
                   (remove :hidden)
                   (map #(assoc % :score (score %)))
                   (sort by-score)
                   vec)]
    {:posts posts
     :paging (select-keys (:paging feed) [:cursor :hasMore])
     :tagCounts (frequencies (mapcat :tags posts))
     :authorCount (count (into #{} (map (comp :id :author)) posts))}))

(defn- round-trip [text]
  (let [feed (host/parse text)
        state (screen feed)
        out (host/emit state)
        posts (:posts state)
        top (first posts)]
    [(count (:items feed)) (count posts) (:authorCount state) (count (:tagCounts state))
     (count (filter (comp :verified :author) posts))
     (reduce + (map :score posts))
     (reduce + (keep :priceCents posts))
     (count (filter :place posts))
     (:id top) (:title top) (count out)]))

(defn run* [n] (round-trip (feed-text n)))

(def ^:private payload (feed-text 800))

(defn run [] (round-trip payload))

;; What run returns on JVM Clojure 1.12.6; clj-corpus-bench and jvm.clj check every run against it.
(def expected '[800 751 350 12 169 2231505 1901714 268 104557 "naïve data résumé the naïve" 522816])
