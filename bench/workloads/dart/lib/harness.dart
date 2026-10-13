// @ai-generated(solo)
// jvm.clj's report lines for a port: `once` for the cold-process run, none for the timed loop.
import 'edn.dart';

void benchMain(List<String> args, Object? Function() run) {
  final sw = Stopwatch()..start();
  double now() => sw.elapsedMicroseconds / 1000.0;
  var t = now();
  final first = run();
  final firstMs = now() - t;
  final printed = edn(first);
  if (args.isNotEmpty && args.first == 'once') {
    print('first_ms $firstMs');
    print('expected same');
    print('result $printed');
    return;
  }
  // clj-corpus-bench's policy for AOT code; comparing each result outside the timed span keeps the work live.
  final times = <double>[];
  var stable = true;
  while (times.length < 30 && (times.length < 3 || times.fold(0.0, (a, b) => a + b) < 2000.0)) {
    t = now();
    final r = run();
    times.add(now() - t);
    if (edn(r) != printed) stable = false;
  }
  final sorted = [...times]..sort();
  print('first_ms $firstMs');
  print('warmup_iterations 0');
  print('iterations ${times.length}');
  print('median_ms ${sorted[sorted.length ~/ 2]}');
  print('min_ms ${sorted.first}');
  // The harness (scripts/corpus_refs_dart.py) checks the printed result against the source's `expected`.
  print('expected ${stable ? 'same' : 'DIFFERENT'}');
  print('result $printed');
}
