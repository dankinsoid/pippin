### Функции
Clojure-fn ≠ Swift-замыкание: несколько арностей, `IFn` на keyword/map/vector/set, метаданные, identity, `apply`, свой RC и конвенция +0. Скомпилированная fn = `{header, table[arity]→C-функция, env}`. На границе — адаптеры в обе стороны (`Value.callAsFunction`, host-fn для Swift-closure).

