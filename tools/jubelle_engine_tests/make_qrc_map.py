#!/usr/bin/env python3
"""Writes qrcmap.h: maps the engine's Qt resource paths (":/...") to the real files
in the repository, so the Qt-free engine tests can read fonts, styles and data.
Chord description files are left out: without Qt the engine cannot parse their
numbers, and the tests don't need them."""
import glob, os, re, sys

root, out_path = sys.argv[1], sys.argv[2]
entries = []
for q in sorted(glob.glob(os.path.join(root, "src/engraving/**/*.qrc"), recursive=True)):
    d = os.path.dirname(q)
    text = open(q, encoding="utf-8").read()
    for blk in re.finditer(r'<qresource(?:\s+prefix="([^"]*)")?>(.*?)</qresource>', text, re.S):
        prefix = (blk.group(1) or "/").strip("/")
        for m in re.finditer(r'<file(?:\s+alias="([^"]*)")?>([^<]*)</file>', blk.group(2)):
            real = os.path.normpath(os.path.join(d, m.group(2)))
            if "/data/chords/" in real.replace("\\", "/"):
                continue
            alias = m.group(1) or m.group(2)
            key = ":/" + "/".join(x for x in (prefix, alias) if x)
            entries.append((key, real.replace("\\", "/")))

with open(out_path, "w", encoding="utf-8") as f:
    f.write("#pragma once\n#include <map>\n#include <string>\n")
    f.write("static const std::map<std::string, std::string> QRC_MAP {\n")
    for k, v in entries:
        f.write('    { "%s", "%s" },\n' % (k, v))
    f.write("};\n")
