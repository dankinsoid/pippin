## Open decisions

- **File extension and reader-conditional key.** Source stays `.clj` (`.cljc` for portable user
  code) until the project has a name; the key in `#?(:key …)` and the extension are the same word
  and permanent, and they should name the runtime (portable C core), not Apple or Swift. Reader
  conditionals are in (the reader takes any feature set), so the default set is `#{:default}` alone until
  the key exists; the corpus harness reads medley with `#{:clj}` so its JVM branches surface as
  resolution errors in the backlog rather than as silently empty bodies. A separate extension for files that
  call Swift is refused from the other side of the same question (design §5): clojure-lsp, cljfmt and editor
  highlighting all key off `.clj`/`.cljc`/`.cljs`, so the declared host boundary is a `require-swift` form in
  `ns` instead.


