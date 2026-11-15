# Contributing to Jubelle

Thanks for your interest in Jubelle. It is a personal project: its direction is set by its owner's
needs as a composer and arranger. Ideas, bug reports and fixes are still welcome.

## Reporting a problem or suggesting an idea

- Use the **Bug report** or **Feature request** form under
  [Issues](https://github.com/nlinemusic/stavelle/issues/new/choose).
- Say which Jubelle version you're using (**Help → About Jubelle**) and your operating system.

### Keep private information out of public issues

This repository is public. **Don't attach:**
- scores you don't want to share;
- crash logs or diagnostic files;
- anything that contains your name, file paths or account details.

Describe the problem in words, or use a small example score made for the report. Jubelle's crash
reporting (being developed) sends reports privately, never to this public repository.

## Code contributions

- Open an issue first to discuss the change. Pull requests are reviewed at the owner's discretion,
  and not every change fits the project's plans.
- Keep MuseScore file compatibility (`.mscz`): Jubelle scores must still open in MuseScore Studio 4.7.7.
- Put Jubelle's own code in `jubelle/` subfolders where possible, and keep changes to
  MuseScore-derived code small and clearly commented.
- Add or update tests. Jubelle's fast test tools are in `tools/jubelle_engine_tests` and
  `tools/jubelle_platform_tests`.
- Don't change or remove existing copyright notices or licence headers.

By contributing, you agree that your contribution is licensed under the GNU General Public
License version 3, the licence of this project. (MuseScore's contributor licence agreement applies
to the MuseScore project, not to Jubelle.)

## Code of conduct

Please follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Upstream

Jubelle is built on MuseScore Studio. MuseScore's original contribution guide is kept in
[docs/upstream/](docs/upstream/MuseScore-CONTRIBUTING.md) for reference. Problems that also occur in
MuseScore Studio itself are best reported to the
[MuseScore project](https://github.com/musescore/MuseScore/issues).
