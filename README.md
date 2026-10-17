# VArpeggio

VArpeggio is a native Qt 6 piano practice, performance, recording, and notation application for KDE/Linux. It was created by Voncloft for learning with a real MIDI keyboard, playing or editing a grand staff, and turning supported score and media formats into practice material.

## Main features

- **Wait mode:** playback advances only while the correct key or complete chord is held for the written duration; repeated notes require a fresh key press unless they are tied.
- **Listen:** plays the generated notes represented on the live staff without requiring keyboard input.
- **Play Along:** follows the score clock while showing the current notes, keyboard keys, and falling-note display.
- **Free Play:** immediately displays played notes on the grand staff, full 88-key keyboard, and optional falling-note view. Recording is optional.
- **Daily practice:** dated routines for scales, chords, separate hands, hands together, repertoire, and ear training, with previous/next navigation.
- **Chord practice:** exercises every root with perfect fifths, triads, suspended chords, extended chords, inversions, separate or combined hands, and block or broken patterns.
- **Ear training:** plays a hidden note or chord for the player to identify on a MIDI, computer, or onscreen keyboard.
- **Full score display:** treble and bass staves, chords, beams, accidentals, rests, key and time signatures, repeats, ties, ledger lines, and MIDI pitches 21–108.
- **MIDI input and output:** receives notes from ALSA MIDI keyboards and can send playback, tone selection, and volume messages to compatible hardware.
- **Metronome:** optional audible beat during supported practice and playback modes.
- **Recorded piano playback:** uses the bundled CC0 FreePats Kawai upright recordings with natural, bright, and soft profiles instead of a simple oscillator.

## Score import and export

VArpeggio imports:

- MIDI (`.mid`, `.midi`)
- MusicXML (`.musicxml`, `.mxl`, `.xml`)
- ABC notation (`.abc`)
- VArpeggio projects (`.varpeggio`, JSON format)
- playable VArpeggio PDFs and ordinary engraved PDFs (`.pdf`)

VArpeggio exports:

- MIDI, MusicXML, ABC, and VArpeggio projects
- engraved PDF, SVG, and PNG scores
- WAV piano audio
- MP4 falling-note piano videos
- CSV note data

VArpeggio-generated PDFs carry editable note data as an attachment. Reopening one restores its playback and practice data while preserving the exact page under **Exact PDF**.

## Free Play and score editing

Every played key appears immediately. A held key grows from a short value into quarter, half, whole, or tied sustain notation while it remains down.

- Click the staff to place the orange insertion line.
- Play one or more keys to insert notes at that position.
- Right-click notes to delete them or change their duration or accidental treatment.
- Right-click empty staff space to insert rests, change the time signature, change the key signature, or add or remove a repeat end.
- Group mode allows notes entered separately to share one musical position as a chord.
- Sustain-pedal presses are recorded independently from note length and displayed as `Ped.` and `*` release marks.
- Use the Earlier–Latest scrollbar to revisit older measures.
- **Preview score** plays the actual editable staff.
- **Clear staff** removes the working score after confirmation.
- The top-right Save controls export the finished score.

## Plain PDF recognition

When an ordinary PDF has no attached note data or same-name MIDI/MusicXML sidecar, VArpeggio performs optical music recognition:

1. On first use, it looks for an existing Audiveris installation.
2. If none exists, it installs the official Flathub build for the current user when Flatpak is available. Otherwise, it downloads the official Audiveris Linux release from GitHub and unpacks it into VArpeggio's per-user application-data directory without root access.
3. Audiveris exports MusicXML, which VArpeggio imports for Live follow, Listen, Wait mode, Play Along, and falling notes.
4. VArpeggio reads a printed metronome marking such as `quarter note = 101` directly from the PDF when Audiveris omits tempo from its MusicXML.
5. If Audiveris setup or conversion fails, VArpeggio uses its built-in Qt PDF/OpenCV reader.

The Audiveris runtime is downloaded only once per user and reused for later PDFs. The original PDF always remains available under **Exact PDF**.

Optical recognition is a draft, not a guarantee. Clean, conventionally engraved piano scores work best. Scans, handwriting, unusual engraving, dense voices, missing text layers, and complicated rhythms or accidentals can require correction.

## Audio and falling-note video transcription

Choose **Practice → Video/audio to notes** to transcribe local media.

For falling-note tutorials, Automatic mode analyzes the complete recording, estimates tempo from the soundtrack, finds the visible keyboard geometry, supports cropped and nonstandard 53-, 61-, 88-, or other visible key ranges, tracks complete falling bars, and maps their play-line crossings to note-on and note-off events. Bar height provides held-note duration, and color can help assign left and right hands. Candidate results are rendered back from the generated notes and compared with the source soundtrack.

The full-bar tracking approach was informed by the Apache-2.0-licensed [LumaChords](https://github.com/adalkiran/lumachords) project. VArpeggio has its own C++/Qt implementation, calibration, transcription, audio validation, dynamics, notation, and KDE interface; LumaChords source code and assets are not included.

Soundtrack-only polyphonic transcription remains experimental. Clean solo piano works best. Vocals, drums, effects, and dense backing tracks can hide or introduce detected pitches. VArpeggio does not currently run a neural source-separation model.

**Hear original recording (reference only)** is an optional comparison switch. It does not replace the generated notes used by Listen, Live follow, or export.

## Playback and live display

- The BPM control accepts 30–260 BPM and can be changed while Listen or Play Along is active.
- Changing BPM retimes generated piano playback, live staff, falling notes, keyboard highlights, and metronome from the same musical position.
- The orange play line can be dragged to seek in Wait, Listen, Play Along, and Free Play score preview.
- The Earlier–Latest history bar and mouse wheel can review completed measures while playback continues; moving back to Latest restores live follow.
- Playback supports pause, resume, and stop.
- When Listen reaches the end, it returns to the beginning and remains stopped until Play is pressed again.
- Live follow advances by measures and keeps the current measure readable instead of shrinking the entire piece onto one line.
- Long notes remain visible as ties and held-note indicators for their complete sounding duration.
- Right, Left, and Together filters support separate-hand practice.

## Interface and remembered settings

- The Grand Staff, combined Bubbles and Live Coach area, and Keyboard are independently collapsible. Their open or closed states are restored the next time VArpeggio starts.
- Bubbles and Live Coach retain their original side-by-side layout inside one collapsible container, with the collapse control at the far-right edge of its header.
- The left sidebar can be pinned open or kept as an icon-only rail that temporarily expands while hovered.
- Window size, position, maximized state, app volume, sidebar state, and the **Use keyboard MIDI sound** selection are remembered between sessions.

## Changelog

### 0.2.33 — 2026-10-17

- Completed the public-release licensing bundle. VArpeggio now ships a human-readable inventory and the complete OFL-1.1 text for every engraving font embedded in Verovio, plus an exact Verovio 6.3.0 source archive link, replacement instructions, and a corresponding-source offer. These notices are installed with the application.

### 0.2.32 — 2026-10-17

- Corrected virtual-keyboard playback for legato MIDI exports. Short overlap tails remain audible, but old key highlights and finger badges now release at the next same-hand attack instead of accumulating into an impossible hand position at triplet and bar changes. During playback, a finger badge and its lit key now change on the same attack rather than showing the next badge early. Genuine long held tones remain highlighted.

### 0.2.31 — 2026-10-17

- Replaced the rigid middle-C MIDI hand split for cross-staff rolling textures. A short lower note that belongs to a nearby upper-hand pattern now stays with that hand when a substantially lower sustained bass is present. This preserves every imported note on the correct staff and prevents impossible wide left-hand assignments such as fingers 5 and 3 across an octave-plus span.

### 0.2.30 — 2026-10-17

- Collapsed duplicate same-key attacks from overlapping MIDI tracks or voices into one printed notehead and one finger number. Playback data remains untouched. Rapid neighboring attacks also receive separate fingering columns, so two valid `1` labels no longer merge visually into a nonexistent finger 11.

### 0.2.29 — 2026-10-17

- Eliminated the remaining edge clipping on dense vertical chord fingering. When a chord stack is present, the Grand Staff frame exposes the complete safe notation canvas so low ledger notes, stems, and the bottom finger digit—and the equivalent high right-hand case—remain fully visible.

### 0.2.28 — 2026-10-17

- Restored vertical fingering stacks for simultaneous chord notes and made the Grand Staff frame grow only by the height required for the largest active stack. Normal passages retain the compact frame; large chords no longer lose their outer finger numbers above or below the cropped canvas.
- Added **Settings → Show Bubbles + Live Coach**. The entire practice section—including its minimized header—can be hidden to reclaim space and restored from the menu at any time; the choice is remembered across launches.

### 0.2.27 — 2026-10-17

- Added fingering-aware horizontal paging for dense scores. Fingering remains on one unclipped row per hand; when neighboring chord groups would collide, the score automatically shows fewer complete measures and gives the printed numbers more horizontal room instead of stacking them vertically.

### 0.2.26 — 2026-10-17

- Changed simultaneous chord fingering from a vertical stack to a horizontal, pitch-ordered row. Right-hand chord fingers remain above the treble staff, left-hand fingers remain below the bass staff, and edge chords shift inward to keep every number visible.

### 0.2.25 — 2026-10-17

- Kept stacked left-hand fingering in a dedicated lane below the bass staff. Dyads and chords no longer lose their upper finger number among the staff lines, and the complete stack stays inside the score canvas.
- Added a rendered A-major regression check with sustained left-hand dyads so both `1` and `5` remain visibly separated at the same compact score scale used by the daily plan.

### 0.2.24 — 2026-10-17

- Removed the daily chord drill's hard-coded `5` on every left-hand bass note. All three chord-drill variations now enter the same PianoPlayer-based phrase optimizer used by Wait, Listen, Play Along, Free Play, imports, and the synchronized keyboard hints.
- Removed the separate fingering shortcut from the Chord Practice dialog as well. Generated exercises now share one inference path in every mode, and phrase detection uses the silence after a key release so sustained, spaced bass notes are analyzed as one physical hand movement. Explicit fingering authored in MusicXML, saved projects, or a curated lesson remains authoritative.

### 0.2.23 — 2026-10-16

- Replaced the experimental chain of fingering corrections with one score-wide physical optimizer used by Wait, Listen, Play Along, Free Play, lessons, imports, and synchronized keyboard hints. The optimizer adapts PianoPlayer's MIT-licensed medium-hand geometry, movement-time cost, finger-strength weighting, black-key reach, and crossing constraints, while retaining conventional scale/chord shapes as a uniform musical prior and preserving authored finger numbers.
- Made the score's measure window responsive. It now shows as many complete measures as the available panel width can hold legibly (two through five) without resizing or distorting either clef.
- Added a remembered 61-, 76-, or 88-key physical range for the virtual keyboard. Key width and octave positions remain identical to the full 88-key layout; keys outside the selected physical range are grey while idle but remain clickable and light normally. A future 88-key instrument can enable the complete A0-C8 physical range with one selection.
- Matched the physical-range selector to the light piano-sound control, including a readable dropdown menu under KDE color schemes and large system fonts.
- Added PianoPlayer attribution and its MIT license to `THIRD_PARTY_NOTICES.md`.

### 0.2.22 — 2026-10-16

- Rebuilt automatic left-hand fingering around a physical hand position instead of isolated note-pair labels. Rising bass shapes now move from pinky toward thumb, descending shapes move from thumb toward pinky, and octave drops correctly reset onto the pinky.
- Added mirrored biomechanics repair passes for both hands at imported MIDI boundaries. Nearby notes can no longer jump directly between thumb and pinky, repeated keys keep a stable finger, and medium leaps cannot reuse or reverse fingers unless the hand genuinely relocates. The right hand rises from thumb toward pinky while the left hand rises from pinky toward thumb.
- Corrected dense MIDI chord analysis by sorting unique pitches low-to-high before assigning fingers and by giving duplicate copies of the same MIDI key one shared finger.
- Chord fingering now recognizes major, minor, diminished, and augmented triads plus common seventh chords and their inversions. Incomplete two-note shapes use the neighboring chord as harmonic context and preserve shared tones on the same finger; for example, right-hand C-G followed by E-G is fingered `1-5` then `3-5`.
- Fingering is evaluated as a phrase-level movable hand position: five neighboring scale notes fill one five-finger frame, successive thirds progress through `1-3`, `2-4`, `3-5` (mirrored in the left hand), and longer scales cross the thumb at a natural turning finger. Rhythmic gaps, completed note releases, and genuine register leaps establish new hand positions.
- Added a batch audit for the MIDI/MusicXML/project files under `Desktop/Music Lessons`, including nearby extreme swaps, reversed movements, unstable repeated notes, and chord-order or duplicate-key errors. A malformed `.mxl` file with a missing ZIP directory is reported separately as an import error.
- Added regression coverage at both ends of the 88-key range: left-hand A0-C1-E1 ascending, descending, as a chord, and both directions of an A0-A1 octave; plus right-hand C7-E7-G7 in both directions.

### 0.2.21 — 2026-10-16

- Fixed Wait mode becoming stuck on a correctly played highlighted note because an older, overlapping imported-MIDI note was still being required invisibly. Each orange attack now checks only its own visible note or chord for its written length.
- Releasing a highlighted key before its written length now tells the player which note was released early and that pressing it again will continue, instead of leaving the orange line apparently frozen without an explanation.
- Consecutive repeated notes still require a real release and fresh key press.

### 0.2.20 — 2026-10-16

- Corrected automatic fingering for every imported MIDI, MusicXML/PDF result, and Free Play score. The optimizer now uses the real distance between notes even when a fifth is embedded in a longer passage or might otherwise be mistaken for a hand crossing; for example, a left-hand C-to-G fifth is now `5-1`, and C-E-G is `5-3-1`.
- Added regression tests for left- and right-hand fifths and a broken-chord outline so this cannot regress to the awkward `5-4` C-to-G mapping.

### 0.2.19 — 2026-10-16

- Shortened the orange score playhead to the grand-staff band instead of drawing it through the entire panel. It no longer crosses the upper or lower fingering guides, keeping the initial finger number readable.

### 0.2.18 — 2026-10-16

- Corrected Morning Light's authored left-hand fingering. Its C3 notes now use the thumb (`1`) instead of the little finger (`5`), and the C3-A2-G2 position follows the conventional `1-3-5` shape.
- Added regression coverage for the complete Morning Light bass pitches and fingering sequence.

### 0.2.17 — 2026-10-16

- Made the Grand Staff viewport completely static. It permanently uses the same A0-to-C8 vertical bounds, 212-pixel panel height, and notation scale regardless of the notes, measure, hand filter, or playback position.
- High and low passages now use octave notation inside that fixed viewport without triggering zoom or changing the panel size: what is visible at the beginning is exactly the scale retained everywhere.

### 0.2.15 — 2026-10-16

- Added just seven pixels of height back to the tightly cropped Grand Staff panel so the upper right-hand fingering coordinates remain fully visible. The notation and clef scale are unchanged.

### 0.2.14 — 2026-10-16

- Removed the last oversized blank bands inside the Grand Staff canvas. The A0/C8 range test now leaves an approximately five-pixel frame around its `15ma` and `8vb` markings.
- The change reduces the panel and internal viewport heights only; it does not raise the maximum notation or clef scale.

### 0.2.13 — 2026-10-16

- Tightened the white Grand Staff canvas itself around its highest and lowest visible notation, removing the remaining broad blank band inside the score border.
- Reduced the panel height without enlarging the notation: the treble clef, bass clef, staff, notes, and annotations retain the same capped rendering scale, including an A0-to-C8 extreme-range score.

### 0.2.12 — 2026-10-16

- Shrunk the expanded Grand Staff panel around the notation's useful vertical band instead of leaving a large empty area above and below it. The viewport retains enough room for A0, C8, fingering, octave brackets, and pedal marks.
- The score may crop unused internal canvas on wide windows, but its uniform scale is capped at the established size: treble and bass clefs are neither enlarged nor distorted to fill the smaller panel. Staff clicking, seeking, and edit-cursor placement use the same cropped coordinate mapping.

### 0.2.11 — 2026-10-16

- **Note names** and **Finger numbers** are now both restored as active learning aids when VArpeggio reopens, not merely saved as checkbox values. Their states are persisted immediately when toggled and confirmed again during a clean application close.

### 0.2.10 — 2026-10-16

- Finishing the Free Play staff editor with **Apply changes** now exits insertion mode and removes the orange `ADD` line. The line can also be dismissed with **Done editing — hide ADD line** in the staff's right-click menu or with Escape; canceling the editor still preserves the previous insertion position.

### 0.2.9 — 2026-10-16

- Finger-number help now extends to the full 88-key virtual keyboard in Wait, Listen, Play Along, and Free Play. Each current or upcoming key displays its synchronized 1–5 finger badge; chords show one badge per key, active keys change badge color, and playback, score preview, seeking, and practice progression all advance the hints with the score.
- Moved right-hand score fingering into a dedicated lane above the measure headings so finger numbers on the first beat of M1, M2, and later measures remain fully visible instead of colliding with bar labels.
- Added rendered keyboard and dense-score screenshot checks alongside exact B-flat-major pitch and fingering regression coverage.

### 0.2.8 — 2026-10-16

- Added an optional, remembered **Finger numbers** learning aid across Wait, Listen, Play Along, and Free Play. Built-in melodies and chord exercises include authored 1–5 fingering, daily scales use key-specific conventional crossings, and imported or newly played notes receive practical suggestions when the source contains none. Treble and bass each use one consistent horizontal fingering guide outside their visible notation; only simultaneous chord fingers form a vertical stack at the chord's fixed X position. Learning-aid switches occupy a dedicated responsive header row so large KDE fonts do not hide them.
- Automatic fingering now evaluates each melodic phrase as a whole, uses neighboring fingers for neighboring notes, and reserves thumb crossings for ergonomic hand-position changes instead of producing adjacent `1 → 5` jumps.
- Large register leaps now start a new fingering phrase and reset to its natural hand position. For example, a new ascending left-hand B–C–D group uses `5–4–3` instead of carrying an unnecessary `1–3–2` thumb crossing across the leap.
- Short monotonic phrases of up to five notes remain in one natural five-finger position, preventing needless end-of-phrase crossings such as a descending left-hand `1–2–3–1` instead of `1–2–3–4`.
- Added dedicated fingering regression coverage for both hands, short positions, octave-scale crossings, large-leap resets, repeated notes, chords, black-key patterns, authored overrides, and adjacent-finger safety.
- Extreme 88-key pitches now use conventional grouped `8va`/`8vb` or `15ma`/`15mb` octave brackets, keeping the sounding pitch unchanged while avoiding tall ladders of ledger lines and off-frame noteheads.
- Fixed overlapping note-name helpers on close chord tones by arranging each chord's labels into a readable, pitch-ordered column.
- Reflowed the Daily Practice Plan around KDE's actual system font so its date navigation, instructions, lesson rows, and action buttons remain fully readable at large font sizes.
- VArpeggio project files now preserve lesson fingering metadata when saved and reopened.

### 0.2.7 — 2026-10-15

- Increased the internal sampled-piano sink safety buffer from 64 KiB to 96 KiB to prevent occasional PC-speaker underruns during desktop scheduling stalls.
- Audio recovery now restarts from silence and fades the still-active piano voice back in, avoiding a second crack after an underrun.

### 0.2.6 — 2026-10-15

- Added a remembered icon-only/full-text switch for the left sidebar. The compact rail temporarily expands on hover, minimizes again when the pointer leaves, and uses a pushpin to stay open.
- Added a comprehensive Chord Practice section covering all 12 roots, perfect fifth/power chords, common triads, suspended chords, sixths, sevenths, ninths, elevenths, thirteenths, inversions, separate or combined hands, and block or broken patterns.
- Removed video transcription from the normal sidebar and placed it only under the top-level **Experimental → Video Notes** menu.
- The volume slider now restores its last value when VArpeggio opens again.
- Listen mode now returns the score and orange play line to the beginning when a song finishes, then remains stopped until Play is pressed again.
- Bubbles and Live Coach retain their separate side-by-side cards inside one collapsible middle section, with its expand/collapse control at the far-right edge of the section header.
- Chord Practice now uses an explicit high-contrast light palette for its fields, dropdown lists, tempo control, and complete unclipped action buttons, independent of the active KDE color scheme.
- VArpeggio now restores its previous window size, position, and maximized state when reopened.
- The **Use keyboard MIDI sound** selection is retained across exits and reconnects automatically when the saved keyboard adapter becomes available again.

### 0.2.5 — 2026-10-15

- Added remembered accordion controls for the complete Grand Staff box, the combined Bubbles and Live Coach section, and the complete Keyboard box. Each section independently reopens in its last expanded or collapsed state.

### 0.2.4 — 2026-10-15

- Free Play now records sustain-pedal down and release timing separately from key length and draws `Ped.` and `*` release marks below the grand staff.
- VArpeggio project, MIDI, MusicXML, and engraved PDF saves now preserve recorded pedal events; reopening an editable project restores the pedal notation.

### 0.2.3 — 2026-10-14

- Free Play now remains cached for the entire VArpeggio session. Switching to Wait, Listen, or Play Along and returning restores its notes, rests, meter and key changes, repeat marks, grouping state, and insertion position.
- The Free Play staff is cleared only by the explicit **Clear staff** action, not by changing modes.

### 0.2.2 — 2026-10-14

- Fixed Free Play score preview so the grand staff follows the orange play line and advances measure-by-measure during playback.
- Preview now starts from a manually positioned orange line instead of always jumping back to the beginning.
- While Free Play preview is active or paused, the orange line can be dragged to another section and resumed from that point; stopping restores normal point-and-click notation editing.

### 0.2.1 — 2026-10-13

- Fixed physical MIDI keyboards leaving an organ/reed voice humming after Stop. Hardware shutdown now reliably releases pedals and every key, sends all-sound-off and controller-reset messages, and drains the adapter before closing it.

### 0.2.0 — 2026-10-13

- Made Wait mode duration-aware: the score and falling notes advance only while the complete written note or chord is held.
- Made consecutive repeated pitches require release and a new attack, while genuine ties remain continuous held notes.
- Kept Play Along tempo-driven without allowing a quick key touch to skip the expected group, and reset its scoring for each written attack.
- Added measure-history review to Wait, Listen, and Play Along. Drag the Earlier–Latest bar or use the mouse wheel over the grand staff; playback continues until the user explicitly pauses it.
- Added looping practice passes, pause/resume, draggable seeking, system-font-aware interface sizing, a scrollable sidebar, and a larger readable Live Coach panel.
- Improved grand-staff paging, 88-key ledger-note visibility, key signatures, ties, falling-note timing, MIDI hardware playback, and recorded-piano audio stability.

### 0.1.0

- Initial VArpeggio development release.

## Build

Required development and runtime components include Qt 6 Core, Widgets, Multimedia, XML, SVG, PDF/PDF Widgets, Concurrent, and Network; ALSA; OpenCV 4; zlib; and libsndfile. Python 3, FFmpeg, Poppler's `pdfdetach`, `rsvg-convert`, `pdfunite`, and `pdfattach` support media and engraved-score workflows. `bsdtar` or Flatpak is used for automatic Audiveris setup.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/varpeggio
```

## System installation and packaging

For a normal system install:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j
sudo cmake --install build
```

For a staged `spkgbuild` or another package builder:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j
DESTDIR="$PKG" cmake --install build
```

The install layout is:

- `/usr/bin/varpeggio`
- `/usr/share/applications/com.voncloft.VArpeggio.desktop`
- `/usr/share/icons/hicolor/scalable/apps/com.voncloft.VArpeggio.svg`
- `/usr/libexec/varpeggio/` for the private engraving helper and Verovio runtime
- `/usr/share/varpeggio/` for recorded piano data
- `/usr/share/doc/varpeggio/` for license notices

Audiveris is not installed into `/usr` by this package. Each user receives a private runtime on the first ordinary-PDF import when one is needed.

## Keyboard controls

- `A W S E D F T G Y H U J K`: notes from C4 through C5
- `Space`: Listen/Stop, or replay the hidden ear-training sound
- `R`: start or stop optional Free Play recording
- `Esc`: stop playback

## License

Copyright © 2026 Voncloft.

VArpeggio's original source code is released under **GNU GPL-3.0-only**. See [LICENSE](LICENSE).

Bundled data and third-party components retain their own licenses. In particular, the recorded piano is CC0, Verovio is LGPL-3.0-only, Verovio's embedded fonts are OFL-1.1, and the separately downloaded and launched Audiveris engine is AGPL-3.0. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and the [LICENSES](LICENSES/) directory for the complete component inventory, license texts, and corresponding-source information.

The VArpeggio and Voncloft names and artwork are not automatically licensed as trademarks by the GPL.
