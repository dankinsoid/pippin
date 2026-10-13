// @ai-generated(solo)
// workloads.render (bench/workloads/src/workloads/render.clj) in idiomatic Dart.
import 'dart:convert';

import 'package:workloads/harness.dart';

const escaper = HtmlEscape(HtmlEscapeMode(escapeLtGt: true, escapeQuot: true));

sealed class Node {
  String render();
}

class Text extends Node {
  final String text;
  Text(this.text);
  @override
  String render() => escaper.convert(text);
}

class Number extends Node {
  final int value;
  Number(this.value);
  @override
  String render() => '$value';
}

class Element extends Node {
  final String tag;
  final Map<String, Object> attrs;
  final List<Node> children;
  Element(this.tag, this.children, [this.attrs = const {}]);

  // Each node renders to its own string and its parent concatenates them, as the source's `str` does.
  @override
  String render() {
    final names = attrs.keys.toList()..sort();
    final a = names.map((k) => ' $k="${escaper.convert('${attrs[k]}')}"').join();
    return '<$tag$a>${children.map((c) => c.render()).join()}</$tag>';
  }
}

Element item(int i) => Element('li', [
      Element('span.title', [Text('Item <$i> & co')]),
      Element('div', [
        for (var j = 0; j < i % 4; j++) Element('a', [Text('link '), Number(j)], {'href': '/items/$i/$j'}),
      ], {'style': 'color: red'}),
    ], {'class': i.isEven ? 'even' : 'odd', 'data-id': i});

Element page(List<int> items) => Element('html', [
      Element('head', [Element('title', [Text('List')])]),
      Element('body', [
        Element('h1', [Text('Items')]),
        Element('ul', [for (final i in items) item(i)], {'id': 'items'}),
        Element('footer', [Text('${items.length} items')]),
      ]),
    ]);

List<Object> run(int n) {
  final html = page(List.generate(n, (i) => i)).render();
  final small = Iterable.generate(n).fold(0, (a, i) => a + item(i).render().length);
  return [html.length, small, html.substring(0, 200)];
}

void main(List<String> args) => benchMain(args, () => run(4000));
