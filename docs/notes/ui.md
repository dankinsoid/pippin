## UI (lib/src/pippin/ui/, lib/test/pippin/ui/, Tests/PippinTests/ReconcilerTests.swift)

- **The reconciler is a plain library on the load path, not an embedded one.** `lib/src` and `lib/test` are
  its roots, loaded exactly as a corpus library is (`Runtime.loadPath`, `clj_load_resource_path`), so
  `boot/`, `libs_clj.inc` and the count of embedded libs that `make boot` and the shaker rest on are
  untouched. The language knows nothing about UI (design §5b), and neither does the core binary.
- **The backend protocol as built** (`pippin.ui.reconciler/Backend`): `create!` (tag, attrs → an opaque
  handle), `update!` (tag, handle, prev attrs, new attrs), `remove!` (tag, handle) — the three per tag of
  design §5b — plus `insert!` (parent, handle, index) and `detach!` (parent, handle), which belong to no tag.
  A tag's own three are what a per-backend registry resolves; the reconciler passes the tag through and never
  looks at a handle. `:key` is the reconciler's attribute and reaches no backend decision, but it is passed
  in the attrs map as the app wrote it rather than stripped, so no map is rebuilt per render.
- **What the zero-call test proves.** `an-unchanged-subtree-costs-no-backend-call`: a chain eight nodes deep
  is built once, both renders hold that pointer, only the root's attrs move — the log is the root's one
  `update!` and the returned mount child is `identical?` to the old one. The contrast is
  `an-equal-subtree-with-a-new-pointer-is-walked`: the same subtree built twice is `=` at every level, so a
  props diff emits nothing either — the empty log is not the evidence. What tells the two apart is that the
  second case rebuilds the mount node level by level, and `a-walk-that-reaches-the-bottom-says-so` shows the
  walk arriving at the leaf. So the zero in the first case comes from the pointer check and nothing else.
- **Both backends run the same `clojure.test` namespace.** `ReconcilerTests` requires it from the load path,
  then compiles the three files as C units (`skip_embedded`, so `clojure.test` stays interpreted), dlopens
  them, registers them for their paths and loads them again; dev and `--closed` must return the interpreted
  run's `[test pass fail error]`. Collection feeds the source text, not `clj_load_file`: a path whose unit is
  already registered runs that unit, so the second compile would see no forms.
- [ ] **A keyed reorder is not minimized.** Children are matched by key and each one that is not already at
  its index costs `detach!` + `insert!`, which is at most one move per child but not the fewest possible —
  a reversal of n rows is n-1 moves where a longest-increasing-subsequence pass would be ~n/2. Trigger: a
  measured frame where the moves, and not the attribute writes, are what costs.
- [ ] **A list that goes from unkeyed to keyed is remounted.** The old children carry no key, so nothing
  matches and every row is destroyed and rebuilt. It is silent, not loud, because the two trees are each
  legal on their own. Trigger: it happening to someone; the fix is to record the old list's scheme in the
  mount node and refuse the switch.
- [ ] **`patch-in` rests on an invariant nothing checks.** Re-rendering one node in place leaves its
  ancestors' `:hiccup` pointing at the subtree the app no longer renders, so a later render of the parent
  that re-describes that node reverts the component's own frame (design §5b, "Перерисовка одного
  компонента"). Trigger: the cursor/subscription slice, which is what decides how a component boundary is
  marked in the tree.
- [ ] **A `[:native thunk]` node cannot carry a `:key`.** It has no attrs map, and the key in the vector's
  meta (design §5b, "`:key` сопоставляется по проходу") is not read, so a keyed list of native nodes has to
  wrap each one in a host tag. Trigger: a real screen whose reordered rows are native views.
- [ ] **Children are flattened, not slotted.** `append-children` drops a `nil` child and splices a seq into
  its parent's children, so `[:column (when c [:banner]) [:list]]` shifts `:list` to index 0 when `c` turns
  false and remounts it; design §5b ("Идентичность узла") has `nil` hold its slot and a seq be one keyed
  fragment. Trigger: the component slice, which rewrites `children-of` anyway.
- [ ] **No component heads.** `hiccup?` takes only a keyword head, so `[#'f args]` (and a bare fn) is
  refused; the argument memo, the var watch that re-renders mounted instances, the paren-call lint and the
  node-owned `:ui/local` atom and `:ui/managed` resource of design §5b ("Компонент", "Локальное состояние")
  are all unbuilt. Trigger: the cursor/subscription slice.
- [ ] **The 2026-10-05 identity and authoring rules are unbuilt** (design §5b): `:key` matched across the
  pass (unmatched keyed removals and creations paired by key and head after the diff), single-child wrapper
  chains matched by their base, the key in a vector's meta, standard attributes expanded into layout
  wrappers and layout-only tags without a host view, a view-valued attribute as a named child slot,
  `defui` and `ui->>`, and the loud error for a Reagent form-2 head. Trigger: the component slice.
- [ ] **Whether UIKit drops first responder on a move is unverified.** A pass-wide key move and a wrapper
  appearing around a field both `detach!` + `insert!` its view; if a superview change resigns first
  responder, the backend has to restore it after `insert!`. Trigger: the first move of a focused field on a
  device.
- [ ] **No lazy layout, `:items`/`:item`, cursor-bound attributes or event vectors** (design §5b). They wait
  for Yoga and the UIKit backend. Unbuilt beside the lazy layout itself: the size estimate of rows never
  laid out (and the scroll anchoring it needs), an `UIAccessibilityContainer` over virtualized rows, swipe
  actions, reordering, sticky headers, prefetch. Trigger: a list longer than a screen on a device; the
  accessibility container before any such list ships.
