;; Level 0 (design §5 «C — уровень 0»): declarations parsed out of a header by clang, interpreted and compiled.
(ns fixture.c-decls
  (:require-c [AppKit :as ak :refer [NSTextAlignmentJustified NSUnderlineStyleDouble NSAppKitVersionNumber
                                     NSBundleDidLoadNotification NSClassFromString NSHomeDirectory
                                     NSSearchPathForDirectoriesInDomains NSApplicationDirectory NSUserDomainMask
                                     NSSelectorFromString NSStringFromSelector abs free malloc strlen
                                     NSStringFromRange NSRangeFromString NSUnionRange NSStringFromPoint
                                     NSFoundationVersionNumber NSWindowDidResizeNotification]]
              [Math :header "math.h" :refer [FP_INFINITE]]
              [CljFixture :header "clj_fixture.h" :refer [CLJ_FIXTURE_CONSTANT]]))

(defn show [& xs] (apply println (map pr-str xs)))

;; An NS_ENUM value, an NS_OPTIONS value and an integer #define, by value at parse time. Justified because
;; Center and Right swap between arm64 and x86_64 (TARGET_ABI_USES_IOS_VALUES).
(show NSTextAlignmentJustified NSUnderlineStyleDouble FP_INFINITE (= NSTextAlignmentJustified ak/NSTextAlignmentJustified))

;; An extern const double: dlsym at load, so the value is the running framework's, not a number written here.
(show (number? NSAppKitVersionNumber) (pos? NSAppKitVersionNumber) (double? NSAppKitVersionNumber))

;; A constant is a number where a selector wants NSUInteger: the gap docs/notes/ios.md records, closed.
(show (.object-at-index (ns-array ["zero" "one" "two" "three"]) NSTextAlignmentJustified))

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

;; An NS_ENUM argument is its underlying integer type, which clang is asked for; the value filling it is
;; a constant of the same parse, and the NSArray comes back a handle because only strings and numbers convert.
(let [dirs (ns-array->vec (NSSearchPathForDirectoriesInDomains NSApplicationDirectory NSUserDomainMask true))]
  (show (vector? dirs) (pos? (count dirs)) (string? (first dirs))))

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

;; A struct by value is a map of the header's member names, in the registers its ABI class picks.
(show (NSStringFromRange {:location 3 :length 4}) (NSRangeFromString "{5, 6}")
      (NSUnionRange {:location 1 :length 2} {:location 10 :length 1}))
(show (NSStringFromPoint {:x 1.5 :y -2.0}) (try (NSStringFromRange {:location 3}) (catch :default e (ex-message e))))

;; A global the program may reassign is a reference: deref reads it now.
(show (number? @NSFoundationVersionNumber) (string? @NSWindowDidResizeNotification))

;; Nothing links clj_fixture.h's two symbols: each is a report line, and the module's constant still loads.
(show CLJ_FIXTURE_CONSTANT)
(doseq [[sym why] (sort-by key (:pippin/c-refused (meta (find-ns 'CljFixture))))]
  (println sym "-" why))

;; What the parse refused and why: the report is part of the product, so a wrong number cannot be silent.
(doseq [[sym why] (sort-by key (:pippin/c-refused (meta (find-ns 'AppKit))))]
  (println sym "-" why))

;; A refused name is "Unable to resolve", not a value.
(show (try (require-c '[AppKit :refer [NSLog]]) (catch :default e (ex-message e))))
(show (try (require-c '[AppKit :refer [NSNoSuchSymbolHere]]) (catch :default e (ex-message e))))
(show (try (require-c '[NotAModule :refer [x]]) (catch :default e (subs (ex-message e) 0 44))))
(show (try (require-c '[AppKit :refer :all]) (catch :default e (ex-message e))))
