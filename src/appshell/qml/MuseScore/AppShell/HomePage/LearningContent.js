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

    { section: "Start here", title: "Workflow profiles: MuseScore, Finale or Noteflight habits",
      summary: "Menus, shortcuts and note input arranged the way you are used to.",
      body: [
        "<b>View › Workflow profile</b> has four profiles. The change happens at once, without restarting, and only changes how Jubelle is laid out: every command is still there in every profile, and your scores are not affected.",
        "<b>Jubelle</b>: Jubelle's own menus and shortcuts (the default).",
        "<b>MuseScore Studio</b>: MuseScore 4's menus (no Playback menu: its commands are in View and Tools) and MuseScore's shortcuts.",
        "<b>Finale-style</b>: the number keys add intervals (2 = a second above, Shift+2 below), durations are on the numeric keypad or Alt/Option + 2–7, L flips stems, H hides, / breaks beams; accidentals change the note you just entered. Menus include Utilities, Document and MIDI/Audio.",
        "<b>Noteflight-style</b>: Space turns the note into a rest, P plays, Z respells (C♯ ↔ D♭), : adds a tenuto; accidentals change the selected note. Menus include Score, Staff, Measure, Notes and Text.",
        "Your own changes (Preferences › Shortcuts, the note input toolbar, note input preferences) are kept for each profile and come back when you return to it. <b>Restore this profile's defaults…</b> undoes them for the current profile.",
        "The Finale and Noteflight profiles follow those programs' habits; they are not copies of them. On guitar tablature staves, Shift+number keeps MuseScore's tab durations."
      ] },
    { section: "Start here", title: "Reporting a bug",
      summary: "A report you check and send yourself, with your private details removed.",
      body: [
        "1. <b>Help › Report a bug…</b>",
        "2. Give it a title and describe what happened. Steps to reproduce, what you expected and what actually happened help most.",
        "3. Add screenshots if they help: <b>Add picture…</b> for a file, or <b>Capture Jubelle's window</b>.",
        "4. <b>Preview report</b> shows everything that will be in it. <b>Create report</b> saves a ZIP file; <b>Show in folder</b> finds it. Send it by e-mail or attach it to a GitHub issue yourself.",
        "Jubelle adds its version, your system's name and version, the graphics system, screen sizes, processor cores and memory, which notation engine is in use, and (if you leave the box ticked) the end of its log and the last crash. Your user name, folders, file names, score titles, e-mail addresses, passwords and keys are removed first.",
        "Your score is only added if you tick <b>Include the open score</b>. Nothing is ever uploaded."
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
    { section: "Writing", title: "Harmonise melody (big band)",
      summary: "Voice a melody for trumpets, trombones and saxophones with the Pandemonium Big Band lesson rules.",
      body: [
        "1. Write the melody (usually in Trumpet 1) and the chord symbols (usually in the piano or guitar staff).",
        "2. Select the melody bars on the melody staff.",
        "3. <b>Tools › Arranging › Harmonise Melody…</b> and choose a technique for each section.",
        "4. <b>Preview</b> writes the parts and plays them. <b>Keep</b> leaves them in the score; <b>Discard</b> puts the score back exactly as it was. One Undo also removes everything.",
        "5. To compare, <b>Save as version</b>, change the techniques, save again, and click a version to hear it.",
        "<b>Trumpets:</b> closed voicing (four notes within an octave, at least a minor third under the lead; the root is left out unless the melody is on the 3rd; a melody on the root gets the 6th or 13th underneath). Above concert A5 the automatic choice switches to trumpet triads: trumpets 1 and 4 in octaves.",
        "<b>Trombones:</b> an octave below the trumpets. With trumpet triads, trombone 1 sits between B♭3 and A4 and the others close under it.",
        "<b>Saxophones:</b> doubling the trombones with the baritone on roots (the full-band soli), or one of the nine section voicings: unison, octave unison (4/1 or 2/3), two-part, closed, Drop 2, Drop 2 and 4, four-part Drop 2, Drop 2 with baritone roots. Spread voicings need medium tempo or slower: the dialog warns when the line is too fast.",
        "<b>Passing notes:</b> a note that isn't in the chord is planed (when it moves a half step to a chord tone), re-harmonised as a diatonic passing chord (V7 of a major or minor chord, ii of a dominant), or voiced as a diminished chord.",
        "<b>Alterations:</b> on a V chord that resolves, ♭9 and ♯5 are added where the melody allows (never a ♯9 under a melody on the 3rd). The chord symbol is updated so the rhythm section plays the same.",
        "Nothing is guessed: the chords come from your chord symbols, and the same settings always write the same notes. Notes outside a player's range are listed after Preview."
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
    { section: "Playback", title: "Why MuseSounds don't play in Jubelle",
      summary: "Jubelle finds an installed MuseSounds engine, but the engine won't start outside MuseScore Studio.",
      body: [
        "MuseSounds are Muse Group's own sound libraries, installed with Muse Hub. Jubelle can see an installed MuseSounds engine, and it tries to use it the same way MuseScore does. The engine then refuses to start, without saying why. It is most likely made to work only inside the official MuseScore Studio.",
        "Jubelle will not imitate MuseScore or get round the engine's checks, so MuseSounds stay silent in Jubelle. Nothing is wrong with your computer or your installation, and MuseScore Studio still plays them as before.",
        "Scores that use MuseSounds open normally in Jubelle; those instruments play with Jubelle's built-in sounds instead. To hear realistic instruments in Jubelle, use VST3 instruments (Mixer › the instrument's sound › VST3), SoundFonts (SF2, SF3) or SFZ libraries through the free sfizz VST3 plugin.",
        "<b>Preferences › Audio & MIDI</b> shows whether a MuseSounds engine was found and whether it started."
      ] },
    { section: "Playback", title: "VST3 instruments: key switches, level and bend range",
      summary: "Make a sample library change articulation by itself, sit at the right level and glide smoothly.",
      body: [
        "1. Open the instrument's window from the Mixer and press <b>Instrument settings…</b>.",
        "2. <b>Level trim</b> makes this instrument louder or quieter than the others without changing its dynamics.",
        "3. <b>Pitch-bend range</b>: set it to the bend range in the library's own settings. Glissandos and falls wider than that then glide instead of jumping.",
        "4. <b>Key switches</b>: if the library changes articulation with low \"key switch\" notes, enter the key for staccato, mute, falls and so on, and the key for Normal. Choose whether the library calls middle C \"C4\" or \"C3\". Before each note Jubelle sends the right key.",
        "Everything is saved with the score. The Plugins page (Home › Plugins › Audio plugins) lists every VST3 plugin, switches plugins on or off and explains any that could not be loaded."
      ] },
    { section: "Export", title: "Making a video of the score",
      summary: "Still printed pages on white paper, with the sound, in the order you choose.",
      body: [
        "1. <b>File › Export…</b> and choose MP4 Video.",
        "2. Under <b>Pages and order</b>, click pages to add them, use <b>Add again</b> to repeat one, <b>Remove</b> to leave one out, and the arrows to change the order. The sentence below the list says what the video will show and how long it is. <b>Preview</b> plays the first seconds of each entry.",
        "3. The video starts on the first page, silent for the lead-in, with no title screen. The playback cursor is off unless you tick <b>Show playback cursor</b>.",
        "The sound follows the pages you chose, with a short crossfade wherever music is skipped or repeated. <b>Sound quality (AAC)</b> sets the video's own sound quality, separate from MP3 export."
      ] },
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
        "Jubelle balances the built-in sounds so a section sounds like a section before you touch the mixer, and plays dynamics from ppp to fff in clear steps (mf to f is plainly louder) for winds, brass, strings and voices. The mixer's faders stay yours and sit on top of this.",
        "Piano and other keyboards, guitars, harp and percussion follow MuseScore's own dynamics exactly: with these sounds a harder key press also brightens the tone, so they keep MuseScore's sound.",
        "For other sounds, load a VST3 instrument in the mixer. Its presets can be saved and recalled from the plug-in's window. The status bar shows which sound source is playing."
      ] },

    // ---------------------------------------------------------------- Export
    { section: "Export and sharing", title: "Exporting audio",
      summary: "The same defaults as MuseScore; no clipping on loud passages.",
      body: [
        "1. <b>File › Export › Export…</b>, then choose MP3, WAV, OGG or FLAC.",
        "The defaults are the same as MuseScore Studio's: 44.1 kHz, WAV in 32-bit float, FLAC in 16-bit, MP3 at 128 kbps. 32, 48 and 96 kHz and 16 or 24-bit are there if you need them (MP3 goes no higher than 48 kHz).",
        "Loud tuttis never clip: a limiter turns the whole mix down gently, just before a peak and only as far as needed, and leaves everything quieter untouched. 16-bit files get a tiny, inaudible dither so quiet endings stay clean. Exporting the same score twice gives identical files."
      ] },
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
      summary: "Upload straight from Jubelle, or open YouTube Studio in your browser.",
      body: [
        "1. Export the score as an MP4 video first (<b>File › Export…</b>).",
        "2. On the <b>Publish</b> tab, choose <b>Publish to YouTube…</b> and pick the video (the newest is first).",
        "<b>Uploading from Jubelle</b> needs a one-time set-up: your own Google Cloud project with YouTube Data API v3 switched on and an OAuth client of type Desktop app. The dialog has a button that opens Google Cloud Console, and the steps. Paste the client ID and secret, then press <b>Connect</b>: you sign in on Google's page in your browser and allow one permission, “Manage your YouTube videos”.",
        "Then give a title and description, choose who can see it (Private unless you choose otherwise), and press <b>Upload to YouTube</b>. You see the progress and get a link.",
        "The sign-in token is kept on this computer only (Windows Credential Manager). <b>Disconnect</b> removes it and withdraws the permission at Google. While your Google project is in Testing status, Google asks you to sign in again every 7 days.",
        "Without connecting, <b>Open YouTube Studio</b> opens YouTube's upload page in your browser and shows the video so you can drag it in."
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
