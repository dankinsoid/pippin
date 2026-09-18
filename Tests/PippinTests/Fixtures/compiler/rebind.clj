;; A rebound operator: its specialized users see the new root, and the boot fn bound back restores them.
(ns fixture.rebind)

(defn twice [x] (* x 2))
(defn use-twice [] (twice 21))
(defn scale [x k] (loop [i 0 s x] (if (< i k) (recur (inc i) (* s 2.0)) s)))
(defn use-scale [] (scale 1.5 2))

(println (use-twice) (use-scale) (with-redefs [* +] [(use-twice) (use-scale)]) (use-twice) (use-scale))
(def boot-mul *)
(alter-var-root #'clojure.core/* (fn [_] (fn [a b] (- a b))))
(println (use-twice) (use-scale) (with-redefs [* boot-mul] [(use-twice) (use-scale)]) (use-twice))
(alter-var-root #'clojure.core/* (fn [_] boot-mul))
(println (use-twice) (use-scale) (with-redefs [* -] (use-twice)) (use-twice))
