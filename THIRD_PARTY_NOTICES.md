# Keyflow open-source component notice

Keyflow-specific source code remains separate from the following open-source components. No closed-source piano library or proprietary optical-recognition engine is intentionally bundled.

## Bundled data

- **FreePats Upright Piano KW** — recordings of a Kawai upright piano by the FreePats project, dedicated to the public domain under **CC0 1.0**. The complete license, README, SFZ mapping, and unmodified FLAC recordings are under `soundfonts/UprightPianoKW/`. Source: <https://github.com/freepats/upright-piano-KW>

## Bundled open-source library

- **Verovio 6.3.0** — music-notation engraving library and Python binding, **LGPL-3.0-only**. The bundled wheel metadata and complete GPL/LGPL license texts are under `tools/python/verovio-6.3.0.dist-info/`. Source: <https://github.com/rism-digital/verovio>

## Optional separately launched runtime

- **Audiveris** — optical music recognition, **GNU AGPL 3.0**. Keyflow launches the unmodified Audiveris executable as a separate process and reads its exported MusicXML. A portable Keyflow distribution that includes Audiveris must also include Audiveris's copyright/license notice and a corresponding-source location for that exact build. Source: <https://github.com/Audiveris/audiveris>

## Linked frameworks and system libraries

- **Qt 6** — LGPL/GPL/commercial multi-license; Keyflow dynamically links the system Qt libraries. Source: <https://code.qt.io/>
- **OpenCV** — Apache License 2.0. Source: <https://github.com/opencv/opencv>
- **libsndfile** — LGPL 2.1. Source: <https://github.com/libsndfile/libsndfile>
- **ALSA library** — LGPL 2.1 or later. Source: <https://github.com/alsa-project/alsa-lib>
- **zlib** — zlib license. Source: <https://github.com/madler/zlib>

## Separately executed system tools

- **FFmpeg** — LGPL/GPL depending on the particular system build. Keyflow invokes the user's installed executable for local media conversion. Source: <https://ffmpeg.org/>
- **Poppler `pdfdetach`** — GPL. Keyflow invokes the user's installed executable to inspect PDF attachments. Source: <https://poppler.freedesktop.org/>

The Apache-2.0 LumaChords project informed the general falling-note validation approach; its source code and assets are not included in Keyflow. Source: <https://github.com/adalkiran/lumachords>

This inventory is intended to make redistribution review straightforward; it is not legal advice.
