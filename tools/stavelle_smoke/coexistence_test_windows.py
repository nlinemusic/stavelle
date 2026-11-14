#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
#
# Stavelle: verifies on a clean Windows machine that Stavelle and an installed MuseScore
# coexist independently.
#
#   1. Install the official MuseScore Studio release and use it (so it has settings/data).
#   2. Take a snapshot of everything that belongs to MuseScore: its installation folder,
#      its settings / recovery / documents folders, and its registry entries (file types,
#      file associations, ProgIDs, URL scheme, registered application, uninstall entry).
#   3. Install Stavelle and use it. MuseScore's snapshot must be unchanged (Stavelle may
#      only add its own "Open with" entry), and both applications must work.
#   4. Uninstall Stavelle. MuseScore's snapshot must be exactly as before, Stavelle must be
#      gone, and MuseScore must still work.
#
# Usage: coexistence_test_windows.py <MuseScore.msi> <Stavelle.msi> <test-score> <work-dir>

import hashlib
import json
import os
import subprocess
import sys
import time
import winreg
from pathlib import Path

failures = []
lines = []


def trace(msg):
    print(time.strftime("%H:%M:%S ") + msg, flush=True)


def check(name, ok, detail=""):
    msg = ("PASS  " if ok else "FAIL  ") + name + (f"  ({detail})" if detail else "")
    print(msg, flush=True)
    lines.append(msg)
    if not ok:
        failures.append(name)


def run_limited(cmd, timeout, log_path=None):
    """Runs a command with a hard time limit. Output goes to a file (never to pipes): on
    Windows a helper process that outlives the app can hold a pipe open and make Python
    wait forever. On timeout the whole process tree is killed and None is returned."""
    out = open(log_path, "w", encoding="utf-8", errors="replace") if log_path else subprocess.DEVNULL
    started = time.monotonic()
    trace(f"START (limit {timeout}s): {' '.join(map(str, cmd))}")
    try:
        proc = subprocess.Popen([str(c) for c in cmd], stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        try:
            rc = proc.wait(timeout=timeout)
            trace(f"END   exit {rc} after {time.monotonic() - started:.1f}s")
            return rc
        except subprocess.TimeoutExpired:
            trace(f"TIMEOUT after {timeout}s: {' '.join(map(str, cmd))}")
            # Record what was running, so the cause of a hang is visible in the log.
            tasks = subprocess.run(["tasklist", "/V", "/FO", "CSV"], capture_output=True, text=True, errors="replace",
                                   timeout=60).stdout
            trace("Running processes at timeout:\n" + "\n".join(
                line for line in tasks.splitlines() if any(k in line.lower() for k in ("msiexec", "musescore", "stavelle", "crashpad", "python"))))
            subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=60)
            try:
                proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                trace("process did not exit after taskkill")
            return None
    finally:
        if log_path:
            out.close()


APP_PROCESSES = ("MuseScore4.exe", "Stavelle.exe", "crashpad_handler.exe")


def close_app_processes():
    """Make sure no MuseScore/Stavelle process (or helper) holds installed files, so Windows
    Installer never has to wait for, or forcibly close, running programs."""
    for name in APP_PROCESSES:
        subprocess.run(["taskkill", "/F", "/T", "/IM", name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=60)


def msiexec(args, log):
    close_app_processes()
    # REBOOT=ReallySuppress: never restart the machine (stronger than /norestart).
    # MSIRESTARTMANAGERCONTROL=Disable: never ask Restart Manager to shut down other programs.
    rc = run_limited(["msiexec"] + args + ["/qn", "/norestart", "REBOOT=ReallySuppress", "MSIRESTARTMANAGERCONTROL=Disable",
                                           "/l*v", str(log)], timeout=600)
    return "timed out" if rc is None else rc


def hash_tree(root):
    out = {}
    root = Path(root)
    if not root.exists():
        return out
    for p in sorted(root.rglob("*")):
        if p.is_file():
            try:
                out[str(p.relative_to(root))] = hashlib.sha256(p.read_bytes()).hexdigest()
            except OSError as e:
                out[str(p.relative_to(root))] = f"unreadable: {e}"
    return out


def read_key(hive, path):
    """Returns {subpath: {value_name: value}} for a registry key and all its subkeys."""
    result = {}
    try:
        key = winreg.OpenKey(hive, path, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY)
    except OSError:
        return result
    with key:
        values = {}
        i = 0
        while True:
            try:
                name, data, typ = winreg.EnumValue(key, i)
                values[name] = repr(data)
                i += 1
            except OSError:
                break
        result[path] = values
        i = 0
        subs = []
        while True:
            try:
                subs.append(winreg.EnumKey(key, i))
                i += 1
            except OSError:
                break
    for s in subs:
        result.update(read_key(hive, path + "\\" + s))
    return result


def subkey_names(hive, path):
    names = []
    try:
        with winreg.OpenKey(hive, path, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            i = 0
            while True:
                try:
                    names.append(winreg.EnumKey(key, i))
                    i += 1
                except OSError:
                    break
    except OSError:
        pass
    return names


def musescore_registry():
    snap = {}
    for hive_name, hive in (("HKLM", winreg.HKEY_LOCAL_MACHINE), ("HKCU", winreg.HKEY_CURRENT_USER)):
        classes = r"SOFTWARE\Classes"
        for ext in (".mscz", ".mscx", ".mscs", ".mscz~"):
            for k, v in read_key(hive, classes + "\\" + ext).items():
                snap[f"{hive_name}\\{k}"] = v
        for name in subkey_names(hive, classes):
            if name.lower().startswith("musescore"):
                for k, v in read_key(hive, classes + "\\" + name).items():
                    snap[f"{hive_name}\\{k}"] = v
        for name in subkey_names(hive, "SOFTWARE"):
            if name.lower().startswith("musescore"):
                for k, v in read_key(hive, "SOFTWARE\\" + name).items():
                    snap[f"{hive_name}\\{k}"] = v
        reg_apps = read_key(hive, r"SOFTWARE\RegisteredApplications").get(r"SOFTWARE\RegisteredApplications", {})
        snap[f"{hive_name}\\RegisteredApplications(MuseScore)"] = {k: v for k, v in reg_apps.items() if "musescore" in k.lower()}
        app_paths = r"SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths"
        for name in subkey_names(hive, app_paths):
            if name.lower().startswith("musescore"):
                for k, v in read_key(hive, app_paths + "\\" + name).items():
                    snap[f"{hive_name}\\{k}"] = v
        uninstall = r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"
        for name in subkey_names(hive, uninstall):
            entry = read_key(hive, uninstall + "\\" + name).get(uninstall + "\\" + name, {})
            if "musescore" in entry.get("DisplayName", "").lower():
                snap[f"{hive_name}\\{uninstall}\\{name}"] = entry
    return snap


def without_stavelle_open_with(reg):
    """Stavelle may add its own value to a file type's OpenWithProgids list, nothing else."""
    out = {}
    for k, v in reg.items():
        if k.endswith("\\OpenWithProgids"):
            v = {n: d for n, d in v.items() if not n.startswith("Stavelle.")}
            if not v:
                continue  # an "Open with" list holding only Stavelle's entry belongs to Stavelle
        out[k] = v
    return out


def default_handlers(reg):
    """The program each score type opens with on double-click (the file type's default value)."""
    out = {}
    for k, v in reg.items():
        for ext in (".mscz", ".mscx", ".mscs"):
            if k.endswith("\\Classes\\" + ext):
                out[k] = v.get("", "(none)")
    return out


def musescore_data_dirs():
    local = Path(os.environ["LOCALAPPDATA"])
    roaming = Path(os.environ["APPDATA"])
    docs = Path(os.environ["USERPROFILE"]) / "Documents"
    return {"LocalAppData\\MuseScore": local / "MuseScore", "AppData\\MuseScore": roaming / "MuseScore",
            "Documents\\MuseScore4": docs / "MuseScore4"}


def snapshot(ms_install):
    trace("snapshot of MuseScore files and registry")
    return {"install": hash_tree(ms_install),
            "data": {k: hash_tree(p) for k, p in musescore_data_dirs().items()},
            "registry": musescore_registry()}


def diff(a, b):
    keys = sorted(set(a) | set(b))
    return [k for k in keys if a.get(k) != b.get(k)]


def run_converter(exe, src, out):
    if Path(out).exists():
        Path(out).unlink()
    rc = run_limited([exe, "-o", out, src], timeout=180, log_path=str(out) + ".log")
    close_app_processes()
    return rc == 0 and Path(out).exists() and Path(out).stat().st_size > 0


TEST_LIMIT_SECONDS = 15 * 60


def start_watchdog_and_heartbeat():
    """Heartbeat: a line every 30 s, visible in GitHub's live log even if the log is never saved.
    Watchdog: end the whole test after TEST_LIMIT_SECONDS (normal run: about 2 minutes)."""
    import threading
    started = time.monotonic()

    def loop():
        while True:
            time.sleep(30)
            elapsed = time.monotonic() - started
            trace(f"heartbeat: test running {elapsed:.0f}s")
            if elapsed > TEST_LIMIT_SECONDS:
                trace(f"WATCHDOG: test exceeded {TEST_LIMIT_SECONDS}s; ending it")
                close_app_processes()
                os._exit(3)

    threading.Thread(target=loop, daemon=True).start()


def main():
    start_watchdog_and_heartbeat()
    ms_msi, st_msi, score, work = (Path(a).resolve() for a in sys.argv[1:5])
    work.mkdir(parents=True, exist_ok=True)
    pf = Path(os.environ.get("ProgramW6432", os.environ["ProgramFiles"]))

    # 1. Install MuseScore and use it
    rc = msiexec(["/i", str(ms_msi)], work / "musescore_install.log")
    check("Official MuseScore installs", rc == 0, f"msiexec exit {rc}")
    ms_exes = sorted(pf.glob("MuseScore*/bin/MuseScore*.exe"))
    check("MuseScore executable found", bool(ms_exes), ", ".join(map(str, ms_exes)))
    if not ms_exes:
        return finish(work)
    ms_exe = ms_exes[0]
    ms_install = ms_exe.parent.parent
    check("MuseScore works", run_converter(ms_exe, score, work / "musescore_1.pdf"))
    check("MuseScore created its own data folder", musescore_data_dirs()["LocalAppData\\MuseScore"].exists())

    # 2. Snapshot MuseScore
    s0 = snapshot(ms_install)
    check("MuseScore registry entries captured", len(s0["registry"]) > 0, f"{len(s0['registry'])} keys")
    print("Score file types before Stavelle:", default_handlers(s0["registry"]))

    # 3. Install Stavelle and use it
    rc = msiexec(["/i", str(st_msi)], work / "stavelle_install.log")
    check("Stavelle installs alongside MuseScore", rc == 0, f"msiexec exit {rc}")
    st_exe = pf / "Stavelle" / "bin" / "Stavelle.exe"
    check("Stavelle installed to its own folder", st_exe.exists(), str(st_exe))
    check("Stavelle not installed inside MuseScore's folder", not any(ms_install.rglob("Stavelle*")))
    if st_exe.exists():
        check("Stavelle works", run_converter(st_exe, score, work / "stavelle_1.pdf"))
        check("Stavelle saves a score", run_converter(st_exe, score, work / "stavelle_saved.mscz"))

    s1 = snapshot(ms_install)
    d = diff(s0["install"], s1["install"])
    check("MuseScore installation files unchanged after installing and using Stavelle", not d, ", ".join(d[:5]))
    for name in s0["data"]:
        d = diff(s0["data"][name], s1["data"][name])
        check(f"MuseScore {name} (settings, recovery, plugins) unchanged", not d, ", ".join(d[:5]))
    d = diff(without_stavelle_open_with(s0["registry"]), without_stavelle_open_with(s1["registry"]))
    check("MuseScore registry entries unchanged (only Stavelle's own 'Open with' entry added)", not d, ", ".join(d[:5]))
    check("Double-clicking score files still opens what it did before",
          default_handlers(s0["registry"]) == default_handlers(s1["registry"]), str(default_handlers(s1["registry"])))
    check("Stavelle offered in 'Open with' for .mscz",
          any(n.startswith("Stavelle.") for k, v in s1["registry"].items() if k.endswith(".mscz\\OpenWithProgids") for n in v))
    check("MuseScore still works with Stavelle installed", run_converter(ms_exe, score, work / "musescore_2.pdf"))

    s1b = snapshot(ms_install)  # MuseScore may update its own settings when it runs

    # 4. Uninstall Stavelle
    rc = msiexec(["/x", str(st_msi)], work / "stavelle_uninstall.log")
    check("Stavelle uninstalls", rc == 0, f"msiexec exit {rc}")
    check("Stavelle program files removed", not st_exe.exists())
    s2 = snapshot(ms_install)
    d = diff(s1b["install"], s2["install"])
    check("MuseScore installation files unchanged after uninstalling Stavelle", not d, ", ".join(d[:5]))
    for name in s1b["data"]:
        d = diff(s1b["data"][name], s2["data"][name])
        check(f"MuseScore {name} unchanged after uninstalling Stavelle", not d, ", ".join(d[:5]))
    d = diff(s0["registry"], s2["registry"])
    check("MuseScore registry entries exactly as before Stavelle was installed", not d, ", ".join(d[:5]))
    check("MuseScore still works after Stavelle is uninstalled", run_converter(ms_exe, score, work / "musescore_3.pdf"))

    negative_control(ms_install, st_msi, work)
    return finish(work)


def negative_control(ms_install, st_msi, work):
    """Proves the checks above can actually detect interference: install Stavelle again,
    deliberately tamper with MuseScore's registry, default handler, program files and
    settings, and require every tampering to be detected. Then undo it all."""
    trace("negative control: deliberate tampering must be detected")
    before = snapshot(ms_install)
    rc = msiexec(["/i", str(st_msi)], work / "negative_install.log")
    check("Negative control: Stavelle reinstalls", rc == 0, f"msiexec exit {rc}")

    hive = winreg.HKEY_CURRENT_USER
    ext_key = r"SOFTWARE\Classes\.mscz"
    progid_key = r"SOFTWARE\Classes\MuseScore.mscz.negativecontrol"
    open_with_key = ext_key + r"\OpenWithProgids"
    with winreg.CreateKeyEx(hive, ext_key, 0, winreg.KEY_ALL_ACCESS) as k:
        try:
            old_default = winreg.QueryValueEx(k, "")
        except OSError:
            old_default = None
        winreg.SetValueEx(k, "", 0, winreg.REG_SZ, "Stavelle.mscz.tampered")
    winreg.CreateKeyEx(hive, progid_key, 0, winreg.KEY_ALL_ACCESS).Close()
    with winreg.CreateKeyEx(hive, open_with_key, 0, winreg.KEY_ALL_ACCESS) as k:
        winreg.SetValueEx(k, "MuseScore.injected", 0, winreg.REG_SZ, "")
    planted_file = ms_install / "bin" / "stavelle_negative_control.txt"
    planted_file.write_text("tampered", encoding="utf-8")
    data_dir = musescore_data_dirs()["LocalAppData\\MuseScore"]
    planted_setting = data_dir / "stavelle_negative_control.ini"
    if data_dir.exists():
        planted_setting.write_text("tampered", encoding="utf-8")

    after = snapshot(ms_install)
    check("Negative control: changed default program for .mscz is detected",
          default_handlers(before["registry"]) != default_handlers(after["registry"]))
    reg_diff = diff(without_stavelle_open_with(before["registry"]), without_stavelle_open_with(after["registry"]))
    check("Negative control: new MuseScore ProgID key is detected", any("negativecontrol" in k for k in reg_diff), ", ".join(reg_diff[:5]))
    check("Negative control: non-Stavelle 'Open with' entry is detected", any(k.endswith("OpenWithProgids") for k in reg_diff))
    check("Negative control: change to MuseScore's program files is detected", bool(diff(before["install"], after["install"])))
    if data_dir.exists():
        check("Negative control: change to MuseScore's settings folder is detected",
              bool(diff(before["data"]["LocalAppData\\MuseScore"], after["data"]["LocalAppData\\MuseScore"])))

    # Undo the tampering and remove Stavelle again
    with winreg.CreateKeyEx(hive, ext_key, 0, winreg.KEY_ALL_ACCESS) as k:
        if old_default is None:
            winreg.DeleteValue(k, "")
        else:
            winreg.SetValueEx(k, "", 0, old_default[1], old_default[0])
    with winreg.CreateKeyEx(hive, open_with_key, 0, winreg.KEY_ALL_ACCESS) as k:
        winreg.DeleteValue(k, "MuseScore.injected")
    winreg.DeleteKey(hive, progid_key)
    planted_file.unlink(missing_ok=True)
    planted_setting.unlink(missing_ok=True)
    rc = msiexec(["/x", str(st_msi)], work / "negative_uninstall.log")
    check("Negative control: Stavelle uninstalls again", rc == 0, f"msiexec exit {rc}")


def finish(work):
    summary = "## Stavelle / MuseScore coexistence (Windows)\n\n" + \
        f"**{len(lines) - len(failures)} passed, {len(failures)} failed**\n\n```\n" + "\n".join(lines) + "\n```\n"
    (work / "coexistence_results.json").write_text(json.dumps({"checks": lines, "failures": failures}, indent=2), encoding="utf-8")
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as f:
            f.write(summary)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
