# 05 · Licensing and obligations

_Not legal advice; this records the project's understanding and the rules it follows._

## 1. What Jubelle is today
- Jubelle is a modified version of MuseScore Studio 4.7.7, which is licensed under the **GNU GPL version 3**
  (`LICENSE.txt`). Jubelle as a whole is therefore distributed under GPL v3, with its source public in this
  repository, which satisfies the source-availability obligation for the releases published here.
- Many MuseScore files carry the header `MuseScore-Studio-CLA-applies`. That refers to MuseScore's Contributor
  License Agreement for contributions **to MuseScore upstream**. It places no extra obligation on a fork, and it
  gives the fork no extra rights: Jubelle uses the code under GPL v3.

## 2. What the new engine may do
| Approach | Allowed? | Obligation |
|---|---|---|
| Copy or port MuseScore code (engraving algorithms, readers) into `jubelle_engine/` | **Yes** under GPL v3 | keep copyright and licence notices; mark changes (a "Jubelle (based on MuseScore Studio)" header and history); the engine stays GPL v3 |
| Write new code from scratch in the engine | Yes | Jubelle's own code; still distributed under GPL v3 because it is combined with GPL code in one program |
| Link the engine into Jubelle with MuseScore's GPL code | Yes | the whole program stays GPL v3 |
| Relicense the new engine under another licence (for example MIT) | **Only** for code that is genuinely new and not derived from MuseScore's | not needed for a personal GPL project, so **not planned** (decision D-E3) |
| Read and write MuseScore's file format | Yes | a file format isn't copyrighted by the code that writes it; the readers can also be reused under GPL |

**Decision (D-E3):** the new engine stays **GPL v3**, so MuseScore's code can be reused and ported freely with
notices. No clean-room separation is needed.

## 3. Third-party components the engine touches
| Component | Licence | Note |
|---|---|---|
| Qt 6 | LGPL v3 (as used) | the bridge only; the engine core is Qt-free |
| FreeType | FreeType licence or GPL v2 (dual) | via `framework/draw` |
| FluidSynth | LGPL v2.1 | audio side, unchanged |
| LAME (MP3) | LGPL | audio side, unchanged |
| FLAC, Opus, libopusenc, dr_libs, stb | BSD-style or public domain | unchanged |
| **Fraunhofer FDK AAC** (MP4 audio) | FDK AAC licence: free use, but it carries patent notices, and the FSF considers it **not GPL-compatible** | inherited from MuseScore, which ships it too. **Risk** recorded (R-L1): for personal and private distribution the practical risk is low, but before any wider distribution replace it with an LGPL/GPL-compatible AAC encoder (for example FFmpeg's native AAC encoder, already bundled for macOS video export) |
| Music fonts: Leland, Bravura, Petaluma, Leipzig, Gootville, Finale Maestro/Broadway, … | SIL Open Font License (mostly) | bundled as today; the engine reads SMuFL metadata |
| MS Basic SoundFont | MIT (see `share/sound/MS Basic_License.md`) | unchanged |
| GoogleTest | BSD 3-clause | tests only |

## 4. Names and trademarks
- "MuseScore" is a trademark of MuseScore Limited. Jubelle keeps describing itself as "based on MuseScore Studio",
  never as MuseScore, and the new engine is called the **Jubelle engine**.
- No proprietary MuseSounds/MuseSampler functionality is used or bypassed (an existing standing rule).
- Workflow profiles imitate habits, not trade dress (no Finale or Noteflight artwork or branding).

## 5. Rules for engine development
1. Every ported file keeps MuseScore's licence header and adds a line saying it is part of the Jubelle engine,
   with a pointer to the original path.
2. No code from programs whose licences don't allow it (Dorico, Sibelius, Finale): study published
   **documentation** and **behaviour** only, never code.
3. New dependencies are recorded here with their licence, before they are added.
