### Dev client (паттерн Expo)
Dev-сборка = ядро + интерпретатор + все мосты для подключённых фреймворков. REPL грузит Clojure поверх. Новая Swift-зависимость или новый протокол для `reify` → генерация стаба + пересборка dev-шелла (редко). `(require-swift 'Foundation.DateFormatter)` в REPL может триггерить генерацию и фоновую пересборку. Hot reload через `@_dynamicReplacement` + dylib — для правок *Swift*-стороны моста.

---

