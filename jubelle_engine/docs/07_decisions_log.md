# 07 · Decisions log

Each decision: id, date, decision, reason, who decided. "User" decisions come from the user's instructions. "Engine"
decisions were made during investigation (the user allowed independent technical decisions in the investigation and
documentation phases) and can be revisited.

| Id | Date | Decision | Reason | By |
|---|---|---|---|---|
| D-E1 | 18 Nov 2026 | The engine is developed **in the same repository**, on the long-lived branch `engine/next`, in the directory `jubelle_engine/` | keeps history and integration with Jubelle; no reason for a separate repository | User (stated preference) and Engine |
| D-E2 | 18 Nov 2026 | Stable baseline preserved by the tag `v4.7.7.261118230` **and** the branch `checkpoint/stable-4.7.7.261118230` (commit `207614e5`) | recoverable regardless of other branches | Engine |
| D-E3 | 18 Nov 2026 | The engine is **GPL v3**; MuseScore code may be ported with notices; no clean-room separation | the program is GPL v3 anyway; reuse saves years of engraving work | Engine |
| D-E4 | 18 Nov 2026 | Engine core in **plain C++17, no Qt**; Qt only in the bridge | testable without the app (as Jubelle's logic already is), usable on any thread; matches MuseScore's C++ standard for integration | Engine |
| D-E5 | 18 Nov 2026 | **Player-centred model**; staves are presentation; **parts are layouts**, not score copies | removes link synchronisation and part copies; makes condensing and divisi native | Engine |
| D-E6 | 18 Nov 2026 | **Transactions and change sets** replace pointer-based undo commands | precise invalidation, safe history, autosave journal, hooks | Engine |
| D-E7 | 18 Nov 2026 | Layout is a **staged, cached, dependency-tracked pipeline**, with windowed system breaking and parallel measure-level stages | the measured cascade and widening are the main cause of slow edits | Engine |
| D-E8 | 18 Nov 2026 | Keep MuseScore's **audio engine, MPE, synths, encoders, draw backend, readers and importers**; replace only the score model, edit, layout and event generation | they are good, separate and tested; minimises risk | Engine |
| D-E9 | 18 Nov 2026 | Migration by **shadow mode** (both engines run, results compared), then an **opt-in per-score switch**, then a default switch only after Phase 7 | the DOM is used directly by 317 files; a big-bang swap would be unsafe | Engine |
| D-E10 | 18 Nov 2026 | **File format stays MuseScore 4.x**; Jubelle-only data in style and meta tags, or a `Jubelle/` folder in the `.mscz` (to be verified that MuseScore ignores it) | MuseScore compatibility is a requirement | User (compatibility) and Engine |
| D-E11 | 18 Nov 2026 | **Condensing is a presentation stage of the new engine only**; it is not implemented in the current engine; implementation waits for the user's explicit approval after testing RC2 | user's instruction | User |
| D-E12 | 18 Nov 2026 | Condensing defaults: woodwind and brass, adjacent same-instrument players with the same transposition, 2–4 per group, off by default for the full score | carries the earlier decisions (G2) forward, extended to 3–4 players | User (earlier) and Engine |
| D-E13 | 18 Nov 2026 | Condensing changes are stored by **time position and IDs**, never bar numbers | fixes the condensed copy's bar-number limitation | Engine |
| D-E14 | 18 Nov 2026 | Performance goals: edit and undo ≤ 16 ms, save ≤ 300 ms, memory ≤ 70 MB on the 39 × 285 orchestra; never slower on small scores | from the measured baselines; one frame per edit | Engine |
| D-E15 | 18 Nov 2026 | Record the FDK AAC licence as risk R-L1; plan a replacement before any wider distribution | it is not GPL-compatible | Engine |

## Open questions for the user (none block Phases 2–5)
1. Should the experimental engine switch be visible in normal Preferences, or only with a developer setting?
   Proposed: Preferences › Advanced, with a warning.
2. Can some of your own real scores be used (privately, never committed) as part of the test corpus?
3. Condensing: confirm the horn pairing default (1+3 / 2+4) and the label style ("Tpt. 1.2").
