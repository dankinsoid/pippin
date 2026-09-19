// @ai-generated(guided)
import CljCore
import Pippin
import Foundation

// The namespace's publics as the EDN scripts/api-diff.clj reads (make api-diff); clojure.core without an argument.
let ns = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "clojure.core"
let source = "(do (require '" + ns + ") " + """
(vec (sort-by (fn [e] (str (:name e)))
              (map (fn [e]
                     (let [v (val e) m (meta v)]
                       {:name (key e)
                        :arglists (:arglists m)
                        :macro (boolean (:macro m))
                        :dynamic (boolean (:dynamic m))}))
                   (ns-publics '
""" + ns + ")))))"

let runtime = Runtime()
do {
	print(try runtime.eval(source).description)
} catch {
	FileHandle.standardError.write(Data("clj-api-dump: \(error)\n".utf8))
	exit(1)
}
