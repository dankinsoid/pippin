// @ai-generated(solo)
// workloads.json (bench/workloads/src/workloads/json.clj) in idiomatic Dart: jsonDecode into fromJson classes,
// toJson and jsonEncode back, as a Flutter app does without code generation.
import 'dart:convert';

import 'package:workloads/harness.dart';
import 'package:workloads/util.dart';

const firstNames = ['Anna', 'Борис', 'Chloé', 'Dmitri', 'Émile', 'Фаина', 'Grace', 'Hiroshi', 'Иван', 'Jürgen',
    'Kateřina', 'Lucía'];
const lastNames = ['Smith', 'Петрова', 'Müller', 'García', 'Новиков', 'Łukasiewicz', "O'Brien", 'Søren', 'Yamada', 'Çelik'];
const words = ['the', 'quick', 'brown', 'fox', 'café', 'naïve', 'über', 'привет', 'мир', '東京', 'data', 'screen', 'feed',
    'today', 'день', 'résumé', 'jalapeño', 'smörgåsbord', 'news', 'Ереван'];
const fragments = [r'\"quoted\"', r'\n', r'\t', r'C:\\Users', 'https://example.com/a/b?c=1',
    r'caf\u00e9 \u043f\u0440\u0438\u0432\u0435\u0442'];
const tagPool = ['news', 'tech', 'food', 'travel', 'music', 'sport', 'art', 'science', 'games', 'books', 'film', 'photo'];
const kinds = ['post', 'photo', 'video', 'repost', 'ad'];
const currencies = ['USD', 'EUR', 'AMD', 'RUB'];
const cities = ['Yerevan', 'Lisbon', 'Tbilisi', 'Berlin', 'Kyoto', 'São Paulo', 'Montréal', 'Москва'];

// ---- the response's text, as the source writes it

String pick(List<String> v, int x) => v[x % v.length];

String q(String s) => '"$s"';

String pad(int n, int width) => '$n'.padLeft(width, '0');

String title(int r) => [for (var k = 0; k < 3 + r % 4; k++) pick(words, r ~/ 7 + k * 5)].join(' ');

String body(int r, int i) {
  final nw = words.length;
  final parts = <String>[];
  for (var k = 0; k < 12 + r % 20; k++) {
    final t = (r ~/ 3 + k * 11 + i) % (nw + fragments.length);
    parts.add(t < nw ? words[t] : fragments[t - nw]);
  }
  return parts.join(' ');
}

String rating(int r) => '${1 + r % 4}.${r ~/ 4 % 10}${1 + r ~/ 40 % 9}';

String coordinate(int r, bool sign, int whole) =>
    '${sign ? '-' : ''}${1 + r ~/ 3 % whole}.${pad(r ~/ 240 % 1000, 3)}${1 + r ~/ 240000 % 9}';

String authorJson(int r4, int r5) {
  final u = r4 % 400;
  return '{"id":${q('u-$u')},"name":${q('${pick(firstNames, r4)} ${pick(lastNames, r4 ~/ 12)}')}'
      ',"handle":${q('@user$u')},"avatarUrl":${q('https://cdn.example.com/avatars/$u.jpg')}'
      ',"verified":${r5 % 5 == 0},"followers":${r5 % 100000}'
      ',"bio":${r5 % 3 == 0 ? 'null' : q(pick(fragments, r5))}'
      ',"_links":{"self":${q('/v2/users/$u')},"posts":${q('/v2/users/$u/posts')}}}';
}

String mediaJson(int r, int i, int k) =>
    '{"type":${q(k.isEven ? 'image' : 'video')},"url":${q('https://cdn.example.com/m/$i/$k.jpg')}'
    ',"width":${640 + 320 * ((r ~/ 3 + k) % 4)},"height":${480 + 240 * ((r ~/ 12 + k) % 3)}'
    ',"alt":${(r + k).isEven ? 'null' : q(pick(words, r + k))}'
    ',"blurhash":"LEHV6nWB2yk8pyo0adR*.7kCMdnj"}';

String commentJson(int r, int i, int k) =>
    '{"id":${q('c-$i-$k')},"author":${q(pick(firstNames, r + k))},"text":${q(title(r + k))},"likes":${(r + k) % 50}}';

String postJson(int i, List<int> r) {
  final kind = pick(kinds, r[0]);
  final r1 = r[1], r11 = r[11], r12 = r[12];
  return '{"id":${q('p-${100000 + i * 7}')},"kind":${q(kind)}'
      ',"createdAt":${q('2026-${pad(1 + r1 % 12, 2)}-${pad(1 + r1 ~/ 12 % 28, 2)}T${pad(r1 % 24, 2)}:${pad(r1 ~/ 24 % 60, 2)}:00Z')}'
      ',"title":${q(title(r[2]))},"body":${q(body(r[3], i))},"author":${authorJson(r[4], r[5])}'
      ',"stats":{"likes":${r[6] % 5000},"comments":${r[6] ~/ 5000 % 300},"shares":${r[7] % 100}'
      ',"views":${100 + r[7] % 100000},"rating":${rating(r[8])}}'
      ',"tags":[${[for (var k = 0; k < r[9] % 4; k++) q(pick(tagPool, r[9] ~/ 4 + k * 3))].join(',')}]'
      ',"media":[${[for (var k = 0; k < r[10] % 3; k++) mediaJson(r[10], i, k)].join(',')}]'
      ',"price":${r11 % 4 == 0 ? '{"amount":${q('${r11 ~/ 8 % 200}.${pad(r11 ~/ 1600 % 100, 2)}')},"currency":${q(pick(currencies, r11 ~/ 160000))}}' : 'null'}'
      ',"location":${r12 % 3 == 0 ? '{"lat":${coordinate(r12, false, 80)},"lon":${coordinate(r12 ~/ 7, (r12 ~/ 5).isEven, 170)},"name":${q(pick(cities, r12 ~/ 11))}}' : 'null'}'
      ',"flags":{"pinned":${r[13] % 50 == 0},"hidden":${r[13] % 17 == 0},"sponsored":${kind == 'ad'}}'
      ',"tracking":{"impressionId":${q('imp-${r[14]}')},"position":$i,"experiment":${q('exp-${r[14] % 7}')},"variant":"b"}'
      ',"commentsPreview":[${[for (var k = 0; k < r[15] % 3; k++) commentJson(r[15], i, k)].join(',')}]}';
}

String feedText(int n) {
  // The generator's low bits cycle with a short period: a post's draws are its states' top 24 bits.
  final rs = [for (final x in lcgList(42, 16 * n)) x ~/ 128];
  return '{"status":"ok","generatedAt":"2026-10-15T12:00:00Z","items":['
      '${[for (var i = 0; i < n; i++) postJson(i, rs.sublist(16 * i, 16 * (i + 1)))].join(',')}'
      '],"paging":{"cursor":${q('c-$n')},"hasMore":true,"limit":$n}'
      ',"experiments":{"feedRanking":"v3","inlineVideo":false}}';
}

// What an http response's body is: the text, made once per process as the source's payload is.
final payload = feedText(800);

// ---- the app's model: fromJson reads the keys it knows, so the unknown ones are dropped

enum Kind { post, photo, video, repost, ad }

enum MediaType { image, video }

enum Currency { USD, EUR, AMD, RUB }

int parseId(String s) => int.parse(s.substring(s.indexOf('-') + 1));

int cents(String amount) {
  final parts = amount.split('.');
  return int.parse(parts[0]) * 100 + int.parse(parts[1]);
}

class Author {
  final int id;
  final String name, handle, avatarUrl;
  final bool verified;
  final int followers;

  Author.fromJson(Map<String, dynamic> j)
      : id = parseId(j['id'] as String),
        name = j['name'] as String,
        handle = j['handle'] as String,
        avatarUrl = j['avatarUrl'] as String,
        verified = j['verified'] as bool,
        followers = j['followers'] as int;

  Map<String, dynamic> toJson() => {
        'id': id,
        'name': name,
        'handle': handle,
        'avatarUrl': avatarUrl,
        'verified': verified,
        'followers': followers,
      };
}

class Stats {
  final int likes, comments, shares, views;
  final double rating;

  Stats.fromJson(Map<String, dynamic> j)
      : likes = j['likes'] as int,
        comments = j['comments'] as int,
        shares = j['shares'] as int,
        views = j['views'] as int,
        rating = (j['rating'] as num).toDouble();

  Map<String, dynamic> toJson() =>
      {'likes': likes, 'comments': comments, 'shares': shares, 'views': views, 'rating': rating};
}

class Media {
  final MediaType type;
  final String url;
  final int width, height;

  Media.fromJson(Map<String, dynamic> j)
      : type = MediaType.values.byName(j['type'] as String),
        url = j['url'] as String,
        width = j['width'] as int,
        height = j['height'] as int;

  Map<String, dynamic> toJson() => {'type': type.name, 'url': url, 'width': width, 'height': height};
}

class Place {
  final double lat, lon;
  final String name;

  Place.fromJson(Map<String, dynamic> j)
      : lat = (j['lat'] as num).toDouble(),
        lon = (j['lon'] as num).toDouble(),
        name = j['name'] as String;

  Map<String, dynamic> toJson() => {'lat': lat, 'lon': lon, 'name': name};
}

class Post {
  final int id;
  final Kind kind;
  final String createdAt, title, body, day;
  final Author author;
  final Stats stats;
  final List<String> tags;
  final List<Media> media;
  final bool pinned, hidden;
  final int? priceCents;
  final Currency? currency;
  final Place? place;
  int score = 0;

  Post.fromJson(Map<String, dynamic> j)
      : id = parseId(j['id'] as String),
        kind = Kind.values.byName(j['kind'] as String),
        createdAt = j['createdAt'] as String,
        title = j['title'] as String,
        body = j['body'] as String,
        day = (j['createdAt'] as String).substring(0, 10),
        author = Author.fromJson(j['author'] as Map<String, dynamic>),
        stats = Stats.fromJson(j['stats'] as Map<String, dynamic>),
        tags = (j['tags'] as List).cast<String>(),
        media = [for (final m in j['media'] as List) Media.fromJson(m as Map<String, dynamic>)],
        pinned = j['flags']['pinned'] as bool,
        hidden = j['flags']['hidden'] as bool,
        priceCents = j['price'] == null ? null : cents(j['price']['amount'] as String),
        currency = j['price'] == null ? null : Currency.values.byName(j['price']['currency'] as String),
        place = j['location'] == null ? null : Place.fromJson(j['location'] as Map<String, dynamic>);

  // Optional parts are written only when present, as the source assocs them only when the response has them.
  Map<String, dynamic> toJson() => {
        'id': id,
        'kind': kind.name,
        'createdAt': createdAt,
        'title': title,
        'body': body,
        'author': author.toJson(),
        'stats': stats.toJson(),
        'tags': tags,
        'media': [for (final m in media) m.toJson()],
        'day': day,
        'pinned': pinned,
        'hidden': hidden,
        'score': score,
        if (priceCents != null) 'priceCents': priceCents,
        if (currency != null) 'currency': currency!.name,
        if (place != null) 'place': place!.toJson(),
      };
}

List<Object> run() {
  final feed = jsonDecode(payload) as Map<String, dynamic>;
  final items = feed['items'] as List;
  final posts = [for (final p in items) Post.fromJson(p as Map<String, dynamic>)].where((p) => !p.hidden).toList();
  for (final p in posts) {
    p.score = p.stats.likes + 2 * p.stats.comments + 3 * p.stats.shares + (p.pinned ? 1000 : 0);
  }
  posts.sort((a, b) => a.score != b.score ? b.score.compareTo(a.score) : a.id.compareTo(b.id));
  final paging = feed['paging'] as Map<String, dynamic>;
  final tagCounts = <String, int>{};
  for (final p in posts) {
    for (final t in p.tags) {
      tagCounts[t] = (tagCounts[t] ?? 0) + 1;
    }
  }
  final authorCount = {for (final p in posts) p.author.id}.length;
  final out = jsonEncode({
    'posts': [for (final p in posts) p.toJson()],
    'paging': {'cursor': paging['cursor'], 'hasMore': paging['hasMore']},
    'tagCounts': tagCounts,
    'authorCount': authorCount,
  });
  final top = posts.first;
  return [
    items.length,
    posts.length,
    authorCount,
    tagCounts.length,
    posts.where((p) => p.author.verified).length,
    posts.fold<int>(0, (a, p) => a + p.score),
    posts.fold<int>(0, (a, p) => a + (p.priceCents ?? 0)),
    posts.where((p) => p.place != null).length,
    top.id,
    top.title,
    out.length,
  ];
}

void main(List<String> args) => benchMain(args, run);
