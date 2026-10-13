// @ai-generated(solo)
// A port's result printed as JVM Clojure's pr-str prints the same value, so the harness compares strings.

String edn(Object? x) {
  final sb = StringBuffer();
  _write(sb, x);
  return sb.toString();
}

void _write(StringBuffer sb, Object? x) {
  switch (x) {
    case null:
      sb.write('nil');
    case bool b:
      sb.write(b);
    case int i:
      sb.write(i);
    case double d:
      sb.write(javaDouble(d));
    case String s:
      sb.write('"');
      for (final c in s.codeUnits) {
        switch (c) {
          case 0x22:
            sb.write(r'\"');
          case 0x5c:
            sb.write(r'\\');
          case 0x0a:
            sb.write(r'\n');
          case 0x09:
            sb.write(r'\t');
          case 0x0d:
            sb.write(r'\r');
          default:
            sb.writeCharCode(c);
        }
      }
      sb.write('"');
    case List l:
      sb.write('[');
      for (var i = 0; i < l.length; i++) {
        if (i > 0) sb.write(' ');
        _write(sb, l[i]);
      }
      sb.write(']');
    case Map m:
      sb.write('{');
      var first = true;
      for (final e in m.entries) {
        if (!first) sb.write(', ');
        first = false;
        _write(sb, e.key);
        sb.write(' ');
        _write(sb, e.value);
      }
      sb.write('}');
    default:
      throw ArgumentError('no EDN form for ${x.runtimeType}');
  }
}

/// Java's Double.toString: plain from 10^-3 to 10^7, else `d.dddE<n>`; both shortest round-trip digits.
String javaDouble(double d) {
  if (d.isNaN) return '##NaN';
  if (d.isInfinite) return d > 0 ? '##Inf' : '##-Inf';
  if (d == 0) return d.isNegative ? '-0.0' : '0.0';
  final a = d.abs();
  if (a >= 1e-3 && a < 1e7) {
    final s = d.toString();
    return s.contains('.') ? s : '$s.0';
  }
  final parts = d.toStringAsExponential().split('e');
  final mantissa = parts[0].contains('.') ? parts[0] : '${parts[0]}.0';
  return '${mantissa}E${int.parse(parts[1])}';
}
