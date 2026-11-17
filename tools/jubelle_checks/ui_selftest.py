#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle build check: every Jubelle window and panel opens without QML errors.

Starts Jubelle with a score and JUBELLE_UI_SELFTEST set. Jubelle then opens each window
and panel in turn, records whether it appeared and any QML errors, writes the report and
quits (see src/appshell/internal/jubelle/uiselftest.cpp).

Usage: ui_selftest.py <Jubelle executable> <score> <work dir>
"""
import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from smoke_test import run_app  # noqa: E402


def main():
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    exe, score, work = Path(sys.argv[1]), Path(sys.argv[2]).resolve(), Path(sys.argv[3])
    work.mkdir(parents=True, exist_ok=True)
    report = (work / "ui_selftest.md").resolve()
    if report.exists():
        report.unlink()
    os.environ["JUBELLE_UI_SELFTEST"] = str(report)
    result, elapsed = run_app(exe, [score], timeout=600)
    if not report.exists():
        text = (f"# Jubelle interface self-test\n\n**Result: FAIL**: no report was written "
                f"(exit {result.returncode} after {elapsed:.0f} s).\n\nLast output:\n\n```\n"
                + "\n".join(result.stdout.splitlines()[-40:]) + "\n```\n")
        report.write_text(text, encoding="utf-8")
    text = report.read_text(encoding="utf-8")
    print(text)
    return 0 if "Result: PASS" in text else 1


if __name__ == "__main__":
    sys.exit(main())
