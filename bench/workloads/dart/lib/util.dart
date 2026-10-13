// @ai-generated(solo)
// workloads.util's generator: a Dart int is 64-bit like a JVM long, and the product of a 31-bit state stays below 2^62.

int lcg(int x) => (x * 1103515245 + 12345) & 0x7fffffff;

List<int> lcgList(int seed, int n) {
  final out = <int>[];
  var x = seed;
  for (var i = 0; i < n; i++) {
    x = lcg(x);
    out.add(x);
  }
  return out;
}
