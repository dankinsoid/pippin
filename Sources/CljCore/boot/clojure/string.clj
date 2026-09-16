;; @ai-generated(guided)
(ns clojure.string
  (:refer-clojure :exclude [replace reverse]))

(defn reverse
  "Returns s with its characters reversed."
  [s] (str-reverse* s))

(defn re-quote-replacement
  "Escapes s so that it substitutes as itself in replace and replace-first."
  [s] (re-quote-replacement* s))

(defn- text
  "Clojure's (.toString s): any value stringifies, nil throws as the JVM's NPE does."
  [s]
  (if (nil? s)
    (throw (ex-info "Cannot convert nil to a string" {}))
    (if (string? s) s (str s))))

;; A replacement of another type than the match is where (replace s \x "y") throws on the JVM.
(defn- replace-with
  [what s match replacement first?]
  (let [s (text s)
        literal (if first? str-replace-first* str-replace*)]
    (cond
      (regex? match) (re-replace* match s replacement first?)
      (or (char? match) (string? match))
      (if (if (char? match) (char? replacement) (string? replacement))
        (literal s match replacement)
        (throw (ex-info (str what ": invalid replacement arg: " (pr-str replacement)) {})))
      :else (throw (ex-info (str what ": invalid match arg: " (pr-str match)) {})))))

(defn replace
  "Replaces every match of match (a pattern, string or char) in s. A pattern takes a replacement
  string in which $1 and ${name} name groups, or a function of the match as re-find returns it."
  [s match replacement] (replace-with "replace" s match replacement false))

(defn replace-first
  "Replaces the first match of match in s; see replace."
  [s match replacement] (replace-with "replace-first" s match replacement true))

(defn join
  "Returns a string of the items of coll, separated by separator (default none)."
  ([coll] (apply str coll))
  ([separator coll]
   (loop [sb "" more (seq coll) first? true]
     (if more
       (recur (str sb (if first? "" separator) (first more)) (next more) false)
       sb))))

(defn capitalize
  "Converts the first character of s to upper-case and the rest to lower-case."
  [s]
  (let [s (text s)]
    (if (< (count s) 2)
      (str-upper* s)
      (str (str-upper* (subs s 0 1)) (str-lower* (subs s 1))))))

(defn upper-case "Converts s to upper-case (ASCII letters only, NOTES.md)." [s] (str-upper* (text s)))
(defn lower-case "Converts s to lower-case (ASCII letters only, NOTES.md)." [s] (str-lower* (text s)))

;; Clojure's split takes only a pattern; the literal string and char are a deviation (NOTES.md).
(defn split
  "Splits s on separator, a pattern or a literal string or char; limit caps the number of parts."
  ([s separator] (if (regex? separator) (re-split* separator s) (str-split* s separator)))
  ([s separator limit] (if (regex? separator) (re-split* separator s limit) (str-split* s separator limit))))

(defn split-lines
  "Splits s on \\n or \\r\\n."
  [s] (str-split-lines* s))

(defn trim "Removes whitespace from both ends of s." [s] (str-trim* s))
(defn triml "Removes whitespace from the left side of s." [s] (str-triml* s))
(defn trimr "Removes whitespace from the right side of s." [s] (str-trimr* s))

(defn trim-newline
  "Removes every trailing newline or return character from s."
  [s]
  (loop [index (count s)]
    (if (zero? index)
      ""
      (let [ch (nth s (dec index))]
        (if (or (= ch \newline) (= ch \return))
          (recur (dec index))
          (subs s 0 index))))))

(defn blank?
  "True when s is nil, empty, or only whitespace."
  [s] (str-blank?* s))

(defn escape
  "Returns s with each character mapped by cmap (char → replacement) replaced; other characters stay."
  [s cmap]
  (when-not (string? s) (throw (ex-info (str "escape expects a string, got: " (type s)) {})))
  (loop [index 0 buffer ""]
    (if (= (count s) index)
      buffer
      (let [ch (nth s index)]
        (recur (inc index)
               (if-let [replacement (cmap ch)]
                 (str buffer replacement)
                 (str buffer ch)))))))

(defn index-of
  "The index of value (a string or char) in s, from from-index; nil when absent."
  ([s value] (str-index-of* s value))
  ([s value from-index] (str-index-of* s value from-index)))

(defn last-index-of
  "The last index of value (a string or char) in s, at or before from-index; nil when absent."
  ([s value] (str-last-index-of* s value))
  ([s value from-index] (str-last-index-of* s value from-index)))

(defn starts-with?
  "True when s starts with substr."
  [s substr]
  (let [s (text s)]
    (when-not (string? substr) (throw (ex-info (str "starts-with? expects a string, got: " (type substr)) {})))
    (and (<= (count substr) (count s)) (= substr (subs s 0 (count substr))))))

(defn ends-with?
  "True when s ends with substr."
  [s substr]
  (let [s (text s)]
    (when-not (string? substr) (throw (ex-info (str "ends-with? expects a string, got: " (type substr)) {})))
    (and (<= (count substr) (count s)) (= substr (subs s (- (count s) (count substr)))))))

(defn includes?
  "True when s includes substr."
  [s substr] (some? (str-index-of* s substr)))
