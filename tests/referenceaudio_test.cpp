#include "referenceaudio.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

static bool check(bool condition, const char *message) {
    if (!condition)
        std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

static bool writeFixture(const QString &path, const QVector<float> &samples, bool floatFormat) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    const quint16 bytesPerSample = floatFormat ? 4 : 2;
    const quint32 dataBytes = samples.size() * bytesPerSample;
    out.writeRawData("RIFF", 4);out << quint32(36 + dataBytes);out.writeRawData("WAVE", 4);
    out.writeRawData("fmt ", 4);out << quint32(16) << quint16(floatFormat ? 3 : 1) << quint16(2);
    out << quint32(44100) << quint32(44100 * 2 * bytesPerSample) << quint16(2 * bytesPerSample) << quint16(bytesPerSample * 8);
    out.writeRawData("data", 4);out << dataBytes;
    for (float value : samples) {
        if (floatFormat)
            out << value;
        else
            out << qint16(std::lround(value * 32768.0f));
    }
    return out.status() == QDataStream::Ok && file.flush();
}

struct LoadResult {
    ReferenceAudioPtr audio;
    QString error;
    bool timedOut = false;
};

static LoadResult awaitResult(ReferenceAudioLoader &loader, const std::function<void()> &start) {
    LoadResult result;
    bool complete = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    const auto ready = QObject::connect(&loader, &ReferenceAudioLoader::ready, &loop, [&](ReferenceAudioPtr audio) {
        result.audio = std::move(audio);complete = true;loop.quit();
    });
    const auto error = QObject::connect(&loader, &ReferenceAudioLoader::error, &loop, [&](const QString &message) {
        result.error = message;complete = true;loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] {result.timedOut = true;loader.cancel();loop.quit();});
    timeout.start(10000);
    start();
    if (!complete)
        loop.exec();
    QObject::disconnect(ready);
    QObject::disconnect(error);
    return result;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("VArpeggioReferenceAudioTests");
    QTemporaryDir directory;
    bool ok = check(directory.isValid(), "temporary fixture directory is available");
    if (!directory.isValid())return 1;

    QVector<float> expected;
    expected.reserve(4096);
    for (int frame = 0; frame < 2048; ++frame) {
        // Nonzero at the very first frame: any startup attack truncation,
        // inserted priming silence, or unexpected stereo conversion fails.
        expected << (.25f + (frame % 7) * .01f) << (-.125f - (frame % 5) * .01f);
    }
    const QString floatPath = directory.filePath("immediate-stereo-float.wav");
    ok &= check(writeFixture(floatPath, expected, true), "stereo float fixture writes successfully");
    ReferenceAudioLoader loader;
    auto decoded = awaitResult(loader, [&] {loader.load(floatPath, 44100, directory.path());});
    ok &= check(!decoded.timedOut && decoded.error.isEmpty() && bool(decoded.audio), "stereo float source decodes successfully without audio output");
    if (decoded.audio) {
        ok &= check(decoded.audio->sampleRate == 44100 && decoded.audio->frameCount == 2048, "requested stereo float output retains native rate and complete frame count");
        ok &= check(decoded.audio->samples != nullptr && std::memcmp(decoded.audio->samples, expected.constData(), expected.size() * sizeof(float)) == 0,
                    "mapped PCM preserves every sample including the complete first attack");
        const auto stats = loader.stats();
        ok &= check(stats.buffers > 0 && stats.decodedFrames == 2048 && stats.insertedSilenceFrames == 0 && stats.overlappingFrames == 0 && stats.firstBufferStartUs == 0,
                    "decoder buffers join at time zero without gaps or duplicate frames");
        ok &= check(!loader.isLoading(), "loader finishes before publishing the immutable audio");
        const QString cachePath = decoded.audio->cacheFilePath();
        ok &= check(QFileInfo::exists(cachePath), "mapped temporary cache remains while its first owner exists");
        auto retained = decoded.audio;
        decoded.audio.reset();
        loader.cancel();
        ok &= check(QFileInfo::exists(cachePath) && retained->samples[0] == expected[0] && retained->samples[4095] == expected[4095],
                    "mapping survives loader cancellation while another shared owner reads it");
        retained.reset();
        ok &= check(!QFileInfo::exists(cachePath), "last shared owner unmaps and removes the temporary recording cache");
    }

    const QVector<float> pcmExpected{.5f, -.5f, .25f, -.25f, .125f, -.125f, 0.f, 0.f};
    const QString pcmPath = directory.filePath("immediate-stereo-pcm16.wav");
    ok &= check(writeFixture(pcmPath, pcmExpected, false), "PCM16 stereo source fixture writes successfully");
    auto pcm = awaitResult(loader, [&] {loader.load(pcmPath, 44100, directory.path());});
    ok &= check(!pcm.timedOut && pcm.error.isEmpty() && bool(pcm.audio), "PCM16 source converts to requested stereo floating point");
    if (pcm.audio) {
        ok &= check(pcm.audio->frameCount == 4 && pcm.audio->sampleRate == 44100 &&
                    std::memcmp(pcm.audio->samples, pcmExpected.constData(), pcmExpected.size() * sizeof(float)) == 0,
                    "sample format conversion preserves the first frame and stereo channel values exactly");
        pcm.audio.reset();
    }

    auto missing = awaitResult(loader, [&] {loader.load(directory.filePath("does-not-exist.wav"), 44100, directory.path());});
    ok &= check(!missing.timedOut && !missing.audio && !missing.error.isEmpty() && !loader.isLoading(), "invalid source emits an error without ready or a lingering job");
    auto badRate = awaitResult(loader, [&] {loader.load(floatPath, 0, directory.path());});
    ok &= check(!badRate.timedOut && !badRate.audio && !badRate.error.isEmpty(), "invalid requested sample rate is rejected");

    bool cancelledReady = false;
    bool cancelledError = false;
    QEventLoop cancelledLoop;
    const auto cancelledReadyConnection = QObject::connect(&loader, &ReferenceAudioLoader::ready, &cancelledLoop, [&](ReferenceAudioPtr) {cancelledReady = true;});
    const auto cancelledErrorConnection = QObject::connect(&loader, &ReferenceAudioLoader::error, &cancelledLoop, [&](const QString &) {cancelledError = true;});
    loader.load(floatPath, 44100, directory.path());
    loader.cancel();
    QTimer::singleShot(150, &cancelledLoop, &QEventLoop::quit);
    cancelledLoop.exec();
    QObject::disconnect(cancelledReadyConnection);
    QObject::disconnect(cancelledErrorConnection);
    ok &= check(!cancelledReady && !cancelledError && !loader.isLoading(), "cancelled decoding cannot publish a stale ready result or error");

    const auto fixture = ReferenceAudioData::fromSamples(48000, {.1f, .2f, .3f, .4f});
    ok &= check(fixture && fixture->sampleRate == 48000 && fixture->frameCount == 2 && fixture->samples[0] == .1f && fixture->samples[3] == .4f,
                "small synthetic fixtures retain their own interleaved stereo storage");
    ok &= check(!ReferenceAudioData::fromSamples(0, {.1f, .2f}) && !ReferenceAudioData::fromSamples(44100, {.1f}), "malformed synthetic fixture data is rejected");
    if (ok)std::puts("Reference audio decode, first attack, mapping lifetime, conversion, errors, and cancellation checks passed.");
    return ok ? 0 : 1;
}
