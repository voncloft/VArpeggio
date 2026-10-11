#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <functional>

enum class Hand { Left, Right };
enum class AccidentalMark { Automatic, Natural, Sharp, Flat };

struct NoteEvent {
    int note = 60;
    double start = 0.0;
    double duration = 1.0;
    int velocity = 92;
    Hand hand = Hand::Right;
    AccidentalMark accidental = AccidentalMark::Automatic;
};

struct TimeSignatureEvent {
    double beat = 0.0;
    int beats = 4;
    int beatType = 4;
};

struct KeySignatureEvent {
    double beat = 0.0;
    int fifths = 0;
};

struct StaffRestEvent {
    double beat = 0.0;
    double duration = 1.0;
    Hand hand = Hand::Right;
};

struct TranscriptionInfo {
    int tempo = 0;
    double tempoConfidence = 0.0;
    double audiovisualAgreement = -1.0;
    double renderedAgreement = -1.0;
    double audioOffsetSeconds = 0.0;
    double sourceStartSeconds = 0.0;
    int refinementPasses = 0;
    bool tempoEstimated = false;
    // Geometry retained from visual falling-note recognition.  The UI uses it
    // to draw the accepted note bars directly over the source video so a user
    // can see a shortened, missed, or wrongly mapped bar while it happens.
    int videoWidth = 0;
    int videoHeight = 0;
    int keyboardTop = 0;
    int keyboardBottom = 0;
    int visibleLow = 21;
    int visibleHigh = 108;
    double whiteKeyWidth = 0.0;
    double fallPixelsPerSecond = 0.0;
    QVector<double> laneCenters;
    QVector<double> laneBarWidths;
    double directorStartSeconds = 0.0;
    QVector<NoteEvent> directorNotes;
    QVector<double> directorBarCenters;
    QVector<double> directorBarWidths;
};

class MusicDocument {
public:
    static QString noteName(int midi, bool withOctave = true);
    static QVector<NoteEvent> morningLight();
    static QVector<NoteEvent> odeToJoy();
    static QVector<NoteEvent> scaleRoutine();
    static QVector<NoteEvent> chordRoutine(int variation = 0);

    static bool importFile(const QString &path, QVector<NoteEvent> &notes,
                           QString &title, int &tempo, QString *error,
                           QVector<TimeSignatureEvent> *timeSignatures = nullptr,
                           QVector<double> *repeatEnds = nullptr,
                           QVector<KeySignatureEvent> *keySignatures = nullptr,
                           QVector<StaffRestEvent> *rests = nullptr);
    static bool saveMidi(const QString &path, const QVector<NoteEvent> &notes, int tempo, QString *error);
    static bool saveMusicXml(const QString &path, const QVector<NoteEvent> &notes, int tempo,
                             const QString &title, QString *error);
    static bool saveEngravedScore(const QString &musicXmlPath, const QString &pdfPath,
                                  const QVector<NoteEvent> &notes, int tempo,
                                  const QString &title, QString *error,
                                  const QVector<TimeSignatureEvent> &timeSignatures = {},
                                  const QVector<double> &repeatEnds = {},
                                  const QVector<KeySignatureEvent> &keySignatures = {},
                                  const QVector<StaffRestEvent> &rests = {});
    static bool saveAbc(const QString &path, const QVector<NoteEvent> &notes, int tempo,
                        const QString &title, QString *error);
    static bool saveJson(const QString &path, const QVector<NoteEvent> &notes, int tempo,
                         const QString &title, QString *error,
                         const QVector<TimeSignatureEvent> &timeSignatures = {},
                         const QVector<double> &repeatEnds = {},
                         const QVector<KeySignatureEvent> &keySignatures = {},
                         const QVector<StaffRestEvent> &rests = {});
    static bool saveCsv(const QString &path, const QVector<NoteEvent> &notes, QString *error);
    static bool saveWav(const QString &path, const QVector<NoteEvent> &notes, int tempo, QString *error);

    static bool detectTempo(const QString &path, int &tempo, double *confidence,
                            QString *error, std::function<void(int)> progress = {},
                            std::function<bool()> canceled = {});

    static bool transcribeMedia(const QString &path, int tempo, QVector<NoteEvent> &notes,
                                QString *error, std::function<void(int)> progress = {},
                                std::function<bool()> canceled = {},
                                TranscriptionInfo *info = nullptr);
    static bool transcribeFallingNotesVideo(const QString &path, int tempo, QVector<NoteEvent> &notes,
                                            QString *error, std::function<void(int)> progress = {},
                                            std::function<bool()> canceled = {},
                                            TranscriptionInfo *info = nullptr);

private:
    static bool importMidi(const QByteArray &data, QVector<NoteEvent> &notes, int &tempo, QString *error);
    static bool importMusicXml(const QByteArray &data, QVector<NoteEvent> &notes, int &tempo,
                               QString &title, QString *error);
    static bool importAbc(const QByteArray &data, QVector<NoteEvent> &notes, int &tempo, QString *error);
    static bool importJson(const QByteArray &data, QVector<NoteEvent> &notes, int &tempo,
                           QString &title, QString *error,
                           QVector<TimeSignatureEvent> *timeSignatures,
                           QVector<double> *repeatEnds,
                           QVector<KeySignatureEvent> *keySignatures,
                           QVector<StaffRestEvent> *rests);
};
