# Jubelle releases and updates (R3, decision D6)

Jubelle checks **only** this repository's GitHub Releases, and installs an
update only if it passes two checks:

1. the release's `SHA256SUMS` file is signed (Ed25519) with Jubelle's release
   key, whose public half is built into the app;
2. the downloaded installer's SHA-256 matches its line in that file.

Anything else (an unsigned release, a different key, a changed or damaged
download, a download from anywhere but GitHub) stops the update, and the
download is not kept. Automatic checks are **off** until you switch them on in
Preferences; Help → Check for updates works any time. Testing releases (GitHub
pre-releases) are offered only if you switch them on.

## One-time setup (yours)

1. On your own computer: `pip install cryptography`, then
   `python3 tools/jubelle_release/make_signing_key.py`.
2. In GitHub → Settings → Secrets and variables → Actions:
   - **Secret** `JUBELLE_RELEASE_SIGNING_KEY` = the line in `jubelle-release-key.private.txt`;
   - **Variable** `JUBELLE_RELEASE_PUBLIC_KEY` = the line in `jubelle-release-key.public.txt`.
3. Keep a backup of the private file somewhere safe and offline; then delete
   the copy on your computer if you like.

Builds made before step 2 have no key and refuse every update (install the
next version by hand once).

## Publishing a release

1. Run "Build: Windows" and test the installer.
2. Actions → "Jubelle: publish a release" → Run workflow, with that run's
   number, Stable or Testing, and the notes.

The workflow takes the tested installer from that run, writes and signs
`SHA256SUMS`, checks the signature against the public key, and publishes
`v<version>` with the installer, `SHA256SUMS` and `SHA256SUMS.sig`.

## On your computer

Downloaded installers are kept in Jubelle's update folder, so an earlier
version can be reinstalled. Your scores, projects, settings and plugins live
outside the program folder and are not touched by an update; Windows rolls an
MSI install back by itself if it fails.
