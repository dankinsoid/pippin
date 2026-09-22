// @ai-generated(solo)
import Pippin

/// `complete`/`info`, written in Clojure over `ns-map`/`ns-resolve`/`meta` rather than reimplemented in Swift.
final class NReplHelpers {
	let complete: Value
	let info: Value

	init(_ rt: Runtime) throws {
		_ = try rt.eval(Self.source)
		complete = try rt.eval("pippin.nrepl.util/complete*")
		info = try rt.eval("pippin.nrepl.util/info*")
	}

	private static let source = """
	(ns pippin.nrepl.util)
	(require 'clojure.string)

	(defn- target-ns [ns-name]
	  (or (find-ns (symbol ns-name)) (the-ns 'user)))

	(defn complete*
	  "{:candidate :ns} maps: symbols visible in ns-name whose name starts with prefix."
	  [ns-name prefix]
	  (let [cur (target-ns ns-name)
	        slash (clojure.string/index-of prefix "/")]
	    (vec
	     (sort-by :candidate
	       (if slash
	         (let [alias (subs prefix 0 slash)
	               pfx (subs prefix (inc slash))
	               target (or (get (ns-aliases cur) (symbol alias)) (find-ns (symbol alias)))]
	           (if target
	             (for [[sym _] (ns-publics target)
	                   :when (clojure.string/starts-with? (name sym) pfx)]
	               {:candidate (str alias "/" (name sym)) :ns (str (ns-name target))})
	             []))
	         (for [[sym v] (ns-map cur)
	               :when (and (symbol? sym) (clojure.string/starts-with? (name sym) prefix))]
	           {:candidate (name sym) :ns (str (if (var? v) (:ns (meta v)) (ns-name cur)))}))))))

	(defn- describe-var [v]
	  (let [m (meta v)]
	    (cond-> {:ns (str (:ns m)) :name (str (:name m))}
	      (:doc m) (assoc :doc (:doc m))
	      (:arglists m) (assoc :arglists-str (pr-str (:arglists m)))
	      (:line m) (assoc :line (:line m))
	      (:column m) (assoc :column (:column m))
	      (:macro m) (assoc :macro true))))

	(defn info*
	  "describe-var of sym-name resolved from ns-name, or nil when it is not a var."
	  [ns-name sym-name]
	  (let [v (ns-resolve (target-ns ns-name) (symbol sym-name))]
	    (when (var? v) (describe-var v))))
	"""
}
