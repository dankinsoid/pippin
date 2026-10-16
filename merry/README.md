# Merry prototype

A checker and interpreter for a slice of Merry (`docs/merry.md`), to try the inferred parts of the
design on small programs. No code generation: values are Clojure data, and the interesting output is
what the checker infers.

```sh
cd merry
clojure -M:run check examples/basics.merry            # the file with inferred contracts after each form
clojure -M:run check examples/basics.merry --explain  # plus the elaborated forms
clojure -M:run expand examples/basics.merry           # macro expansion only
clojure -M:run run examples/basics.merry              # check, then call -main
```

`check` prints the source unchanged and puts the contract after each top-level form, in the
notation of `static.clj`:

```clojure
(defn positive-or-log [x]
  (or (pos? x) (do (println "not positive:" x) false)))
;; => [:=> [:int] :bool {:eff #{Console}}]
```

## What the slice covers

- Hindley–Milner with rows; one row machinery for record fields and effect labels (Leijen 2005, as
  in Koka). Effects are inferred; a function's own effect tail is hidden when nothing else mentions it.
- `:-` annotations on parameters and results, partial: an omitted effect is inferred.
- A declared row on `defn` (`{:eff #{Console}}`) is closed; an effect outside it is an error that
  names the chain of calls behind it.
- `:thunk` parameters: the call site wraps the argument (`merry/thunk` in `--explain`), `@` runs it.
  `and`, `or`, `when`, `when-not` are functions in `resources/merry/prelude.merry`.
- `->`, `->>`, `some->`, `some->>`, `cond->`, `cond->>` with `$` as a direct argument of a step; the
  threaded value is evaluated once. `cond` with `_` as its default.
- `[:maybe T]` only from a literal `(if c x nil)`, from builtins such as `first`, and through
  `some->`; a plain value is accepted where a maybe is expected.
- Map literals with keyword keys are closed records; `(:k m)` asks for an open row with `:k`.
- `:int` traps on overflow.

## Not yet

Patterns, `match`/`case` and destructuring; `deftype`; `defeffect`, handlers and `Fail`; givens (so
`+` and `count` are monomorphic); modes and the in-place/copy report; variadic `:thunk` parameters
(`and` takes two arguments); sets, strings as collections, doubles in arithmetic.
