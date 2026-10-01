# pippin

## Docs: read the index, then one file

- `docs/design.md` (the design of record, Russian) and `NOTES.md` (engineering notes, English) are indexes.
  The content is in `docs/design/` (a file per §; §3–§5 a file per subsection) and `docs/notes/` (a file per
  subsystem). Read the index and open only the file you need; do not load whole directories.
- Code and docs cite `design §4 «Атомы»`, `NOTES "Coroutines"`: the index maps a § and a title to its file. The §
  numbers and titles are those anchors, so do not renumber or rename them. A new subsection or subsystem file gets
  a line in its index.
- What is open: `docs/open.md`, or `grep -rnE '\[( |~)\]' docs/design docs/notes`.

## State marks

- `[x]` done, `[~]` partly done (the item names what is left), `[ ]` open. Design §10 steps, §9 questions and the
  §6b interpreter work order carry them; a NOTES entry carries `[ ]` or `[~]` when it names open work (a trigger,
  "Not done", "Deferred"), and an unmarked entry describes what exists.
- A commit that changes the state of something updates its mark and its status text in that same commit, runs
  `make open-items` and commits `docs/open.md` with it; `make open-items-audit` (in `make gates`) fails otherwise.
  A done NOTES item is deleted or rewritten as what exists, per NOTES.md.

## New NOTES entries

- Into the `docs/notes/` file of the subsystem, as `- **Title.** text` with continuation lines indented two spaces,
  and `[ ] `/`[~] ` after the dash when the entry names open work and its trigger. A subsystem without a file gets
  a new file and a line in NOTES.md. Architecture decisions go to the design, in Russian.
