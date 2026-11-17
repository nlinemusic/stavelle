/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * Jubelle Learning Center: the guides. Written for Jubelle; every guide
 * describes a feature as it is in this program. Works offline.
 */
.pragma library

// Each topic: section, title, summary (one line), body (short paragraphs; steps start with "1." etc.)
var topics = [
    // ---------------------------------------------------------------- Start here
    { section: "Start here", title: "Projects: where your music lives",
      summary: "Every score is a project with its own folder, backups and exports.",
      body: [
        "Jubelle keeps each piece in its own project folder. The score, its backups (in Backups) and what you export stay together, so nothing gets lost between versions.",
        "1. On the Home screen, open <b>Projects</b>.",
        "2. Use <b>New score</b> to start a piece. Choose a category and a template, then your instruments.",
        "3. To bring in music you already have, use <b>Import scores…</b>. MuseScore files (.mscz), MusicXML (.musicxml, .mxl, .xml) and Noteflight exports are accepted.",
        "Scores are ordinary .mscz files. MuseScore 4 can open them, and Jubelle opens MuseScore's."
      ] },
    { section: "Start here", title: "Bringing music in from Noteflight or MusicXML",
      summary: "Import keeps your original file and tells you what could not be carried over.",
      body: [
        "1. In Noteflight, export the score as MusicXML (your account and the score's own permissions decide whether you may).",
        "2. In Jubelle: <b>Projects › Import scores…</b> and pick the file.",
        "3. Jubelle makes a new project, keeps the original file inside it, and opens the score ready to edit.",
        "After the import you get a short report: how many parts, bars and notes arrived, and anything MusicXML could not carry (for example some playback settings). Importing the same file twice does not make a duplicate."
      ] },
    { section: "Start here", title: "Templates and the jazz look",
      summary: "Start from a template; switch between engraved and handwritten styles at any time.",
      body: [
        "The Template Library holds Jubelle's templates, including big band, combo, choral, orchestral and solo-with-piano layouts. <b>File › Save as template…</b> turns any score into your own template.",
        "<b>Format › Jazz handwritten look</b> and <b>Format › Standard engraved look</b> change the music and text fonts without touching your layout. <b>Format › Engraving presets…</b> applies a whole house style."
      ] },

    // ---------------------------------------------------------------- Writing
    { section: "Writing", title: "Linked passages: write it once",
      summary: "Link a passage to other staves; edits follow, pitches stay each player's own.",
      body: [
        "Use this when several players share a rhythm or a line, such as a trumpet section soli.",
        "1. Select the passage on the staff you want to lead.",
        "2. <b>Tools › Linked Passages…</b> and tick the staves that should follow, and what follows (rhythm, articulations, dynamics, slurs).",
        "3. Edit the leading staff. The followers change with it, keeping their own pitches and octaves.",
        "A follower you have changed by hand is marked with a small dot. Right-click a linked passage (or use the Properties panel) to <b>Re-sync with the leading staff</b> or <b>Unlink this passage</b>. Links are saved with the score."
      ] },
    { section: "Writing", title: "Courtesy accidentals",
      summary: "Automatic reminders after an accidental, by rules you set.",
      body: [
        "<b>Format › Courtesy accidentals on/off</b> switches them for the score. The rules are in <b>Format › Style</b>: how many bars to look back, whether other staves of the same instrument count, and whether reminders get parentheses.",
        "Courtesy accidentals update as you edit and never change what is played."
      ] },
    { section: "Writing", title: "Chord Identifier",
      summary: "Names the chords in a selection as chord symbols or Roman numerals.",
      body: [
        "1. Select a passage (or nothing, for the whole score).",
        "2. <b>Tools › Chord Identifier…</b>",
        "3. Choose chord symbols or Roman numerals and where to put them.",
        "The result is ordinary chord-symbol text you can edit."
      ] },
    { section: "Writing", title: "Find and Select",
      summary: "Select everything that matches: kind, instruments, voices, pitch, duration and more.",
      body: [
        "<b>Edit › Find and Select…</b> selects every note, rest or marking that matches what you choose. Combine conditions: for example all staccato eighth notes in the saxophones above written C5.",
        "The selection is a normal selection, so you can then delete, transpose, re-voice or change properties in one go."
      ] },
    { section: "Writing", title: "Lining up a title page",
      summary: "Align and distribute title text and images like in a drawing program.",
      body: [
        "Select two or more texts or images and use <b>Format › Align</b>: left, centre, right, top, middle, bottom, or distribute evenly.",
        "Choose what to align against: <b>margins</b>, the whole <b>page</b>, or <b>each other</b>. Sizes and fonts never change."
      ] },

    // ---------------------------------------------------------------- Layout
    { section: "Layout and engraving", title: "Magnetic layout and collision avoidance",
      summary: "Dynamics, text and lines line up in rows and move out of each other's way.",
      body: [
        "On for new scores, off for existing ones until you switch it on, so old scores look exactly as before.",
        "Settings are in <b>Format › Style › Jubelle layout</b>: magnetic rows, collision avoidance, sideways nudging (and its limit), and the engraving rules for where each kind of marking goes.",
        "Anything you place by hand keeps its position. Notes are never moved; the lower-priority marking is the one that gives way."
      ] },
    { section: "Layout and engraving", title: "Proofreading",
      summary: "A list of likely mistakes you can jump to, dismiss or mark as intended.",
      body: [
        "<b>View › Proofread score</b> (also in Tools) opens the Proofread panel and checks the score.",
        "It looks for notes out of range, awkward leaps, odd note spelling, incomplete bars, missing dynamics, unclear rhythm grouping, markings in unusual places, collisions, and part-preparation problems such as a long piece without rehearsal marks.",
        "Click a finding to go there. <b>Dismiss</b> hides it until you check again; marking it as <b>intentional</b> remembers your decision in the score. Proofreading never changes music."
      ] },
    { section: "Layout and engraving", title: "Apply Part Layout",
      summary: "Copy one part's system and page breaks to other parts.",
      body: [
        "1. Lay out one part the way you want it.",
        "2. <b>Format › Apply Part Layout…</b>, choose that part as the source and tick the parts to copy to.",
        "3. Read the status for each part: Matches, or Partly with the reason (for example a multi-measure rest in the way).",
        "Only layout is copied: breaks, keep-together marks, locks, bar widths, spacers and page settings. Never notes."
      ] },
    { section: "Layout and engraving", title: "Page turns",
      summary: "See how much rest each page turn has, and move poor turns to better places.",
      body: [
        "1. <b>Format › Page turns…</b>",
        "2. Choose the part, how the pages are bound (turns after right-hand pages, or after every page for single sheets or a tablet), and how many seconds a turn needs.",
        "3. Each turn is listed as Good, Tight or No time to turn. Press <b>Preview</b> to see what Jubelle would change; nothing changes until you press Apply.",
        "Jubelle moves a turn back to the nearest place with enough rest by adding a page break. Your own page breaks are kept. If there is no rest near a turn, it says so honestly. <b>Remove Jubelle's turns</b> takes its breaks out again."
      ] },
    { section: "Layout and engraving", title: "Condensed score (a separate copy)",
      summary: "Make a conductor's score with two players on a staff, without changing the full score.",
      body: [
        "Switch it on with <b>Tools › Condensing › Enable condensed scores</b>, then <b>Condensed score…</b> to create or update the copy.",
        "For any bars, you can decide yourself: <b>Keep these bars separate</b>, <b>Condense these bars</b>, or back to automatic. Your full score is never changed."
      ] },
    { section: "Layout and engraving", title: "The Timeline and your own markers",
      summary: "See the whole piece at a glance and jump around it.",
      body: [
        "Open the Timeline from the View menu. It shows rehearsal marks, tempo, key and time changes, and a <b>Markers</b> row for your own notes (right-click to add, rename or remove a marker).",
        "Click anywhere to go there. Right-click for <b>Play from here</b>. Markers are saved in the score and are invisible on paper."
      ] },

    // ---------------------------------------------------------------- Playback
    { section: "Playback", title: "Swing",
      summary: "Choose a feel for the score or for single instruments. Playback only.",
      body: [
        "<b>Format › Swing…</b> offers several feels, including a Basie feel, and lets you edit the ratio by tempo. A swing marking can be shown in the score as text, as the eighth-note equation, or both.",
        "Swing changes how eighth notes are played, never how they are written."
      ] },
    { section: "Playback", title: "How articulations sound",
      summary: "Staccato length, accents, falls, scoops, glissandos and turns: for the score, an instrument or one passage.",
      body: [
        "<b>Format › Articulation Playback…</b> sets how each marking sounds for the whole score and for single instruments. <b>Articulation Playback for Selection…</b> overrides it for the passage you highlighted.",
        "The same settings are in the Properties panel when a note with an articulation is selected."
      ] },
    { section: "Playback", title: "Recording tempo by tapping",
      summary: "Tap the beat while the score plays; your rubato becomes hidden tempo markings.",
      body: [
        "1. Select where to start (or a passage, to record only that).",
        "2. <b>Playback › Record tempo…</b> and choose what each tap is (quarter notes, half notes, dotted quarters…).",
        "3. Press <b>Start</b>, then tap: the big button, any key, or a MIDI key or sustain pedal. Your first tap starts the music.",
        "4. Press <b>Finish</b>. Play to hear it; one Undo takes it back.",
        "With <b>Follow me</b> on, the music keeps up with your taps while you tap. Smoothing evens out small unsteadiness. Nothing is printed, and <b>Remove recorded tempo</b> deletes only what Jubelle recorded, not your own tempo markings."
      ] },
    { section: "Playback", title: "Repeats in playback",
      summary: "Choose which repeats are played without removing the signs.",
      body: [
        "<b>Playback › Repeats…</b> lists the repeated sections. Untick one and playback goes straight on; the repeat signs stay in the printed music."
      ] },
    { section: "Playback", title: "Levels, dynamics and other sounds",
      summary: "Built-in instruments are balanced against each other; dynamics are clearly graded.",
      body: [
        "Jubelle balances the built-in sounds so a section sounds like a section before you touch the mixer, and plays dynamics from ppp to fff in clear steps (mf to f is plainly louder). The mixer's faders stay yours and sit on top of this.",
        "For other sounds, load a VST3 instrument in the mixer. Its presets can be saved and recalled from the plug-in's window. The status bar shows which sound source is playing."
      ] },

    // ---------------------------------------------------------------- Export
    { section: "Export and sharing", title: "Drum stems",
      summary: "Kick, snare, hi-hat, toms and cymbals as separate audio files.",
      body: [
        "1. <b>File › Export drum stems…</b>",
        "2. The drums your score actually plays are listed. Put them into groups (Kick, Snare, Hi-hat, Toms, Cymbals and Percussion are suggested), rename groups, add your own.",
        "3. Choose a file for each group, for each single drum, or both; pick the format and folder; press Export.",
        "Save a grouping as a preset to reuse it in other scores. The score is not changed."
      ] },
    { section: "Export and sharing", title: "Video",
      summary: "An MP4 of the score pages following the music, straight from Export.",
      body: [
        "<b>File › Export…</b> and choose <b>MP4 video</b>. Each page is shown while its music plays, with a cursor. Choose the resolution and frame rate there.",
        "Nothing else needs installing: the FFmpeg libraries Jubelle uses come with it (in the ffmpeg folder next to the program, with their licence)."
      ] },
    { section: "Export and sharing", title: "Publishing to YouTube",
      summary: "Opens YouTube Studio with your exported video ready to drag in.",
      body: [
        "1. Export the score as an MP4 video first (<b>File › Export…</b>).",
        "2. On the <b>Publish</b> tab, choose <b>Publish to YouTube…</b> and pick the video (the newest is first).",
        "3. YouTube Studio opens in your browser with the video's folder ready, and you sign in there as usual.",
        "Jubelle does not sign in to your Google account and uploads nothing by itself. Connecting an account inside Jubelle stays switched off until you have approved its sign-in screen."
      ] },

    // ---------------------------------------------------------------- Tutorials
    { section: "Tutorials", title: "Tutorial: a big band chart from blank page to parts",
      summary: "A complete walk through, about 30 minutes.",
      body: [
        "1. <b>Projects › New score</b>, category Jazz, template Big Band. Give it a title and key.",
        "2. Write the lead trumpet's first eight bars.",
        "3. Select them, <b>Tools › Linked Passages…</b>, tick Trumpets 2–4. Now re-pitch the lower trumpets: rhythm and articulations stay linked.",
        "4. <b>Format › Swing…</b>: pick a feel and switch the swing marking on.",
        "5. Add dynamics and articulations. Listen. Adjust falls and accents in <b>Format › Articulation Playback…</b>.",
        "6. <b>Tools › Chord Identifier…</b> on the rhythm section to check your harmony.",
        "7. <b>View › Proofread score</b>, and work through the list.",
        "8. Open Parts. Lay out Trumpet 1, then <b>Format › Apply Part Layout…</b> to the other trumpets.",
        "9. <b>Format › Page turns…</b> on each part (or Apply to all parts).",
        "10. <b>File › Export…</b> for PDFs of the parts, and <b>Export drum stems…</b> if you are mixing a demo."
      ] },
    { section: "Tutorials", title: "Tutorial: preparing parts a player can read at sight",
      summary: "The checks a copyist makes before printing.",
      body: [
        "1. Rehearsal marks at every section, and bar numbers on every system.",
        "2. Multi-measure rests broken at rehearsal marks, double bars and tempo or key changes, so players can count.",
        "3. After a long rest, give a cue or at least a clear marking before the entry.",
        "4. A dynamic at every entry after rests.",
        "5. Page turns only where there is time: <b>Format › Page turns…</b>",
        "6. Run <b>Proofread score</b> on each part, not only the full score: range, collisions, and part-preparation checks.",
        "7. Print one part and read it at the stand before printing them all."
      ] },
    { section: "Tutorials", title: "Tutorial: a rubato ballad with recorded tempo",
      summary: "Make playback breathe without writing a single tempo mark.",
      body: [
        "1. Write the ballad with one plain tempo marking at the start.",
        "2. <b>Playback › Record tempo…</b>, each tap a quarter note, Follow me on.",
        "3. Start, and conduct it with the space bar: slow into the cadences, move on through the middle of phrases.",
        "4. Finish, listen, and re-record any passage by selecting just those bars first.",
        "5. For a video, <b>File › Export…</b> as MP4: the pages follow your tempo."
      ] }
]

function sections() {
    var out = []
    for (var i = 0; i < topics.length; ++i) {
        if (out.indexOf(topics[i].section) < 0) {
            out.push(topics[i].section)
        }
    }
    out.push("Instrument ranges and transposition")
    out.push("Keyboard shortcuts")
    return out
}

function topicsIn(section, search) {
    var out = []
    var needle = (search || "").toLowerCase()
    for (var i = 0; i < topics.length; ++i) {
        var t = topics[i]
        if (needle !== "") {
            var hay = (t.title + " " + t.summary + " " + t.body.join(" ")).toLowerCase()
            if (hay.indexOf(needle) < 0) {
                continue
            }
        } else if (t.section !== section) {
            continue
        }
        out.push(t)
    }
    return out
}
