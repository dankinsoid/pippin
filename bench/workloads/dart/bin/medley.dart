// @ai-generated(solo)
// workloads.medley (bench/workloads/src/workloads/medley.clj) in idiomatic Dart.
import 'package:workloads/harness.dart';
import 'package:workloads/util.dart';

class Rec {
  final int id, group, score;
  final Set<String> tags;
  final int? parent;
  Rec(this.id, this.group, this.score, this.tags, this.parent);
}

class GroupTags {
  int n;
  final Set<int> tags = {};
  GroupTags(this.n);
}

List<Rec> records(int n) {
  final xs = lcgList(42, n);
  // One set per literal, as `#{:a :b}` is one value in the source.
  const ab = {'a', 'b'}, c = {'c'};
  return [
    for (var i = 0; i < n; i++)
      Rec(i, xs[i] % 37, xs[i] % 1000, xs[i].isEven ? ab : c, i > 0 ? xs[i] % i : null),
  ];
}

Iterable<T> interleaveAll<T>(List<Iterable<T>> colls) sync* {
  final its = [for (final c in colls) c.iterator];
  var live = its.length;
  final done = List.filled(its.length, false);
  while (live > 0) {
    for (var k = 0; k < its.length; k++) {
      if (done[k]) continue;
      if (its[k].moveNext()) {
        yield its[k].current;
      } else {
        done[k] = true;
        live--;
      }
    }
  }
}

List<Object?> run(int n) {
  final rs = records(n);
  final byId = {for (final r in rs) r.id: r};
  final scores = byId.map((k, r) => MapEntry(k, r.score));
  final high = Map.fromEntries(scores.entries.where((e) => e.value > 500));
  final keyed = high.map((k, v) => MapEntry('$k', v));
  final oddIds = Map.fromEntries(scores.entries.where((e) => e.key.isOdd));
  final dropped = Map.fromEntries(scores.entries.where((e) => e.value != 0));
  final groups = <int, List<Rec>>{};
  for (final r in rs) {
    groups.putIfAbsent(r.group, () => []).add(r);
  }
  final totals = groups.map((g, rs) => MapEntry(g, rs.fold<int>(0, (a, r) => a + r.score)));
  final collated = <int, int>{};
  for (final r in rs) {
    final acc = collated[r.group];
    collated[r.group] = acc == null ? r.score : acc + r.score;
  }
  final merged = <int, GroupTags>{};
  for (final r in rs.take(2000)) {
    (merged.putIfAbsent(r.group, () => GroupTags(r.id))
          ..n = r.id)
        .tags
        .add(r.score);
  }
  final seenScores = <int>{};
  final distinctScores = rs.where((r) => seenScores.add(r.score)).toList();
  var deduped = 0;
  int? prevKey;
  for (final r in rs) {
    final k = r.score ~/ 100;
    if (deduped == 0 || k != prevKey) deduped++;
    prevKey = k;
  }
  // greatest-by's candidates are varargs: given the seq alone it returns it, and `(:score seq)` is nil.
  final Object? best = null;
  final found = rs.where((r) => r.score == 999).firstOrNull;
  var upto = 0;
  for (final r in rs) {
    upto++;
    if (r.score > 990) break;
  }
  final nested = {for (var g = 0; g < 37; g++) g: <String, int>{'n': 0}};
  for (final r in rs) {
    final m = nested[r.group];
    final v = m?['n'];
    if (v != null) m!['n'] = v + r.score;
  }
  // The source's nested map stays as it was beside pruned, a persistent map's version: pruned works on a copy.
  final pruned = {for (final e in nested.entries) e.key: Map.of(e.value)};
  for (final r in rs.take(20)) {
    final m = pruned[r.group];
    if (m != null) {
      m.remove('n');
      if (m.isEmpty) pruned.remove(r.group);
    }
  }
  final someAssoc = <int, int>{};
  for (final r in rs.take(5000)) {
    if (r.parent != null) someAssoc[r.id] = r.parent!;
  }
  final interleaved = interleaveAll<int>([
    Iterable.generate(1000, (i) => i),
    Iterable.generate(500, (i) => i),
    Iterable.generate(1500, (i) => i),
  ]).length;
  var indexed = 0;
  for (final (i, r) in rs.take(3000).indexed) {
    indexed += i * r.score;
  }
  return [
    high.length,
    keyed.length,
    oddIds.length,
    dropped.length,
    totals.values.fold<int>(0, (a, b) => a + b),
    collated.values.fold<int>(0, (a, b) => a + b),
    merged.length,
    merged.values.fold<int>(0, (a, g) => a + g.tags.length),
    distinctScores.length,
    deduped,
    best,
    found?.id,
    upto,
    nested.values.fold<int>(0, (a, m) => a + m['n']!),
    pruned.length,
    someAssoc.length,
    interleaved,
    indexed,
  ];
}

void main(List<String> args) => benchMain(args, () => run(20000));
