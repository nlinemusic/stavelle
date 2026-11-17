#!/usr/bin/env bash
# Jubelle: rebuild the score templates from MuseScore's untouched originals.
#
#   tools/jubelle_templates/build_templates.sh <engine tool binary>
#
# The originals come from the baseline branch (MuseScore Studio 4.7.7 as
# imported), so building is repeatable; the results are written over
# share/templates. Review the changes with git diff before committing.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TOOL="${1:?path to jubelle_engine_tests}"
BASE="${JUBELLE_TEMPLATE_BASELINE:-origin/checkpoint/baseline-musescore-4.7.7}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
git -C "$ROOT" archive "$BASE" share/templates | tar -x -C "$WORK"
"$TOOL" --build-templates "$WORK/share/templates" "$ROOT/share/templates"
echo "Templates written to $ROOT/share/templates"
