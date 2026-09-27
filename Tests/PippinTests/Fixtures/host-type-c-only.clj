;; A C-only host installs no resolver, so this clause must fail loudly, not quietly miss (design §4).
;; The refusal arrives mid-unwind and carries what it interrupted; the clause beside it may not swallow it.
;; FILE=Tests/PippinTests/Fixtures/host-type-c-only.clj make load-asan
(println (try (try (throw (ex-info "boom" {})) (catch Foundation/CocoaError e :cocoa) (catch :default e :swallowed))
              (catch :default e (ex-message (ex-cause e)))))
(println (try (throw (ex-info "boom" {})) (catch Foundation/CocoaError e :cocoa) (catch :default e :default)))
