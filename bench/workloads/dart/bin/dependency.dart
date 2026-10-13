// @ai-generated(solo)
// workloads.dependency (bench/workloads/src/workloads/dependency.clj) in idiomatic Dart, with
// com.stuartsierra.dependency's graph as a mutable class.
import 'package:workloads/harness.dart';
import 'package:workloads/util.dart';

class Graph {
  final Map<int, Set<int>> dependencies;
  final Map<int, Set<int>> dependents;
  Graph() : dependencies = {}, dependents = {};
  Graph._(this.dependencies, this.dependents);

  // The source's graph is persistent: a version kept beside a changed one is a copy here.
  Graph copy() => Graph._(
        {for (final e in dependencies.entries) e.key: Set.of(e.value)},
        {for (final e in dependents.entries) e.key: Set.of(e.value)},
      );

  Set<int> immediateDependencies(int node) => dependencies[node] ?? const {};
  Set<int> immediateDependents(int node) => dependents[node] ?? const {};

  static Set<int> _transitive(Map<int, Set<int>> neighbors, Set<int> nodeSet) {
    final unexpanded = [for (final n in nodeSet) ...?neighbors[n]];
    final expanded = <int>{};
    while (unexpanded.isNotEmpty) {
      final node = unexpanded.removeLast();
      if (expanded.add(node)) unexpanded.addAll(neighbors[node] ?? const {});
    }
    return expanded;
  }

  Set<int> transitiveDependencies(int node) => _transitive(dependencies, {node});
  Set<int> transitiveDependents(int node) => _transitive(dependents, {node});
  Set<int> get nodes => dependencies.keys.toSet().union(dependents.keys.toSet());
  bool dependsOn(int x, int y) => transitiveDependencies(x).contains(y);

  void depend(int node, int dep) {
    if (node == dep || dependsOn(dep, node)) {
      throw StateError('Circular dependency between $node and $dep');
    }
    dependencies.putIfAbsent(node, () => {}).add(dep);
    dependents.putIfAbsent(dep, () => {}).add(node);
  }

  void removeEdge(int node, int dep) {
    dependencies[node]?.remove(dep);
    dependents[dep]?.remove(node);
  }

  void removeAll(int node) {
    for (final m in [dependencies, dependents]) {
      m.remove(node);
      for (final vs in m.values) {
        vs.remove(node);
      }
    }
  }

  void removeNode(int node) => dependencies.remove(node);

  List<int> topoSort() {
    final sorted = <int>[];
    final g = copy();
    final todo = nodes.where((n) => immediateDependents(n).isEmpty).toSet();
    while (todo.isNotEmpty) {
      final node = todo.first;
      todo.remove(node);
      for (final d in List.of(g.immediateDependencies(node))) {
        g.removeEdge(node, d);
        if (g.immediateDependents(d).isEmpty) todo.add(d);
      }
      g.removeNode(node);
      sorted.add(node);
    }
    return sorted.reversed.toList();
  }

  int Function(int, int) topoComparator() {
    final pos = {for (final (i, n) in topoSort().indexed) n: i};
    // Long/MAX_VALUE in the source: nodes outside the graph sort last.
    const last = 0x7fffffffffffffff;
    return (a, b) => (pos[a] ?? last).compareTo(pos[b] ?? last);
  }
}

Graph build(int n) {
  final g = Graph();
  var x = 7;
  for (var i = 1; i < n; i++) {
    final a = lcg(x), b = lcg(a);
    g
      ..depend(i, a % i)
      ..depend(i, b % i);
    x = b;
  }
  return g;
}

List<int> run(int n) {
  final g = build(n);
  final order = g.topoSort();
  final pos = {for (final (i, node) in order.indexed) node: i};
  var forward = 0;
  for (var i = 0; i < n; i++) {
    for (final d in g.immediateDependencies(i)) {
      if (pos[d]! < pos[i]!) forward++;
    }
  }
  final sorted = [for (var i = 0; i < n; i += 3) i]..sort(g.topoComparator());
  var trans = 0;
  for (var i = n - 40; i < n; i++) {
    trans += g.transitiveDependencies(i).length;
  }
  final dependents = g.transitiveDependents(0).length;
  var hits = 0;
  for (var i = 1; i < n; i += 41) {
    if (g.dependsOn(i, 0)) hits++;
  }
  final pruned = g.copy();
  for (var i = 0; i < n; i += 5) {
    pruned.removeAll(i);
  }
  return [order.length, forward, sorted.length, g.nodes.length, trans, dependents, hits, pruned.nodes.length];
}

void main(List<String> args) => benchMain(args, () => run(1500));
