# 16 · Performance: the Jubelle engine against MuseScore's engine

The user's requirement (25 Nov 2026): Jubelle keeps its behaviour and becomes **noticeably faster and more
responsive**. Each subsystem is benchmarked against the current MuseScore-based engine on the **same scores, same
machine, same operations**. Bottlenecks are found and fixed where it matters, never at the cost of accuracy,
notation quality, compatibility or reliability.

## How it is measured
- **MuseScore's engine (the current Jubelle):** `tools/jubelle_engine_tests/jubelle_baselines.cpp`
  (`JUBELLE_BASELINES=1 JUBELLE_REALISTIC_METRICS=1 jubelle_engine_tests --gtest_filter=Jubelle_Baselines.EngineTimings`).
- **The Jubelle engine:** `je_io_tool bench-scores <scores…>` (release build). It runs the same operations on the same
  files: open the file into the model; enter 10 notes on the first staff from the middle of the score (a quarter,
  C5, one command each, with the linked-staff hook); undo and redo each; save the model as a 4.x archive; and
  measure the model's memory.
- Scores: `demos/Dawn.mscx`, `share/autobotscripts/data/Big_Score.mscz`, the large orchestra made by
  `tools/jubelle_checks/make_large_score.py demos/Dawn.mscx … --instruments 3 --measures 15` (39 staves, 285 bars)
  and `tools/jubelle_templates/testscores/Big_Band_test_music.mscz`.
- Machine: the development workspace (Intel Xeon 2.3 GHz, 2 cores), both engines in the same run.

**What each figure includes (read it this way):** MuseScore's *edit* and *undo* include its layout of the change;
the Jubelle engine doesn't lay out yet (S7), so its edit is the editing work alone. The fair comparison of
responsiveness comes with S7 (the goal: edit + layout ≤ 16 ms on the large orchestra). *Open* and *save* are
comparable now: both read or write the whole file; MuseScore's open is its read without layout.

## Results (25 Nov 2026)
| Score | Engine | Open | Edit | Undo | Redo | Save | Memory |
|---|---|---:|---:|---:|---:|---:|---:|
| Dawn (13 staves, 19 bars) | MuseScore | 10 ms | 8.3 ms (with layout) | 8.0 ms | n/a | 12 ms | n/a |
| | **Jubelle engine** | **6 ms** | 0.08 ms (no layout yet) | 0.01 ms | 0.01 ms | **4.6 ms** | 0.8 MB |
| Big Score (26 staves, 216 bars) | MuseScore | 103 ms | 18.8 ms | 18.5 ms | n/a | 174 ms | 65 MB |
| | **Jubelle engine** | 144–155 ms | 0.55 ms | 0.02 ms | 0.01 ms | **82 ms** | **18 MB** |
| Large orchestra (39 staves, 285 bars) | MuseScore | 982 ms | 109 ms | 109 ms | n/a | 1,039 ms | 115 MB |
| | **Jubelle engine** | **298–315 ms** | 4.8 ms | 0.03 ms | 0.02 ms | **443–496 ms** | **32 MB** |
| Big Band test chart (18 staves, 40 bars) | MuseScore | 29 ms | 28.1 ms | 28.8 ms | n/a | 30 ms | n/a |
| | **Jubelle engine** | **21 ms** | 0.14 ms | 0.02 ms | 0.02 ms | **22 ms** | 3.8 MB |

Memory: MuseScore's is the resident memory added by the open score (with its layout); the Jubelle engine's is the
heap of its model with everything a save needs.

Reading this:
- **Open:** 3× faster on the large orchestra, 1.4× on the big band chart; Big Score is the one where MuseScore's reader
  is faster (its file has many details the Jubelle reader keeps for faithful saving): to be looked at with S2.
- **Save:** 2–2.6× faster on every score.
- **Undo and redo:** instant (tens of microseconds) at any size, because they apply a recorded change set to the
  persistent model instead of replaying commands; with layout this becomes the cost of laying out what changed.
- **Memory:** a quarter to a third of MuseScore's.

## Bottlenecks found and fixed (25 Nov 2026)
| Where | What it was | Fix | Effect |
|---|---|---|---|
| Saving (the writer) | each staff, measure and voice looked through **every** item, line and event of the score: work grew with the square of the score's size | items, lines and events are sorted into their staff and measure once per save | large orchestra save **104.6 s → 0.44 s**; Big Score 660 → 82 ms |
| Saving and reading | the appearance fingerprint of each object made text of every value before hashing | values are hashed directly, fields combined in any order | part of the above |
| Everywhere | reading a text field copied the value and the text | read in place (a reference) | about 10 % of a save |
| Editing | the measure at a time, and the key at a time, were found by looking at every measure or item | binary search over the flow (kept in time order), then back to the nearest key | large orchestra edit 13.6 → 4.8 ms |
| The model (all operations) | the bit count in the persistent map called a slow library routine on x86 builds without the instruction | the inline bit count | a few % of every operation |

## Next
- Opening Big Score (where MuseScore is faster) and the edit cost on the large orchestra (where the deletion of
  the music a note overwrites still looks through every line of the score).
- The responsiveness measures that need later subsystems: layout after an edit (S7), drawing (S8), playback update
  after an edit (S5), selection (S10 interface). Each is benchmarked against MuseScore's engine when it lands.
- The benchmark is to run in CI as an informational step, so a slowdown is seen when it happens.
