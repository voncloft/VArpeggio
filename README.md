# Keyflow MIDI Studio

Keyflow is a native Qt 6 desktop application for KDE. It connects directly to ALSA MIDI keyboards and provides:

- strict chord-aware Wait mode, separate Listen playback, tempo-following Play Along, and Free Play with optional recording;
- right hand, left hand, and hands-together practice;
- grand-staff notation with optional note names, a virtual piano, and falling-note bubbles;
- one-flow daily scale, chord, ear-training, and repertoire routines;
- MIDI, MusicXML, ABC, Keyflow project, and PDF import;
- MIDI, MusicXML, ABC, WAV, MP4 falling-note visualization, SVG, PNG, PDF, JSON/Keyflow, and CSV export;
- offline polyphonic piano transcription from local audio or a video's sound;
- detailed visual transcription of falling-note/bubble videos with full, cropped, 53-, 61-, 88-, or other visible keyboard ranges, including chords, changing highlight colors, both staves, binary and triplet timing, MusicXML, and engraved playable PDF output.

**Wait mode does not run a clock or advance by itself.** It advances only after the correct key or complete chord is played. **Listen** is a separate mode that plays without requiring keyboard input. After audio or video transcription, Listen defaults to the generated piano notes represented in Live follow. **Hear original recording (reference only)**, directly under the song title, switches to the untouched source only when explicitly selected; it stays off by default because source audio may contain notes the draft did not capture. The volume slider has a fixed position and controls either playback source without waiting for the GUI playback timer. Score playback and live keys use direct playback of the bundled CC0 FreePats Kawai upright recordings, with natural, bright, and soft velocity-layer choices. No SoundFont synthesizer is used. WAV export uses the same recorded-piano path selected in the GUI.

The BPM value is directly editable from 30–260 as well as adjustable with −/+. Changing it during Listen or Play Along keeps the same musical position and retimes the generated piano, moving live staff, falling notes, keyboard highlights, and metronome as one clock. When **Hear original recording** is selected, the reference playback rate changes too. The Note Flow and Live Coach cards have fixed geometry so note or chord changes cannot resize that row. **Tutorial grand** is the default bright, wide piano profile for imported falling-note songs. Saving **Falling-note video (.mp4)** renders the generated staff notes and that selected piano into a full 88-key tutorial animation; it never hides the original recording inside the export.

In **Free Play**, every played key appears immediately on the grand staff, falling-note view, and full keyboard. A held note grows and changes from a short value through quarter, half, whole, and tied sustain notation while the key is still down. Click the staff to place the orange insertion line, then play a key or chord to insert it there; the line advances by the longest held note. Right-click a note for Delete or Automatic/♮/♯/♭ notation. Right-click empty staff to insert treble or bass rests, change the time signature, change the key signature (including F major / one flat for B♭), or add/remove a repeat end `:||`. The live staff keeps writing space ahead of the newest note, and its Earlier–Latest scrollbar reviews older notes. **Preview score** plays the actual notes on the editable staff. Recording remains optional, **Clear staff** removes the score after confirmation, and top-right Save exports the current Free Play notation; MusicXML, PDF, and Keyflow project exports retain its meter, key, repeat, rest extent, and accidental data. The daily plan stays open above the score with Previous and Next controls. Its ear-training step plays a hidden note, and every third round plays a hidden major or minor chord; use **Hear again** as often as needed, then find the sound on the MIDI, computer, or onscreen keyboard. A correct answer is revealed on the grand staff.

For a tutorial, choose **Video → notes** and leave **Automatic (recommended)** selected. No BPM guess is required. Keyflow analyzes the complete recording, estimates tempo from the extracted soundtrack, measures the white-key spacing and black-key pattern of the keyboard actually shown, finds the keyboard's real bottom even when the video is letterboxed, and maps the visible range to piano pitches. Its primary visual pass now detects complete falling rectangles across the note-rain area and gives each rectangle a persistent identity before it reaches the keyboard. The lower edge crossing a clean play line becomes note-on and the upper edge becomes note-off, so bar height supplies the full held duration and two vertically separated bars on the same key remain two attacks. New tracks are admitted only high above the decorative strike effects, and brief occlusions are carried forward using the measured fall speed. Two-colour bars are grouped by hue for left/right staff assignment. Key-surface and multi-row scans remain fallback candidates. Every viable result is rendered back from the generated notes and checked against the original soundtrack for octave-resolved pitch and timing; audio validates the visual extraction rather than replacing it. False notes over a silent outro are removed and playback dynamics are derived from the source before the best credible draft is kept. If the picture is hidden or too sparse to trust, Automatic mode analyzes the soundtrack instead. You can also force **Highlighted keyboard only** or **Soundtrack only**.

The full-rectangle tracking design was informed by the Apache-2.0-licensed [LumaChords](https://github.com/adalkiran/lumachords) project. Keyflow keeps its own C++/Qt implementation, keyboard calibration, source-audio validation, dynamics, notation, and KDE interface.

This is a verification loop, not a guarantee of exact transcription: it can reject a worse visual draft and retain a better one, but it cannot prove every note is correct. Soundtrack-only conversion remains experimental. Clean solo piano works best; vocals, drums, effects, and dense backing tracks can add or hide detected pitches. The current offline audio path extracts the complete soundtrack and analyzes piano-like frequencies; it does not isolate a piano stem or run a neural source-separation model. Longer recordings take proportionally longer because Keyflow no longer stops after five minutes.

Playable PDFs and generated transcriptions provide two score views: **Exact PDF** keeps the page-perfect engraving, while **Live follow** shows a readable practice window with a moving play line. Notes that begin together stay grouped as chords with one stem, short-note runs are beamed together, and long notes show curved ties through their full sounding span. Coral dots at the play line mark notes that are still being held. The live grand staff includes the full MIDI 21–108 piano range so extreme notes cannot sound off-screen. Live follow advances only after the complete correct note or chord in Wait mode, and follows the clock in Listen and Play Along. Playback updates the keyboard and coach only when the active note group changes, so their layout stays still while the score moves.

PDFs exported by Keyflow contain the note data as an attachment. Reopening one in Keyflow restores playback and practice. When a plain engraved PDF has no attached or same-name MIDI/MusicXML score, Keyflow now invokes Audiveris optical music recognition, reads its compressed MusicXML result internally, and keeps the original pages under **Exact PDF** while the recognition draft drives Listen, Wait mode, bubbles, and Live follow. Optical recognition is still a draft: scans, unusual engraving, dense voices, accidentals, and complicated rhythms may need manual correction.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/keyflow
```

Runtime requirements are Qt 6 Widgets, Multimedia, XML, SVG, PDF/PDF Widgets, ALSA, OpenCV, zlib, Poppler's `pdfdetach`, FFmpeg for media transcription, and Audiveris for optical recognition of plain PDFs. The current project build can use an Audiveris runtime placed beside the executable under `audiveris-runtime/`.

## Open-source components

Keyflow does not intentionally include closed-source or proprietary runtime assets. The bundled Kawai piano recordings are CC0 public domain. Audiveris is an unmodified, separately launched AGPL-3.0 program. Framework and system-tool licenses and source locations are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). GeneralUser GS was removed because its own license disclosed uncertainty about the origin of some contained samples.

## Keyboard controls

- `A W S E D F T G Y H U J K`: notes from C4 through C5
- `Space`: listen/stop, or replay the hidden ear-training sound
- `R`: start/stop optional Free Play recording
- `Esc`: stop all playback
