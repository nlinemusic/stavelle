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
| D-E12 | 18 Nov 2026 | Condensing: off by default for the full score; **grouping is configurable** (any compatible players, explicit staff sets or automatic sharing); defaults per section are a data table, nothing hardcoded (no fixed 1+3 / 2+4); woodwind and brass proposed, strings not | user's answer of 18 Nov; earlier G2 decisions | **User** |
| D-E13 | 18 Nov 2026 | Condensing changes are stored by **time position and IDs**, never bar numbers | fixes the condensed copy's bar-number limitation | Engine |
| D-E14 | 18 Nov 2026 | Performance goals: edit and undo ≤ 16 ms, save ≤ 300 ms, memory ≤ 70 MB on the 39 × 285 orchestra; never slower on small scores | from the measured baselines; one frame per edit | Engine |
| D-E15 | 18 Nov 2026 | FDK AAC investigated (`08_aac_encoder_licensing.md`): its notice is missing from the installers, and its patent clause makes the combined GPL binary a compliance risk; recommended: notices now, then replace it with FFmpeg's native AAC (already bundled, LGPL) or OS encoders, then remove FDK. Awaiting the user's decision (it changes the released app) | user asked for a thorough investigation, not assuming personal use is exempt | Engine (recommendation) |
| D-E16 | 18 Nov 2026 | The experimental engine switch is **hidden behind a developer setting**; it moves to normal Preferences only when the engine is mature and tested | user's answer | **User** |
| D-E17 | 18 Nov 2026 | The user's own scores may be used for **private, local** testing only: kept **outside the repository** by default (`~/JubellePrivateScores`, or the path in `JUBELLE_PRIVATE_CORPUS`), with `jubelle_engine/corpus/private/` git-ignored as a backstop and a guard check that fails if any such file is ever tracked; never committed, uploaded, published, or built into anything | user's answer | **User** |

## Open questions for the user
1. AAC encoder: approve the two-step recommendation in `08_aac_encoder_licensing.md` (notices in the next release,
   then the replacement)?
2. Condensing: label style "Tpt. 1.2" (proposed) or "Trumpets 1.2"? (Grouping itself is now configurable, D-E12.)
