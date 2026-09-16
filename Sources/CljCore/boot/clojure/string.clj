;; @ai-generated(guided)
;; Patterns are literal strings or chars: the runtime has no regex engine (NOTES.md).
(ns clojure.string
  (:refer-clojure :exclude [replace reverse]))

(defn reverse
  "Returns s with its characters reversed."
  [s] (str-reverse* s))

(defn replace
  "Replaces every instance of match (a string or char) with replacement (a string or char) in s."
  [s match replacement] (str-replace* s match replacement))

(defn replace-first
  "Replaces the first instance of match (a string or char) with replacement in s."
  [s match replacement] (str-replace-first* s match replacement))

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
  (if (< (count s) 2)
    (str-upper* s)
    (str (str-upper* (subs s 0 1)) (str-lower* (subs s 1)))))

(defn upper-case "Converts s to upper-case (ASCII letters only, NOTES.md)." [s] (str-upper* s))
(defn lower-case "Converts s to lower-case (ASCII letters only, NOTES.md)." [s] (str-lower* s))

(defn split
  "Splits s on separator, a literal string (not a regex, NOTES.md); limit caps the number of parts."
  ([s separator] (str-split* s separator))
  ([s separator limit] (str-split* s separator limit)))

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
  [s substr] (and (<= (count substr) (count s)) (= substr (subs s 0 (count substr)))))

(defn ends-with?
  "True when s ends with substr."
  [s substr] (and (<= (count substr) (count s)) (= substr (subs s (- (count s) (count substr))))))

(defn includes?
  "True when s includes substr."
  [s substr] (some? (str-index-of* s substr)))
