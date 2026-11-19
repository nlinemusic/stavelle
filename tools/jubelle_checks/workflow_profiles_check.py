#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jubelle check: every menu entry a workflow profile names exists in Jubelle's
menus (appmenumodel.cpp). An entry that names nothing would be ignored by the
app (and its command would still be added by the fallback), so this catches
typos in the profile files. Usage: workflow_profiles_check.py"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MENU = ROOT / "src/appshell/qml/MuseScore/AppShell/appmenumodel.cpp"
PROFILES = ROOT / "src/appshell/resources/workflowprofiles"


def menu_ids():
    text = MENU.read_text(encoding="utf-8")
    ids = set(re.findall(r'(?:makeMenuItem|makeIconItem)\(\s*"([^"]+)"', text))
    ids |= set(re.findall(r'"(menu-[a-z0-9-]+)"', text))
    for name, value in re.findall(r'static const ActionCode (\w+) = "([^"]+)"', text):
        if name in text.replace(f'static const ActionCode {name}', ''):
            ids.add(value)
    return ids


def main():
    ids = menu_ids()
    bad = 0
    for path in sorted(PROFILES.glob("*.json")):
        profile = json.loads(path.read_text(encoding="utf-8"))
        for menu in profile.get("menus", []):
            for entry in menu.get("entries", []) + menu.get("takes", []):
                if entry in ("-", "*"):
                    continue
                if entry not in ids:
                    print(f"{path.name}: {menu['id']} names '{entry}', which is in no menu")
                    bad += 1
        print(f"{path.name}: {len(profile.get('menus', []))} menus, {len(profile.get('shortcuts', {}))} shortcut changes")
    print("Result:", "PASS" if bad == 0 else f"FAIL ({bad})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
