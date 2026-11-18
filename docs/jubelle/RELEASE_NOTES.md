# Jubelle 4.7.7: release candidate 2

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

## What's new since release candidate 1

### Better audio export
Exports from Jubelle sounded duller and, on loud passages, rougher than the same score exported from MuseScore Studio 4.7.7. Three causes were found and fixed:
- **Piano, keyboards, guitars, harp and percussion sounded duller.** Jubelle played their notes at lower MIDI velocities and made up the level with gain. With a soundfont the velocity also chooses the sample layer and how bright the sound is. Measured on the built-in piano at the same loudness, they had about 6 dB less energy above 4 kHz at p, at mf and with no dynamic marking, and about 3 dB less at ff. These instruments now use MuseScore's own velocities, so they sound and balance exactly as in MuseScore. Winds, brass, strings and voices keep Jubelle's clearer dynamic steps, which change only their level, not their tone.
- **Loud passages crackled.** The anti-clipping limiter turned the level down within a single sample on loud attacks. It now looks 5 ms ahead and turns the level down smoothly before the peak, only as far as needed. Measured on a loud attack, the noise the limiter itself added fell from -50 dB to -93 dB. Anything quieter than about -1 dBFS still passes through untouched.
- **Quiet endings in 16-bit files were grainy.** 16-bit WAV and FLAC exports are now dithered (standard TPDF). Exporting the same score twice still gives identical files.

**Export settings now match MuseScore's defaults:** 44.1 kHz, WAV 32-bit float, FLAC 16-bit, MP3 128 kbps. 32, 48 and 96 kHz and 16 or 24-bit remain available. If you changed these settings yourself in an earlier Jubelle, your choice is kept.

### Report a bug (Help › Report a bug…)
- Fields for a title, what happened, steps to reproduce, the expected and actual result, and comments.
- Screenshots: add a picture, or capture Jubelle's own window.
- **Preview report** shows exactly what will be saved. **Create report** makes a ZIP on this computer (**Show in folder** finds it). Nothing is uploaded: you send it yourself.
- Jubelle adds its version, your system's name and version, the graphics system, screen sizes, processor cores and memory, which notation engine is in use, the workflow profile and, if you leave the box ticked, the end of its log and the last crash.
- Your user name, folders, file names, score titles, e-mail addresses, passwords, keys and tokens are removed first. Your score is never added unless you tick the box (off every time).

### Workflow profiles (View › Workflow profile)
- **Jubelle** (default), **MuseScore Studio**, **Finale-style** and **Noteflight-style** menus, shortcuts, note input toolbar order and note input settings.
- Switch at any time without restarting. Every command stays available in every profile (checked automatically each time the menus are arranged).
- Your own changes are kept for each profile. **Restore this profile's defaults…** undoes them for the current one.
- The Finale and Noteflight profiles follow those programs' habits (for example Finale's number keys for intervals, Noteflight's Space for a rest). They are not copies of them. See the Learning Center guide.

### Also
- Learning Center guides: Workflow profiles, Reporting a bug, Exporting audio.
- The last crash report is kept on this computer so a bug report can include it.

## Checks run for this release
Windows build checks:
- app check (open, save, reopen, MusicXML, MIDI, identity, own data folder);
- interface self-test (every Jubelle window and panel, the Report a bug window, each workflow profile; no QML errors; every command in the menus in every profile);
- MP4 export (picture, sound against a WAV export);
- audio export (bit depth and sample rate of WAV, FLAC, MP3, OGG at the default and at 48 and 96 kHz);
- template prints.

Also:
- **audio against MuseScore 4.7.7** on the same Windows machine: the same 9 scores exported by both (big band, jazz, orchestra, a long orchestral export, strings with dynamics, piano, brass band, band with drums, and a test score with piano dynamics, articulations, crescendo, a loud tutti and tempo changes), measured for clipping, brightness, timing and repeatability;
- the same export checks on the Mac build, and installing the Mac app;
- the Linux whole-app compile and MuseScore's unit tests;
- Jubelle's engine tests, including bug reports (privacy cleaning, the package, safe saving), workflow profiles (on the real default shortcuts: no clashes; menus never lose a command), the limiter, velocities and dither;
- coexistence with MuseScore 4.7.7 on Windows.

The results of each run are listed in the release report.

## Known limitations
- Live score condensing is not included yet. It is last, by design.
- The installers are not code-signed by Microsoft or notarised by Apple, so Windows and macOS warn the first time (see Installing). This is separate from Jubelle's own release signature, which the updater checks.
- No in-app updater on macOS yet.
- The Mac app has not been tried by hand.
- On the largest scores, Jubelle's extra layout features add a little to each edit. See the release report for the measurements against MuseScore 4.7.7.
- Workflow profiles: the Finale and Noteflight profiles are approximations of those programs' habits. On guitar tablature staves, Shift+number in the Finale profile keeps MuseScore's tab durations. Your own custom profiles are planned later.
- Report a bug saves a ZIP for you to send. A secure online submitter can be added later without changing the report.
- Audio: winds, brass and strings keep Jubelle's wider dynamic steps, so a score with many dynamics is not sample-for-sample identical to MuseScore. Piano, guitar, harp and percussion match MuseScore. The limiter delays the sound by 5 ms (audio and video stay in step).
- A hybrid audio cache (pre-rendering playback) is not included. It was judged too risky for playback reliability at this stage.
