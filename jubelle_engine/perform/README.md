# jubelle_engine/perform

Playback event generation per measure x player into MPE; repeats, swing, articulation profiles, jazz gestures. The audio engine itself is unchanged.

Status: **started** with the play order (`repeatlist.{h,cpp}`, MuseScore's RepeatList ported, with Jubelle's "Play this repeat"): the same as MuseScore's on its 72 repeat tests and on every corpus score (`tools/jubelle_checks/je_repeats_reference.py`, in CI). Event generation follows with S5 (see `../../JUBELLE_ENGINE_DEVELOPMENT_MASTER_PLAN.md`). Design: `../docs/03_engine_architecture.md`.
