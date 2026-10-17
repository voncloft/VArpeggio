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

VArpeggio's original source code is released under the **GNU General Public License, version 3**. See [LICENSE](LICENSE).

Bundled data and third-party components retain their own licenses. In particular, the recorded piano is CC0, Verovio is LGPL-3.0-only, and the separately downloaded and launched Audiveris engine is AGPL-3.0. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the complete component inventory and source links.

The VArpeggio and Voncloft names and artwork are not automatically licensed as trademarks by the GPL.
