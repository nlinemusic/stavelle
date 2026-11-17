# Jubelle: handoff to the new Claude project (17 Nov 2026)

This document lets a new Claude project continue Jubelle **from the exact current state**. It is a continuation, not a restart. Nothing has been reset, squashed or recreated.

The same text is kept in two places:
- in the repository, as `docs/jubelle/HANDOFF.md` on `jubelle/phase-2`;
- in the old Claude project, as `claude/Jubelle - Handoff to the new Claude project (17 Nov 2026).md`.

## 1. What Jubelle is
- **A fork of MuseScore Studio 4.7.7** (GPL-3.0), made for one composer and arranger, who is the **user and tester, not a programmer**.
- **It must:**
  - work offline;
  - keep `.mscz` / `.mscx` compatibility with MuseScore 4.7.7;
  - install and run alongside MuseScore 4.7.7 without touching its installation or files;
  - keep every MuseScore copyright notice, licence and attribution.
- **Name:** **Jubelle**, never "Jubelle Testing" or "Just Jubelle". The old working name "Stavelle" survives only in a few workflow and branch names.

## 2. What the new project must be given (it cannot inherit these by itself)
A new Claude project does **not** automatically get the old workspace, repository access or tool permissions. To continue:
1. **Create the project** in claude.ai and add this handoff. Also add the docs listed in §9; download them from the old project, or ask the new session to read them from the old project if it can.
2. **Give the new session the repository** `nlinemusic/stavelle`, with push access (GitHub connection in claude.ai). The new session clones it, checks out **`jubelle/phase-2`** and runs `git log -1`; it should show the commit in §4 or later.
3. **Re-grant permissions as needed:**
   - access to the user's computer folders (Jubelle logs, MuseScore files) only when a task needs them;
   - the MuseSounds log permission was read-only, MuseSounds lines only; it should be asked for again if needed.
4. **Local build tree:** the old session's local build tree (`/home/claude/build`: test binaries, benchmark logs) does **not** transfer. The new session must rebuild the engine and platform tests from the repository (§6). All results that matter are in the docs.
5. **Uploaded files:** files uploaded to the old conversation don't transfer. Two are needed again:
   - the **Pandemonium `Jazz arranging.txt`**, which has never been received; please attach it;
   - the Chord Identifier plugin zip (`chordIdentifierSp3_22_1.zip`), only if that work is revisited. Its rules are already in `src/notation/internal/jubelle/chordidentifier.*`.

## 3. Repository and branches (`github.com/nlinemusic/stavelle`)
| Branch | What it is | Rule |
|---|---|---|
| `jubelle/phase-2` | **Development branch.** All current work | Continue here |
| `main` | Complete Build 1 (merge commit `4d8e06f7`, 17 Nov, approved) | Merging the next build is **authorised once its required checks pass**; never merge failing or incomplete work |
| `checkpoint/complete-build-1` | Exactly the source of Complete Build 1 (`893fd62a`) | Keep |
| `checkpoint/main-before-jubelle-merge` | `main` before Build 1 was merged | Keep |
| `checkpoint/baseline-musescore-4.7.7` | Untouched MuseScore 4.7.7 | Keep (template originals, comparisons) |
| `jubelle/rb2-checkpoint`, `jubelle/rb2b` | Refinement Build 2 states. RB3 is in `jubelle/phase-2`'s history | Keep |
| `stavelle/phase-1` | Phase 1 (reliability) | Keep |
| `jubelle/merge-to-main` | Temporary branch used for the merge (same as `main`) | Harmless; may be left |

**Git rules (the user's, verbatim where quoted):**
- "Do not force-push, delete the repository, … or overwrite main."
- Commits carry `[skip ci]`; builds are started by hand (`workflow_dispatch`) so nothing runs twice.
- Every commit ends with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01JFtWNCTL5iDGVdQAEGtZD3
  ```
  The new session uses its own session link.
- **The user's email is private;** never put it in commits.
- **Known limits of the old session's GitHub access:**
  - tag pushes, changes to repository settings and REST ref writes were refused (403); branch pushes and cancelling runs worked;
  - the default branch is still `stavelle/phase-1` (so the GitHub front page shows old MuseScore text), and switching it to `main` is the user's step;
  - artifact downloads from inside the session were blocked.

## 4. Current state (17 Nov 2026, 18:30 CT)
- **`jubelle/phase-2` HEAD:**
  - `30368a0e`: jazz-turn palette item, audio export 48/96 kHz and 16/24-bit;
  - plus the commit carrying this handoff and the "Jubelle" name fix (see the branch log).
- **Complete Build 1 (CB1):**
  - Windows run **44176705262**, commit `893fd62a`, `Jubelle-4.7.7.261117210-x86_64.msi`;
  - app check 68/68, video export check passed, template prints 59/59, coexistence with MuseScore 4.7.7 33/33;
  - **the user is testing it now.**
- **After CB1 (code only, not built):**
  - `5446dde8`: interface self-test, plus MuseScore checks on a Jubelle feature score;
  - `f56e6268`: three interface faults found by the self-test, fixed;
  - `30368a0e`: jazz-turn palette item, audio defaults;
  - the "Jubelle" name fix.
  - A test build of `5446dde8` (run 44191074117) passed; coexistence run **44196076417** passed **38/38**, including MuseScore 4.7.7 playing a Jubelle score's recorded tempo identically.
- **Local tests at `30368a0e`:** engine tests 331 passed, 22 skipped, plus 24 in the realistic pass; platform tests 101 + 4 YouTube, all passed.
- **macOS:** the workflow `build_macos.yml` exists but last ran on `stavelle/phase-1` (14 Nov). **It has not been run on the Jubelle branch.**

## 5. Architecture
Jubelle keeps MuseScore's module structure and adds its own code in clearly named `jubelle` folders, so upstream code stays recognisable:

| Area | Where | What |
|---|---|---|
| Engraving rules | `src/engraving/rendering/score/jubelle/` | engraving rules, magnetic layout, overlap checker (collision avoidance, grid index) |
| House styles | `src/engraving/style/jubelle/` | per-ensemble house styles |
| Playback | `src/engraving/playback/jubelle/` | swing feels (v2, Basie roles), jazz falls, doits, bends, articulation playback by score, instrument, passage and Properties |
| Notation features | `src/notation/internal/jubelle/` | linked passages, Chord Identifier, proofread, part layout, page alignment, page turns, drum stems, swing marking, tempo tap, timeline markers, condensed copy and planner (**live condensing not started**), instrument reference, find/select |
| VST | `src/framework/vst/internal/jubelle/` and `vstsequencer.cpp` | presets per instrument, dynamics on decibel steps |
| Updates | `src/framework/update/internal/jubelle/` | GitHub Releases updater with Ed25519-signed checksums |
| App shell | `src/appshell/internal/jubelle/` | startup screen, UI self-test, Learning Center model |
| Video | `src/importexport/videoexport/internal/jubelle/` | page sequences; FFmpeg LGPL bundled on Windows |
| YouTube | `src/project/internal/youtubeupload.*`, `project/qml/.../YouTube/` | OAuth (PKCE, `youtube.upload` scope only), resumable upload, secrets in Windows Credential Manager |
| Tests | `tools/jubelle_engine_tests/`, `tools/jubelle_platform_tests/`, `src/*/tests/jubelle_*` | engine and platform tests |
| Checks | `tools/jubelle_checks/` | Windows app check, coexistence test, template prints, UI self-test, video check, FFmpeg bundling, large-score generator |
| Release | `tools/jubelle_release/` | signing key creation and release signing (the key is the user's; **never in the repository, logs or a conversation**) |
| Templates, fonts, gain | `tools/jubelle_templates/`, `tools/jubelle_fonts/`, `tools/jubelle_gain/` | generators and checks |

**Windows build pitfall:** every QML model subfolder must be listed in that module's `target_include_directories`, or qmltyperegistrations fails on Windows only.

## 6. How to build and test
- **CI workflows:**
  - `check_unit_tests.yml`: Linux app compile, then unit tests;
  - `jubelle_platform_tests.yml`;
  - `build_windows.yml -f build_mode=testing -f platforms=windows_x64`, which produces the MSI and runs the app check, video check and UI self-test;
  - `stavelle_coexistence.yml -f build_run_id=<windows run>`;
  - `build_macos.yml`;
  - `jubelle_release.yml`: **never publish a release without the user's approval** after Windows verification.
- **Logs:** `gh api repos/nlinemusic/stavelle/actions/jobs/<id>/logs`, available after the job finishes.
- **Local:**
  - build the engine test target (`tools/jubelle_engine_tests`, RelWithDebInfo, ninja) and the platform tests;
  - useful environment variables: `JUBELLE_REALISTIC_METRICS=1`, `JUBELLE_BASELINES=1`, `JUBELLE_COST_SCORE`, `JUBELLE_WRITE_FEATURE_SCORE=<path>`;
  - the UI self-test runs with `JUBELLE_UI_SELFTEST=<report>` and a score on the command line.

## 7. Decisions and constraints that must be preserved
- **Verification is reported in separate steps:**
  1. code written;
  2. local tests;
  3. Linux compile;
  4. Windows (and now macOS) build;
  5. the user's hands-on testing.
  "Do not mark features complete merely because code has been written." A requirement leaves the outstanding list only after the user confirms it.
- **Requirements:**
  - "Do not invent my decisions."
  - Never weaken tests.
  - Never remove or downgrade requirements.
  - "Do not create a duplicate implementation if any of the underlying functionality already exists."
- **Live condensing is the final feature.** It starts only after the user tests the completed application and explicitly approves it. The existing condensed-copy feature stays as it is.
- **No unnecessary builds.** Consolidate into comprehensive builds; no separate build while the user is testing one.
- **Licensing:**
  - "Do not bypass proprietary licensing or attempt to use restricted MuseSounds functionality."
  - **MuseSounds:** the user will **not** contact Muse Group. The limitation is accepted: no bypassing, no imitating MuseScore, no time spent forcing it. Keep detection and diagnostics, and explain the limitation to users.
  - Noteflight: no bypassing of restrictions.
  - No copying of copyrighted engraving texts.
  - FFmpeg: LGPL build with notices.
- **YouTube:**
  - Keep the design as approved: upload permission only, no extra read-only permission; showing "Connected" is fine.
  - **Not verified** until the user has connected their Google account and finished a private test upload.
- **Signing key:** "Keep the private key secure and never expose it in the repository, logs, or a Claude conversation."
- **Releases:** "Do not publish a release until the Windows build has passed verification and I have approved it."
- **Web version:** research only; no port.
- **Checkpoints:** "Preserve RB3 and other working checkpoints."
- **Working rules:**
  - Explain in plain language.
  - Don't ask the user to edit code or run development commands.
  - Keep the MuseScore 4 workflow familiar.
- **Adopted decisions** are in `Jubelle - Decisions Adopted (15 Nov 2026)`. Those still open are at the end of the outstanding list.

## 8. Next comprehensive build: scope and order (user's instruction, 17 Nov 17:53)
**Order:**
1. **Move to the new project** (this handoff).
2. **Audit and finish the outstanding work** (`Jubelle - Outstanding Requirements (17 Nov 2026)`), including:
   - Build 1 feedback:
     - MP4: white printed pages, no title screen, no cursor by default, page-sequence builder with preview, crossfades at joins, the audio investigation, scrolling settings;
     - audio settings: verify exported depth and rate;
     - instrument ranges by family with ensemble presets;
     - AFM and MPA preparation features;
     - Plugins page for VST3;
     - glissando start per element;
   - the jazz-turn Properties default display;
   - VST3 key switches and level trims;
   - the interface consistency pass;
   - the MuseSounds explanation for users;
   - the **"Jubelle" name everywhere**.
3. **Menu and Properties reorganisation (approved):**
   - nine menus, with no Arrange menu;
   - arranging commands in Tools submenus;
   - icons beside menu titles and important commands, with text labels kept;
   - contextual Properties (glissando, jazz turn, recorded tempo points, timeline markers, Jubelle page breaks, dynamics…) showing the local value with the score default and "Use default";
   - every command preserved, checked by an automatic test.

   The basis is `Jubelle - Menu and Properties Reorganisation (proposal, 17 Nov 2026)`.
4. **Big Band Harmony and Orchestration System:**
   - study the source file, **which has not been supplied yet**;
   - write a source-to-feature mapping and a full proposal;
   - **get the user's approval before any code.**

   Requirements are in the outstanding list §16. It must be deterministic, rule-based and offline, never an AI assistant, and reuse the existing Chord Identifier.
5. **Implement and test the harmony system.**
6. **Performance initiative** (it runs **throughout**; profile first):
   - priority: responsive work after startup;
   - startup preparation in the background, with visible status;
   - hybrid audio caching with automatic invalidation;
   - adaptive profiles (Automatic, High Performance, Balanced, Low Resource);
   - benchmarks against MuseScore 4.7.7 on five score types.

   Earlier measurements are in the outstanding list §13.
7. **Windows and macOS builds and full testing:**
   - automated tests, app checks, harmony examples, menus and Properties, jazz turn, audio at every depth and rate, video and its audio, plugins, MuseScore 4.7.7 round trips, benchmarks, nothing lost;
   - then a report: implemented, tested, passed, failed, limits, performance, remaining.
8. **Merge into `main`** (authorised after the checks pass), with run and artifact links and the commit now on `main`.
9. **The user installs and tests the build.**
10. **Fixes from the user's testing.**
11. **Live condensing, last.**

## 9. Project documents to carry over
**Most important:**
- `Jubelle - Requirements (authoritative, 14 Nov 2026)`;
- `Jubelle - Outstanding Requirements (17 Nov 2026)`;
- `Requirements to Phase Mapping`;
- `Jubelle - Decisions Adopted (15 Nov 2026)`;
- `Jubelle - Complete Build 1 Results and Hands-on Checklist (17 Nov 2026)`;
- `Jubelle - Build 1 Feedback Assessment (17 Nov 2026)`;
- `Jubelle - Menu and Properties Reorganisation (proposal, 17 Nov 2026)`;
- `Jubelle - Engraving Standards Audit (17 Nov 2026)`.

**Also useful:**
- `Engine Baselines`;
- `Playback Libraries Evaluation`;
- `Web Version Feasibility Assessment`;
- the RB1–RB3 and CP1 results;
- `Development Roadmap Decisions`;
- `Phase 6 Decisions`;
- `Articulation Playback Passage Overrides (design)`;
- `Apply Part Layout (proposal)`;
- `Title Page Alignment (proposal)`;
- `Additional Requirements Assessment`;
- `Windows Build Refinement Phase`;
- `Complete Build Estimate and Milestones`.

## 10. User setup steps still open
- Switch the GitHub default branch to `main`.
- Create the signing key once (`tools/jubelle_release/make_signing_key.py`) and add the secret and variable.
- Create the private `nlinemusic/jubelle-crash-reports` repository.
- Set up a Google Cloud OAuth client for YouTube; instructions are in the CB1 checklist.
- Supply `Jazz arranging.txt`.
