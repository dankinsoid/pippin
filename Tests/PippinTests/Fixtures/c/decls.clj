;; Level 0 (design §5 «C — уровень 0»): declarations parsed out of a header by clang, interpreted and compiled.
(ns fixture.c-decls
  (:require-c [AppKit :as ak :refer [NSTextAlignmentCenter NSUnderlineStyleDouble NSAppKitVersionNumber]]
              [Math :header "math.h" :refer [FP_INFINITE]]))

(defn show [& xs] (apply println (map pr-str xs)))

;; An NS_ENUM value, an NS_OPTIONS value and an integer #define, by value at parse time.
(show NSTextAlignmentCenter NSUnderlineStyleDouble FP_INFINITE (= NSTextAlignmentCenter ak/NSTextAlignmentCenter))

;; An extern const double: dlsym at load, so the value is the running framework's, not a number written here.
(show (number? NSAppKitVersionNumber) (pos? NSAppKitVersionNumber) (double? NSAppKitVersionNumber))

;; A constant is a number where a selector wants NSUInteger: the gap docs/notes/ios.md records, closed.
(show (.object-at-index (ns-array ["zero" "one" "two"]) NSTextAlignmentCenter))

;; What the parse refused and why: the report is part of the product, so a wrong number cannot be silent.
(doseq [[sym why] (sort-by key (:pippin/c-refused (meta (find-ns 'AppKit))))]
  (println sym "-" why))

;; A refused name is "Unable to resolve", not a value.
(show (try (require-c '[AppKit :refer [NSLog]]) (catch :default e (ex-message e))))
(show (try (require-c '[AppKit :refer [NSNoSuchSymbolHere]]) (catch :default e (ex-message e))))
(show (try (require-c '[NotAModule :refer [x]]) (catch :default e (subs (ex-message e) 0 44))))
(show (try (require-c '[AppKit :refer :all]) (catch :default e (ex-message e))))
