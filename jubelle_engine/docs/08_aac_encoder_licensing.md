# 08 · AAC encoder licensing: investigation and recommendation

_18 Nov 2026. Not legal advice. Facts about the code were checked in the repository; facts about the licence and its
standing come from the sources listed at the end. Personal use was **not** assumed to remove any obligation:
sharing installers with friends is distribution ("conveying" in GPL terms)._

## 1. Exactly what is in Jubelle
| Item | Finding |
|---|---|
| Library | **Fraunhofer FDK AAC Codec Library for Android, version 2.0.3** (`src/framework/audio/thirdparty/fdk-aac/fdk-aac-2.0.3`) |
| Origin | inherited unchanged from MuseScore Studio 4.7.7 (present since the import commit `d82ef7e0`) |
| How it is built | compiled from source as a **static library** and linked into the audio engine (`muse_audio_engine`) whenever audio export is enabled (`MUSE_MODULE_AUDIO_EXPORT`, on by default), so it is **inside `Jubelle.exe` / the macOS app binary** on every platform |
| What uses it | `AacEncoder` (`audio/engine/internal/export/aacencoder.cpp`), profile **AAC-LC** (`AOT_AAC_LC`), ADTS output, Afterburner on, used by **File › Export › AAC audio** (MuseScore's) and by **Jubelle's MP4 video export** (the soundtrack of every video, including Publish to YouTube) |
| Modified? | no source changes (the build wrapper only silences warnings) |
| Licence | "Software License for The Fraunhofer FDK AAC Codec Library for Android" (file `NOTICE`, © 1995–2018 Fraunhofer-Gesellschaft) |

## 2. What the licence says (summary of `NOTICE`)
1. **Copyright licence:** redistribution and use in source and binary form, with or without modification, **without
   payment of copyright licence fees**, provided that:
   - the complete licence text is kept with **source** redistributions;
   - the complete licence text is kept **in the documentation and/or other materials provided with binary
     redistributions**;
   - recipients of binaries can get the **complete source code** of the FDK AAC Codec (and any modifications) free
     of charge;
   - Fraunhofer's name isn't used to endorse or promote derived products;
   - no copyright licence fees are charged for anyone to use, copy or distribute it;
   - modified versions carry prominent change notices and are renamed "Third-Party Modified Version of the
     Fraunhofer FDK AAC Codec Library for Android".
2. **No patent licence:** no express or implied patent licence is granted; the software may be used **"only for
   purposes that are authorized by appropriate patent licenses"**. Patent licences are offered through Via Licensing
   or the patent owners.
3. Warranty disclaimer.

## 3. How the licence is regarded
- The **FSF** lists it as a free licence that is **incompatible with every version of the GNU GPL**, and warns about
  the trap of the explicit refusal of any patent licence.
- **Debian** keeps fdk-aac in `non-free` and describes the licence as free "as far as it goes", incompatible with any
  version of the GPL, with a special danger in its patent term.
- **Fedora** ships only a stripped version (`fdk-aac-free`, with HE-AAC removed). Its legal review reasoned that once
  no unexpired patents are in play, clause 3 is a "no op". That reasoning applies only to that stripped
  implementation, and the fork has been unmaintained since 2019.
- **FFmpeg** classifies libfdk_aac as "nonfree" for GPL builds: it may only be combined with a GPL FFmpeg using
  `--enable-nonfree`, which makes the result non-redistributable.
- **Patents:** the US patents on AAC-LC (the profile Jubelle uses) have expired. Jubelle doesn't use HE-AAC or HE-AACv2,
  where patent concerns remain. Patent status outside the US and the full list of AAC patents are not public, so
  this is not a complete clearance.

## 4. What this means for distributing Jubelle
Jubelle is distributed as GPL v3 binaries (MuseScore's code, MuseScore Limited and contributors as copyright holders)
**statically combined** with FDK AAC. Three separate questions:

| Question | Situation | Status |
|---|---|---|
| **A. FDK's own conditions** (licence text with binaries, source available) | Source: met. The full FDK source is in the public repository, matching each release tag. **Licence text with the binaries: not met.** The Windows installer shows only the GPL (`LICENSE.rtf`); no FDK notice ships with the MSI or DMG, and the About dialog doesn't mention it | **gap, easy to fix** (add a third-party notices file to the installers and About) |
| **B. GPL v3 compatibility** of the combined binary | GPL v3 doesn't allow distributing the combined work under extra restrictions (§10); FDK's patent clause is such a restriction (the FSF's reason for "incompatible"). MuseScore Limited ships the same combination, and as holder of most of the copyrights it can choose to do so. **Jubelle can't rely on that:** it redistributes MuseScore's GPL code and has no exception from its copyright holders (contributors whose code is GPL-only included) | **real risk in principle**; it can't be fixed by notices, only by removing FDK from the binary |
| **C. Patents** | AAC-LC US patents expired; Jubelle uses AAC-LC only. Remaining exposure is low but not zero, and the licence itself grants nothing | **low**; removed entirely by using an OS encoder (C1) |

"Personal project" doesn't change A or B: they arise when binaries are given to anyone else, including friends.

## 5. Options
| Option | Licence | Effort | Quality | Notes |
|---|---|---|---|---|
| **1. Operating-system encoders** (Windows Media Foundation AAC encoder; macOS AudioToolbox AAC) | the OS vendor's codec, not distributed by Jubelle; no GPL combination | medium (two backends) | good (AAC-LC). Microsoft's encoder is AAC-LC only; Apple's is high quality | **removes A, B and C**; works offline; the encoder is part of Windows 7+ and macOS |
| **2. FFmpeg's native `aac` encoder** through the FFmpeg libraries Jubelle **already bundles** (LGPL build) for video, loaded at run time by `framework/media` | LGPL 2.1+ (dynamically loaded; Jubelle already meets the LGPL notices for FFmpeg) | low to medium (the loader exists; add an audio encode path) | FFmpeg's own guide ranks it second to libfdk_aac for AAC-LC (older guidance rated it as good or better at 128 kb/s) | removes A and B; C as for any AAC-LC encoder (low) |
| 3. Keep FDK and add its notice | FDK + GPL | minimal | best AAC-LC | fixes A only; **B remains** |
| 4. fdk-aac-free (Fedora's stripped fork) | same FDK licence | low | same AAC-LC | B unchanged (same licence text); unmaintained since 2019 |
| 5. Drop AAC export | n/a | minimal | n/a | breaks MP4 video sound; not acceptable |

## 6. Recommendation (needs the user's decision; affects the released app, not the engine)
1. **Now (next release):** add a `THIRD-PARTY-NOTICES` file to both installers and a "Third-party licences" link in
   About, with the FDK licence text and the other components' notices (closes gap A immediately).
2. **Next audio change:** replace FDK with **option 2** (FFmpeg native AAC through the already-bundled LGPL FFmpeg),
   with **option 1** where FFmpeg isn't present. Then remove `fdk-aac` from the build (closes B; C becomes "standard
   AAC-LC, as every OS ships").
3. **Verification:** the existing MP4 check (sound in time, loudness, correlation) and an AAC export check must pass
   unchanged. Add a listening comparison at 256 kb/s (the video default) between the old and new encoders.
4. Until then, R-L1 stays open in the master plan.

## Sources
- FSF, [recent licensing updates](https://www.fsf.org/blogs/licensing/recent-licensing-updates): the FDK AAC licence is free, incompatible with every GPL version, no patent licence
- Debian, [fdk-aac 2.0.3 copyright](https://sources.debian.org/copyright/license/fdk-aac/2.0.3-1)
- Fedora legal review, [bug 1501522](https://lists.fedorahosted.org/archives/list/package-review@lists.fedoraproject.org/2019/6/?page=44) and [fdk-aac-free status 2025](https://www.openwall.com/lists/oss-security/2025/08/13/9)
- Wikipedia, [Fraunhofer FDK AAC](https://en.wikipedia.org/wiki/Fraunhofer_FDK_AAC) (AAC-LC US patents expired)
- FFmpeg wiki, [Encode/AAC](https://trac.ffmpeg.org/wiki/Encode/AAC?version=69) (libfdk_aac is "non-free"; native encoder)
- Microsoft, [Media Foundation AAC Encoder](https://learn.microsoft.com/en-us/windows/win32/medfound/aac-encoder) (AAC-LC)
- `src/framework/audio/thirdparty/fdk-aac/fdk-aac-2.0.3/NOTICE` (the licence text)
