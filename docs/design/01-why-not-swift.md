## 1. Почему Swift плохой хост «в лоб»

- **Нет eval / JIT.** На iOS JIT запрещён (`dynamic-codesigning` только у JSC и браузеров в ЕС). Swift REPL = LLDB + ORC JIT, медленный и глючный. `dlopen` подписанных dylib работает только в dev-сборках на симуляторе/Маке.
- **Нет рефлексии вызовов.** Дженерики мономорфизируются, `Mirror` read-only, Swift ABI (`swiftself`/`swifterror`, скрытые metadata/witness table параметры) libffi не умеет. Произвольный метод произвольного типа динамически не позвать.
- **SwiftUI — плохая цель.** Идентичность завязана на статические `some View`; динамический язык даёт только `AnyView`. UIKit (ObjC) — нормальная.
- **ARC.** Атомарный всегда, циклы через lazy seq, рекурсивный dealloc на длинных списках, `Any`-боксинг.
- **swiftc медленный** (type checker), сгенерированный код с существенциалами — секунды на файл.
- **Swift 6 strict concurrency** спорит с боксами `Any`.

Хорошие хосты — те, где код грузится в рантайме + GC + рефлексия: JVM, JS, CLR, BEAM, Lua, Python. Dart и Swift — нет; ClojureDart живёт AOT-only с hot restart Flutter'а.

**ObjC-рантайм** — отличный хост (Smalltalk поверх C): `objc_msgSend`, `objc_allocateClassPair`, `imp_implementationWithBlock`, `NSInvocation`. Прецеденты: Nu, F-Script, MacRuby, clojure-objc (2014), NativeScript (генерирует metadata на весь SDK). Но экосистема уезжает в Swift-only.

---

