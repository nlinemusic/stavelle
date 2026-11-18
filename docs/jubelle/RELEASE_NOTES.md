# Jubelle 4.7.7: release candidate 1

**What it is:**
- a testing release, made for one composer and arranger;
- based on MuseScore Studio 4.7.7;
- it installs **alongside** MuseScore and keeps its own settings and folders;
- live score condensing is **not** included yet. It is the last feature and comes after this release has been tried.

## Installing

### Windows (10 or 11, 64-bit)
1. Download `Jubelle-4.7.7.261118172-x86_64.msi` from this release.
2. Double-click it. The installer isn't code-signed, so SmartScreen may say "Windows protected your PC". Choose **More info › Run anyway**.
3. It replaces an earlier Jubelle and leaves MuseScore alone. Your scores, projects, settings and plugins stay where they are.
4. Start Jubelle from the Start menu.

### macOS (Apple Silicon and Intel)
1. Download `Jubelle-4.7.7.261118162.dmg` and open it.
2. Drag **Jubelle** into **Applications**.
3. The app is not notarised by Apple, so the first time:
   - **Control-click** Jubelle in Applications and choose **Open**, then **Open** again;
   - or go to System Settings › Privacy & Security and click **Open Anyway** under the Jubelle message.
4. The Mac build is compiled and checked automatically, but has **not yet been tried by hand**.

### If your browser won't let you download it

**Windows (Edge says the file "isn't commonly downloaded"):** the warning appears because the installer is new and isn't code-signed. The file is safe to keep.
1. Press **Ctrl+J** to open your downloads, hover over the Jubelle file, click **…**, then **Keep**.
2. In the dialog that opens, click **Show more**, then **Keep anyway**. Don't use the arrow beside **Delete**: it only deletes.

Or skip the browser and download it with PowerShell. Press Start, type **PowerShell**, open it, paste this one line and press Enter:
```
$ProgressPreference='SilentlyContinue'; Invoke-WebRequest "https://github.com/nlinemusic/stavelle/releases/download/v4.7.7.261118172/Jubelle-4.7.7.261118172-x86_64.msi" -OutFile "$HOME\Downloads\Jubelle-4.7.7.261118172-x86_64.msi"
```
The installer (about 180 MB) arrives in your Downloads folder. Double-click it. If Windows shows "Windows protected your PC", click **More info**, then **Run anyway**.

**macOS (the app "can't be opened" or "can't be checked for malicious software"):** Control-click **Jubelle** in Applications, choose **Open**, then **Open** again. Or go to System Settings › Privacy & Security and click **Open Anyway** beside the Jubelle message.

Or download it with Terminal. Open **Terminal** (Applications › Utilities), paste this one line and press Return:
```
curl -L -o ~/Downloads/Jubelle-4.7.7.261118162.dmg "https://github.com/nlinemusic/stavelle/releases/download/v4.7.7.261118172/Jubelle-4.7.7.261118162.dmg"
```
Then open the disk image from your Downloads folder and drag **Jubelle** to **Applications**.

**Checking the file (optional):**
- **Windows (PowerShell):** `Get-FileHash $HOME\Downloads\Jubelle-4.7.7.261118172-x86_64.msi`
- **macOS (Terminal):** `shasum -a 256 ~/Downloads/Jubelle-4.7.7.261118162.dmg`

The code it prints should match the file's line in `SHA256SUMS` on this page.

### Updates
- **Windows:** Jubelle can update itself. Help › Check for updates works at any time; automatic checks are off until you switch them on in Preferences. Testing releases like this one are offered only if you switch on testing releases.
- Before installing anything, the updater checks that the release's checksum list is signed with Jubelle's release key, which is built into this version, and that the download matches it. An unsigned, altered or damaged download is refused.
- **macOS:** there is no in-app updater yet. Download the new disk image from the Releases page.

### Checking a download yourself
- `SHA256SUMS` lists each file's SHA-256 checksum.
- `SHA256SUMS.sig` is the Ed25519 signature of that list, made with Jubelle's release key.

## What's new since Complete Build 1
### Arranging
- **Harmonise Melody** (Tools › Arranging): the Big Band Harmony and Orchestration System, built on the Pandemonium Big Band lessons:
  - closed voicing and full-band soli;
  - shout-chorus trumpet triads;
  - all nine saxophone-section voicings;
  - passing notes (planing, diatonic, diminished);
  - alterations on a resolving V (♭9♯5 preferred);
  - an optional half-step slide, off by default.
- It writes the parts into the trumpets, trombones and saxophones as one undo step, with Preview, Keep, Discard and Compare (up to four versions).
- Chord symbols are updated so the rhythm section reads what the horns play.
- Each part is spelled as its player reads it. Ranges come from the instrument data, and notes outside them are listed, never moved silently.
- **Proofread:**
  - section voicing: crossings, gaps, the lead doubled in a middle voice;
  - time to change instrument;
  - copying: cue and small-staff sizes, melisma extenders, key and clef changes just after a page turn;
  - lesson 8's check, "looks like a third but sounds like a second".
- **Prepare Parts** (Format) and **Export for Library** (File):
  - numbered part files;
  - an instrumentation list;
  - a folder index.

### Playback and sound
- **VST3:**
  - audio plugins appear on the Plugins page, where you can switch each one on or off (kept across rescans), see plain load errors, and Rescan;
  - per-instrument key switches, level trim and pitch-bend range.
- **Glissando start** for each glissando, in Properties, with a timing bar.
- **Properties:** a value set on the selection shows the default beside it, with **Use default**.
- **Audio export:** 48 or 96 kHz, 16 or 24-bit, with 48 kHz / 24-bit as the default.
- **MuseSounds:** Preferences › Audio & MIDI and the Learning Center explain why an installed MuseSounds engine doesn't play in Jubelle.

### Video (MP4)
- White printed pages, with the first page as the lead-in (no title screen).
- No cursor by default.
- A page-sequence builder with thumbnails and preview.
- Crossfaded joins.
- The video's own audio quality setting (default 256 kb/s).
- The sound is exactly in time with the pictures (an encoder delay of about 40 ms is now absorbed by the silent lead-in).

### Interface
- The same nine menus, regrouped (approved 17 Nov), with icons beside menu titles and key commands. Every existing command is still there, checked automatically against 255 commands.
- Properties gains a Jubelle section that shows the settings for what is selected.
- Learning Center:
  - instrument ranges grouped by family, with ensemble presets and your own selection;
  - new guides.
- **Performance profiles** (Preferences › General › Performance): Automatic, High performance, Balanced, Low resource.
- The name "Jubelle" everywhere it is shown.

### macOS preparation
- LGPL FFmpeg (with VideoToolbox) is built and bundled for video export.
- YouTube sign-in is kept in the macOS Keychain.
- Export checks run on the packaged Mac app.

## Checks run for this release
Windows build checks:
- app check (open, save, reopen, MusicXML, MIDI, identity, own data folder);
- interface self-test (every Jubelle window and panel, no QML errors);
- MP4 export (picture, sound against a WAV export);
- audio export (bit depth and sample rate of WAV, FLAC, MP3, OGG);
- template prints.

Also:
- the same export checks on the Mac build;
- platform tests on Windows, macOS and Linux;
- the Linux whole-app compile and MuseScore's unit tests;
- Jubelle's engine tests;
- coexistence with MuseScore 4.7.7 on Windows.

The results of each run are listed in the release report.

## Known limitations
- Live score condensing is not included yet. It is last, by design.
- The installers are not code-signed by Microsoft or notarised by Apple, so Windows and macOS warn the first time (see Installing). This is separate from Jubelle's own release signature, which the updater checks.
- No in-app updater on macOS yet.
- The Mac app has not been tried by hand.
- On the largest scores, Jubelle's extra layout features add a little to each edit. See the release report for the measurements against MuseScore 4.7.7.
- A hybrid audio cache (pre-rendering playback) is not included. It was judged too risky for playback reliability at this stage.
