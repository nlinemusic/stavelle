# 14 · Testing build E1: the new engine with live condensing

_18 Nov 2026, following the user's instruction of 20:36: the **next Jubelle testing build uses the new engine and
includes condensing**. There is no further testing build on the MuseScore-based engine alone. Condensing in the new
engine is **approved** (configurable player groups, D-E12; abbreviated labels such as "Tpt. 1.2", D-E18)._

## 1. What changes in the plan
| Before | Now |
|---|---|
| Phases 4 → 5 → 6 in order; the new engine becomes the default only after Phase 7 | **E1 is the target**: the work needed for a testing build with the new engine and condensing comes first; everything else follows in later engine builds (E2, E3 …) |
| Condensing waits for approval | approved; implemented in `je::present` now |
| Experimental switch off by default | **on in testing builds**; the hidden developer setting (D-E16) turns the new engine off, as a fallback for diagnosis |
| AAC replacement and style-setting preservation "for a future release" | ship **in E1** (they're in modules E1 keeps) |

Unchanged: the stable release v4.7.7.261118230 (tag and checkpoint branch) and `main` are not touched; all work is on
`engine/next`; MuseScore 4.x file compatibility; every current Jubelle feature keeps working; nothing is published
until the Windows and macOS checks in §4 pass.

## 2. What "the new engine" does in E1, and what it doesn't yet
Replacing every part of MuseScore's engine (about 260,000 lines, with every Jubelle feature on top) in one step is
the risky wholesale replacement the project ruled out, and it couldn't be tested thoroughly in a reasonable time. E1
therefore puts the new engine in charge of **the parts that condensing needs**, and keeps MuseScore's proven code for
the rest until each part is replaced and compared in later builds.

| Layer | E1 | Later builds |
|---|---|---|
| Music model (`je::model`) | **new engine**: a player-centred copy of the score, built from MuseScore's score on open and kept in step after every edit and undo (change sets from the existing undo stack) | becomes the only model |
| Presentation (`je::present`): layouts, staff assignment, **condensing** | **new engine**: groups, phrases, classifier, staff plans, labels, condensing changes | divisi, more layouts |
| Jubelle engine data (`je::io` store) | **new engine**: versioned JSON in score meta tags (condensing settings and changes, style mirror) | all Jubelle data |
| Editing and undo | MuseScore's (every existing command and Jubelle feature unchanged); clicks on a condensed staff select the **source** notes | `je::edit` transactions (E2) |
| Playback | MuseScore's, from the players' music (condensing never changes sound) | `je::perform` (E2) |
| Layout and drawing | MuseScore's layout, used as the **drawing backend**: the new engine's condensed presentation is turned into a display score that MuseScore lays out | `je::layout` stages one by one, compared in shadow mode (E3 onward) |
| File reading and writing | MuseScore's, plus the engine store | `je::io` native (later) |

So in E1 the condensed full score is **live**: it is part of the score (not a separate copy file), it updates after
each edit, and its settings are saved inside the score. The existing "condensed copy" command stays available.

## 3. Work for E1, in order
| # | Work | Exit criterion |
|---|---|---|
| E1.1 | Model core types on the copy-on-write containers (score, flows, players, voices, events, notes, ties, attachments needed for condensing) | unit and snapshot tests, sanitizers clean |
| E1.2 | DOM → model converter, and incremental sync after edits, undo and redo | every corpus score converts; after scripted edit sequences, sync equals a full conversion |
| E1.3 | `je::present` condensing: groups (configurable, defaults table), phrases, classifier (ported from the tested planner, with smoothing), staff plans, labels ("Tpt. 1.2"; full names where the layout uses long names) | the specification's tests (`04_…` §14) and the planner's existing tests pass |
| E1.4 | Presentation → display score (drawing backend), live update of the changed systems only | display equals a full rebuild after every scripted edit; update time measured on the large orchestra |
| E1.5 | Interface: a "Condensed" view of the full score, condensing settings (groups, staff sets, on/off), expand / condense here, reset; selection mapped to source notes | the interface self-test covers each command |
| E1.6 | Engine store in meta tags; Jubelle style-setting preservation (approved 18 Nov) | save in Jubelle → open and save in official MuseScore 4.7.7 → reopen in Jubelle keeps every setting (CI) |
| E1.7 | AAC encoder replacement (approved 18 Nov; `08_…`) | AAC export and MP4 sound checks pass on Windows and macOS; FDK gone from the build |
| E1.8 | Full test pass (§4), then the testing build | all gates green; the user's hands-on test |

## 4. Gates before E1 is published (Windows and macOS)
1. The full existing app checks (open, save, reopen, identity, timing), interface self-test, platform reliability
   tests, export checks, and coexistence test, with the new engine **on**. No check is disabled or weakened.
2. Engine tests (unit, property, sanitizers) on Windows, macOS and Linux.
3. Condensing tests: specification cases, the planner's tests, round trips (condensing settings survive save,
   MuseScore save, reopen), edit-sync equality, and determinism.
4. File compatibility: official MuseScore 4.7.7 opens every file Jubelle E1 writes, with the same music (MIDI
   identical) and every player on their own staff.
5. Performance: no measured regression against the engine baselines with condensing off; the condensed update time
   on the large orchestra is reported.
6. A visual review set (PDF pages of condensed scores) for the user.
7. Published as a **pre-release** with its own version; the stable release stays the latest stable version.
