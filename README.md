# VArpeggio

VArpeggio is a native Qt 6 piano practice, performance, recording, and notation application for KDE/Linux. It was created by Voncloft for learning with a real MIDI keyboard, playing or editing a grand staff, and turning supported score and media formats into practice material.

## Main features

- **Wait mode:** playback advances only after the correct key or complete chord is played.
- **Listen:** plays the generated notes represented on the live staff without requiring keyboard input.
- **Play Along:** follows the score clock while showing the current notes, keyboard keys, and falling-note display.
- **Free Play:** immediately displays played notes on the grand staff, full 88-key keyboard, and optional falling-note view. Recording is optional.
- **Daily practice:** dated routines for scales, chords, separate hands, hands together, repertoire, and ear training, with previous/next navigation.
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
- The orange play line can be dragged to seek in Wait, Listen, and Play Along modes.
- Playback supports pause, resume, and stop.
- Live follow advances by measures and keeps the current measure readable instead of shrinking the entire piece onto one line.
- Long notes remain visible as ties and held-note indicators for their complete sounding duration.
- Right, Left, and Together filters support separate-hand practice.

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
