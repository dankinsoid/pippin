;; Names the parity work added, in compiled code: a .method send reaching a deftype's protocol method, print-method, with-open, the -int twins, chunks and trailing-map kwargs.
(ns fixture.parity)

(defprotocol Res (-close [r]) (-readLine [r]))
(deftype Src [lines closed]
  Res
  (-close [_] (vreset! closed true))
  (-readLine [_] (let [[l & more] @lines] (vreset! lines more) l)))
(deftype Pt [x])
(defmethod print-method Pt [p w] (.write w "#pt"))

(defn kw-args [& {:keys [a b]}] [a b])

(defn run []
  (let [closed (volatile! false)]
    [(with-open [r (->Src (volatile! ["a" "b"]) closed)] (doall (line-seq r)))
     @closed
     (pr-str [(->Pt 1)])
     (with-out-str (.write *out* "w") (.write *out* 33))
     (unchecked-add-int 2147483647 1)
     (unchecked-byte 300)
     (unchecked-char 65)
     (vec (chunk-cons (chunk (doto (chunk-buffer 2) (chunk-append 1) (chunk-append 2))) [3]))
     (= (hash [1 2]) (hash-ordered-coll [1 2]))
     (replace {1 :a} [1 2])
     (kw-args :a 1 {:b 2})
     (with-precision 3 (/ 2M 3))]))
(prn (run))
