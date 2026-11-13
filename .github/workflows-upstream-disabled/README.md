# Disabled MuseScore workflows

These GitHub Actions workflows belong to the official MuseScore project
(nightly publishing, translation syncing, issue triage, Linux/WebAssembly
builds, deployment). They depend on MuseScore's private credentials or are
not needed for Stavelle, and some run on a schedule that would use up build
minutes. GitHub only runs workflows inside `.github/workflows/`, so moving
them here disables them while keeping them available for reference.
