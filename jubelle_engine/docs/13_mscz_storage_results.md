# 13 · Jubelle data inside `.mscz`: tested against official MuseScore 4.7.7

_Decision D-E10 required this test before relying on MuseScore ignoring Jubelle's data. Test:
`tools/jubelle_checks/mscz_storage_test.py`, run by the `mscz_storage` job of "Jubelle: Coexistence Test (Windows)"
(runs 44441927195, 44442286667, 44442706173, 18 Nov 2026) with the official MuseScore 4.7.7 Windows installer._

## Method
A score saved by Jubelle (`tools/jubelle_checks/data/Jubelle_features.mscz`: 17 instruments, 40 bars, with
Jubelle's real style values and meta tags) was changed in one way per variant. For each variant, official MuseScore:
1. opened it and wrote a PDF;
2. wrote MIDI, compared byte for byte with the unchanged score's MIDI;
3. saved it as a new `.mscz`, which was inspected.

## Results
| Where Jubelle data is put | MuseScore opens it | Same music | **Kept when MuseScore saves** |
|---|---|---|---|
| A `Jubelle/` folder inside the ZIP | yes | yes | **no**: the folder is dropped |
| A score meta tag (`<metaTag name="jubelle…">`) | yes | yes | **yes** |
| A 120 kB JSON value in a meta tag (quotes, `&`, `<>`, accents, ♭ ♯ 𝄞) | yes | yes | **yes, exactly** |
| An unknown style value (`score_style.mss`) | yes | yes | **no** |
| An unknown tag inside a chord (`score.mscx`) | yes | yes | **no** |
| An unknown style value in a part's `.mss` | yes | yes | **no** |
| All of the above together | yes | yes | only the meta tags |
| The score exactly as Jubelle saved it | yes | yes | **Jubelle's 11 style values are lost; its 2 meta tags are kept** |
| Element IDs (`<eid>`) | n/a | n/a | **8,449 of 17,851 kept: exactly as many as the main score holds** (8,449); the other 9,402 belong to the part copies stored inside the main file, which MuseScore renumbers |

MuseScore printed no warning about any variant.

## Conclusions for the engine (decision D-E10, revised)
1. **Compatibility holds:** extra Jubelle data never stops MuseScore from opening a file, and never changes the music
   it reads.
2. **The durable store is the score meta tag.** Jubelle's engine data goes into namespaced meta tags with versioned
   JSON values (`jubelle.store/1`), which survive MuseScore saving the file, even at 120 kB.
3. **A `Jubelle/` folder is only for caches** (data that can be rebuilt, e.g. layout caches, thumbnails of layouts),
   since MuseScore drops it on save.
4. **Element-anchored data** (per-element Jubelle settings, condensing changes, linked passages) is keyed by the
   **main score's `eid`s** (kept by MuseScore) and by flow time positions, never by part-copy IDs (renumbered) or
   by unknown element tags (dropped).
5. **Style values that MuseScore doesn't know are dropped** when MuseScore saves. The engine mirrors every
   `Sid::jubelle…` value into the meta-tag store, and restores it on load when the style value is missing.

## Finding about the current release (not an engine change)
Today, if a Jubelle score is opened **and saved in MuseScore**, Jubelle's **style settings are lost**: courtesy
accidental rules, magnetic layout, collision avoidance, engraving rules and nudge settings. Its meta-tag data (swing,
articulation playback, tempo points, page class) survives. Element-level Jubelle data (Play this repeat) is also lost.
Opening in MuseScore without saving changes nothing.
**Recommended small fix** for a future release (needs the user's approval; it changes the released app): mirror
Jubelle's style values into a meta tag on save, and restore them on load when missing.
