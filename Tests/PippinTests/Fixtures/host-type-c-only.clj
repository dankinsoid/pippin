;; A C-only host installs no resolver, so this clause must fail loudly, not quietly miss (design §4).
;; FILE=Tests/PippinTests/Fixtures/host-type-c-only.clj make load-asan
(println (try (throw (ex-info "boom" {})) (catch Foundation/CocoaError e :cocoa) (catch :default e :default)))
