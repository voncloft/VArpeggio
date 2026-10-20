#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <memory>

class QAudioDecoder;
class QTemporaryFile;

struct ReferenceAudioData;
using ReferenceAudioPtr = std::shared_ptr<const ReferenceAudioData>;

// Published as const shared ownership. The audio callback reads only samples;
// the owning UI retains another reference until playback has stopped.
struct ReferenceAudioData {
    int sampleRate = 44100;
    qint64 frameCount = 0;
    const float *samples = nullptr;

    ReferenceAudioData();
    ~ReferenceAudioData();
    ReferenceAudioData(const ReferenceAudioData &) = delete;
    ReferenceAudioData &operator=(const ReferenceAudioData &) = delete;

    // Small, allocation-owning fixture factory. Real recordings use the
    // disk-backed mapping and never collect the complete audio in a QVector.
    static ReferenceAudioPtr fromSamples(int sampleRate, QVector<float> interleavedStereo);
    QString cacheFilePath() const;

private:
    friend class ReferenceAudioLoader;
    std::unique_ptr<QTemporaryFile> m_file;
    uchar *m_mapping = nullptr;
    QVector<float> m_fixtureSamples;
};

Q_DECLARE_METATYPE(ReferenceAudioPtr)

struct ReferenceAudioDecodeStats {
    qint64 buffers = 0;
    qint64 decodedFrames = 0;
    qint64 insertedSilenceFrames = 0;
    qint64 overlappingFrames = 0;
    qint64 firstBufferStartUs = -1;
    qint64 maximumBufferBytes = 0;
};

class ReferenceAudioLoader : public QObject {
    Q_OBJECT
public:
    explicit ReferenceAudioLoader(QObject *parent = nullptr);
    ~ReferenceAudioLoader() override;

    // An explicit directory is useful for tests. Production defaults to the
    // app's cache directory, so long recordings are backed by normal storage.
    void load(const QString &path, int sampleRate, const QString &cacheDirectory = {});
    void cancel();
    bool isLoading() const { return m_decoder != nullptr; }
    ReferenceAudioDecodeStats stats() const { return m_stats; }

signals:
    void ready(ReferenceAudioPtr audio);
    void error(const QString &message);
    void progress(qint64 positionMs, qint64 durationMs);

private:
    bool appendAvailable();
    bool appendSilence(qint64 frames);
    bool writeAll(const char *data, qint64 bytes);
    void finish();
    void fail(const QString &message);

    QAudioDecoder *m_decoder = nullptr;
    std::unique_ptr<QTemporaryFile> m_file;
    int m_sampleRate = 44100;
    qint64 m_frames = 0;
    ReferenceAudioDecodeStats m_stats;
};
