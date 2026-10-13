// @ai-generated(solo)
// workloads.combinatorics (bench/workloads/src/workloads/combinatorics.clj) in idiomatic Dart: the
// clojure.math.combinatorics algorithms the workload reaches, as lazy `sync*` iterables over mutable lists.
import 'package:workloads/harness.dart';

Never notPorted(String what) => throw UnimplementedError('$what: no workload reaches it, so it is not ported');

bool sortedNumbers(List<num> s) {
  for (var i = 1; i < s.length; i++) {
    if (s[i - 1] > s[i]) return false;
  }
  return true;
}

bool allDifferent(List<Object> s) => s.toSet().length == s.length;

Iterable<List<int>> lexPermutations(List<int> c) sync* {
  final v = List.of(c)..sort();
  if (v.isEmpty) {
    yield [];
    return;
  }
  final len = v.length;
  while (true) {
    yield List.of(v);
    var j = len - 2;
    while (j >= 0 && v[j] >= v[j + 1]) {
      j--;
    }
    if (j < 0) return;
    final vj = v[j];
    var l = len - 1;
    while (vj >= v[l]) {
      l--;
    }
    v[j] = v[l];
    v[l] = vj;
    for (var k = j + 1, m = len - 1; k < m; k++, m--) {
      final t = v[k];
      v[k] = v[m];
      v[m] = t;
    }
  }
}

Iterable<List<int>> permutations(List<int> items) {
  if (sortedNumbers(items)) return lexPermutations(items);
  if (allDifferent(items)) {
    return lexPermutations(List.generate(items.length, (i) => i)).map((p) => [for (final i in p) items[i]]);
  }
  notPorted('permutations of an unsorted multiset');
}

Iterable<List<int>> indexCombinations(int n, int cnt) sync* {
  final c = [0, for (var j = 1; j <= n; j++) j + cnt - (n + 1)];
  var j = 1;
  while (true) {
    yield [for (var k = n; k >= 1; k--) c[k]];
    if (j > n) return;
    c[j]--;
    if (c[j] < j) {
      j++;
    } else {
      while (j != 1) {
        c[j - 1] = c[j] - 1;
        j--;
      }
    }
  }
}

Iterable<List<T>> combinations<T extends Object>(List<T> items, int t) sync* {
  final vItems = items.reversed.toList();
  if (t == 0) {
    yield [];
    return;
  }
  final cnt = items.length;
  if (t > cnt) return;
  if (t == 1) {
    for (final item in items.toSet()) {
      yield [item];
    }
  } else if (allDifferent(items)) {
    if (t == cnt) {
      yield items;
    } else {
      for (final idx in indexCombinations(t, cnt)) {
        yield [for (final i in idx) vItems[i]];
      }
    }
  } else {
    notPorted('combinations of a multiset');
  }
}

Iterable<List<T>> subsets<T extends Object>(List<T> items) sync* {
  for (var n = 0; n <= items.length; n++) {
    yield* combinations(items, n);
  }
}

Iterable<List<T>> cartesianProduct<T>(List<List<T>> seqs) sync* {
  if (seqs.any((s) => s.isEmpty)) return;
  final idx = List.filled(seqs.length, 0);
  while (true) {
    yield [for (var k = 0; k < seqs.length; k++) seqs[k][idx[k]]];
    var i = seqs.length - 1;
    while (i >= 0 && idx[i] + 1 == seqs[i].length) {
      idx[i] = 0;
      i--;
    }
    if (i < 0) return;
    idx[i]++;
  }
}

Iterable<List<T>> selections<T>(List<T> items, int n) => cartesianProduct(List.filled(n, items));

// Knuth's algorithm H: restricted growth strings, at most r and at least s parts.
Iterable<List<int>> growthStringsH(int n, int? r, int? s) sync* {
  final a = s != null ? [for (var i = 1; i <= n; i++) i - (n - s + 1) > 0 ? i - (n - s + 1) : 0] : List.filled(n, 0);
  final b = List.filled(n, 1);
  while (true) {
    yield List.of(a);
    if (b[n - 1] > a[n - 1] && (r == null || a[n - 1] < r - 1)) {
      a[n - 1]++;
      continue;
    }
    var j = n - 2;
    while (!(a[j] < b[j] &&
        (r == null || a[j] < r - 1) &&
        (s == null || s - b[j] - (a[j] + 1 == b[j] ? 1 : 0) <= n - j))) {
      j--;
    }
    if (j == 0) return;
    a[j]++;
    final x = s == null ? null : s - (b[j] + (a[j] == b[j] ? 1 : 0));
    var currentMax = b[j] + (b[j] == a[j] ? 1 : 0);
    for (var i = j + 1; i < n; i++) {
      if (s != null && i > n - x! - 1) {
        final newAi = i - n + s;
        a[i] = newAi;
        b[i] = currentMax;
        if (newAi + 1 > currentMax) currentMax = newAi + 1;
      } else {
        a[i] = 0;
        b[i] = currentMax;
      }
    }
  }
}

// Each partition's parts stay lazy, as the source's `map` over the groups: `length` never builds them.
Iterable<Iterable<List<int>>> lexPartitionsH(int n, int? from, int? to) sync* {
  if (n == 0) {
    if ((from ?? 0) <= 0 && 0 <= (to ?? 0)) yield [];
    return;
  }
  from = from != null && from <= 1 ? null : from;
  to = to != null && to >= n ? null : to;
  final lo = from ?? 1, hi = to ?? n;
  if (!(1 <= lo && lo <= hi && hi <= n)) return;
  if (n == 1) {
    yield [
      [0]
    ];
  } else if (to == 1) {
    yield [List.generate(n, (i) => i)];
  } else {
    for (final g in growthStringsH(n, to, from)) {
      final groups = <int, List<int>>{};
      for (var i = 0; i < n; i++) {
        groups.putIfAbsent(g[i], () => []).add(i);
      }
      yield Iterable.generate(groups.length, (k) => groups[k]!);
    }
  }
}

Iterable<Iterable<List<T>>> partitions<T extends Object>(List<T> items, {int? min, int? max}) {
  if (items.isEmpty || allDifferent(items)) {
    return lexPartitionsH(items.length, min, max).map((parts) => parts.map((p) => [for (final o in p) items[o]]));
  }
  notPorted('partitions of a multiset');
}

int factorial(int n) {
  var acc = 1;
  for (var k = n; k > 0; k--) {
    acc *= k;
  }
  return acc;
}

int countPermutations(List<Object> l) => allDifferent(l) ? factorial(l.length) : notPorted('multiset count');

List<int> nthPermutationDistinct(List<int> l, int n) {
  // The source's assert, on by default in Clojure; a Dart `assert` is compiled out of an AOT build.
  if (n >= factorial(l.length)) throw ArgumentError('$n is too large');
  final digits = <int>[];
  for (var q = n, divisor = 1; q != 0; divisor++) {
    digits.add(q % divisor);
    q ~/= divisor;
  }
  final indices = [...List.filled(l.length - digits.length, 0), ...digits.reversed];
  final rest = List.of(l);
  return [for (final i in indices) rest.removeAt(i)];
}

List<int> nthPermutation(List<int> items, int n) {
  if (sortedNumbers(items) && allDifferent(items)) return nthPermutationDistinct(items, n);
  notPorted('nth-permutation of an unsorted or multiset input');
}

int nTakeK(int n, int k) {
  if (k < 0 || k > n) return 0;
  if (k == 0) return 1;
  if (k == 1) return n;
  if (k > n ~/ 2) return nTakeK(n, n - k);
  var num = 1, den = 1;
  for (var i = n - k + 1; i <= n; i++) {
    num *= i;
  }
  for (var i = 1; i <= k; i++) {
    den *= i;
  }
  return num ~/ den;
}

List<T> nthCombination<T extends Object>(List<T> items, int t, int n) {
  if (!allDifferent(items)) notPorted('nth-combination of a multiset');
  if (n >= nTakeK(items.length, t)) throw ArgumentError('$n is too large');
  final comb = <T>[];
  var start = 0;
  while (n != 0 && start < items.length) {
    final dcdt = nTakeK(items.length - start - 1, t - 1);
    if (n < dcdt) {
      comb.add(items[start]);
      t--;
    } else {
      n -= dcdt;
    }
    start++;
  }
  return comb..addAll(items.skip(start).take(t));
}

int weigh(List<int> xs) {
  var s = 0;
  for (final (i, x) in xs.indexed) {
    s += x * (i + 1);
  }
  return s;
}

int sum(Iterable<int> xs) => xs.fold(0, (a, b) => a + b);

List<int> range(int n) => List.generate(n, (i) => i);

List<int> run(int k) => [
      sum(permutations(range(k)).map(weigh)),
      sum(permutations([1, 1, 2, 2, 3, 3, 4]).map(weigh)),
      sum(combinations(range(3 * k), 4).map(weigh)),
      sum(subsets(range(k + 6)).map((s) => s.length)),
      partitions(range(k + 1)).length,
      partitions(range(k), min: 2, max: 3).length,
      sum(cartesianProduct(List.filled(4, range(9))).map(weigh)),
      selections(['a', 'b', 'c'], k).length,
      countPermutations(range(k + 4)),
      sum([for (var i = 0; i < 3628800; i += 9973) weigh(nthPermutation(range(10), i))]),
      sum([for (var i = 0; i < 142506; i += 211) weigh(nthCombination(range(30), 5, i))]),
    ];

void main(List<String> args) => benchMain(args, () => run(8));
