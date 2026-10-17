# VArpeggio open-source component notice

VArpeggio-specific source code remains separate from the following open-source components. No closed-source piano library or proprietary optical-recognition engine is intentionally bundled.

## Bundled data

- **FreePats Upright Piano KW** — recordings of a Kawai upright piano by the FreePats project, dedicated to the public domain under **CC0 1.0**. The complete license, README, SFZ mapping, and unmodified FLAC recordings are under `soundfonts/UprightPianoKW/`. Source: <https://github.com/freepats/upright-piano-KW>

## Bundled open-source library

- **Verovio 6.3.0** — music-notation engraving library and Python binding, **LGPL-3.0-only**. The bundled wheel metadata and complete GPL/LGPL license texts are under `tools/python/verovio-6.3.0.dist-info/`. The exact source archive, replacement instructions, and corresponding-source offer are in `LICENSES/VEROVIO-SOURCE.md`. Source: <https://github.com/rism-digital/verovio/releases/tag/version-6.3.0>

- **Verovio engraving fonts** — the bundled wheel contains unmodified Bravura, Gootville, Leipzig, Leland, Petaluma, and Liberation font data under the **SIL Open Font License 1.1**. Copyright, designer, source, and Reserved Font Name notices are in `LICENSES/VEROVIO-FONTS.md`; the complete license is in `LICENSES/OFL-1.1.txt`.

## Adapted open-source algorithm

- **PianoPlayer** — physical piano-fingering search by Marco Musy, **MIT License**. VArpeggio's native C++ fingering optimizer adapts its hand geometry, finger weights, black-key bias, movement-effort cost, and crossing constraints; no Python runtime is required. Source: <https://github.com/marcomusy/pianoplayer>

MIT License

Copyright (c) 2017 Marco Musy

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Linked frameworks and system libraries

- **Qt 6** — LGPL/GPL/commercial multi-license; VArpeggio dynamically links the system Qt libraries. Source: <https://code.qt.io/>
- **OpenCV** — Apache License 2.0. Source: <https://github.com/opencv/opencv>
- **libsndfile** — LGPL 2.1. Source: <https://github.com/libsndfile/libsndfile>
- **ALSA library** — LGPL 2.1 or later. Source: <https://github.com/alsa-project/alsa-lib>
- **zlib** — zlib license. Source: <https://github.com/madler/zlib>

## Separately executed system tools

- **FFmpeg** — LGPL/GPL depending on the particular system build. VArpeggio invokes the user's installed executable for local media conversion. Source: <https://ffmpeg.org/>
- **Poppler `pdfdetach`** — GPL. VArpeggio invokes the user's installed executable to inspect PDF attachments. Source: <https://poppler.freedesktop.org/>
- **Audiveris** — optional optical music recognition, **GNU AGPL 3.0**. VArpeggio can install the official Flathub package for the current user or download and unpack the official Linux release from GitHub into its per-user data folder. It launches the unmodified executable as a separate process and imports its MusicXML output. It is not required because VArpeggio retains a native fallback reader. Source and license: <https://github.com/Audiveris/audiveris>

The Apache-2.0 LumaChords project informed the general falling-note validation approach; its source code and assets are not included in VArpeggio. Source: <https://github.com/adalkiran/lumachords>

This inventory is intended to make redistribution review straightforward; it is not legal advice.
