### Мультиметоды
Целиком в рантайме (MultiFn + таблица + иерархия + кэш диспатча под тем же Mutex). Хост всплывает только в `isa?` на хостовых типах — читать суперкласс из class metadata, конформанс через `swift_conformsToProtocol`, `@objc` — через `class_getSuperclass`.

