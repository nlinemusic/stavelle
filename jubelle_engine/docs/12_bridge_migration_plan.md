# 12 · Bridge and consumer migration plan (Phase 2.6)

317 files outside the engraving library include its DOM headers. They are migrated in this order, from the inside
out, each step safe on its own because **MuseScore's engine also implements every new interface** (an adapter over
the DOM). Nothing switches engine until Phase 4.8.

| Order | Consumer group | Files (approx.) | What it uses the DOM for | Migration |
|---:|---|---:|---|---|
| 1 | `notation` internals (`INotation*` implementations) | 40 | everything | becomes the bridge: two implementations, the MuseScore DOM one (today's code) and the Jubelle engine one |
| 2 | `notation/internal/jubelle` (Jubelle logic) | 17 | reading music, writing edits | port to the engine API (most logic is already plain C++ planners with tests) |
| 3 | `playback` | 3 | track and instrument lookups | engine API (players, instruments, tracks) |
| 4 | `instrumentsscene` | 4 | parts and staves | engine API (players, layouts) |
| 5 | `inspector` (Properties) | 44 | reading and writing properties by `Pid` on selected elements | an **element-properties API** (`get(id, Pid)`, `set(ids, Pid, value)`, element kind, default value): already `Pid`-based, so mechanical |
| 6 | `palette` | 24 | palette cells hold DOM element prototypes, drop logic | palette cells keep MSCX fragments (as today in files); drop goes through `applyPaletteElement` |
| 7 | `notationscene` (score view, widgets) | 61 | painting, hit testing, edit modes, note input bar, various dialogs | painting through the display list; dialogs through the engine API |
| 8 | `project` | 7 | thumbnails, score properties, templates | engine API |
| 9 | `braille` | 4 | reading the score | snapshot reader API |
| 10 | `importexport` | 121 | MusicXML, MIDI, Guitar Pro, MNX, MEI, … | **stay on the DOM** behind the converter: import into a DOM score, convert to the model. Export converts the model to a DOM, then exports. Rewritten only if a reason appears |
| 11 | plugin API (`engraving/api`) | 34 | the QML plugin object model | API objects wrap engine IDs; plugin compatibility tests |

## Engine-neutral interfaces introduced (implemented by both engines)
- `IScoreReader` (snapshot queries: players, measures, events, attachments, properties, time maps)
- `IElementProperties` (by ID and `Pid`, with defaults and per-layout overrides)
- `IPainting` (display list per page, hit testing)
- `ITransactions` (begin/commit/rollback, operations of `10_edit_vocabulary.md`, undo/redo, change notifications)
- `IPlaybackSource` (MPE event maps by track, change notifications)

## The switch (decision D-E16)
- Hidden developer setting `jubelle/engine/experimental` (off), not shown in Preferences until the engine is mature.
- When on: new scores and opened scores use the Jubelle engine; saving writes MuseScore 4.x files through `je::io`; a
  status-bar indicator shows "Jubelle engine (experimental)"; Report a Bug records the engine as such (diagnostics id
  `engine`).
- When off: exactly the released behaviour.
