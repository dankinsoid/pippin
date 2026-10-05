;; Level 0 (design §5 «C — уровень 0»): declarations parsed out of a header by clang, interpreted and compiled.
(ns fixture.c-decls
  (:require-c [AppKit :as ak :refer [NSTextAlignmentCenter NSUnderlineStyleDouble NSAppKitVersionNumber
                                     NSBundleDidLoadNotification NSClassFromString NSHomeDirectory
                                     NSSelectorFromString NSStringFromSelector abs free malloc strlen]]
              [Math :header "math.h" :refer [FP_INFINITE]]))

(defn show [& xs] (apply println (map pr-str xs)))

;; An NS_ENUM value, an NS_OPTIONS value and an integer #define, by value at parse time.
(show NSTextAlignmentCenter NSUnderlineStyleDouble FP_INFINITE (= NSTextAlignmentCenter ak/NSTextAlignmentCenter))

;; An extern const double: dlsym at load, so the value is the running framework's, not a number written here.
(show (number? NSAppKitVersionNumber) (pos? NSAppKitVersionNumber) (double? NSAppKitVersionNumber))

;; A constant is a number where a selector wants NSUInteger: the gap docs/notes/ios.md records, closed.
(show (.object-at-index (ns-array ["zero" "one" "two"]) NSTextAlignmentCenter))

;; A C function is dlsym plus the level-1 dispatcher, whose eight integer slots are all arguments here.
(show (strlen "hello") (abs -7) (NSStringFromSelector (NSSelectorFromString "countOfFoo:")))

;; A path is not a value this file can write, so what the call proves is that the process answered it.
(show (string? (NSHomeDirectory)) (pos? (count (NSHomeDirectory))))

;; Level 0 meets level 1 at the call site: a Class a C function answered is a receiver the bridge sends to.
(show (.string-with-string (NSClassFromString "NSString") "from a Class the header found")
      (nil? (NSClassFromString "NoSuchClassAnywhere")))

;; The arity is the header's, and an argument that does not fit the slot is an error, not a wrong call.
(show (try (strlen) (catch :default e (ex-message e))))
(show (try (strlen 5) (catch :default e (ex-message e))))

;; A '^' return is memory the callee owns, so the wrapper neither releases it nor takes a message.
(let [p (malloc 16)]
  (show (some? p) (try (.length p) (catch :default e (ex-message e))))
  (show (nil? (free p))))

;; A const NSString * global is read once at load and crosses as a value, as a level-1 '@' return does.
(show NSBundleDidLoadNotification (string? NSBundleDidLoadNotification))

;; Where a notification name is for: the name the header gave, through NSNotificationCenter.
(let [centre (.default-center (objc-class "NSNotificationCenter"))
      seen (atom nil)
      token (.add-observer-for-name centre NSBundleDidLoadNotification :object nil :queue nil
                                    :using-block (objc-block "v@?@" [note] (reset! seen (.name note))))]
  (.post-notification-name centre NSBundleDidLoadNotification :object nil)
  (.remove-observer centre token)
  (show @seen (= @seen NSBundleDidLoadNotification)))

;; What the parse refused and why: the report is part of the product, so a wrong number cannot be silent.
(doseq [[sym why] (sort-by key (:pippin/c-refused (meta (find-ns 'AppKit))))]
  (println sym "-" why))

;; A refused name is "Unable to resolve", not a value.
(show (try (require-c '[AppKit :refer [NSLog]]) (catch :default e (ex-message e))))
(show (try (require-c '[AppKit :refer [NSNoSuchSymbolHere]]) (catch :default e (ex-message e))))
(show (try (require-c '[NotAModule :refer [x]]) (catch :default e (subs (ex-message e) 0 44))))
(show (try (require-c '[AppKit :refer :all]) (catch :default e (ex-message e))))
