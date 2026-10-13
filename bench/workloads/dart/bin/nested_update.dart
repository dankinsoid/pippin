// @ai-generated(solo)
// workloads.nested-update (bench/workloads/src/workloads/nested_update.clj) in idiomatic Dart.
import 'package:workloads/harness.dart';
import 'package:workloads/util.dart';

class User {
  String? name;
  int? visits, seen, score;
  List<int>? tags;
}

class AppState {
  final users = <int, User>{};
  final log = <int>[];
  var ok = 0, err = 0;
  final index = <int, Map<int, int>>{};

  void step(int x) {
    final uid = x % 500;
    switch ((x ~/ 7) % 6) {
      case 0:
        users.putIfAbsent(uid, User.new).name = 'user-$uid';
      case 1:
        final u = users.putIfAbsent(uid, User.new);
        u.visits = (u.visits ?? 0) + 1;
      case 2:
        (users.putIfAbsent(uid, User.new).tags ??= []).add(x % 10);
      case 3:
        if (x.isEven) {
          ok++;
        } else {
          err++;
        }
        if (log.length >= 100) log.removeAt(0);
        log.add(uid);
      case 4:
        final u = users.putIfAbsent(uid, User.new);
        u.seen = x;
        u.score = (u.score ?? 0) + x % 13;
      case 5:
        index.putIfAbsent(x % 50, () => {})[x % 7] = uid;
    }
  }
}

List<int> run(int n) {
  final st = AppState();
  var x = 1, reads = 0;
  for (var i = 0; i < n; i++) {
    x = lcg(x);
    reads += (st.users[x % 500]?.visits ?? 0) + (st.index[x % 50]?.length ?? 0);
    st.step(x);
  }
  final users = st.users.values;
  return [
    users.length,
    users.fold(0, (a, u) => a + (u.visits ?? 0)),
    users.fold(0, (a, u) => a + (u.tags?.length ?? 0)),
    users.fold(0, (a, u) => a + (u.score ?? 0)),
    st.ok,
    st.err,
    st.log.fold(0, (a, b) => a + b),
    st.index.values.fold(0, (a, m) => a + m.length),
    reads,
  ];
}

void main(List<String> args) => benchMain(args, () => run(200000));
