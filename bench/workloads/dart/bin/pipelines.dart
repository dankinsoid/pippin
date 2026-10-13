// @ai-generated(solo)
// workloads.pipelines (bench/workloads/src/workloads/pipelines.clj) in idiomatic Dart.
import 'package:workloads/harness.dart';

int sum(Iterable<int> xs) => xs.fold(0, (a, b) => a + b);

List<num> run(int n) {
  final v = List<int>.generate(n, (i) => i);
  final range = Iterable<int>.generate(n);
  final chunkSums = <int>[];
  for (var i = 0; i < n; i += 16) {
    final s = sum(v.getRange(i, i + 16 < n ? i + 16 : n));
    if (s != 0) chunkSums.add(s);
  }
  final byKey = <int, int>{};
  for (final x in v) {
    byKey[x % 100] = (byKey[x % 100] ?? 0) + x;
  }
  var iterated = 0;
  for (var k = 0, x = 1; k < 5000; k++, x = (x * 7) % 1000003) {
    iterated += x;
  }
  var deduped = 0;
  int? prev;
  for (final x in v.expand((x) => [x, x])) {
    if (deduped == 0 || x != prev) deduped++;
    prev = x;
  }
  var running = 0, maxRunning = 0;
  for (final (i, x) in v.take(n ~/ 4).indexed) {
    running += x;
    if (i == 0 || running > maxRunning) maxRunning = running;
  }
  var products = 0;
  for (var i = 0; i < n; i++) {
    products += v[i] * v[n - 1 - i];
  }
  var rising = 0;
  for (var i = 1; i < n; i++) {
    if (v[i] - v[i - 1] > 0) rising++;
  }
  var walked = 0;
  for (final x in v) {
    walked += x;
  }
  var kv = 0;
  for (final (i, x) in v.indexed) {
    kv += i * (x % 7);
  }
  final groups = <int, List<int>>{};
  for (final x in v.take(n ~/ 4)) {
    groups.putIfAbsent(x % 7, () => []).add(x);
  }
  return [
    sum(range.map((x) => x * x).where((x) => x.isEven).map((x) => x % 1000)),
    sum(v.where((x) => x.isOdd).map((x) => x + 1)),
    chunkSums.length,
    sum(byKey.values),
    sum(v.map((x) => (k: x % 10, v: x)).where((m) => m.v.isOdd).map((m) => m.v)),
    iterated,
    deduped,
    maxRunning,
    sum(v.take(n ~/ 4).expand((x) => List.filled(x % 3, x))),
    sum(v.where((x) => x % 3 == 0)),
    v.map((x) => x % 1000).toSet().length,
    v.map((x) => 0.5 * x).fold(0.0, (a, b) => a + b),
    v.map((x) => x.toDouble() / 3.0).fold(0.0, (a, b) => a + b),
    products,
    rising,
    walked,
    kv,
    groups.length,
  ];
}

void main(List<String> args) => benchMain(args, () => run(300000));
