#include "referenceaudio.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QAudioFormat>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QUrl>
#include <algorithm>
#include <array>
#include <limits>

ReferenceAudioData::ReferenceAudioData() = default;
ReferenceAudioData::~ReferenceAudioData() {
    if (m_mapping && m_file)
        m_file->unmap(m_mapping);
}

ReferenceAudioPtr ReferenceAudioData::fromSamples(int rate, QVector<float> values) {
    if (rate <= 0 || values.isEmpty() || values.size() % 2 != 0)
        return {};
    auto data = std::make_shared<ReferenceAudioData>();
    data->sampleRate = rate;
    data->frameCount = values.size() / 2;
    data->m_fixtureSamples = std::move(values);
    data->samples = data->m_fixtureSamples.constData();
    return data;
}

QString ReferenceAudioData::cacheFilePath() const {
    return m_file ? m_file->fileName() : QString();
}

ReferenceAudioLoader::ReferenceAudioLoader(QObject *parent) : QObject(parent) {
    qRegisterMetaType<ReferenceAudioPtr>();
}

ReferenceAudioLoader::~ReferenceAudioLoader() {
    cancel();
}

void ReferenceAudioLoader::cancel() {
    if (m_decoder) {
        QAudioDecoder *decoder = m_decoder;
        m_decoder = nullptr;
        decoder->disconnect(this);
        decoder->stop();
        decoder->deleteLater();
    }
    m_file.reset();
    m_frames = 0;
}

void ReferenceAudioLoader::load(const QString &path, int sampleRate, const QString &cacheDirectory) {
    cancel();
    m_stats = {};
    if (sampleRate < 8000 || sampleRate > 192000) {
        emit error(QStringLiteral("The audio output sample rate is unsupported."));
        return;
    }
    const QFileInfo source(path);
    if (!source.isFile() || !source.isReadable()) {
        emit error(QStringLiteral("The original recording cannot be opened."));
        return;
    }
    QString directory = cacheDirectory;
    if (directory.isEmpty())
        directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (directory.isEmpty() || !QDir().mkpath(directory)) {
        emit error(QStringLiteral("The temporary audio cache directory cannot be created."));
        return;
    }
    m_file = std::make_unique<QTemporaryFile>(QDir(directory).filePath(QStringLiteral("reference-audio-XXXXXX.f32")));
    m_file->setAutoRemove(true);
    if (!m_file->open()) {
        const QString detail = m_file->errorString();
        m_file.reset();
        emit error(QStringLiteral("The temporary audio cache cannot be opened: ") + detail);
        return;
    }
    m_sampleRate = sampleRate;
    auto *decoder = new QAudioDecoder(this);
    m_decoder = decoder;
    QAudioFormat output;
    output.setSampleRate(sampleRate);
    output.setChannelConfig(QAudioFormat::ChannelConfigStereo);
    output.setSampleFormat(QAudioFormat::Float);
    decoder->setAudioFormat(output);
    decoder->setSource(QUrl::fromLocalFile(source.absoluteFilePath()));
    connect(decoder, &QAudioDecoder::bufferReady, this, [this, decoder] {
        if (m_decoder == decoder)
            appendAvailable();
    });
    connect(decoder, &QAudioDecoder::finished, this, [this, decoder] {
        if (m_decoder == decoder)
            finish();
    });
    connect(decoder, qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this,
            [this, decoder](QAudioDecoder::Error code) {
        if (m_decoder == decoder && code != QAudioDecoder::NoError)
            fail(QStringLiteral("The original recording could not be decoded: ") + decoder->errorString());
    });
    connect(decoder, &QAudioDecoder::positionChanged, this, [this, decoder](qint64 position) {
        if (m_decoder == decoder)
            emit progress(position, decoder->duration());
    });
    decoder->start();
}

bool ReferenceAudioLoader::writeAll(const char *data, qint64 bytes) {
    if (!m_file)
        return false;
    while (bytes > 0) {
        const qint64 written = m_file->write(data, bytes);
        if (written <= 0) {
            fail(QStringLiteral("The audio cache could not be written: ") + m_file->errorString());
            return false;
        }
        data += written;
        bytes -= written;
    }
    return true;
}

bool ReferenceAudioLoader::appendSilence(qint64 frames) {
    constexpr qint64 maximumFrames = std::numeric_limits<qint64>::max() / (2 * qint64(sizeof(float)));
    if (frames < 0 || m_frames > maximumFrames - frames) {
        fail(QStringLiteral("The recording timestamps exceed the supported audio cache size."));
        return false;
    }
    constexpr qint64 chunkFrames = 4096;
    static const std::array<float, chunkFrames * 2> zeroes{};
    while (frames > 0) {
        const qint64 count = std::min(frames, chunkFrames);
        if (!writeAll(reinterpret_cast<const char *>(zeroes.data()), count * 2 * sizeof(float)))
            return false;
        frames -= count;
        m_frames += count;
        m_stats.insertedSilenceFrames += count;
    }
    return true;
}

bool ReferenceAudioLoader::appendAvailable() {
    while (m_decoder && m_decoder->bufferAvailable()) {
        const QAudioBuffer buffer = m_decoder->read();
        if (!buffer.isValid())
            continue;
        const QAudioFormat format = buffer.format();
        if (format.sampleRate() != m_sampleRate || format.channelCount() != 2 || format.sampleFormat() != QAudioFormat::Float) {
            fail(QStringLiteral("The decoder did not provide the requested stereo floating-point audio format."));
            return false;
        }
        if (buffer.byteCount() != buffer.frameCount() * 2 * qint64(sizeof(float))) {
            fail(QStringLiteral("The decoder returned an incomplete audio buffer."));
            return false;
        }
        if (m_stats.buffers == 0)
            m_stats.firstBufferStartUs = buffer.startTime();
        ++m_stats.buffers;
        m_stats.decodedFrames += buffer.frameCount();
        m_stats.maximumBufferBytes = std::max(m_stats.maximumBufferBytes, qint64(buffer.byteCount()));
        qint64 skipped = 0;
        if (buffer.startTime() >= 0) {
            // Preserve the media timestamp, including genuine gaps between
            // audio packets or audio beginning after video time zero. Ignore
            // subframe timestamp rounding; buffers normally join exactly.
            constexpr qint64 maximumFrames = std::numeric_limits<qint64>::max() / (2 * qint64(sizeof(float)));
            const long double timestampFrame = buffer.startTime() * static_cast<long double>(m_sampleRate) / 1000000.0L;
            if (timestampFrame > maximumFrames) {
                fail(QStringLiteral("The recording timestamps exceed the supported audio cache size."));
                return false;
            }
            const qint64 firstFrame = qRound64(static_cast<double>(timestampFrame));
            const qint64 difference = firstFrame - m_frames;
            if (difference > 1 && !appendSilence(difference))
                return false;
            if (difference < -1) {
                skipped = std::min(-difference, qint64(buffer.frameCount()));
                m_stats.overlappingFrames += skipped;
            }
        }
        const qint64 frames = buffer.frameCount() - skipped;
        constexpr qint64 maximumFrames = std::numeric_limits<qint64>::max() / (2 * qint64(sizeof(float)));
        if (frames < 0 || frames > maximumFrames - m_frames) {
            fail(QStringLiteral("The original recording is too large for the audio cache."));
            return false;
        }
        if (!writeAll(reinterpret_cast<const char *>(buffer.constData<float>() + skipped * 2), frames * 2 * sizeof(float)))
            return false;
        m_frames += frames;
    }
    return m_decoder != nullptr;
}

void ReferenceAudioLoader::finish() {
    if (!appendAvailable())
        return;
    if (m_frames <= 0) {
        fail(QStringLiteral("The original recording contains no decodable audio."));
        return;
    }
    if (!m_file->flush()) {
        fail(QStringLiteral("The audio cache could not be finalized: ") + m_file->errorString());
        return;
    }
    const qint64 bytes = m_frames * 2 * qint64(sizeof(float));
    uchar *mapping = m_file->map(0, bytes, QFileDevice::MapPrivateOption);
    if (!mapping) {
        fail(QStringLiteral("The decoded original recording could not be mapped for playback: ") + m_file->errorString());
        return;
    }
    auto audio = std::make_shared<ReferenceAudioData>();
    audio->sampleRate = m_sampleRate;
    audio->frameCount = m_frames;
    audio->samples = reinterpret_cast<const float *>(mapping);
    audio->m_mapping = mapping;
    audio->m_file = std::move(m_file);
    QAudioDecoder *decoder = m_decoder;
    m_decoder = nullptr;
    decoder->disconnect(this);
    decoder->deleteLater();
    emit ready(std::move(audio));
}

void ReferenceAudioLoader::fail(const QString &message) {
    cancel();
    emit error(message);
}
