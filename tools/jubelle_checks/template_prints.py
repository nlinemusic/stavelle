#!/usr/bin/env python3
# Jubelle: PDF prints of every score template and of the template test scores,
# made by the real application, for visual review (templates work, 15 Nov 2026).
#
# Usage: template_prints.py <path-to-Jubelle-executable> <output-dir>
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]


def convert(exe, src, dst):
    start = time.time()
    try:
        proc = subprocess.run([str(exe), "-o", str(dst), str(src)], stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=240)
        ok = proc.returncode == 0 and dst.exists() and dst.stat().st_size > 0
    except subprocess.TimeoutExpired:
        ok = False
    return ok, time.time() - start


def main():
    exe = pathlib.Path(sys.argv[1])
    out = pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    templates = ROOT / "share" / "templates"
    sources = []
    for cat in json.loads((templates / "categories.json").read_text(encoding="utf-8")):
        for f in cat["files"]:
            sources.append((templates / f, cat["title"]))
    for f in sorted((ROOT / "tools" / "jubelle_templates" / "testscores").glob("*.mscz")):
        sources.append((f, "Test scores"))

    failures = 0
    lines = ["| Category | Template | PDF | Seconds |", "|---|---|---|---:|"]
    for src, cat in sources:
        name = src.stem
        dst = out / (cat.replace(" ", "_") + "__" + name + ".pdf")
        ok, secs = convert(exe, src, dst)
        failures += 0 if ok else 1
        lines.append(f"| {cat} | {name} | {'ok' if ok else 'FAILED'} | {secs:.1f} |")
        print(("OK   " if ok else "FAIL ") + str(src), flush=True)
    (out / "README.md").write_text("# Template prints\n\n" + "\n".join(lines) + "\n", encoding="utf-8")
    print(f"{len(sources) - failures}/{len(sources)} printed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
