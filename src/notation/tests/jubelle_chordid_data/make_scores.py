#!/usr/bin/env python3
"""Writes the Chord Identifier test scores (MuseScore 3.01 .mscx format).
Run from this folder: python3 make_scores.py"""

HEAD = '''<?xml version="1.0" encoding="UTF-8"?>
<museScore version="3.01">
  <Score>
    <Division>480</Division>
    <Style>
      <Spatium>1.76389</Spatium>
      </Style>
    <metaTag name="workTitle">{title}</metaTag>
'''

def part(name, clef="G", transpose=None):
    t = ""
    if transpose:
        t = "        <transposeDiatonic>%d</transposeDiatonic>\n        <transposeChromatic>%d</transposeChromatic>\n" % transpose
    return f'''    <Part>
      <Staff id="{{sid}}">
        <StaffType group="pitched">
          <name>stdNormal</name>
          </StaffType>
        <defaultClef>{clef}</defaultClef>
        </Staff>
      <trackName>{name}</trackName>
      <Instrument>
        <longName>{name}</longName>
        <trackName>{name}</trackName>
{t}        <Channel>
          <program value="0"/>
          </Channel>
        </Instrument>
      </Part>
'''

# (pitch, tpc) for concert pitch; tpc2 for written pitch (only transposing staves)
NOTES = {
    "C3": (48, 14), "G2": (43, 15), "E4": (64, 18), "G4": (67, 15), "C5": (72, 14), "F4": (65, 13),
    "A4": (69, 17), "B4": (71, 19), "D5": (74, 16), "F5": (77, 13), "E5": (76, 18), "D4": (62, 16),
    "C4": (60, 14), "Db4": (61, 9), "Gb4": (66, 8), "Bb4": (70, 12), "Db5": (73, 9), "F#4": (66, 20),
    "A#4": (70, 24), "C#5": (73, 21), "A3": (57, 17), "E3": (52, 18), "G#3": (56, 22), "B3": (59, 19),
    "D4n": (62, 16), "A2": (45, 17), "E2": (40, 18),
}

def chord(dur, names, written=None):
    s = f"          <Chord>\n            <durationType>{dur}</durationType>\n"
    for i, n in enumerate(names):
        p, tpc = NOTES[n]
        s += f"            <Note>\n              <pitch>{p}</pitch>\n              <tpc>{tpc}</tpc>\n"
        if written:
            s += f"              <tpc2>{written[i]}</tpc2>\n"
        s += "              </Note>\n"
    return s + "            </Chord>\n"

def rest():
    return "          <Rest>\n            <durationType>measure</durationType>\n            <duration>4/4</duration>\n            </Rest>\n"

def measure(voices, first=False, keysig=None, last=False):
    s = "      <Measure>\n"
    for vi, v in enumerate(voices):
        s += "        <voice>\n"
        if first and vi == 0:
            if keysig is not None:
                s += keysig
            s += "          <TimeSig>\n            <sigN>4</sigN>\n            <sigD>4</sigD>\n            </TimeSig>\n"
        s += v
        if last and vi == 0:
            s += "          <BarLine>\n            <subtype>end</subtype>\n            <span>1</span>\n            </BarLine>\n"
        s += "          </voice>\n"
    return s + "        </Measure>\n"

def score(title, parts, staves, keysig=None):
    s = HEAD.format(title=title)
    for i, p in enumerate(parts):
        s += p.replace("{sid}", str(i + 1))
    for i, bars in enumerate(staves):
        s += f'    <Staff id="{i + 1}">\n'
        for b, voices in enumerate(bars):
            s += measure(voices, first=(b == 0), keysig=keysig, last=(b == len(bars) - 1))
        s += "      </Staff>\n"
    return s + "    </Score>\n  </museScore>\n"

# progression.mscx: Upper (treble) + Lower (bass), C major
#  bar 1: C (C3 held whole) then F/C (F A C over the held C3)
#  bar 2: G7 (upper B D F, lower G2); upper staff alone is Bdim
#  bar 3: C in two voices + C3, repeated on beat 3 (same chord)
#  bar 4: Dm7 (upper only; lower rests)
#  bar 5: cluster C Db D (not a chord)
#  bar 6: Gb major spelled Gb Bb Db; bar 7: F# major spelled F# A# C#
upper = [
    [chord("half", ["E4", "G4", "C5"]) + chord("half", ["F4", "A4", "C5"])],
    [chord("whole", ["B4", "D5", "F5"])],
    [chord("half", ["E5"]) + chord("half", ["E5"]), chord("whole", ["G4", "C5"])],
    [chord("whole", ["D4", "F4", "A4", "C5"])],
    [chord("whole", ["C4", "Db4", "D4n"])],
    [chord("whole", ["Gb4", "Bb4", "Db5"])],
    [chord("whole", ["F#4", "A#4", "C#5"])],
]
lower = [
    [chord("whole", ["C3"])],
    [chord("whole", ["G2"])],
    [chord("half", ["C3"]) + chord("half", ["C3"])],
    [rest()], [rest()], [rest()], [rest()],
]
open("progression.mscx", "w").write(score("progression", [part("Upper"), part("Lower", "F")], [upper, lower]))

# minor.mscx: A minor (no sharps, minor mode): Am, E7, Am
minor_key = "          <KeySig>\n            <accidental>0</accidental>\n            <mode>minor</mode>\n            </KeySig>\n"
m_up = [[chord("whole", ["A3", "C4", "E4"])], [chord("whole", ["G#3", "B3", "D4n", "E4"])], [chord("whole", ["A3", "C4", "E4"])]]
open("minor.mscx", "w").write(score("minor", [part("Piano")], [m_up], keysig=minor_key))

# trumpet.mscx: Trumpet in Bb (sounds a major 2nd lower). Concert C E G is written D F# A;
# bar 2: concert E G C (C/E) written F# A D (D/F#).
tr = [[chord("whole", ["C4", "E4", "G4"], written=[16, 20, 17])],
      [chord("whole", ["E4", "G4", "C5"], written=[20, 17, 16])]]
open("trumpet.mscx", "w").write(score("trumpet", [part("Trumpet in Bb", transpose=(-1, -2))], [tr]))
print("ok")
