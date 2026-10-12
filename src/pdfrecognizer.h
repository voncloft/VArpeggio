#pragma once

#include "musicdocument.h"

#include <QString>
#include <QVector>
#include <functional>

struct PdfRecognitionResult {
    QVector<NoteEvent> notes;
    QVector<TimeSignatureEvent> timeSignatures{{0.0,4,4}};
    QVector<KeySignatureEvent> keySignatures{{0.0,0}};
    QString error;
    int tempo=96;
    int pages=0;
    int staffSystems=0;
    bool canceled=false;
    QString engine="VArpeggio native reader";
};

// Native optical recognition for clean, conventionally engraved piano PDFs.
// PDF rendering is provided by Qt and all image analysis runs in-process.
class PdfRecognizer {
public:
    // Reads a printed metronome marking such as "quarter note = 101" from
    // the PDF text layer without running optical note recognition.
    static int detectPrintedTempo(const QString &path,int fallbackTempo);
    static PdfRecognitionResult recognize(const QString &path,int fallbackTempo,
                                           std::function<void(int)> progress={},
                                           std::function<bool()> canceled={});
};
