# VArpeggio open-source component notice

VArpeggio-specific source code remains separate from the following open-source components. No closed-source piano library or proprietary optical-recognition engine is intentionally bundled.

## Bundled data

- **FreePats Upright Piano KW** — recordings of a Kawai upright piano by the FreePats project, dedicated to the public domain under **CC0 1.0**. The complete license, README, SFZ mapping, and unmodified FLAC recordings are under `soundfonts/UprightPianoKW/`. Source: <https://github.com/freepats/upright-piano-KW>

## Bundled open-source library

- **Verovio 6.3.0** — music-notation engraving library and Python binding, **LGPL-3.0-only**. The bundled wheel metadata and complete GPL/LGPL license texts are under `tools/python/verovio-6.3.0.dist-info/`. Source: <https://github.com/rism-digital/verovio>

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
