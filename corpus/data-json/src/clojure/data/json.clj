;; Copyright (c) Stuart Sierra, 2012. All rights reserved.  The use
;; and distribution terms for this software are covered by the Eclipse
;; Public License 1.0 (http://opensource.org/licenses/eclipse-1.0.php)
;; which can be found in the file epl-v10.html at the root of this
;; distribution.  By using this software in any fashion, you are
;; agreeing to be bound by the terms of this license.  You must not
;; remove this notice, or any other, from this software.

(ns ^{:author "Stuart Sierra"
      :doc "JavaScript Object Notation (JSON) parser/generator.
  See http://www.json.org/"}
  clojure.data.json
  (:refer-clojure :exclude (read))
  ;; pippin: no clojure.pprint or java.io here: read, write and pprint go; read-str and write-str stay.
  )

;; CUSTOM PUSHBACK READER

;; pippin: *warn-on-reflection* names the JVM compiler (design §8); its two set! forms go.

;; pippin: definterface is refused (design §8): a protocol, called as functions. No char[] window or
;; String(char[]): read-quoted-string reads the window in place (position, charAt, substring).
(defprotocol InternalPBR
  (readChar [this])
  (readChars [this bufflen])
  (unreadChar [this c])
  (unreadChars [this bufflen])
  (toReader [this])
  (position [this])
  (charAt [this i])
  (substring [this start end]))

;; pippin: ReaderPBR, the stream over a java.io.PushbackReader, goes with read.

(comment
  (compile 'clojure.data.json)
  )

;; pippin: no mutable fields (docs/notes/type-descriptor.md): pos is the one slot of a long array.
(deftype StringPBR [^String s pos ^long len]
  InternalPBR
  (readChar [_]
    (if (< (aget pos 0) len)
      (let [p (aget pos 0)]
        (aset pos 0 (unchecked-inc p))
        (let [c (int (nth s p))]
          c))
      (let [i (int -1)]
        i)))
  (readChars [_ bufflen]
    (let [p (aget pos 0)
          remaining (- len p)
          n (Math/min remaining bufflen)]
      (when (pos? n)
        (aset pos 0 (+ p n)))
      (if (pos? n) n -1)))
  (unreadChar [_ _c]
    ;; ASSERT: c should never be -1 (EOF)
    (aset pos 0 (unchecked-dec (aget pos 0)))
    nil)
  (unreadChars [_ bufflen]
    (aset pos 0 (unchecked-subtract (aget pos 0) bufflen))
    nil)
  (toReader [_]
    (subs s (aget pos 0) len))
  (position [_]
    (aget pos 0))
  (charAt [_ i]
    (nth s i))
  (substring [_ start end]
    (subs s start end)))

(defn- string-pbr
  [^String s]
  (->StringPBR s (long-array 1) (count s)))

;; pippin: no StringBuilder: a string in a volatile, which (vswap! v str x) appends to in place while the volatile
;; alone holds it.
(defn- string-builder
  ([] (volatile! ""))
  ([s] (volatile! s)))

(defn- append [sb x]
  (vswap! sb str x)
  sb)

(defn- builder-str [sb]
  @sb)

;; pippin: no JVM exception classes: Exception. and EOFException. are ex-info.
(defn- json-error [msg]
  (ex-info msg {}))

;;; JSON READER

(defn- default-write-key-fn
  [x]
  (cond (or (keyword? x) (symbol? x))
        (name x)
        (nil? x)
        (throw (json-error "JSON object properties may not be nil"))
        :else (str x)))

(defn- default-value-fn [k v] v)

(declare -read)

(defmacro ^:private codepoint [c]
  (int c))

(defn- codepoint-clause [[test result]]
  (cond (list? test)
        [(map int test) result]
        (= test :whitespace)
        ['(9 10 13 32) result]
        (= test :js-separators)
        ['(16r2028 16r2029) result]
        :else
        [(int test) result]))

(defmacro ^:private codepoint-case [e & clauses]
  `(case ~e
     ~@(mapcat codepoint-clause (partition 2 clauses))
     ~@(when (odd? (count clauses))
         [(last clauses)])))

(defn- read-hex-char [stream]
  ;; Expects to be called with the head of the stream AFTER the
  ;; initial "\u".  Reads the next four characters from the stream.
  (let [a (readChar stream)
        b (readChar stream)
        c (readChar stream)
        d (readChar stream)]
    (when (or (neg? a) (neg? b) (neg? c) (neg? d))
      (throw (json-error
              "JSON error (end-of-file inside Unicode character escape)")))
    (let [s (str (char a) (char b) (char c) (char d))]
      (char (Integer/parseInt s 16)))))

(defn- read-escaped-char [stream]
  ;; Expects to be called with the head of the stream AFTER the
  ;; initial backslash.
  (let [c (readChar stream)]
    (when (neg? c)
      (throw (json-error "JSON error (end-of-file inside escaped char)")))
    (codepoint-case c
      (\" \\ \/) (char c)
      \b \backspace
      \f \formfeed
      \n \newline
      \r \return
      \t \tab
      \u (read-hex-char stream))))

(defn- slow-read-string [stream ^String already-read]
  (let [buffer (string-builder already-read)]
    (loop []
      (let [c (readChar stream)]
        (when (neg? c)
          (throw (json-error "JSON error (end-of-file inside string)")))
        (codepoint-case c
          \" (builder-str buffer)
          \\ (do (append buffer (read-escaped-char stream))
                 (recur))
          (do (append buffer (char c))
              (recur)))))))

(defn- read-quoted-string [stream]
  ;; Expects to be called with the head of the stream AFTER the
  ;; opening quotation mark.
  (let [start (position stream)
        read (readChars stream 64)
        end-index (unchecked-dec-int read)]
    (when (neg? read)
      (throw (json-error "JSON error (end-of-file inside string)")))
    (loop [i (int 0)]
      (let [c (int (charAt stream (+ start i)))]
        (codepoint-case c
          \" (let [off (unchecked-inc-int i)
                   len (unchecked-subtract-int read off)]
               (unreadChars stream len)
               (substring stream start (+ start i)))
          \\ (let [off i
                   len (unchecked-subtract-int read off)]
               (unreadChars stream len)
               (slow-read-string stream (substring stream start (+ start i))))
          (if (= i end-index)
            (do (unreadChar stream c)
                (slow-read-string stream (substring stream start (+ start i))))
            (recur (unchecked-inc-int i))))))))

(defn- read-integer [^String string]
  (if (< (count string) 18)  ; definitely fits in a Long
    (Long/valueOf string)
    (or (try (Long/valueOf string)
             (catch NumberFormatException e nil))
        (bigint string))))

(defn- read-decimal [^String string bigdec?]
  (if bigdec?
    (bigdec string)
    (Double/valueOf string)))

(defn- read-number [stream bigdec?]
  (let [buffer (string-builder)
        decimal? (loop [stage :minus]
                   (let [c (readChar stream)]
                     (case stage
                       :minus
                       (codepoint-case c
                         \-
                         (do (append buffer (char c))
                             (recur :int-zero))
                         \0
                         (do (append buffer (char c))
                             (recur :frac-point))
                         (\1 \2 \3 \4 \5 \6 \7 \8 \9)
                         (do (append buffer (char c))
                             (recur :int-digit))
                         (throw (json-error "JSON error (invalid number literal)")))
                       ;; Number must either be a single 0 or 1-9 followed by 0-9
                       :int-zero
                       (codepoint-case c
                         \0
                         (do (append buffer (char c))
                             (recur :frac-point))
                         (\1 \2 \3 \4 \5 \6 \7 \8 \9)
                         (do (append buffer (char c))
                             (recur :int-digit))
                         (throw (json-error "JSON error (invalid number literal)")))
                       ;; at this point, there is at least one digit
                       :int-digit
                       (codepoint-case c
                         (\0 \1 \2 \3 \4 \5 \6 \7 \8 \9)
                         (do (append buffer (char c))
                             (recur :int-digit))
                         \.
                         (do (append buffer (char c))
                             (recur :frac-first))
                         (\e \E)
                         (do (append buffer (char c))
                             (recur :exp-symbol))
                         ;; early exit
                         :whitespace
                         (do (unreadChar stream c)
                             false)
                         (\, \] \})
                         (do (unreadChar stream c)
                             false)
                         -1
                         false
                         (throw (json-error "JSON error (invalid number literal)")))
                       ;; previous character is a "0"
                       :frac-point
                       (codepoint-case c
                         \.
                         (do (append buffer (char c))
                             (recur :frac-first))
                         (\e \E)
                         (do (append buffer (char c))
                             (recur :exp-symbol))
                         ;; early exit
                         :whitespace
                         (do (unreadChar stream c)
                             false)
                         (\, \] \})
                         (do (unreadChar stream c)
                             false)
                         -1
                         false
                         ;; Disallow zero-padded numbers or invalid characters
                         (throw (json-error "JSON error (invalid number literal)")))
                       ;; previous character is a "."
                       :frac-first
                       (codepoint-case c
                         (\0 \1 \2 \3 \4 \5 \6 \7 \8 \9)
                         (do (append buffer (char c))
                             (recur :frac-digit))
                         (throw (json-error "JSON error (invalid number literal)")))
                       ;; any number of following digits
                       :frac-digit
                       (codepoint-case c
                         (\0 \1 \2 \3 \4 \5 \6 \7 \8 \9)
                         (do (append buffer (char c))
                             (recur :frac-digit))
                         (\e \E)
                         (do (append buffer (char c))
                             (recur :exp-symbol))
                         ;; early exit
                         :whitespace
                         (do (unreadChar stream c)
                             true)
                         (\, \] \})
                         (do (unreadChar stream c)
                             true)
                         -1
                         true
                         (throw (json-error "JSON error (invalid number literal)")))
                       ;; previous character is a "e" or "E"
                       :exp-symbol
                       (codepoint-case c
                         (\- \+)
                         (do (append buffer (char c))
                             (recur :exp-first))
                         (\0 \1 \2 \3 \4 \5 \6 \7 \8 \9)
                         (do (append buffer (char c))
                             (recur :exp-digit)))
                       ;; previous character is a "-" or "+"
                       ;; must have at least one digit
                       :exp-first
                       (codepoint-case c
                         (\0 \1 \2 \3 \4 \5 \6 \7 \8 \9)
                         (do (append buffer (char c))
                             (recur :exp-digit))
                         (throw (json-error "JSON error (invalid number literal)")))
                       ;; any number of following digits
                       :exp-digit
                       (codepoint-case c
                         (\0 \1 \2 \3 \4 \5 \6 \7 \8 \9)
                         (do (append buffer (char c))
                             (recur :exp-digit))
                         :whitespace
                         (do (unreadChar stream c)
                             true)
                         (\, \] \})
                         (do (unreadChar stream c)
                             true)
                         -1
                         true
                         (throw (json-error "JSON error (invalid number literal)"))))))]
    (if decimal?
      (read-decimal (builder-str buffer) bigdec?)
      (read-integer (builder-str buffer)))))

(defn- next-token [stream]
  (loop [c (readChar stream)]
    (if (< 32 c)
      (int c)
      (codepoint-case (int c)
        :whitespace (recur (readChar stream))
        c))))

(defn invalid-array-exception []
  (json-error "JSON error (invalid array)"))

(defn- eof-array-exception []
  (json-error "JSON error (EOF in array)"))

(defn- read-array* [stream options]
  ;; Handles all array values after the first.
  (loop [result (transient [])]
    (let [r (conj! result (-read stream true nil options))]
      (codepoint-case (int (next-token stream))
        \] (persistent! r)
        \, (recur r)
        -1 (throw (eof-array-exception))
        (throw (invalid-array-exception))))))

(defn- read-array [stream options]
  ;; Expects to be called with the head of the stream AFTER the
  ;; opening bracket.
  ;; Only handles array value.
  (let [c (int (next-token stream))]
    (codepoint-case c
      \] []
      \, (throw (invalid-array-exception))
      -1 (throw (eof-array-exception))
      (do (unreadChar stream c)
          (read-array* stream options)))))

(defn- object-colon-exception []
  (json-error "JSON error (missing `:` in object)"))

(defn- eof-object-exception []
  (json-error "JSON error (EOF in object)"))

(defn- invalid-key-exception [c]
  (if (= c -1)
    (throw (eof-object-exception))
    (throw (json-error (str "JSON error (non-string key in object), found `" (char c) "`, expected `\"`")))))

(comment
  (compile 'clojure.data.json)
  )

(defn- read-key [stream]
  (let [c (int (next-token stream))]
    (if (= c (codepoint \"))
      (let [key (read-quoted-string stream)]
        (if (= (codepoint \:) (int (next-token stream)))
          key
          (throw (object-colon-exception))))
      (if (= c (codepoint \}))
        nil
        (invalid-key-exception c)))))

(defn- read-object [stream options]
  ;; Expects to be called with the head of the stream AFTER the
  ;; opening bracket.
  (let [key-fn (get options :key-fn)
        value-fn (get options :value-fn)]
    (loop [result (transient {})]
      (if-let [key (read-key stream)]
        (let [key (cond-> key key-fn key-fn)
              value (-read stream true nil options)
              r (if value-fn
                  (let [out-value (value-fn key value)]
                    (if-not (= value-fn out-value)
                      (assoc! result key out-value)
                      result))
                  (assoc! result key value))]
          (codepoint-case (int (next-token stream))
            \, (recur r)
            \} (persistent! r)
            -1 (throw (eof-object-exception))
            (throw (json-error "JSON error (missing entry in object)"))))
        (let [r (persistent! result)]
          (if (empty? r)
            r
            (throw (json-error "JSON error empty entry in object is not allowed"))))))))

(defn- -read
  [stream eof-error? eof-value options]
  (let [c (int (next-token stream))]
    (codepoint-case c
        ;; Read numbers
        (\- \0 \1 \2 \3 \4 \5 \6 \7 \8 \9)
        (do (unreadChar stream c)
            (read-number stream (:bigdec options)))

        ;; Read strings
        \" (read-quoted-string stream)

        ;; Read null as nil
        \n (if (and (= (codepoint \u) (readChar stream))
                    (= (codepoint \l) (readChar stream))
                    (= (codepoint \l) (readChar stream)))
             nil
             (throw (json-error "JSON error (expected null)")))

        ;; Read true
        \t (if (and (= (codepoint \r) (readChar stream))
                    (= (codepoint \u) (readChar stream))
                    (= (codepoint \e) (readChar stream)))
             true
             (throw (json-error "JSON error (expected true)")))

        ;; Read false
        \f (if (and (= (codepoint \a) (readChar stream))
                    (= (codepoint \l) (readChar stream))
                    (= (codepoint \s) (readChar stream))
                    (= (codepoint \e) (readChar stream)))
             false
             (throw (json-error "JSON error (expected false)")))

        ;; Read JSON objects
        \{ (read-object stream options)

        ;; Read JSON arrays
        \[ (read-array stream options)

        (if (neg? c) ;; Handle end-of-stream
          (if eof-error?
            (throw (json-error "JSON error (end-of-file)"))
            eof-value)
          (throw (json-error
                  (str "JSON error (unexpected character): " (char c))))))))

(defn- -read1
  [stream eof-error? eof-value options]
  (let [val (-read stream eof-error? eof-value options)]
    (if-let [extra-data-fn (:extra-data-fn options)]
      (if (or eof-error? (not (identical? eof-value val)))
        (let [c (readChar stream)]
          (if (neg? c)
            val
            (do
              (unreadChar stream c)
              (extra-data-fn val (toReader stream)))))
        val)
      val)))

(defn on-extra-throw
  "Pass as :extra-data-fn to `read` or `read-str` to throw if data is found
  after the first object."
  [val rdr]
  (throw (ex-info "Found extra data after json object" {:val val})))

(defn on-extra-throw-remaining
  "Pass as :extra-data-fn to `read` or `read-str` to throw if data is found
  after the first object and return the remaining data in ex-data :remaining."
  [val rdr]
  ;; pippin: the stream's toReader is the rest of its string, not a Reader to slurp.
  (let [remaining rdr]
    (throw (ex-info (str "Found extra data after json object: " remaining)
             {:val val, :remaining remaining}))))

(def default-read-options {:bigdec false
                           :key-fn nil
                           :value-fn nil})

;; pippin: read, over a java.io.Reader, is not carried.

(defn read-str
  "Reads one JSON value from input String. Options are the same as for
  read."
  [string & {:as options}]
  (let [{:keys [eof-error? eof-value]
         :or {eof-error? true}} options]
    (->> options
         (merge default-read-options)
         (-read1 (string-pbr string) eof-error? eof-value))))

;;; JSON WRITER


(defprotocol JSONWriter
  (-write [object out options]
    "Print object to Appendable out as JSON"))

(defn- ->hex-string [out cp]
  (let [cpl (long cp)]
    (append out "\\u")
    (cond
      (< cpl 16)
      (append out "000")
      (< cpl 256)
      (append out "00")
      (< cpl 4096)
      (append out "0"))
    (append out (Integer/toHexString cp))))

(def ^{:tag "[S"} codepoint-decoder
  (let [shorts (short-array 128)]
    (dotimes [i 128]
      (codepoint-case i
        \" (aset shorts i (short 1))
        \\ (aset shorts i (short 1))
        \/ (aset shorts i (short 2))
        \backspace (aset shorts i (short 3))
        \formfeed  (aset shorts i (short 4))
        \newline   (aset shorts i (short 5))
        \return    (aset shorts i (short 6))
        \tab       (aset shorts i (short 7))
        (if (< i 32)
          (aset shorts i (short 8))
          (aset shorts i (short 0)))))
    shorts))

(defn- slow-write-string [^CharSequence s out options]
  (let [decoder codepoint-decoder
        slash (get options :escape-slash)
        escape-js-separators (get options :escape-js-separators)
        escape-unicode (get options :escape-unicode)]
    (dotimes [i (count s)]
      (let [cp (int (nth s i))]
        (if (< cp 128)
          (case (aget decoder cp)
            0 (append out (char cp))
            1 (do (append out (char (codepoint \\))) (append out (char cp)))
            2 (append out (if slash "\\/" "/"))
            3 (append out "\\b")
            4 (append out "\\f")
            5 (append out "\\n")
            6 (append out "\\r")
            7 (append out "\\t")
            8 (->hex-string out cp))
          (codepoint-case cp
            :js-separators (if escape-js-separators
                             (->hex-string out cp)
                             (append out (char cp)))
            (if escape-unicode
              (->hex-string out cp) ; Hexadecimal-escaped
              (append out (char cp)))))))))

(defn- write-string [^CharSequence s out options]
  (let [decoder codepoint-decoder
        l (count s)]
    (append out \")
    (loop [i 0]
      (if (= i l)
        (append out s)
        (let [cp (int (nth s i))]
          (if (and (< cp 128)
                   (zero? (aget decoder cp)))
            (recur (unchecked-inc i))
            (do
              (append out (subs s 0 i))
              (slow-write-string (subs s i l) out options))))))
    (append out \")))

(defn- write-indent [out options]
  (let [indent-depth (:indent-depth options)]
    (append out \newline)
    (loop [i indent-depth]
      (when (pos? i)
        (append out "  ")
        (recur (dec i))))))

(defn- write-object [m out options]
  (let [key-fn (get options :key-fn)
        value-fn (get options :value-fn)
        indent (get options :indent)
        opts (cond-> options
               indent (update :indent-depth inc))]
    (append out \{)
    (when (and indent (seq m))
      (write-indent out opts))
    (loop [x m, have-printed-kv false]
      (when (seq x)
        (let [[k v] (first x)
              out-key (key-fn k)
              out-value (value-fn k v)
              nxt (next x)]
          (when-not (string? out-key)
            (throw (json-error "JSON object keys must be strings")))
          (if-not (= value-fn out-value)
            (do
              (when have-printed-kv
                (append out \,)
                (when indent
                  (write-indent out opts)))
              (write-string out-key out opts)
              (append out \:)
              (when indent
                (append out \space))
              (-write out-value out opts)
              (when (seq nxt)
                (recur nxt true)))
            (when (seq nxt)
              (recur nxt have-printed-kv))))))
    (when (and indent (seq m))
      (write-indent out options)))
  (append out \}))

(defn- write-array [s out options]
  (let [indent (get options :indent)
        opts (cond-> options
                  indent (update :indent-depth inc))]
    (append out \[)
    (when (and indent (seq s))
      (write-indent out opts))
    (loop [x s]
      (when (seq x)
        (let [fst (first x)
              nxt (next x)]
          (-write fst out opts)
          (when (seq nxt)
            (append out \,)
            (when indent
              (write-indent out opts))
            (recur nxt)))))
    (when (and indent (seq s))
      (write-indent out options)))
  (append out \]))

(defn- write-bignum [x out options]
  (append out (str x)))

;; pippin: no Float here (docs/jvm-differences.md): write-float goes with its extension.

(defn- write-double [^Double x out options]
  (cond (infinite? x)
        (throw (json-error "JSON error: cannot write infinite Double"))
        (NaN? x)
        (throw (json-error "JSON error: cannot write Double NaN"))
        :else
        (append out (str x))))

(defn- write-plain [x out options]
  (append out (str x)))

(defn- write-uuid [^java.util.UUID x out options]
  (append out \")
  (append out (str x))
  (append out \"))

;; pippin: no java.time and no java.sql: write-instant, write-date and write-sql-date go, and an #inst is
;; written by :default-write-fn.

(defn- write-null [x out options]
  (append out "null"))

(defn- write-named [x out options]
  (write-string (name x) out options))

;; pippin: (class x) is refused (design §8); every array kind is one type here.
(def ^:private array-type (type (object-array 0)))

(defn- write-generic [x out options]
  (if (identical? array-type (type x))
    (-write (seq x) out options)
    ((:default-write-fn options) x out options)))

(defn- write-ratio [x out options]
  (-write (double x) out options))

;; pippin: the JVM classes as this runtime's types (corpus/data-json/SOURCE); a ratio, bigint and decimal have no
;; type name, so a value gives theirs.

;; nil, true, false
(extend nil                    JSONWriter {:-write write-null})
(extend Boolean                JSONWriter {:-write write-plain})

;; Numbers
(extend Long                   JSONWriter {:-write write-plain})
(extend Double                 JSONWriter {:-write write-double})
(extend (type 1/2)             JSONWriter {:-write write-ratio})
(extend (type (inc' Long/MAX_VALUE)) JSONWriter {:-write write-bignum})
(extend (type 1M)              JSONWriter {:-write write-bignum})
(extend UUID                   JSONWriter {:-write write-uuid})

;; Symbols, Keywords, and Strings
(extend Keyword                JSONWriter {:-write write-named})
(extend Symbol                 JSONWriter {:-write write-named})
(extend String                 JSONWriter {:-write write-string})

;; Collections
(extend clojure.lang.IPersistentMap        JSONWriter {:-write write-object})
(extend clojure.lang.IPersistentCollection JSONWriter {:-write write-array})

;; Maybe a Java array, otherwise fail
(extend Object                 JSONWriter {:-write write-generic})

(defn- default-write-fn [x out options]
  (throw (json-error (str "Don't know how to write JSON of " (type x)))))

(def default-write-options {:escape-unicode true
                            :escape-js-separators true
                            :escape-slash true
                            :key-fn default-write-key-fn
                            :value-fn default-value-fn
                            :default-write-fn default-write-fn
                            :indent false
                            :indent-depth 0 ;; internal, to track nesting depth
                            })

;; pippin: write, to a java.io.Writer, is not carried; write-str takes its options.

(defn write-str
  "Converts x to a JSON-formatted string. Options are the same as
  write."
  ^String [x & {:as options}]
  (let [sw (string-builder)]
    (-write x sw (merge default-write-options options))
    (builder-str sw)))

;;; JSON PRETTY-PRINTER

;; pippin: pprint, over clojure.pprint, is not carried.

;; DEPRECATED APIs from 0.1.x

(defn read-json
  "DEPRECATED; replaced by read-str.

  Reads one JSON value from input String or Reader. If keywordize? is
  true (default), object keys will be converted to keywords. If
  eof-error? is true (default), empty input will throw an
  EOFException; if false EOF will return eof-value."
  ([input]
   (read-json input true true nil))
  ([input keywordize?]
   (read-json input keywordize? true nil))
  ([input keywordize? eof-error? eof-value]
   (let [key-fn (if keywordize? keyword identity)]
     ;; pippin: a String only; read, over a java.io.Reader, is not carried.
     (condp instance? input
       String
       (read-str input
         :key-fn key-fn
         :eof-error? eof-error?
         :eof-value eof-value)))))

;; pippin: write-json and print-json write to a java.io.Writer, pprint-json through pprint: not carried.

(defn json-str
  "DEPRECATED; replaced by 'write-str'.

  Converts x to a JSON-formatted string.

  Valid options are:
    :escape-unicode false
        to turn of \\uXXXX escapes of Unicode characters."
  [x & options]
  (apply write-str x options))
