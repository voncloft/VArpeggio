#pragma once

#include <QAudio>
#include <QByteArray>
#include <memory>
#include <optional>

class SynthStream;

// Optional Linux output path. The public interface deliberately contains no
// PulseAudio implementation types, and the source must outlive this object.
// Controls and queries are serialized by Synthesizer's owning output worker;
// native callbacks run independently under the PulseAudio mainloop lock.
class PulseAudioOutput final {
public:
    PulseAudioOutput(SynthStream &source, int sampleRate,
                     qsizetype requestedBufferBytes,
                     QByteArray deviceId = {});
    ~PulseAudioOutput();
    PulseAudioOutput(const PulseAudioOutput &) = delete;
    PulseAudioOutput &operator=(const PulseAudioOutput &) = delete;

    bool available() const;
    bool start();
    void reset();
    qsizetype bufferSize() const;
    std::optional<qint64> processedFrames() const;
    bool failed() const;
    QAudio::State state() const;
    QAudio::Error error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
