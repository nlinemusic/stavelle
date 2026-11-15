# ![Jubelle](branding/jubelle_banner.png)

**Jubelle** is a music notation application for composing and arranging, with a particular focus on
jazz and big-band writing. It is a personal project, developed in the open, and built on the
open-source engine of [MuseScore Studio](https://github.com/musescore/MuseScore) 4.7.7.

[![License: GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue.svg)](LICENSE.txt)

> **Status:** in active development. Windows test builds are produced from the `jubelle/phase-2`
> branch. There is no public release yet, and features are listed below only as far as they have
> actually been built and tested.

## What Jubelle adds

Jubelle keeps everything MuseScore Studio 4.7.7 can do, and adds:

- **Projects:** a "Jubelle Projects" library with categories, a standard folder for each
  composition, exports saved next to the score, and a picture on each project card.
- **New Score:** composition name, category and suggested templates in one setup screen.
- **Playback:** articulation playback you can adjust for the whole score, per instrument or per
  passage; swing feels including a Basie-style feel; repeat playback controls.
- **Engraving and arranging tools:**
  - automatic courtesy accidentals with your own rules;
  - chord identification;
  - linked passages (music that stays in sync across staves);
  - Apply Part Layout;
  - title-page alignment;
  - a condensed full-score copy for winds and brass.
- **Reliability:** crash-safe saving, so the last good version of a score is never corrupted.

Templates, music fonts, jazz articulation playback and the Jubelle interface are being refined now.
More features are planned: a timeline, advanced selection, score proofreading, live tempo
recording, page-turn optimisation, magnetic layout, drum stems and video export.

## Compatibility

- Jubelle reads and writes standard MuseScore files (`.mscz`, `.mscx`). Scores made in Jubelle open
  in MuseScore Studio 4.7.7.
- Jubelle installs and runs alongside MuseScore Studio, with its own settings, so neither affects
  the other.
- **Platforms:** Windows now; macOS later.

## Building

Jubelle builds the same way as MuseScore Studio. The repository's GitHub Actions workflows produce
the Windows installer:

- **Build: Windows**, started manually, makes the `.msi` installer and runs Jubelle's application
  checks.
- **Check: Unit Tests** compiles the whole application on Linux and runs the test suite.

To build on your own machine, follow MuseScore's
[developer environment guide](https://github.com/musescore/MuseScore/wiki/Set-up-developer-environment).
The steps and dependencies are the same.

Jubelle's own fast test tools, which need no Qt and no build minutes:

```sh
cmake -S tools/jubelle_engine_tests -B build-engine-tests -GNinja && cmake --build build-engine-tests
build-engine-tests/jubelle_engine_tests
```

## Repository layout

| Path | What it is |
|---|---|
| `src/` | The application. Jubelle's own code is mostly in `jubelle/` subfolders (for example `src/notation/internal/jubelle/`) |
| `share/` | Templates, instruments, styles and other installed data |
| `branding/` | Jubelle's logo sources and artwork |
| `tools/jubelle_*` | Jubelle's test tools |
| `docs/upstream/` | MuseScore's original README, contribution guide and GitHub templates, kept for reference |
| `.github/workflows-upstream-disabled/` | MuseScore's own CI workflows, kept but switched off |

## Relationship to MuseScore

Jubelle is derived from MuseScore Studio, by MuseScore Limited and its contributors, and stays
compatible with it. MuseScore's copyright notices, licence headers and third-party attributions are
kept unchanged throughout the source code. MuseScore™ is a trademark of MuseScore Limited. Jubelle is
an independent project and is not affiliated with or endorsed by MuseScore Limited or Muse Group.

Jubelle does not include or use the proprietary MuseSounds libraries.

## Licence

Jubelle is licensed under the **GNU General Public License version 3**, like MuseScore Studio; see
[LICENSE.txt](LICENSE.txt). Bundled fonts and third-party components keep their own licences, found
next to each component (for example in `fonts/` and `thirdparty/`).

## Contributing and feedback

See [CONTRIBUTING.md](CONTRIBUTING.md). Please read it before opening an issue, especially the
section on **not posting private scores or diagnostic files** in public issues.
