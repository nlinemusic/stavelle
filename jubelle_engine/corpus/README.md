# Test corpus

Scores used by the engine's comparison tests and benchmarks.

## Public corpus (in the repository)
- `demos/` (MuseScore's demo scores)
- `tools/jubelle_templates/testscores/` (Big Band test music)
- `tools/jubelle_checks/data/` (Jubelle features score)
- generated scores (`tools/jubelle_checks/make_large_score.py`, `make_audio_test_score.py`)

## Private corpus (the user's own scores): never in Git
Decision D-E17: copies of the user's scores may be used **locally and privately** only.
- Default location, **outside the repository**: `~/JubellePrivateScores/` (or the folder named by the environment
  variable `JUBELLE_PRIVATE_CORPUS`).
- `jubelle_engine/corpus/private/` is git-ignored as a backstop, in case scores are ever placed here.
- `tools/jubelle_checks/private_corpus_guard.py` fails if any file under a private-corpus path is tracked by Git, or
  if a tracked file's name matches one in the private folder. It runs in the engine CI.
- Tests that use the private corpus **skip** when it isn't present (as in CI), and never copy, upload or print the
  scores' contents (only counts and timings, with file names replaced by numbers in shared reports).
