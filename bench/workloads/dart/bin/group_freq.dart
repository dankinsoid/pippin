// @ai-generated(solo)
// workloads.group-freq (bench/workloads/src/workloads/group_freq.clj) in idiomatic Dart.
import 'dart:collection';

import 'package:workloads/harness.dart';
import 'package:workloads/util.dart';

const syllables = ['ka', 'lo', 'mi', 'ne', 'ru', 'sa', 'ti', 'vo', 'ze', 'pa', 'qu', 'do'];
const cities = ['Yerevan', 'Gyumri', 'Vanadzor', 'Dilijan', 'Goris'];

class Person {
  final int id, age, salary;
  final String dept, city;
  Person(this.id, this.dept, this.city, this.age, this.salary);
}

String word(int x) => [for (var i = 0; i < x % 3 + 1; i++) syllables[(x ~/ (12 * i + 1)) % 12]].join();

Map<K, List<V>> groupBy<K, V>(Iterable<V> xs, K Function(V) key) {
  final out = <K, List<V>>{};
  for (final x in xs) {
    out.putIfAbsent(key(x), () => []).add(x);
  }
  return out;
}

Map<K, int> frequencies<K>(Iterable<K> xs) {
  final out = <K, int>{};
  for (final x in xs) {
    out[x] = (out[x] ?? 0) + 1;
  }
  return out;
}

List<Object?> run(int n) {
  final xs = lcgList(3, n);
  final words = xs.map(word).toList();
  final freqs = frequencies(words);
  final top = (freqs.entries.toList()
        ..sort((a, b) => a.value != b.value ? b.value.compareTo(a.value) : a.key.compareTo(b.key)))
      .take(10);
  final byLen = groupBy(words, (String w) => w.length).map((k, ws) => MapEntry(k, ws.length));
  final ps = [
    for (var i = 0; i < n; i++)
      Person(i, 'd${xs[i] % 23}', cities[xs[i] % 5], 20 + xs[i] % 45, 100 * (xs[i] % 997)),
  ];
  final byDept = groupBy(ps, (Person p) => p.dept);
  final payroll = byDept.map((d, ps) => MapEntry(d, ps.fold<int>(0, (a, p) => a + p.salary)));
  final oldest = byDept.map((d, ps) => MapEntry(
      d, ps.reduce((a, b) => 1000000 * b.age + b.id >= 1000000 * a.age + a.id ? b : a).id));
  final byCityAge = frequencies(ps.map((p) => (p.city, p.age ~/ 10)));
  final merged = <String, int>{};
  for (final p in ps) {
    merged[p.city] = (merged[p.city] ?? 0) + p.salary;
  }
  final ranked = (List.of(ps)
        ..sort((a, b) => a.salary != b.salary ? b.salary.compareTo(a.salary) : a.id.compareTo(b.id)))
      .take(100);
  final buckets = <String, Set<String>>{};
  for (final p in ps) {
    buckets.putIfAbsent(p.dept, () => {}).add(p.city);
  }
  return [
    freqs.length,
    [for (final e in top) [e.key, e.value]],
    SplayTreeMap.of(byLen),
    payroll.values.fold<int>(0, (a, b) => a + b),
    oldest.values.fold<int>(0, (a, b) => a + b),
    byCityAge.length,
    SplayTreeMap.of(merged),
    [for (final p in ranked) p.id],
    buckets.values.fold<int>(0, (a, s) => a + s.length),
  ];
}

void main(List<String> args) => benchMain(args, () => run(50000));
