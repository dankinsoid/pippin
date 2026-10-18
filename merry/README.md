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
type notation of `docs/merry.md` and `static.clj`:

```clojure
(defn positive-or-log [x]
  (or (pos? x) (do (println "not positive:" x) false)))
;; => (Fn [Int] Bool {:eff #{Console}})

(defn label [s]
  (match s
    [:paid amount] (str "paid " amount)
    [:refund]      "refunded"))
;; => (Fn [(or [:paid a] [:refund])] String {:eff #{}})
```

## What the slice covers

- Hindley–Milner with rows; one row machinery for record fields, effect labels and variant tags
  (Leijen 2005, as in Koka). Effects are inferred; a function's own effect tail is hidden when
  nothing else mentions it.
- Types are written as in the doc: `Int`, `nil`, `(or nil T)`, `[T]`, `{:k T & r}`,
  `(Fn [A & B] C {:eff #{Console}})`, `(Thunk T)`, variants `(or :red [:paid Int] & r)`.
- `:-` annotations on parameters and results, partial: an omitted effect is inferred.
- A declared row on `defn` (`{:eff #{Console}}`) is closed; an effect outside it is an error that
  names the chain of calls behind it.
- `Thunk` parameters: the call site wraps the argument (`merry/thunk` in `--explain`), `@` runs it.
  A variadic `& xs :- [(Thunk T)]` wraps every extra argument, so `and` and `or` keep any arity;
  they, `when` and `when-not` are functions in `resources/merry/prelude.merry`.
- Comparisons and `=` stay variadic, as in Clojure: `(<= lo x hi)`.
- `->`, `->>`, `some->`, `some->>`, `cond->`, `cond->>` with `$` as a direct argument of a step; the
  threaded value is evaluated once. `cond` with `_` as its default.
- `(or nil T)` only from a literal `(if c x nil)` or a `nil` branch, from builtins such as `first`,
  and through `some->`; a plain value is accepted where a maybe is expected.
- Map literals with keyword keys are closed records; `(:k m)` asks for an open row with `:k`.
- Structural variants: a keyword literal is an open enum `(or :red & r)`, a keyword-headed vector
  `[:paid 10]` is a variant (a vector when a vector is expected). Branches of `if` merge their rows.
- `match` infers the type of its value from the patterns (`docs/merry.md` «Вывод типа проверяемого
  значения из `match`»): without `_` the variant is closed, with `_` or a final name it is open;
  keyword literals give an enum. A missing case is an error with its pattern; a variant that flows
  in and is not handled is an error at the call or the binding. Patterns: `_`, names, literals,
  `[:tag p ...]`, `{:k p}`; a name after a `nil` branch sees the value without `nil`.
- `Int` traps on overflow.
- `Any` is the top type: anything goes up into it, and a value of `Any` comes down only through a
  check (`(inc x)` over `Any` is an error).

## Not yet

`deftype` and nominal sums; guards, `or`-patterns, equality by repeated names, nested
exhaustiveness (a refutable sub-pattern leaves its tag uncovered); `case`, `let` destructuring;
`defeffect`, handlers and `Fail`; givens (so `+` and `count` are monomorphic); modes and the
in-place/copy report (`check --costs`); `Datum`/`Json` and narrowing by predicate; sets, tuples,
strings as collections, doubles in arithmetic.
