#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes a MusicXML test score for comparing audio exports (Jubelle against
MuseScore): both apps import the same file the same way, so they play exactly
the same notes. It has, in turn:

  1. piano: soft, medium and loud chords (pp, mf, ff), dense close voicings
  2. piano: staccato, accent, tenuto and marcato notes
  3. trumpet, alto sax and strings: sustained notes with a crescendo pp -> ff
     and a diminuendo back, then a loud tutti chord with accents
  4. tempo changes (100 -> 140 -> 80 BPM) with running eighth notes
  5. unmarked music (no dynamic at all)

Usage: make_audio_test_score.py <out.musicxml>
"""
import sys

PARTS = [
    ("P1", "Piano", "keyboard.piano", 1, 0),
    ("P2", "Trumpet", "brass.trumpet", 57, 0),
    ("P3", "Alto Saxophone", "wind.reed.saxophone.alto", 66, 0),
    ("P4", "Strings", "strings.group", 49, 0),
]

STEPS = "CDEFGAB"


def pitch(midi):
    names = [("C", 0), ("C", 1), ("D", 0), ("D", 1), ("E", 0), ("F", 0), ("F", 1), ("G", 0), ("G", 1), ("A", 0), ("A", 1), ("B", 0)]
    step, alter = names[midi % 12]
    octave = midi // 12 - 1
    return f"<pitch><step>{step}</step>{'<alter>1</alter>' if alter else ''}<octave>{octave}</octave></pitch>"


def note(midis, dur, typ, art=None, tie=None):
    out = []
    for i, m in enumerate(midis):
        n = "<note>" + ("<chord/>" if i else "") + pitch(m) + f"<duration>{dur}</duration><type>{typ}</type>"
        notations = ""
        if art and i == 0:
            notations += f"<articulations><{art}/></articulations>"
        if notations:
            n += f"<notations>{notations}</notations>"
        out.append(n + "</note>")
    return "".join(out)


def rest(dur, typ="whole"):
    return f"<note><rest measure=\"yes\"/><duration>{dur}</duration><type>{typ}</type></note>" if typ == "whole" else \
        f"<note><rest/><duration>{dur}</duration><type>{typ}</type></note>"


def dyn(mark):
    return f"<direction placement=\"below\"><direction-type><dynamics><{mark}/></dynamics></direction-type></direction>"


def wedge(kind):
    return f"<direction placement=\"below\"><direction-type><wedge type=\"{kind}\"/></direction-type></direction>"


def tempo(bpm):
    return (f"<direction placement=\"above\"><direction-type><metronome><beat-unit>quarter</beat-unit>"
            f"<per-minute>{bpm}</per-minute></metronome></direction-type><sound tempo=\"{bpm}\"/></direction>")


Q = 4          # divisions per quarter
W = 16


def measures_for(pid):
    m = []
    # section 1: piano chords at pp, mf, ff (others rest)
    for mark in ("pp", "mf", "ff"):
        if pid == "P1":
            m.append(dyn(mark) + note([48, 55, 60, 64, 67, 72], 8, "half") + note([50, 57, 62, 65, 69, 74], 8, "half"))
        else:
            m.append(rest(W))
    # section 2: piano articulations
    for art in ("staccato", "accent", "tenuto", "strong-accent"):
        if pid == "P1":
            m.append(dyn("mf") + "".join(note([60 + k, 64 + k], Q, "quarter", art) for k in (0, 2, 4, 5)))
        else:
            m.append(rest(W))
    # section 3: sustained crescendo / diminuendo, then a loud accented tutti
    held = {"P1": [43], "P2": [67], "P3": [64], "P4": [55, 60, 64]}[pid]
    if pid == "P1":
        m += [rest(W), rest(W), rest(W), rest(W)]
    else:
        m.append(dyn("pp") + wedge("crescendo") + note(held, W, "whole"))
        m.append(note(held, W, "whole") + wedge("stop"))
        m.append(dyn("ff") + wedge("diminuendo") + note(held, W, "whole"))
        m.append(note(held, W, "whole") + wedge("stop"))
    tutti = {"P1": [36, 48, 55, 60, 64, 67, 72], "P2": [72], "P3": [67], "P4": [48, 55, 64, 72]}[pid]
    m.append(dyn("fff") + note(tutti, Q, "quarter", "accent") + note(tutti, Q, "quarter", "accent")
             + note(tutti, 8, "half", "strong-accent"))
    # section 4: tempo changes, running eighths in the piano and sax
    for bpm in (100, 140, 80):
        if pid in ("P1", "P3"):
            base = 60 if pid == "P1" else 69
            run = "".join(note([base + d], 2, "eighth") for d in (0, 2, 4, 5, 7, 5, 4, 2))
            m.append((tempo(bpm) if pid == "P1" else "") + dyn("mf") + run)
        else:
            m.append((tempo(bpm) if pid == "P1" else "") + rest(W))
    # section 5: unmarked (no dynamic): the instruments' natural level
    m.append(rest(W))
    natural = {"P1": [60, 64, 67], "P2": [72], "P3": [64], "P4": [48, 60]}[pid]
    m.append(note(natural, W, "whole"))
    m.append(rest(W))
    return m


def main():
    out = sys.argv[1]
    x = ['<?xml version="1.0" encoding="UTF-8"?>',
         '<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">',
         '<score-partwise version="4.0"><work><work-title>Audio export comparison</work-title></work><part-list>']
    for pid, name, sound, prog, _ in PARTS:
        x.append(f'<score-part id="{pid}"><part-name>{name}</part-name><score-instrument id="{pid}-I1">'
                 f'<instrument-name>{name}</instrument-name><instrument-sound>{sound}</instrument-sound></score-instrument>'
                 f'<midi-instrument id="{pid}-I1"><midi-program>{prog}</midi-program></midi-instrument></score-part>')
    x.append("</part-list>")
    for pid, name, sound, prog, _ in PARTS:
        x.append(f'<part id="{pid}">')
        for i, content in enumerate(measures_for(pid)):
            attrs = ""
            if i == 0:
                clef = '<clef><sign>F</sign><line>4</line></clef>' if False else '<clef><sign>G</sign><line>2</line></clef>'
                attrs = (f"<attributes><divisions>{Q}</divisions><key><fifths>0</fifths></key><time><beats>4</beats>"
                         f"<beat-type>4</beat-type></time>{clef}</attributes>")
                if pid == "P1":
                    attrs += tempo(100)
            x.append(f'<measure number="{i + 1}">{attrs}{content}</measure>')
        x.append("</part>")
    x.append("</score-partwise>")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(x))


if __name__ == "__main__":
    main()
