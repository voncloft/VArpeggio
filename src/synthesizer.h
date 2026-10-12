#pragma once

#include "musicdocument.h"

#include <QAudioSink>
#include <QIODevice>
#include <QHash>
#include <QMutex>
#include <QVector>
#include <array>
#include <atomic>
#include <memory>

enum class SynthInstrument {
    AcousticGrand = 0,
    BrightPiano,
    SoftPiano
};

struct RecordedPianoLibrary;

class SynthStream : public QIODevice {
    Q_OBJECT
public:
    explicit SynthStream(QObject *parent = nullptr);
    ~SynthStream() override;
    quint64 noteOn(int midi, int velocity = 96);
    void noteOff(int midi);
    void noteOffVoice(quint64 voiceId);
    void startSequence(const QVector<NoteEvent> &notes, int tempo, double startBeat = 0.0);
    void stopSequence();
    void click(bool accent);
    void allOff();
    void setMuted(bool muted);
    void setVolume(double volume);
    void setInstrument(SynthInstrument instrument);
    void setOutputSampleRate(int sampleRate);
    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *, qint64) override { return 0; }
    bool recordedPianoAvailable() const { return bool(m_recordedPiano); }

private:
    struct Voice {
        int note;
        quint64 id;
        double phase;
        double phase2;
        double phase3;
        double phase4;
        double phaseStep;
        double phase2Step;
        double phase3Step;
        double phase4Step;
        double level;
        double naturalGain;
        double brightness;
        double velocity;
        bool releasing;
        bool click;
        bool sequenced;
        double decay;
        SynthInstrument instrument;
    };
    struct ScheduledEvent {
        qint64 frame;
        bool noteOn;
        int note;
        int velocity;
        quint64 voiceId;
    };
    struct RecordedVoice {
        quint64 id;
        int note;
        int region;
        double position;
        double step;
        double gain;
        double attackGain;
        double releaseGain;
        bool releasing;
        bool sequenced;
    };
    QMutex m_mutex;
    QVector<Voice> m_voices;
    bool m_muted = false;
    SynthInstrument m_instrument = SynthInstrument::AcousticGrand;
    double m_muteGain = 1.0;
    double m_lastOutputLeft = 0.0;
    double m_lastOutputRight = 0.0;
    double m_polyphonyGain = .82;
    // The bundled piano samples are nearly mono.  A real sampled grand (and
    // the tutorial soundtracks VArpeggio is matching) has different early
    // reflections at the two microphones.  Keep a short, bounded ambience
    // line here; it adds piano width and continuity without a long hall tail.
    static constexpr int kPianoRoomFrames = 4096;
    std::array<float,kPianoRoomFrames> m_pianoRoom{};
    int m_pianoRoomIndex = 0;
    double m_toneLowLeft = 0.0;
    double m_toneLowRight = 0.0;
    double m_bassLowLeft = 0.0;
    double m_bassLowRight = 0.0;
    double m_airLowLeft = 0.0;
    double m_airLowRight = 0.0;
    // UI controls must never wait for the audio callback. In particular, a
    // dragged volume handle used to block behind a whole output buffer and
    // looked frozen during dense playback.
    // Match the initial 115% position shown by the GUI volume slider.  The
    // mix itself keeps headroom dynamically, so this remains clean.
    std::atomic<double> m_volumeTarget {1.15};
    double m_volumeGain = 1.15;
    int m_sampleRate = 48000;
    static constexpr int kWaveTableSize = 4096;
    std::array<float,kWaveTableSize+1> m_sineTable{};
    double oscillator(double phase) const;
    void shapePianoStereo(double &left,double &right);
    void startRecordedVoice(quint64 id,int note,int velocity,bool sequenced);
    void stopRecordedVoice(quint64 id);
    void stopRecordedVoices(bool sequencedOnly);
    quint64 m_nextVoiceId = 1;
    std::shared_ptr<const RecordedPianoLibrary> m_recordedPiano;
    QVector<RecordedVoice> m_recordedVoices;
    QVector<ScheduledEvent> m_sequence;
    qsizetype m_sequenceIndex = 0;
    qint64 m_sequenceFrame = 0;
    bool m_sequenceActive = false;
};

class Synthesizer : public QObject {
    Q_OBJECT
public:
    explicit Synthesizer(QObject *parent = nullptr);
    quint64 noteOn(int midi, int velocity = 96) { return m_stream.noteOn(midi, velocity); }
    void noteOff(int midi) { m_stream.noteOff(midi); }
    void noteOffVoice(quint64 voiceId) { m_stream.noteOffVoice(voiceId); }
    void startSequence(const QVector<NoteEvent> &notes, int tempo, double startBeat = 0.0) { m_stream.startSequence(notes, tempo, startBeat); }
    void stopSequence() { m_stream.stopSequence(); }
    void click(bool accent) { m_stream.click(accent); }
    void allOff() { m_stream.allOff(); }
    void setMuted(bool muted) { m_stream.setMuted(muted); }
    void setVolume(double volume) { m_stream.setVolume(volume); }
    void setInstrument(SynthInstrument instrument) { m_stream.setInstrument(instrument); }
    static bool saveWav(const QString &path,const QVector<NoteEvent> &notes,int tempo,QString *error = nullptr,
                        SynthInstrument instrument = SynthInstrument::AcousticGrand);
    bool available() const { return bool(m_sink); }

private:
    SynthStream m_stream;
    std::unique_ptr<QAudioSink> m_sink;
    bool m_recoveringAudio = false;
};
