## Numeric tower (bigint.c, ratio.c, decimal.c, number.c, builtins_number.c)

- **Six kinds, one ladder.** `clj_num_kind_of` answers fixnum < long < bigint < ratio < decimal < double, the
  order of Clojure's `Ops.combine`, and the larger kind of a pair decides the result (`clj_num_arith`,
  `clj_num_cmp` in number.c). The fixnum/fixnum and double-with-fixnum paths stay in builtins.c ahead of
  it, so `+` on two fixnums is still two tag checks and an overflow-checked add (bench/RESULTS.md: the
  counting loop, `reduce +` and `swap! inc` rows did not move). A bigint is sign + magnitude in
  little-endian base 2^32 limbs (bigint.c, no external library); a ratio holds two bigints, gcd-reduced
  with a positive denominator and never an integer; a decimal is a bigint unscaled value and an `int32`
  scale, as `java.math.BigDecimal`.
- **JVM promotion rules, exactly.** A bigint result stays a bigint (`(type (+' 1N 1))` is BigInt), so
  `(= 1N 1)` and `(== 1N 1)` are true and `(hash 1N)` equals `(hash 1)` — map keys of the two agree.
  Plain `+ - * inc dec` still throw "integer overflow" on a fixnum overflow; only `+' -' *' inc' dec'`
  promote. `=` keeps Clojure's category rule (integer / ratio / decimal / floating never equal across
  kinds), while `==` and `compare` compare numerically across all five.
- **What the reader takes**: `0N`, `1/2`, `0.0M`, `1M`, `1e10M`, and any integer literal in every radix
  (`0x7FFFFFFFFFFFFFFF`, `-0x8000000000000000`, `2r1011`, octal, `NrDDD`) — a long while the digits fit 64
  bits, a bigint past them, with the `N` suffix forcing a bigint. A ratio is normalised at read through the same divide as `/`, so `12/12` reads
  as `1` and `0/2` as `0`, as LispReader's `reduceBigInt` does; `1/0` is a read error.
- **`range` and every index argument span the whole int64** (`clj_range`, `clj_index_arg` in coll.h): a
  range's bounds and step are `int64_t`, its elements come out of `clj_long_new`, and the step is guarded by
  `__builtin_add_overflow` so `(range Long/MAX_VALUE)` and `(take 3 (range (- Long/MAX_VALUE 1) Long/MAX_VALUE))`
  walk instead of wrapping; `count` divides an unsigned span, so `Long/MIN_VALUE`..`Long/MAX_VALUE` does not
  overflow, and a count past `Long/MAX_VALUE` throws rather than lying. The iterator's range fast path owns a
  boxed element (`it->item`, `it->slots`) where a fixnum element is an immediate. An index argument is a fixnum
  or a box widened to the same `intptr_t`: a box is out of bounds for every collection, so the bounds check
  that follows reports the index error the JVM reports instead of a cast error. A bigint bound names itself
  ("range bound outside the 64-bit long"), as `range*` takes a long.
- **The 63-bit fixnum is a representation, not the contract** (long.c). A value outside it is a boxed
  `int64_t` of kind `CLJ_NUM_LONG`, so `9223372036854775807` reads as an integer, `(int? Long/MAX_VALUE)`
  is true, `(+ Long/MAX_VALUE 1)` throws "integer overflow", `(long 9223372036854775807)` returns it and
  the bit ops cover all 64 bits (`(bit-shift-left 1 63)` is `Long/MIN_VALUE`). **The box is canonical**:
  `clj_long_new` hands back a fixnum whenever one fits and `clj_long_box` asserts the value is outside the
  range, so `=`, `hash` and `clj_compare` never cross-check the two representations; the box's hash is the
  fixnum hash widened to 64 bits, so a fixnum, a box and a bigint of one value agree. `clj_int64_of` is the
  accessor that takes either. One descriptor serves both, named `long`, so `(type 1)` and
  `(type Long/MAX_VALUE)` are one value and `Long` is bound to it.
- **`unchecked-add`/`-subtract`/`-multiply`/`-inc`/`-dec`/`-negate` wrap at 64 bits**, as on the JVM:
  the arithmetic is done in `uint64_t` and `clj_long_new` is the one range check on the way out, boxing
  when the result leaves the fixnum. `(unchecked-inc 4611686018427387903)` is `4611686018427387904`,
  `(unchecked-inc Long/MAX_VALUE)` is `Long/MIN_VALUE`.
- [ ] **No float and no `*math-context*`.** `float` range-checks against `Float` and narrows through it
  (`(float Double/MIN_VALUE)` is `0.0`) but returns a double box, so `(double? (float 0.0))` is true where
  the JVM says false. `with-precision` and rounding modes do not exist: decimal `+ - *` are exact, and `/`
  succeeds only when the quotient terminates, else it throws "Non-terminating decimal expansion;
  with-precision is not supported". `(/ 1M 3M)` is that throw; `(/ 1M 2M)` is `0.5M`.
- [ ] **Division is shift-subtract**, quadratic in the bit length (`mag_divmod`), and `gcd` is Euclid over it.
  Every bigint the runtime meets is a few limbs, so the constant factors never showed. Trigger: a profile
  with `quot`/`rem`/`gcd` on thousand-bit values; the fix is Knuth D and a binary gcd.
- **Printing follows `print-method`, not `toString`**: `pr-str` appends the tag (`1N`, `1.5M`, `1/2`), `str`
  does not (`"1"`, `"1.5"`, `"1/2"`), and `str` of a non-finite double is Java's `Infinity`/`-Infinity`/`NaN`
  where `pr-str` writes `##Inf`. A decimal prints by `BigDecimal.toString`'s rule (plain notation while the
  scale is non-negative and the adjusted exponent is above -7, scientific otherwise), so `1e10M` prints
  `1E+10M`. `numerator`/`denominator` demote to the canonical
  integer, so `(numerator 1/2)` is `1` and only a value past 64 bits stays a bigint.
- [ ] **`clj_bigint_to_double` and `clj_decimal_to_double` round through the decimal text** (`strtod`), which
  is correct and slow, rather than reimplementing correct rounding over the limbs. Trigger: a profile with
  bigint-to-double in a loop.
- **A decimal built from a double keeps `Double.toString`'s scale** (`BigDecimal.valueOf`), so
  `(bigdec 0.1)` is `0.1M`; a value at or past 1e7 takes the scientific branch, where the JVM's
  `Double.toString` switches too, but our scale can differ from the JVM's by the trailing zero it keeps.
  Equality and hashing ignore trailing zeros, so only the printed form differs.
- [ ] **Every bigint carries at least one spare limb**: results are sized by the worst case and the count is
  trimmed without a `clj_realloc`, so a one-limb value can occupy two. Four bytes per bigint; trigger is
  a heap profile with many of them.

