// @ai-generated(guided)
import CljCore
import Clojure
import Foundation

// Dumps this runtime's clojure.core to stdout as the EDN scripts/api-diff.clj reads: a vector of
// {:name :arglists :macro :dynamic} sorted by name. `make api-diff` runs it against the JVM's dump.
let source = """
(vec (sort-by (fn [e] (str (:name e)))
              (map (fn [e]
                     (let [v (val e) m (meta v)]
                       {:name (key e)
                        :arglists (:arglists m)
                        :macro (boolean (:macro m))
                        :dynamic (boolean (:dynamic m))}))
                   (ns-publics 'clojure.core))))
"""

let runtime = Runtime()
do {
	print(try runtime.eval(source).description)
} catch {
	FileHandle.standardError.write(Data("clj-api-dump: \(error)\n".utf8))
	exit(1)
}
