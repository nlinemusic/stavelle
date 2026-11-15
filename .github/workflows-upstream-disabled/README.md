# Disabled MuseScore workflows

These GitHub Actions workflows belong to the official MuseScore project
(nightly publishing, translation syncing, issue triage, Linux/WebAssembly
builds, deployment). They depend on MuseScore's private credentials or are
not needed for Jubelle, and some run on a schedule that would use up build
minutes. GitHub only runs workflows inside `.github/workflows/`, so moving
them here disables them while keeping them available for reference.

`dependabot.yml` is also kept here: it opened weekly automatic update requests,
each of which would start every build (including the costly macOS build).

`issue_add_label_config.yml` and `issue_assign_by_label_config.yml` were used
only by `triage_issues.yml` (labelling MuseScore issues and assigning them to
MuseScore team members), so they are kept here with it.
