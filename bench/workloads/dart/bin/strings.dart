// @ai-generated(solo)
// workloads.strings (bench/workloads/src/workloads/strings.clj) in idiomatic Dart.
import 'package:workloads/harness.dart';

String row(int i) =>
    [i, 'name-${i % 97}', i * 3, i.isEven ? 'yes' : 'no', 'c${i % 13}'.toUpperCase()].join(',');

String capitalize(String s) => s.isEmpty ? s : s[0].toUpperCase() + s.substring(1).toLowerCase();

List<Object> run(int n) {
  final csv = [for (var i = 0; i < n; i++) row(i)].join('\n');
  final lines = csv.split('\n');
  final cells = [for (final l in lines) l.split(',')];
  final total = cells.fold(0, (a, c) => a + int.parse(c[2]));
  final names = <String, int>{};
  for (final c in cells) {
    names[c[1]] = (names[c[1]] ?? 0) + 1;
  }
  final upper = cells.where((c) => c[1].toUpperCase().startsWith('NAME-1')).length;
  final joined = [for (var i = 0; i < n ~/ 4; i++) '$i'].join('|');
  final replaced = csv.replaceAll('name', 'N');
  final regexed = csv.substring(0, csv.length < 200000 ? csv.length : 200000).replaceAll(RegExp('[0-9]+'), '#');
  final trimmed = lines.take(5000).fold(0, (a, l) => a + '  $l  '.trim().length);
  var built = '';
  for (var i = 0; i < n ~/ 2; i++) {
    built = built.length < 4000 ? '$built$i' : built.substring(2000);
  }
  final words = List.filled(2000, 'alpha beta gamma delta').join(' ').split(' ');
  final caps = words.map(capitalize).join(' ');
  final rev = [for (var i = 0; i < 20000; i++) '$i'].where((s) => s == s.split('').reversed.join()).length;
  // indexOf's -1 for a miss is the source's `(or nil -1)`.
  final idx = lines.take(10000).fold(0, (a, l) => a + l.indexOf('-5'));
  return [
    csv.length,
    total,
    names.length,
    upper,
    joined.length,
    replaced.length,
    regexed.length,
    trimmed,
    built.length,
    caps.length,
    rev,
    idx,
    caps.substring(0, 64),
  ];
}

void main(List<String> args) => benchMain(args, () => run(20000));
