#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle check: the menu reorganisation keeps every command.

Lists every command placed in the application menus (makeMenuItem("...") in
appmenumodel.cpp) and compares it with the list taken before the menus were
reorganised (data/menu_commands_baseline.txt). Fails when a command that was
in the menus is in none of them now, or when a menu command has no action
definition anywhere in the code. Shortcuts belong to commands, so they are
untouched by moving a command between menus.

Usage: menu_commands_check.py [--write-baseline]
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MENU = ROOT / "src/appshell/qml/MuseScore/AppShell/appmenumodel.cpp"
BASELINE = Path(__file__).parent / "data/menu_commands_baseline.txt"


def menu_commands():
    text = MENU.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)          # commented-out blocks
    text = "\n".join(l for l in text.splitlines() if not l.strip().startswith("//"))
    return sorted(set(re.findall(r'(?:makeMenuItem|makeIconItem)\(\s*"([^"]+)"', text)))


def defined_actions():
    found = set()
    for path in (ROOT / "src").rglob("*.cpp"):
        try:
            t = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        found.update(re.findall(r'UiAction\(\s*"([^"]+)"', t))
        found.update(re.findall(r'UiAction\(\s*([A-Z_]+_CODE)', t))
    return found


def main():
    now = menu_commands()
    if "--write-baseline" in sys.argv:
        BASELINE.write_text("\n".join(now) + "\n", encoding="utf-8")
        print(f"baseline written: {len(now)} commands")
        return 0
    before = [l.strip() for l in BASELINE.read_text(encoding="utf-8").splitlines() if l.strip()]
    missing = [c for c in before if c not in now]
    defined = defined_actions()
    # action:// codes and the few constants are defined elsewhere; check plain codes only
    undefined = [c for c in now if not c.startswith("action://") and c not in defined
                 and not re.search(r'"%s"' % re.escape(c), (ROOT / "src").joinpath("framework").as_posix())]
    undefined = [c for c in undefined if not any(c in p.read_text(encoding="utf-8", errors="replace")
                                                  for p in (ROOT / "src").rglob("*uiactions*.cpp"))]
    added = [c for c in now if c not in before]
    print(f"Menu commands before: {len(before)}, now: {len(now)} (+{len(added)} new)")
    for c in added:
        print(f"  new: {c}")
    for c in missing:
        print(f"  MISSING from every menu: {c}")
    for c in undefined:
        print(f"  NO ACTION DEFINITION: {c}")
    ok = not missing and not undefined
    print("Result:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
