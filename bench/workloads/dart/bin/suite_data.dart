// @ai-generated(solo)
// workloads.suite-data (bench/workloads/src/workloads/suite_data.clj) in idiomatic Dart.
import 'dart:collection';

import 'package:workloads/harness.dart';

sealed class Shape {
  int area();
  Shape scale(int k);
}

class Circle extends Shape {
  final int r;
  Circle(this.r);
  @override
  int area() => 3 * r * r;
  @override
  Shape scale(int k) => Circle(r * k);
}

class Rect extends Shape {
  final int w, h;
  Rect(this.w, this.h);
  @override
  int area() => w * h;
  @override
  Shape scale(int k) => Rect(w * k, h * k);
}

class Tri extends Shape {
  final int b, h;
  Tri(this.b, this.h);
  @override
  int area() => (b * h) ~/ 2;
  @override
  Shape scale(int k) => Tri(b * k, h * k);
}

sealed class Figure {}

class Point extends Figure {
  final int x, y;
  Point(this.x, this.y);
}

class Segment extends Figure {
  final int x1, x2;
  Segment(this.x1, this.x2);
}

class Other extends Figure {}

int describe(Figure f) => switch (f) {
      Point p => p.x + p.y,
      Segment s => s.x2 - s.x1,
      Other() => 1,
    };

String kind(Figure f) => switch (f) { Point() => 'point', Segment() => 'segment', Other() => 'other' };

Map<K, int> frequencies<K>(Iterable<K> xs) {
  final out = <K, int>{};
  for (final x in xs) {
    out[x] = (out[x] ?? 0) + 1;
  }
  return out;
}

Iterable<int> flatten(Object x) sync* {
  if (x is List) {
    for (final y in x) {
      yield* flatten(y as Object);
    }
  } else {
    yield x as int;
  }
}

int sum(Iterable<int> xs) => xs.fold(0, (a, b) => a + b);

List<Object> run(int n) {
  final range = List.generate(n, (i) => i);
  final shapes = <Shape>[
    for (final i in range)
      switch (i % 3) { 0 => Circle(i % 10), 1 => Rect(i % 7, i % 5), _ => Tri(i % 9, i % 4) },
  ];
  final areas = sum(shapes.map((s) => s.area()));
  final scaled = sum(shapes.map((s) => s.scale(2).area()));
  final fields = sum(shapes.map((s) => switch (s) { Circle c => c.r, Rect r => r.w, Tri t => t.b }));
  // `record?` and `satisfies?` have no static counterpart here: a dynamic type test over the list as Objects.
  final records = shapes.cast<Object>().whereType<Shape>().length;
  final figures = <Figure>[
    for (final i in range)
      switch (i % 3) { 0 => Point(i, 2 * i), 1 => Segment(i, i + 5), _ => Other() },
  ];
  final described = sum(figures.map(describe));
  final s1 = {for (var i = 0; i < n; i += 2) i};
  final s2 = {for (var i = 0; i < n; i += 3) i};
  final index = <String, Set<Figure>>{};
  for (final f in figures.take(300)) {
    index.putIfAbsent(kind(f), () => {}).add(f);
  }
  final sets = [
    s1.union(s2).length,
    s1.intersection(s2).length,
    s1.difference(s2).length,
    s2.where((x) => x.isEven).toSet().length,
    index.length,
  ];
  final merged = frequencies(range.map((i) => i % 13));
  frequencies(range.map((i) => i % 17)).forEach((k, v) => merged[k] = (merged[k] ?? 0) + v);
  final zm = {for (var i = 0; i < 1000; i++) i: '$i'};
  final upTo3000 = List.generate(3000, (i) => i);
  final parts = [for (var i = 0; i + 3 <= upTo3000.length; i++) upTo3000.sublist(i, i + 3)].length;
  final inter = sum(upTo3000.expand((i) => [i, 3000 + i]));
  final sorted = (range.map((i) => (i * 7919) % 10007).toList()..sort((a, b) => b.compareTo(a))).take(5).toList();
  final sm = SplayTreeMap<int, int>();
  for (final i in range) {
    sm[(i * 31) % 1009] = i;
  }
  final stack = <int>[];
  for (final i in range) {
    if (i % 3 == 0) {
      if (stack.isNotEmpty) stack.removeLast();
    } else {
      stack.add(i);
    }
  }
  final tr = <int>[for (final i in range) i];
  final trm = <int, int>{};
  for (final i in range) {
    trm[i % 777] = i;
  }
  // postwalk's inc of every number, over typed figures: each one copied with its fields incremented.
  final walked = [
    for (final f in figures.take(200))
      switch (f) { Point p => Point(p.x + 1, p.y + 1), Segment s => Segment(s.x1 + 1, s.x2 + 1), Other o => o },
  ];
  final nested = sum(flatten([for (var i = 0; i < 3000; i++) [i, [i, [i]]]]));
  return [
    areas,
    scaled,
    fields,
    records,
    described,
    sets,
    sum(merged.values),
    zm.length,
    parts,
    inter,
    sorted,
    [sm.firstKey(), sm[sm.firstKey()]],
    [sm.lastKey(), sm[sm.lastKey()]],
    stack.length,
    tr.length,
    trm.length,
    sum(walked.map((f) => f is Point ? f.x : 0)),
    nested,
  ];
}

void main(List<String> args) => benchMain(args, () => run(30000));
