## Diagnostics (Sources/CljCore/diagnostic.c, the position half in analyzer.c, load.c, reader.c)

- **The structure is in the ex-data and the rendering is outside it** (design §3 «Диагностика»), so one
  renderer serves every consumer and a test asserts the data while a snapshot covers the text. The keys
  are the rich meta's (design §4 «Локация в коде»): `:file :line :column :end-line :end-column`, plus
  `:form-line`/`:form-column` (the enclosing top-level form), `:suggestion`, and `:arities`/`:variadic`/
  `:given`/`:fn` on an arity error. Nothing was renamed or dropped: `:file`, `:line` and `:column` are
  the keys a `catch` plus `ex-data` already read, and what moved is their value — the innermost
  positioned form rather than the top-level one.
- **The reported position is the deepest link of the cause chain whose data carries a `:line` *and* a
  `:file`** (`clj_diagnostic_position`, `wrap_pending`). The file is the condition, not a nicety: a node
  carries a line and no file of its own, so a line taken from a call site inside an already-loaded
  function, joined to the file the loader is reading, would name a line of the wrong file and the
  excerpt would quote it. A deeper position without a file still shows as a `raised at L:C` note.
- **Every non-empty list the reader positions now carries `:end-line`/`:end-column`** as well, the column
  after the closing delimiter, as tools.reader and ClojureScript write it. JVM Clojure's `LispReader`
  writes only `:line`/`:column`, so `(meta '(a b))` differs there (docs/jvm-differences.md); the design
  names the richer meta as the target and cljs as the precedent. The cost is two more entries in the one
  map per list, built in a single `clj_map_from_items` rather than four `assoc` steps.
- **A compiled unit's constant pool reads with positions off** (`clj_reader_no_positions`,
  `clj_c_const`). The pool's text is not the user's source, so the reader was attaching the position of
  the constant *inside the pool* — `(meta '(inc 1))` answered `{:line 1 :column 1}` in a compiled unit
  against the real position interpreted. docs/notes/compiler.md says a quoted list's position is nil
  compiled and set interpreted; it now is, and the flag also saves the map per constant list.
- **An arity error names the counts a call may pass, not the overloads as written**, and it reads them
  through `clj_fn_accepts` rather than off the fn's fields. A compiled closure keeps only its lowest
  arity where an interpreted one keeps the rest arity's own count (`fn_arity_bounds`, compiler.c), so the
  fields answer differently in the two backends and the shared predicate does not: `(defn f ([a] a) ([a
  b] b) ([a b & r] r))` is "takes at least 1" either way. `Fixtures/compiler/diagnostics.clj` is the
  differential, and it was what caught the divergence.
- [~] **An arity error has no position.** The interpreter refuses at the call site, which it has, and a
  compiled fn refuses inside its own dispatcher, which has no caller; attaching the interpreter's would
  plant exactly the backend divergence the fixture exists to catch. Trigger: `:file` on a node (design §4
  «Локация в коде»), or the arity check moved to the call site in both backends — threading a site
  through `clj_c_invoke` changes the emitter and so regenerates the committed compiled core.
- [~] **A macro's arity error counts `&form` and `&env`**, so `(defn)` is "Wrong number of args (2) …
  which takes at least 3" where a reader passed none and may pass one or more. The count was the
  expander's before the arities were added and the arities inherit its offset. Trigger: the macro bit
  reaching the throw — it is on the var, and `clj_arity_error` holds only the fn.
- [ ] **`clj_fn_accepts` over-accepts in a compiled unit** for a fn whose rest arity takes more fixed
  parameters than one of its fixed arities (`([a] …) ([a b c & r] …)` answers true for 2), because
  `fn_arity_bounds` (compiler.c) keeps one minimum for the fixed and the rest arity. The call still throws,
  in the dispatcher, so the only visible effect is on the arities: the counts would name 2 as accepted
  beside a refusal of 2, so `arities_disagree` drops them whole and the compiled message names none where
  the interpreted one says "1 or at least 3". Fix: emit the rest arity's own count as `min`, the mask
  already carrying the fixed ones. Trigger: a `make boot`, since the committed compiled core holds the
  emitted bounds and would keep the old ones until it is regenerated.
- **The nearest-name suggestion is the one §3-07 permits and no more** (`nearest_name`, analyzer.c):
  Levenshtein over the locals in scope and then the namespace's own, referred and `clojure.core`
  mappings — the three sets `clj_ns_resolve` itself walks — with the threshold at a third of the name, so
  a name under three characters gets no hint at all. A local wins a tie against a var, being the nearer
  binding; a tie between two vars is broken by the shorter name and then by its bytes, never by the order
  a namespace's map happens to walk, or a snapshot would not mean anything. A candidate that
  `clj_ns_resolve` does not answer for is dropped, since `:refer-clojure :exclude` hides a core name the
  walk still offered. Nothing within the threshold means no `:suggestion` key and no `help:` line.
- **No message is formatted into a `char[]`** (`clj_error_message`): §3-07 counts truncating a diagnostic
  to a fixed buffer a defect, and the analyzer's 512 and the loader's 600 both cut the tail off a message
  that embedded a printed form. A long *form* inside a message is still shortened by `clj_pr_str_max`,
  which the section allows; the assembled message is not.
- **The excerpt is read from the file named by `:file`** at render time rather than carried in the data:
  the loader's bytes are gone by then, and a diagnostic that is only printed should not pay for a copy of
  the line. No file, or a file that no longer reads, means no excerpt and the rest of the diagnostic
  stands. A tab in the quoted line's prefix is copied into the underline, so the carets stay under the
  span whatever the terminal's tab width is, and a column is counted as the reader counts it — one per
  non-continuation byte.
- [ ] **A message can still name generated code.** `fail_form` prints the form it refuses, so a macro that
  builds `(let [(a b) 1] …)` renders "Unsupported binding form: (a b)" — the position is the user's own
  `(bad)` line, which is what §3-07 asks for, but the form quoted in the text is the macro's, which it
  forbids. Trigger: the `:origin` chain (design §4 «Локация в коде»), which is what would tell `fail_form`
  whose form it is holding.
- **The `in the top-level form` note is printed only when that form is on another line.** On the quoted
  line the excerpt already shows it; the note earns its place exactly when a reader would have to go
  looking for it.
- **A reader error already points at the opening delimiter.** An unclosed `(defn n [x]` renders at 1:1
  with that form underlined, which is the half of §3-07's reader rule that a reader acts on; the message
  is still "EOF while reading", and naming the delimiter in it is a wording change over the fourteen cases
  of ReaderTests' error table.
- [ ] **Only `clj-load` renders.** nREPL still answers with the printed `#error` map and the Swift host
  with `ClojureError`; §3-07 wants one format for CLI, nREPL and LSP. Trigger: the LSP step (§10, step
  10), where the JSON shape and the code actions are built, so that the structure is not built twice.
