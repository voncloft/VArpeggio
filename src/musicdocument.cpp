#include "musicdocument.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <zlib.h>

namespace {
const char *kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

void setError(QString *error, const QString &message) { if (error) *error = message; }

bool writeAll(const QString &path, const QByteArray &bytes, QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { setError(error, file.errorString()); return false; }
    if (file.write(bytes) != bytes.size() || !file.commit()) { setError(error, file.errorString()); return false; }
    return true;
}

int inferKeySignatureImpl(const QVector<NoteEvent> &notes) {
    if (notes.isEmpty()) return 0;

    std::array<double, 12> pitchWeight{};
    std::array<bool, 12> present{};
    double totalWeight = 0.0;
    int distinctPitches = 0;
    for (const auto &note : notes) {
        const int pitch = ((note.note % 12) + 12) % 12;
        const double weight = qBound(0.25, note.duration, 4.0);
        pitchWeight[pitch] += weight;
        totalWeight += weight;
        if (!present[pitch]) { present[pitch] = true; ++distinctPitches; }
    }
    if (distinctPitches < 3 || totalWeight <= 0.0) return 0;

    const auto mod12 = [](int value) { value %= 12; return value < 0 ? value + 12 : value; };
    constexpr std::array<int, 7> majorIntervals{0, 2, 4, 5, 7, 9, 11};
    const auto outsideWeight = [&](int fifths) {
        std::array<bool, 12> inScale{};
        const int tonic = mod12(7 * fifths);
        for (int interval : majorIntervals) inScale[mod12(tonic + interval)] = true;
        double outside = 0.0;
        for (int pitch = 0; pitch < 12; ++pitch)
            if (!inScale[pitch]) outside += pitchWeight[pitch];
        return outside;
    };

    // First minimize notes that would need an accidental, then prefer the
    // signature with fewer symbols when two spellings explain the music about
    // equally well. Relative minor and modal music correctly share the same
    // signature because they share the same seven-note pitch collection.
    int bestFifths = 0;
    double bestOutside = outsideWeight(0);
    double bestCost = bestOutside + totalWeight * 0.018 * qAbs(bestFifths);
    for (int fifths = -7; fifths <= 7; ++fifths) {
        const double outside = outsideWeight(fifths);
        const double cost = outside + totalWeight * 0.018 * qAbs(fifths);
        if (cost < bestCost - 1e-9 ||
            (qAbs(cost - bestCost) <= 1e-9 && qAbs(fifths) < qAbs(bestFifths))) {
            bestCost = cost;
            bestOutside = outside;
            bestFifths = fifths;
        }
    }

    // Do not invent a key signature for noisy/chromatic material. A signature
    // must explain nearly all of the duration-weighted notes and improve on C
    // major by a useful amount.
    const double cOutside = outsideWeight(0);
    if (bestFifths != 0 &&
        (bestOutside / totalWeight > 0.12 || cOutside - bestOutside < totalWeight * 0.06))
        return 0;
    return bestFifths;
}

quint16 zipLe16(const uchar *p) { return quint16(p[0])|(quint16(p[1])<<8); }
quint32 zipLe32(const uchar *p) { return quint32(p[0])|(quint32(p[1])<<8)|(quint32(p[2])<<16)|(quint32(p[3])<<24); }

struct ZipEntry {
    QString name;
    quint16 flags=0;
    quint16 method=0;
    quint32 compressedSize=0;
    quint32 uncompressedSize=0;
    quint32 localOffset=0;
};

bool extractZipEntry(const QByteArray &archive,const ZipEntry &entry,QByteArray &bytes) {
    const auto *p=reinterpret_cast<const uchar*>(archive.constData());
    const qsizetype size=archive.size();
    if(entry.flags&1 || entry.localOffset>quint32(size-30) || zipLe32(p+entry.localOffset)!=0x04034b50) return false;
    const quint16 nameLength=zipLe16(p+entry.localOffset+26);
    const quint16 extraLength=zipLe16(p+entry.localOffset+28);
    const quint64 dataOffset=quint64(entry.localOffset)+30+nameLength+extraLength;
    if(dataOffset+entry.compressedSize>quint64(size) || entry.uncompressedSize>64u*1024u*1024u) return false;
    const char *compressed=archive.constData()+dataOffset;
    if(entry.method==0){
        if(entry.compressedSize!=entry.uncompressedSize) return false;
        bytes=QByteArray(compressed,qsizetype(entry.compressedSize));
        return true;
    }
    if(entry.method!=8) return false;
    bytes.resize(qsizetype(entry.uncompressedSize));
    z_stream stream{};
    stream.next_in=reinterpret_cast<Bytef*>(const_cast<char*>(compressed));
    stream.avail_in=entry.compressedSize;
    stream.next_out=reinterpret_cast<Bytef*>(bytes.data());
    stream.avail_out=entry.uncompressedSize;
    if(inflateInit2(&stream,-MAX_WBITS)!=Z_OK){bytes.clear();return false;}
    const int result=inflate(&stream,Z_FINISH);
    const bool ok=result==Z_STREAM_END && stream.total_out==entry.uncompressedSize;
    inflateEnd(&stream);
    if(!ok)bytes.clear();
    return ok;
}

bool readCompressedMusicXml(const QString &path, QByteArray &xml, QString *error) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)){setError(error,file.errorString());return false;}
    const QByteArray archive=file.readAll();
    const auto *p=reinterpret_cast<const uchar*>(archive.constData());
    if(archive.size()<22){setError(error,"This .mxl file is not a readable ZIP archive.");return false;}

    qsizetype endOffset=-1;
    const qsizetype first=qMax<qsizetype>(0,archive.size()-22-65535);
    for(qsizetype i=archive.size()-22;i>=first;--i){
        if(zipLe32(p+i)==0x06054b50){endOffset=i;break;}
    }
    if(endOffset<0){setError(error,"This .mxl file has no ZIP directory.");return false;}
    const quint16 entryCount=zipLe16(p+endOffset+10);
    const quint32 directorySize=zipLe32(p+endOffset+12);
    const quint32 directoryOffset=zipLe32(p+endOffset+16);
    if(quint64(directoryOffset)+directorySize>quint64(archive.size())){
        setError(error,"This .mxl file has a damaged ZIP directory.");return false;
    }

    QHash<QString,ZipEntry> entries;
    qsizetype cursor=directoryOffset;
    for(quint16 i=0;i<entryCount;++i){
        if(cursor+46>archive.size() || zipLe32(p+cursor)!=0x02014b50) break;
        const quint16 nameLength=zipLe16(p+cursor+28);
        const quint16 extraLength=zipLe16(p+cursor+30);
        const quint16 commentLength=zipLe16(p+cursor+32);
        if(cursor+46+nameLength+extraLength+commentLength>archive.size()) break;
        ZipEntry entry;
        entry.flags=zipLe16(p+cursor+8);
        entry.method=zipLe16(p+cursor+10);
        entry.compressedSize=zipLe32(p+cursor+20);
        entry.uncompressedSize=zipLe32(p+cursor+24);
        entry.localOffset=zipLe32(p+cursor+42);
        const QByteArray nameBytes=archive.mid(cursor+46,nameLength);
        entry.name=(entry.flags&0x800)?QString::fromUtf8(nameBytes):QString::fromLocal8Bit(nameBytes);
        entries.insert(entry.name,entry);
        cursor+=46+nameLength+extraLength+commentLength;
    }

    auto findEntry=[&](const QString &wanted)->const ZipEntry*{
        auto exact=entries.constFind(wanted);
        if(exact!=entries.cend())return &exact.value();
        for(auto it=entries.cbegin();it!=entries.cend();++it)
            if(it.key().compare(wanted,Qt::CaseInsensitive)==0)return &it.value();
        return nullptr;
    };

    QByteArray containerBytes;
    QString rootEntry;
    if(const ZipEntry *container=findEntry(QStringLiteral("META-INF/container.xml"));container&&extractZipEntry(archive,*container,containerBytes)){
        QDomDocument document;
        if(document.setContent(containerBytes)){
            const auto roots=document.elementsByTagName(QStringLiteral("rootfile"));
            if(!roots.isEmpty())rootEntry=roots.at(0).toElement().attribute(QStringLiteral("full-path"));
        }
    }
    if(rootEntry.isEmpty()){
        for(auto it=entries.cbegin();it!=entries.cend();++it){
            if(it.key().endsWith(QStringLiteral(".xml"),Qt::CaseInsensitive)
               && it.key().compare(QStringLiteral("META-INF/container.xml"),Qt::CaseInsensitive)!=0){rootEntry=it.key();break;}
        }
    }
    const ZipEntry *score=findEntry(rootEntry);
    if(!score || !extractZipEntry(archive,*score,xml) || xml.isEmpty()){
        setError(error,"This .mxl file does not contain a readable MusicXML score.");return false;
    }
    return true;
}

quint32 be32(const uchar *p) { return (quint32(p[0])<<24)|(quint32(p[1])<<16)|(quint32(p[2])<<8)|p[3]; }
quint16 be16(const uchar *p) { return (quint16(p[0])<<8)|p[1]; }
quint32 le32(const uchar *p) { return quint32(p[0])|(quint32(p[1])<<8)|(quint32(p[2])<<16)|(quint32(p[3])<<24); }
quint16 le16(const uchar *p) { return quint16(p[0])|(quint16(p[1])<<8); }
void putBe16(QByteArray &b, quint16 v) { b.append(char(v>>8)); b.append(char(v)); }
void putBe32(QByteArray &b, quint32 v) { b.append(char(v>>24)); b.append(char(v>>16)); b.append(char(v>>8)); b.append(char(v)); }
void putLe16(QByteArray &b, quint16 v) { b.append(char(v)); b.append(char(v>>8)); }
void putLe32(QByteArray &b, quint32 v) { b.append(char(v)); b.append(char(v>>8)); b.append(char(v>>16)); b.append(char(v>>24)); }

QByteArray varLen(quint32 value) {
    quint32 buffer = value & 0x7f;
    while ((value >>= 7)) { buffer <<= 8; buffer |= ((value & 0x7f) | 0x80); }
    QByteArray out;
    for (;;) { out.append(char(buffer & 0xff)); if (buffer & 0x80) buffer >>= 8; else break; }
    return out;
}

int pitchStep(const QString &step) {
    static const QMap<QString,int> map{{"C",0},{"D",2},{"E",4},{"F",5},{"G",7},{"A",9},{"B",11}};
    return map.value(step.toUpper(), 0);
}

QString xmlEscape(QString s) { return s.replace('&', "&amp;").replace('<', "&lt;").replace('>', "&gt;").replace('"', "&quot;"); }

QVector<NoteEvent> sortNotes(QVector<NoteEvent> notes) {
    std::sort(notes.begin(), notes.end(), [](const auto &a, const auto &b) {
        if (!qFuzzyCompare(a.start + 1.0, b.start + 1.0)) return a.start < b.start;
        return a.note < b.note;
    });
    return notes;
}

struct DecodedAudio {
    QVector<float> samples;
    int sampleRate = 22050;
};

struct OnsetAnalysis {
    QVector<double> envelope;
    double framesPerSecond = 0.0;
};

struct TempoEstimate {
    int bpm = 0;
    double preciseBpm = 0.0;
    double confidence = 0.0;
    int peakCount = 0;
};

struct TimingAgreement {
    double score = -1.0;
    double offsetSeconds = 0.0;
};

struct PitchOnsetEvidence {
    QVector<float> values;
    int frameCount = 0;
    double framesPerSecond = 0.0;

    double strength(double second,int midi) const {
        if(frameCount<=0||midi<21||midi>108||framesPerSecond<=0)return 0.0;
        const int center=qRound(second*framesPerSecond),radius=qMax(2,qRound(framesPerSecond*.10));double result=0;
        for(int frame=qMax(0,center-radius);frame<qMin(frameCount,center+radius+1);++frame)
            result=qMax(result,double(values[frame*88+midi-21]));
        return result;
    }
};

bool decodeMonoAudio(const QString &path, DecodedAudio &audio, QString *error,
                     const std::function<bool()> &canceled) {
    QTemporaryDir temp;
    if (!temp.isValid()) { setError(error, "Could not create a temporary audio workspace."); return false; }
    const QString wav = temp.filePath("audio.wav");
    QProcess ff;
    ff.setProcessChannelMode(QProcess::SeparateChannels);
    // Decode the complete source. The former five-minute cap silently removed
    // the last 110 seconds of the user's Moonlight Sonata video.
    ff.start("ffmpeg", {"-v","error","-y","-i",path,"-vn","-ac","1","-ar","22050","-c:a","pcm_s16le",wav});
    while (!ff.waitForFinished(100)) {
        if (canceled && canceled()) {
            ff.kill(); ff.waitForFinished(); setError(error, "Transcription canceled."); return false;
        }
    }
    if (ff.exitStatus()!=QProcess::NormalExit || ff.exitCode()!=0) {
        setError(error, "This file has no readable audio track. Try WAV, MP3, M4A, OGG, FLAC, or a standard MP4.");
        return false;
    }
    QFile file(wav);
    if (!file.open(QIODevice::ReadOnly)) { setError(error, "Could not read the decoded audio."); return false; }
    const QByteArray bytes = file.readAll();
    if (bytes.size() < 44 || QByteArray(bytes.constData(), 4)!="RIFF") {
        setError(error, "The decoded soundtrack was not a readable WAV file."); return false;
    }
    const uchar *raw = reinterpret_cast<const uchar*>(bytes.constData());
    int dataPos=12, dataSize=0, sampleRate=22050;
    while (dataPos+8 <= bytes.size()) {
        const QByteArray id(bytes.constData()+dataPos,4);
        const quint32 rawLength=le32(raw+dataPos+4);
        if (rawLength>quint32(std::numeric_limits<int>::max())) break;
        const int length=int(rawLength);
        if (dataPos+8+length > bytes.size()) break;
        if (id=="fmt " && length>=8) sampleRate=int(le32(raw+dataPos+12));
        if (id=="data") { dataSize=length; dataPos+=8; break; }
        dataPos+=8+length+(length&1);
    }
    if (dataSize<2 || sampleRate<4000) { setError(error, "The decoded soundtrack contains no usable samples."); return false; }
    const int sampleCount=dataSize/2;
    audio.samples.resize(sampleCount); audio.sampleRate=sampleRate;
    const uchar *pcm=raw+dataPos;
    for (int i=0;i<sampleCount;++i) audio.samples[i]=qint16(le16(pcm+i*2))/32768.0f;
    return true;
}

double percentile(QVector<double> values, double fraction) {
    if (values.isEmpty()) return 0.0;
    const int index=qBound(0,qRound((values.size()-1)*fraction),values.size()-1);
    std::nth_element(values.begin(),values.begin()+index,values.end());
    return values[index];
}

bool buildOnsetAnalysis(const DecodedAudio &audio, OnsetAnalysis &analysis, QString *error,
                        const std::function<void(int)> &progress,
                        const std::function<bool()> &canceled) {
    constexpr int frameSize=2048, hop=256;
    if (audio.samples.size()<frameSize) { setError(error,"The soundtrack is too short for automatic tempo detection."); return false; }
    const int totalFrames=1+(audio.samples.size()-frameSize)/hop;
    const int firstBin=qMax(1,qRound(50.0*frameSize/audio.sampleRate));
    const int lastBin=qMin(frameSize/2,qRound(5000.0*frameSize/audio.sampleRate));
    QVector<float> window(frameSize),previous(lastBin-firstBin+1),current(previous.size());
    for(int i=0;i<frameSize;++i)window[i]=float(.5-.5*std::cos(2.0*M_PI*i/(frameSize-1)));
    cv::Mat timeDomain(frameSize,1,CV_32F),spectrum;
    QVector<double> rawFlux(totalFrames);
    for(int frame=0;frame<totalFrames;++frame){
        if(canceled&&canceled()){setError(error,"Transcription canceled.");return false;}
        const int offset=frame*hop;
        for(int i=0;i<frameSize;++i){
            const int sampleIndex=offset+i;
            const float value=audio.samples[sampleIndex]-.97f*(sampleIndex?audio.samples[sampleIndex-1]:0.0f);
            timeDomain.at<float>(i)=value*window[i];
        }
        cv::dft(timeDomain,spectrum,cv::DFT_COMPLEX_OUTPUT);
        double flux=0;
        for(int bin=firstBin;bin<=lastBin;++bin){
            const cv::Vec2f value=spectrum.at<cv::Vec2f>(bin);
            const float magnitude=std::log1p(40.0f*std::hypot(value[0],value[1])/frameSize);
            const int index=bin-firstBin;current[index]=magnitude;
            if(frame)flux+=qMax(0.0f,magnitude-previous[index]);
        }
        rawFlux[frame]=flux/qMax(1,current.size());
        previous.swap(current);
        if(progress&&frame%100==0)progress(qMin(80,frame*80/qMax(1,totalFrames)));
    }
    const double rate=double(audio.sampleRate)/hop;
    int medianWidth=qMax(5,qRound(rate));if(!(medianWidth&1))++medianWidth;
    const int radius=medianWidth/2;
    QVector<double> filtered(totalFrames),scratch;scratch.reserve(medianWidth);
    for(int i=0;i<totalFrames;++i){
        scratch.clear();const int begin=qMax(0,i-radius),end=qMin(totalFrames,i+radius+1);
        for(int j=begin;j<end;++j)scratch.append(rawFlux[j]);
        const int middle=scratch.size()/2;std::nth_element(scratch.begin(),scratch.begin()+middle,scratch.end());
        filtered[i]=qMax(0.0,rawFlux[i]-scratch[middle]);
    }
    QVector<double> smoothed(totalFrames);
    for(int i=0;i<totalFrames;++i)smoothed[i]=.25*filtered[qMax(0,i-1)]+.5*filtered[i]+.25*filtered[qMin(totalFrames-1,i+1)];
    const int skipFrames=qMin(totalFrames,qRound(rate*3.0));
    QVector<double> useful;useful.reserve(qMax(0,totalFrames-skipFrames));
    for(int i=skipFrames;i<totalFrames;++i)useful.append(smoothed[i]);
    const double ceiling=percentile(useful,.99);
    if(ceiling<=1e-8){setError(error,"The soundtrack is too quiet or steady to estimate a tempo.");return false;}
    for(int i=0;i<totalFrames;++i)smoothed[i]=i<skipFrames?0.0:qMin(1.0,smoothed[i]/ceiling);
    analysis.envelope=std::move(smoothed);analysis.framesPerSecond=rate;
    if(progress)progress(100);
    return true;
}

PitchOnsetEvidence buildPitchOnsetEvidence(const DecodedAudio &audio,const std::function<bool()> &canceled) {
    PitchOnsetEvidence result;constexpr int frameSize=4096,hop=512,pitchCount=88;
    if(audio.samples.size()<frameSize||audio.sampleRate<=0)return result;
    result.frameCount=1+(audio.samples.size()-frameSize)/hop;result.framesPerSecond=double(audio.sampleRate)/hop;
    QVector<float> window(frameSize);for(int i=0;i<frameSize;++i)window[i]=float(.5-.5*std::cos(2*M_PI*i/(frameSize-1)));
    QVector<float> logMagnitude(result.frameCount*pitchCount);cv::Mat frame(frameSize,1,CV_32F),spectrum;
    for(int frameIndex=0;frameIndex<result.frameCount;++frameIndex){
        if(canceled&&canceled())return {};
        const int offset=frameIndex*hop;for(int i=0;i<frameSize;++i)frame.at<float>(i)=audio.samples[offset+i]*window[i];
        cv::dft(frame,spectrum,cv::DFT_COMPLEX_OUTPUT);
        for(int pitch=0;pitch<pitchCount;++pitch){
            const int midi=21+pitch;const double hz=440.0*std::pow(2.0,(midi-69)/12.0);const int center=qBound(2,qRound(hz*frameSize/audio.sampleRate),frameSize/2-2);double peak=0;
            for(int bin=center-1;bin<=center+1;++bin){const auto value=spectrum.at<cv::Vec2f>(bin);peak=qMax(peak,std::hypot(double(value[0]),double(value[1])));}
            logMagnitude[frameIndex*pitchCount+pitch]=float(std::log1p(peak));
        }
    }
    result.values.fill(0.0f,result.frameCount*pitchCount);
    for(int pitch=0;pitch<pitchCount;++pitch){
        QVector<double> lane;lane.reserve(result.frameCount);
        for(int frameIndex=4;frameIndex+2<result.frameCount;++frameIndex){
            double future=qMax(double(logMagnitude[frameIndex*pitchCount+pitch]),double(logMagnitude[(frameIndex+1)*pitchCount+pitch]));
            std::array<double,4> history{};for(int back=0;back<4;++back)history[back]=logMagnitude[(frameIndex-4+back)*pitchCount+pitch];
            std::sort(history.begin(),history.end());const double onset=qMax(0.0,future*.5+future*.5-(history[1]+history[2])*.5);
            result.values[frameIndex*pitchCount+pitch]=float(onset);lane.append(onset);
        }
        const double scale=qMax(.01,percentile(lane,.95));
        for(int frameIndex=0;frameIndex<result.frameCount;++frameIndex)result.values[frameIndex*pitchCount+pitch]=float(result.values[frameIndex*pitchCount+pitch]/scale);
    }
    return result;
}

TempoEstimate estimateTempoFromOnsets(const OnsetAnalysis &analysis) {
    TempoEstimate result;
    const auto &e=analysis.envelope;const int count=e.size();const double rate=analysis.framesPerSecond;
    if(count<qRound(rate*6.0)||rate<=0)return result;
    for(int i=1;i+1<count;++i)if(e[i]>=.10&&e[i]>e[i-1]&&e[i]>=e[i+1])++result.peakCount;
    if(result.peakCount<8)return result;
    const int maxLag=qMin(count-1,qCeil(rate*8.1));
    QVector<double> autocorrelation(maxLag+1);
    double zero=0;for(double value:e)zero+=value*value;zero/=qMax(1,count);
    if(zero<=1e-12)return result;
    autocorrelation[0]=1;
    for(int lag=1;lag<=maxLag;++lag){double sum=0;for(int i=0;i+lag<count;++i)sum+=e[i]*e[i+lag];autocorrelation[lag]=(sum/qMax(1,count-lag))/zero;}
    auto sample=[&](double lag){
        if(lag<0||lag>=maxLag)return 0.0;const int a=int(std::floor(lag));const double f=lag-a;
        return autocorrelation[a]*(1.0-f)+autocorrelation[qMin(maxLag,a+1)]*f;
    };
    auto pulseScore=[&](double bpm){
        if(bpm<=0)return 0.0;const double lag=rate*60.0/bpm;
        return sample(lag)+.50*sample(2*lag)+.25*sample(4*lag)+.20*sample(lag*.5);
    };
    // Slow classical movements are common here; 60 BPM was too high a floor
    // and forced Moonlight's triplet subdivision to masquerade as the beat.
    constexpr double firstBpm=35.0,step=.1;constexpr int candidateCount=1451;
    QVector<double> scores(candidateCount);
    int best=0;
    for(int i=0;i<candidateCount;++i){const double bpm=firstBpm+i*step;scores[i]=pulseScore(bpm)+.80*pulseScore(bpm*.5)+.45*pulseScore(bpm*2.0);if(scores[i]>scores[best])best=i;}
    double refined=firstBpm+best*step;
    if(best>0&&best+1<candidateCount){const double left=scores[best-1],middle=scores[best],right=scores[best+1],denominator=left-2*middle+right;if(qAbs(denominator)>1e-12)refined+=qBound(-1.0,.5*(left-right)/denominator,1.0)*step;}
    auto scoreAt=[&](double bpm){const int i=qBound(0,qRound((bpm-firstBpm)/step),candidateCount-1);return scores[i];};
    const double rawRefined=refined,rawBestScore=scores[best];
    // A continuous triplet accompaniment can make three note attacks per beat
    // look like the tempo itself. Prefer the one-third pulse only when it is a
    // genuinely strong independent subharmonic. This is deliberately stricter
    // than half-tempo folding: ordinary pop eighth-notes often have a strong
    // half pulse, while a 3:1 family is characteristic of pieces such as the
    // first movement of Moonlight Sonata.
    const double thirdBpm=rawRefined/3.0,thirdScore=scoreAt(thirdBpm);
    const bool tripletSubdivision=rawRefined>=120.0&&thirdBpm>=firstBpm&&thirdScore>=rawBestScore*.68;
    if(tripletSubdivision)refined=thirdBpm;
    const double doubleScore=scoreAt(rawRefined*2.0);
    const bool lowHalfPulse=!tripletSubdivision&&rawRefined<60.0&&rawRefined*2.0<=180.0&&doubleScore>=rawBestScore*.72;
    if(lowHalfPulse)refined=rawRefined*2.0;
    if(qEnvironmentVariableIsSet("KEYFLOW_TEMPO_DEBUG"))
        std::fprintf(stderr,"tempo raw %.2f score %.6f half %.6f double %.6f third %.6f selected %.2f\n",rawRefined,rawBestScore,scoreAt(rawRefined/2.0),doubleScore,thirdScore,refined);
    double runner=0;
    for(int i=1;i+1<candidateCount;++i){
        if(scores[i]<scores[i-1]||scores[i]<scores[i+1])continue;
        const double bpm=firstBpm+i*step;
        if(qAbs(bpm-refined)<3.0)continue;
        runner=qMax(runner,scores[i]);
    }
    const double margin=qBound(0.0,(scores[best]-runner)/qMax(1e-9,qAbs(scores[best])),1.0);
    const double support=qBound(0.0,(result.peakCount-8)/32.0,1.0);
    result.preciseBpm=refined;result.bpm=qBound(35,qRound(refined),180);
    result.confidence=qBound(0.0,margin*2.5*.72+support*.28,1.0);
    if(tripletSubdivision)result.confidence=qBound(.35,result.confidence*.8+.15,1.0);
    if(lowHalfPulse)result.confidence=qBound(.35,result.confidence*.8+.15,1.0);
    return result;
}

TimingAgreement compareOnsets(const QVector<double> &visualTimes,const OnsetAnalysis &audio,double minimumOffset=-.75,double maximumOffset=3.5) {
    TimingAgreement result;
    if(visualTimes.size()<6||audio.envelope.isEmpty()||audio.framesPerSecond<=0)return result;
    double baseline=0;for(double value:audio.envelope)baseline+=value;baseline/=audio.envelope.size();
    double best=-1;
    const double step=1.0/audio.framesPerSecond;
    for(double offset=minimumOffset;offset<=maximumOffset;offset+=step){
        double sum=0;int overlap=0;
        for(double visualTime:visualTimes){
            const int center=qRound((visualTime+offset)*audio.framesPerSecond);
            if(center<2||center+2>=audio.envelope.size())continue;
            double local=0;for(int d=-2;d<=2;++d)local=qMax(local,audio.envelope[center+d]);
            sum+=local;++overlap;
        }
        if(overlap<qMax(6,qRound(visualTimes.size()*.70)))continue;
        const double score=sum/overlap;
        if(score>best){best=score;result.offsetSeconds=offset;}
    }
    if(best<0)return result;
    result.score=qBound(0.0,(best-baseline)/qMax(.05,1.0-baseline)*1.35,1.0);
    return result;
}

// Render the proposed score to a neutral piano-like signal and compare it with
// the same span of the original recording.  Comparing only twelve pitch
// classes made a C2 count as a C6 and let piano harmonics masquerade as played
// notes.  Keep octave-resolved semitone bands and score the strongest exact
// bands in every frame; a small chroma component tolerates different piano
// timbres without being able to hide a wrong octave.
double compareRenderedDraft(const QVector<NoteEvent> &notes,int tempo,
                            const DecodedAudio &source,double sourceStartSeconds) {
    if(notes.size()<6||source.samples.isEmpty()||tempo<=0)return -1.0;
    const int sampleRate=source.sampleRate;
    const double secondsPerBeat=60.0/tempo;
    double scoreEnd=0;for(const auto &note:notes)scoreEnd=qMax(scoreEnd,(note.start+note.duration)*secondsPerBeat+.28);
    const int sourceStart=qMax(0,qRound(sourceStartSeconds*sampleRate));
    const int available=qMax(0,source.samples.size()-sourceStart);
    const int renderSamples=qMin(available,qRound(scoreEnd*sampleRate));
    if(renderSamples<sampleRate*2)return -1.0;
    QVector<float> rendered(renderSamples);
    for(const auto &note:notes){
        const int begin=qMax(0,qRound(note.start*secondsPerBeat*sampleRate));
        const double held=qMax(.025,note.duration*secondsPerBeat);
        const int end=qMin(renderSamples,begin+qRound((held+.25)*sampleRate));
        const double frequency=440.0*std::pow(2.0,(note.note-69)/12.0);
        const double step=2.0*M_PI*frequency/sampleRate;
        const double amplitude=.075*note.velocity/127.0;
        for(int i=begin;i<end;++i){
            const double t=(i-begin)/double(sampleRate);
            const double attack=qMin(1.0,t/.008);
            const double release=t<=held?1.0:std::exp(-(t-held)*14.0);
            const double decay=std::exp(-t*1.15);
            const double phase=(i-begin)*step;
            rendered[i]+=float(amplitude*attack*release*decay*(std::sin(phase)+.24*std::sin(phase*2.006)+.09*std::sin(phase*3.018)));
        }
    }
    constexpr int frameSize=8192,hop=2048;
    if(renderSamples<frameSize)return -1.0;
    QVector<float> window(frameSize);for(int i=0;i<frameSize;++i)window[i]=float(.5-.5*std::cos(2*M_PI*i/(frameSize-1)));
    cv::Mat sourceFrame(frameSize,1,CV_32F),draftFrame(frameSize,1,CV_32F),sourceSpectrum,draftSpectrum;
    double weightedSimilarity=0,totalWeight=0;int compared=0;
    for(int offset=0;offset+frameSize<=renderSamples;offset+=hop){
        double draftRms=0,sourceRms=0;
        for(int i=0;i<frameSize;++i){
            const float d=rendered[offset+i],s=source.samples[sourceStart+offset+i];
            draftRms+=d*d;sourceRms+=s*s;draftFrame.at<float>(i)=d*window[i];sourceFrame.at<float>(i)=s*window[i];
        }
        draftRms=std::sqrt(draftRms/frameSize);sourceRms=std::sqrt(sourceRms/frameSize);
        if(draftRms<1e-5||sourceRms<2e-5)continue;
        cv::dft(draftFrame,draftSpectrum,cv::DFT_COMPLEX_OUTPUT);cv::dft(sourceFrame,sourceSpectrum,cv::DFT_COMPLEX_OUTPUT);
        double draftPitch[88]{},sourcePitch[88]{},draftChroma[12]{},sourceChroma[12]{};
        const int firstBin=qMax(1,qRound(35.0*frameSize/sampleRate));
        const int lastBin=qMin(frameSize/2,qRound(4200.0*frameSize/sampleRate));
        for(int bin=firstBin;bin<=lastBin;++bin){
            const double hz=bin*double(sampleRate)/frameSize;
            const int midi=qRound(69.0+12.0*std::log2(hz/440.0));if(midi<21||midi>108)continue;
            const auto dv=draftSpectrum.at<cv::Vec2f>(bin),sv=sourceSpectrum.at<cv::Vec2f>(bin);
            const int pitch=midi-21;draftPitch[pitch]+=double(dv[0])*dv[0]+double(dv[1])*dv[1];sourcePitch[pitch]+=double(sv[0])*sv[0]+double(sv[1])*sv[1];
        }
        QVector<int> draftOrder(88),sourceOrder(88);std::iota(draftOrder.begin(),draftOrder.end(),0);std::iota(sourceOrder.begin(),sourceOrder.end(),0);
        for(int pitch=0;pitch<88;++pitch){draftPitch[pitch]=std::log1p(std::sqrt(draftPitch[pitch]));sourcePitch[pitch]=std::log1p(std::sqrt(sourcePitch[pitch]));draftChroma[(pitch+21)%12]+=draftPitch[pitch];sourceChroma[(pitch+21)%12]+=sourcePitch[pitch];}
        constexpr int strongestPitches=6;
        std::partial_sort(draftOrder.begin(),draftOrder.begin()+strongestPitches,draftOrder.end(),[&](int a,int b){return draftPitch[a]>draftPitch[b];});
        std::partial_sort(sourceOrder.begin(),sourceOrder.begin()+strongestPitches,sourceOrder.end(),[&](int a,int b){return sourcePitch[a]>sourcePitch[b];});
        int exactMatches=0;for(int i=0;i<strongestPitches;++i)for(int j=0;j<strongestPitches;++j)if(draftOrder[i]==sourceOrder[j]){++exactMatches;break;}
        const double exactSimilarity=exactMatches/double(strongestPitches);
        double dn=0,sn=0,dot=0;for(int p=0;p<12;++p){dn+=draftChroma[p]*draftChroma[p];sn+=sourceChroma[p]*sourceChroma[p];dot+=draftChroma[p]*sourceChroma[p];}
        if(dn<=1e-12||sn<=1e-12)continue;
        const double chromaSimilarity=qBound(0.0,dot/std::sqrt(dn*sn),1.0);
        const double similarity=.85*exactSimilarity+.15*chromaSimilarity;
        const double weight=qMin(1.0,draftRms/.08);weightedSimilarity+=similarity*weight;totalWeight+=weight;++compared;
    }
    if(compared<8||totalWeight<=0)return -1.0;
    return qBound(0.0,weightedSimilarity/totalWeight,1.0);
}

double lastAudibleSecond(const DecodedAudio &audio) {
    if(audio.samples.isEmpty()||audio.sampleRate<=0)return -1.0;
    const int window=qMax(1,qRound(audio.sampleRate*.05));
    QVector<double> rms; rms.reserve(audio.samples.size()/window+1);
    double peak=0;
    for(int begin=0;begin<audio.samples.size();begin+=window){
        const int end=qMin(audio.samples.size(),begin+window);double sum=0;
        for(int i=begin;i<end;++i)sum+=double(audio.samples[i])*audio.samples[i];
        const double value=std::sqrt(sum/qMax(1,end-begin));rms.append(value);peak=qMax(peak,value);
    }
    // Keep a quiet natural piano tail, but reject black/silent outro frames and
    // video overlays that otherwise look like dozens of highlighted keys.
    const double threshold=qMax(.00025,peak*.003);
    for(int i=rms.size()-1;i>=0;--i)if(rms[i]>=threshold)return (i+1)*window/double(audio.sampleRate);
    return -1.0;
}

void applySourceDynamics(QVector<NoteEvent> &notes,int tempo,const DecodedAudio &source,double sourceStartSeconds) {
    if(notes.isEmpty()||source.samples.isEmpty()||source.sampleRate<=0||tempo<=0)return;
    const double secondsPerBeat=60.0/tempo;QVector<double> levels;levels.reserve(notes.size());
    for(const auto &note:notes){
        const int center=qRound((sourceStartSeconds+note.start*secondsPerBeat+.055)*source.sampleRate);
        const int radius=qRound(source.sampleRate*.045);const int begin=qMax(0,center-radius),end=qMin(source.samples.size(),center+radius);
        double sum=0;for(int i=begin;i<end;++i)sum+=double(source.samples[i])*source.samples[i];
        levels.append(std::log(qMax(1e-7,std::sqrt(sum/qMax(1,end-begin)))));
    }
    const double quiet=percentile(levels,.12),loud=percentile(levels,.90),span=qMax(.05,loud-quiet);
    for(int i=0;i<notes.size();++i){
        const double normalized=qBound(0.0,(levels[i]-quiet)/span,1.0);
        // Preserve audible accompaniment while allowing the original phrase
        // accents to shape the sampled piano instead of assigning every note 94.
        notes[i].velocity=qBound(54,qRound(58.0+58.0*std::sqrt(normalized)),116);
    }
}
}

int MusicDocument::inferKeySignature(const QVector<NoteEvent> &notes) {
    return inferKeySignatureImpl(notes);
}

QString MusicDocument::noteName(int midi, bool withOctave) {
    const int normalized = ((midi % 12) + 12) % 12;
    return QString::fromLatin1(kNames[normalized]) + (withOctave ? QString::number(midi / 12 - 1) : QString());
}

QVector<NoteEvent> MusicDocument::morningLight() {
    QVector<NoteEvent> n;
    const int melody[] = {60,62,64,67,69,67,64,62,60,64,67,72};
    const int bass[] = {48,48,45,43,48,45};
    for (int i=0;i<12;++i) n << NoteEvent{melody[i], double(i), i==11?2.0:0.82, 92, Hand::Right};
    for (int i=0;i<6;++i) n << NoteEvent{bass[i], double(i*2), 1.7, 76, Hand::Left};
    return sortNotes(n);
}

QVector<NoteEvent> MusicDocument::odeToJoy() {
    QVector<NoteEvent> n;
    const int melody[] = {64,64,65,67,67,65,64,62,60,60,62,64,64,62,62};
    for (int i=0;i<15;++i) n << NoteEvent{melody[i], double(i), i>12?1.4:0.82, 92, Hand::Right};
    for (int bar=0;bar<4;++bar) {
        const int root = (bar==2) ? 43 : 48;
        n << NoteEvent{root, double(bar*4), 3.6, 72, Hand::Left};
        n << NoteEvent{root+7, double(bar*4), 3.6, 66, Hand::Left};
    }
    return sortNotes(n);
}

QVector<NoteEvent> MusicDocument::scaleRoutine() {
    QVector<NoteEvent> n;
    const int rightScale[] = {0,2,4,5,7,9,11,12,11,9,7,5,4,2,0};
    // Contrary motion in C major does not mirror the semitone distances.
    // The left hand descends C-B-A-G-F-E-D-C, staying on the same natural
    // pitch collection as the ascending right hand.
    const int leftScale[] = {0,-1,-3,-5,-7,-8,-10,-12,-10,-8,-7,-5,-3,-1,0};
    for (int i=0;i<15;++i) {
        n << NoteEvent{60+rightScale[i], i*.5, .42, 88, Hand::Right};
        n << NoteEvent{48+leftScale[i], i*.5, .42, 76, Hand::Left};
    }
    return sortNotes(n);
}

QVector<NoteEvent> MusicDocument::chordRoutine(int variation) {
    const int progressions[][4] = {{0,5,7,0},{0,9,5,7},{9,5,0,7}};
    const int *roots = progressions[variation%3];
    QVector<NoteEvent> n;
    for (int i=0;i<4;++i) {
        const int qualityThird = (roots[i]==9) ? 3 : 4;
        const double beat = i*2.0;
        for (int p : {48+roots[i], 60+roots[i], 60+roots[i]+qualityThird, 67+roots[i]})
            n << NoteEvent{p, beat, 1.6, p<60?74:90, p<60?Hand::Left:Hand::Right};
    }
    return sortNotes(n);
}

bool MusicDocument::importFile(const QString &path, QVector<NoteEvent> &notes, QString &title, int &tempo, QString *error,QVector<TimeSignatureEvent>*timeSignatures,QVector<double>*repeatEnds,QVector<KeySignatureEvent>*keySignatures,QVector<StaffRestEvent>*rests) {
    const QString ext = QFileInfo(path).suffix().toLower();
    QByteArray data;
    if (ext=="mxl") {
        if (!readCompressedMusicXml(path,data,error)) return false;
    } else {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { setError(error, file.errorString()); return false; }
        data=file.readAll();
    }
    title = QFileInfo(path).completeBaseName();
    bool ok = false;
    if (ext=="mid" || ext=="midi") ok = importMidi(data, notes, tempo, error);
    else if (ext=="musicxml" || ext=="xml" || ext=="mxl") ok = importMusicXml(data, notes, tempo, title, error);
    else if (ext=="abc") ok = importAbc(data, notes, tempo, error);
    else if (ext=="json" || ext=="keyflow") ok = importJson(data, notes, tempo, title, error,timeSignatures,repeatEnds,keySignatures,rests);
    else { setError(error, "This score format is not supported for note-aware import."); return false; }
    if (ok) notes = sortNotes(notes);
    return ok;
}

bool MusicDocument::importMidi(const QByteArray &data, QVector<NoteEvent> &notes, int &tempo, QString *error) {
    const auto *p = reinterpret_cast<const uchar*>(data.constData()); const int size=data.size();
    if (size<14 || QByteArray(data.constData(),4)!="MThd") { setError(error,"Not a valid MIDI file."); return false; }
    const quint32 headerLen=be32(p+4); if (headerLen<6 || 8+int(headerLen)>size) { setError(error,"Damaged MIDI header."); return false; }
    const int tracks=be16(p+10), division=be16(p+12); if (division<=0 || division&0x8000) { setError(error,"SMPTE MIDI timing is not supported."); return false; }
    int pos=8+headerLen; struct Raw { int note, vel; qint64 start, end; }; QVector<Raw> raw;
    quint32 firstTempo=500000;
    for (int tr=0; tr<tracks && pos+8<=size; ++tr) {
        if (QByteArray(data.constData()+pos,4)!="MTrk") break;
        quint32 len=be32(p+pos+4); pos+=8; int end=qMin(size,pos+int(len)); qint64 tick=0; int running=0;
        std::map<int,QVector<QPair<qint64,int>>> active;
        auto readVlq=[&](quint32 &v){ v=0; for(int i=0;i<4 && pos<end;++i){ uchar c=p[pos++]; v=(v<<7)|(c&0x7f); if(!(c&0x80)) return true;} return false; };
        while(pos<end) {
            quint32 delta=0; if(!readVlq(delta)) break; tick+=delta; if(pos>=end) break;
            int status=p[pos++]; if(status<0x80){ --pos; status=running; } else if(status<0xf0) running=status;
            if(status==0xff) { if(pos>=end) break; int type=p[pos++]; quint32 l=0; if(!readVlq(l)||pos+int(l)>end) break; if(type==0x51&&l==3&&firstTempo==500000) firstTempo=(quint32(p[pos])<<16)|(quint32(p[pos+1])<<8)|p[pos+2]; pos+=l; }
            else if(status==0xf0 || status==0xf7) { quint32 l=0; if(!readVlq(l)) break; pos=qMin(end,pos+int(l)); }
            else {
                int type=status&0xf0; int need=(type==0xc0||type==0xd0)?1:2; if(pos+need>end) break; int d1=p[pos++], d2=need==2?p[pos++]:0;
                int key=((status&0x0f)<<8)|d1;
                if(type==0x90 && d2>0) active[key].append({tick,d2});
                else if(type==0x80 || (type==0x90&&d2==0)) { auto &stack=active[key]; if(!stack.isEmpty()){ auto start=stack.takeFirst(); raw<<Raw{d1,start.second,start.first,tick}; } }
            }
        }
        pos=end;
    }
    tempo=qBound(30, int(qRound(60000000.0/firstTempo)), 300);
    // Some downloadable piano MIDIs duplicate the same performance across
    // several channels. A piano key cannot sound as four independent unisons;
    // importing every copy makes attacks clip and engraves four stacked notes.
    // Merge only genuinely coincident copies, retaining the strongest velocity.
    std::sort(raw.begin(),raw.end(),[](const Raw &a,const Raw &b){if(a.start!=b.start)return a.start<b.start;if(a.note!=b.note)return a.note<b.note;return a.end<b.end;});
    QVector<Raw> unique;
    for(const auto &event:raw){
        if(!unique.isEmpty()&&unique.last().note==event.note&&qAbs(unique.last().start-event.start)<=2&&qAbs(unique.last().end-event.end)<=2)unique.last().vel=qMax(unique.last().vel,event.vel);
        else unique.append(event);
    }
    for(const auto &r:unique) if(r.end>r.start) notes<<NoteEvent{r.note,double(r.start)/division,qMax(.05,double(r.end-r.start)/division),r.vel,r.note<60?Hand::Left:Hand::Right};
    if(notes.isEmpty()) { setError(error,"No playable notes were found in this MIDI file."); return false; }
    return true;
}

bool MusicDocument::importMusicXml(const QByteArray &data, QVector<NoteEvent> &notes, int &tempo,
                                   QString &title, QString *error) {
    QDomDocument doc; QString msg; int line=0,col=0;
    if(!doc.setContent(data,&msg,&line,&col)){ setError(error,QString("MusicXML error at %1:%2 — %3").arg(line).arg(col).arg(msg)); return false; }
    auto parts=doc.elementsByTagName("part"); if(parts.isEmpty()){ setError(error,"No MusicXML part was found."); return false; }
    const QString xmlTitle=doc.documentElement().firstChildElement("work").firstChildElement("work-title").text().trimmed();
    const QString movementTitle=doc.documentElement().firstChildElement("movement-title").text().trimmed();
    if(!xmlTitle.isEmpty())title=xmlTitle;else if(!movementTitle.isEmpty())title=movementTitle;
    auto sounds=doc.elementsByTagName("sound");for(int i=0;i<sounds.size();++i){bool ok=false;double value=sounds.at(i).toElement().attribute("tempo").toDouble(&ok);if(ok&&value>=20&&value<=400){tempo=qRound(value);break;}}
    QHash<QString,int> openTies;
    for(int pi=0;pi<parts.size();++pi){ QDomElement part=parts.at(pi).toElement(); double partTime=0; int divisions=1;
        for(QDomNode m=part.firstChild();!m.isNull();m=m.nextSibling()){
            QDomElement measure=m.toElement(); if(measure.tagName()!="measure") continue; double cursor=partTime,lastStart=cursor,maxCursor=cursor;
            for(QDomNode n=measure.firstChild();!n.isNull();n=n.nextSibling()){
                QDomElement e=n.toElement();
                if(e.tagName()=="attributes"){ auto d=e.elementsByTagName("divisions"); if(!d.isEmpty()) divisions=qMax(1,d.at(0).toElement().text().toInt()); }
                else if(e.tagName()=="backup") cursor-=e.firstChildElement("duration").text().toDouble()/divisions;
                else if(e.tagName()=="forward"){cursor+=e.firstChildElement("duration").text().toDouble()/divisions;maxCursor=qMax(maxCursor,cursor);}
                else if(e.tagName()=="note"){
                    double dur=e.firstChildElement("duration").text().toDouble()/divisions; bool chord=!e.firstChildElement("chord").isNull(); bool rest=!e.firstChildElement("rest").isNull();
                    double start=chord?lastStart:cursor; if(!rest){ auto pitch=e.firstChildElement("pitch"); int midi=(pitch.firstChildElement("octave").text().toInt()+1)*12+pitchStep(pitch.firstChildElement("step").text())+pitch.firstChildElement("alter").text().toInt(); int staff=qMax(1,e.firstChildElement("staff").text().toInt());QString voice=e.firstChildElement("voice").text();if(voice.isEmpty())voice="1";QString tieKey=QString("%1:%2:%3:%4").arg(pi).arg(staff).arg(voice).arg(midi);bool tieStart=false,tieStop=false;for(QDomElement tie=e.firstChildElement("tie");!tie.isNull();tie=tie.nextSiblingElement("tie")){tieStart|=tie.attribute("type")=="start";tieStop|=tie.attribute("type")=="stop";}if(tieStop&&openTies.contains(tieKey)){int index=openTies.value(tieKey);notes[index].duration=qMax(notes[index].duration,start+dur-notes[index].start);if(!tieStart)openTies.remove(tieKey);}else{notes<<NoteEvent{midi,start,qMax(.05,dur),90,(staff==2||midi<60)?Hand::Left:Hand::Right};if(tieStart)openTies[tieKey]=notes.size()-1;} }
                    if(!chord){ lastStart=cursor; cursor+=dur;maxCursor=qMax(maxCursor,cursor); }
                }
            }
            partTime=maxCursor;
        }
    }
    if(notes.isEmpty()){ setError(error,"No pitched notes were found in this MusicXML file."); return false; }
    return true;
}

bool MusicDocument::importAbc(const QByteArray &data, QVector<NoteEvent> &notes, int &tempo, QString *error) {
    QString text=QString::fromUtf8(data); auto q=QRegularExpression("(?m)^Q:.*?([0-9]{2,3})").match(text); if(q.hasMatch()) tempo=qBound(30,q.captured(1).toInt(),300);
    QString body; for(const auto &line:text.split('\n')) if(!QRegularExpression("^[A-Za-z]:").match(line.trimmed()).hasMatch()) body+=line+" ";
    QRegularExpression rx("([_^^=]?)([A-Ga-gz])([,']*)([0-9]*)(?:/([0-9]+))?"); auto it=rx.globalMatch(body); double beat=0;
    while(it.hasNext()){ auto m=it.next(); QString letter=m.captured(2); double len=m.captured(4).isEmpty()?1:m.captured(4).toDouble(); if(!m.captured(5).isEmpty())len/=m.captured(5).toDouble(); if(letter=="z"){beat+=len;continue;} bool lower=letter[0].isLower(); int midi=60+pitchStep(letter.toUpper())+(lower?12:0); for(QChar c:m.captured(3)) midi+=(c=='\''?12:-12); if(m.captured(1)=="^")midi++; else if(m.captured(1)=="_")midi--; notes<<NoteEvent{midi,beat,qMax(.05,len*.88),90,midi<60?Hand::Left:Hand::Right}; beat+=len; }
    if(notes.isEmpty()){ setError(error,"No ABC notes were found."); return false; } return true;
}

bool MusicDocument::importJson(const QByteArray &data, QVector<NoteEvent> &notes, int &tempo, QString &title, QString *error,QVector<TimeSignatureEvent>*timeSignatures,QVector<double>*repeatEnds,QVector<KeySignatureEvent>*keySignatures,QVector<StaffRestEvent>*rests) {
    auto doc=QJsonDocument::fromJson(data); if(!doc.isObject()){setError(error,"Invalid Keyflow project.");return false;} auto o=doc.object(); tempo=o.value("tempo").toInt(tempo); title=o.value("title").toString(title);
    for(auto v:o.value("notes").toArray()){auto n=v.toObject();notes<<NoteEvent{n.value("note").toInt(),n.value("start").toDouble(),n.value("duration").toDouble(1),n.value("velocity").toInt(90),n.value("hand").toString()=="left"?Hand::Left:Hand::Right,AccidentalMark(qBound(0,n.value("accidental").toInt(0),3))};}
    if(timeSignatures){timeSignatures->clear();for(const auto &value:o.value("timeSignatures").toArray()){const auto event=value.toObject();timeSignatures->append({event.value("beat").toDouble(),event.value("beats").toInt(4),event.value("beatType").toInt(4)});}}
    if(keySignatures){keySignatures->clear();for(const auto &value:o.value("keySignatures").toArray()){const auto event=value.toObject();keySignatures->append({event.value("beat").toDouble(),qBound(-7,event.value("fifths").toInt(),7)});}}
    if(repeatEnds){repeatEnds->clear();for(const auto &value:o.value("repeatEnds").toArray())repeatEnds->append(value.toDouble());}
    if(rests){rests->clear();for(const auto &value:o.value("rests").toArray()){const auto event=value.toObject();rests->append({event.value("beat").toDouble(),event.value("duration").toDouble(1),event.value("hand").toString()=="left"?Hand::Left:Hand::Right});}}
    if(notes.isEmpty()){setError(error,"The project contains no notes.");return false;}return true;
}

bool MusicDocument::saveMidi(const QString &path, const QVector<NoteEvent> &notes, int tempo, QString *error) {
    struct E{qint64 tick;int order;QByteArray bytes;}; QVector<E> ev; const int ppq=480;
    QByteArray tempoBytes; tempoBytes.append(char(0xff));tempoBytes.append(char(0x51));tempoBytes.append(char(3));int us=60000000/qMax(1,tempo);tempoBytes.append(char(us>>16));tempoBytes.append(char(us>>8));tempoBytes.append(char(us));ev<<E{0,0,tempoBytes};
    for(const auto &n:notes){int tick=qRound64(n.start*ppq),end=qRound64((n.start+n.duration)*ppq);QByteArray on;on.append(char(0x90));on.append(char(n.note));on.append(char(n.velocity));QByteArray off;off.append(char(0x80));off.append(char(n.note));off.append(char(0));ev<<E{tick,2,on};ev<<E{end,1,off};}
    std::sort(ev.begin(),ev.end(),[](auto&a,auto&b){return a.tick==b.tick?a.order<b.order:a.tick<b.tick;}); QByteArray track; qint64 prev=0; for(const auto&e:ev){track+=varLen(e.tick-prev);track+=e.bytes;prev=e.tick;}track+=QByteArray{"\0\xff\x2f\0",4};
    QByteArray out("MThd",4);putBe32(out,6);putBe16(out,0);putBe16(out,1);putBe16(out,ppq);out+="MTrk";putBe32(out,track.size());out+=track;return writeAll(path,out,error);
}

bool MusicDocument::saveMusicXml(const QString &path,const QVector<NoteEvent>&source,int tempo,const QString&title,QString*error){
    auto notes=sortNotes(source);QString x="<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">\n<score-partwise version=\"4.0\"><work><work-title>"+xmlEscape(title)+"</work-title></work><part-list><score-part id=\"P1\"><part-name>Piano</part-name></score-part></part-list><part id=\"P1\"><measure number=\"1\"><attributes><divisions>480</divisions><key><fifths>0</fifths></key><time><beats>4</beats><beat-type>4</beat-type></time><staves>2</staves><clef number=\"1\"><sign>G</sign><line>2</line></clef><clef number=\"2\"><sign>F</sign><line>4</line></clef></attributes><direction><sound tempo=\""+QString::number(tempo)+"\"/></direction>";
    double cursor=0,last=-99;for(const auto&n:notes){if(n.start>cursor+.001){x+="<note><rest/><duration>"+QString::number(qRound((n.start-cursor)*480))+"</duration></note>";cursor=n.start;}int pc=n.note%12;QString step=QString::fromLatin1(kNames[pc]).left(1);bool sharp=QString::fromLatin1(kNames[pc]).contains('#');x+="<note>"+(qAbs(n.start-last)<.001?QString("<chord/>"):QString())+"<pitch><step>"+step+"</step>"+(sharp?QString("<alter>1</alter>"):QString())+"<octave>"+QString::number(n.note/12-1)+"</octave></pitch><duration>"+QString::number(qRound(n.duration*480))+"</duration><staff>"+QString::number(n.hand==Hand::Left?2:1)+"</staff></note>";if(qAbs(n.start-last)>.001)cursor=n.start+n.duration;else cursor=qMax(cursor,n.start+n.duration);last=n.start;}x+="</measure></part></score-partwise>";return writeAll(path,x.toUtf8(),error);
}

bool MusicDocument::saveEngravedScore(const QString &musicXmlPath,const QString &pdfPath,
                                      const QVector<NoteEvent> &notes,int tempo,
                                      const QString &title,QString *error,
                                      const QVector<TimeSignatureEvent> &timeSignatures,
                                      const QVector<double> &repeatEnds,
                                      const QVector<KeySignatureEvent> &keySignatures,
                                      const QVector<StaffRestEvent> &rests){
    QTemporaryDir temp;
    if(!temp.isValid()){setError(error,"Could not create the score workspace.");return false;}
    const QString project=temp.filePath("score.keyflow");
    if(!saveJson(project,notes,tempo,title,error,timeSignatures,repeatEnds,keySignatures,rests))return false;
    const QString helper=QCoreApplication::applicationDirPath()+"/keyflow_score.py";
    if(!QFileInfo::exists(helper)){setError(error,"The detailed score engraver is missing from the build folder.");return false;}
    const QString pythonRuntime=QCoreApplication::applicationDirPath()+"/python/verovio";
    if(!QFileInfo::exists(pythonRuntime)){setError(error,"The bundled Verovio score engine is missing. Rebuild Keyflow so the complete video-to-score runtime is copied beside the app.");return false;}
    for(const QString &program:{QStringLiteral("python3"),QStringLiteral("rsvg-convert"),QStringLiteral("pdfunite"),QStringLiteral("pdfattach")})if(QStandardPaths::findExecutable(program).isEmpty()){setError(error,program+" is required to create the engraved PDF but is not installed.");return false;}
    QStringList arguments{helper,"--project",project,"--musicxml",musicXmlPath};
    if(!pdfPath.isEmpty())arguments<<QStringLiteral("--pdf")<<pdfPath;
    QProcess process;process.setProcessChannelMode(QProcess::SeparateChannels);process.start("python3",arguments);
    if(!process.waitForFinished(180000)||process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0){
        QString details=QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if(details.isEmpty())details="The detailed score engraver did not finish.";
        setError(error,details);return false;
    }
    if(!QFileInfo::exists(musicXmlPath)||(!pdfPath.isEmpty()&&!QFileInfo::exists(pdfPath))){
        setError(error,"The detailed score files were not created.");return false;
    }
    return true;
}

bool MusicDocument::saveAbc(const QString&path,const QVector<NoteEvent>&source,int tempo,const QString&title,QString*error){auto notes=sortNotes(source);QString s="X:1\nT:"+title+"\nM:4/4\nL:1/4\nQ:1/4="+QString::number(tempo)+"\nK:C\n";int i=0;while(i<notes.size()){double start=notes[i].start;QVector<NoteEvent>g;while(i<notes.size()&&qAbs(notes[i].start-start)<.001)g<<notes[i++];if(g.size()>1)s+='[';for(const auto&n:g){int pc=n.note%12;QString name=QString::fromLatin1(kNames[pc]).replace("#","^");if(n.note>=72)name=name.toLower()+QString(n.note/12-6,'\'');else if(n.note<60)name+=QString(5-n.note/12,',');s+=name;}if(g.size()>1)s+=']';double d=g[0].duration;if(d<.75)s+="/2";else if(d>1.5)s+='2';s+=' ';}return writeAll(path,s.toUtf8(),error);}

bool MusicDocument::saveJson(const QString&path,const QVector<NoteEvent>&notes,int tempo,const QString&title,QString*error,const QVector<TimeSignatureEvent>&timeSignatures,const QVector<double>&repeatEnds,const QVector<KeySignatureEvent>&keySignatures,const QVector<StaffRestEvent>&rests){QJsonObject o{{"format","keyflow-project"},{"version",2},{"title",title},{"tempo",tempo}};QJsonArray a;for(const auto&n:notes)a.append(QJsonObject{{"note",n.note},{"start",n.start},{"duration",n.duration},{"velocity",n.velocity},{"hand",n.hand==Hand::Left?"left":"right"},{"accidental",int(n.accidental)}});o["notes"]=a;QJsonArray meters;for(const auto&m:timeSignatures)meters.append(QJsonObject{{"beat",m.beat},{"beats",m.beats},{"beatType",m.beatType}});o["timeSignatures"]=meters;QJsonArray keys;for(const auto&k:keySignatures)keys.append(QJsonObject{{"beat",k.beat},{"fifths",k.fifths}});o["keySignatures"]=keys;QJsonArray repeats;for(double beat:repeatEnds)repeats.append(beat);o["repeatEnds"]=repeats;QJsonArray restArray;for(const auto&r:rests)restArray.append(QJsonObject{{"beat",r.beat},{"duration",r.duration},{"hand",r.hand==Hand::Left?"left":"right"}});o["rests"]=restArray;return writeAll(path,QJsonDocument(o).toJson(QJsonDocument::Indented),error);}
bool MusicDocument::saveCsv(const QString&path,const QVector<NoteEvent>&notes,QString*error){QByteArray b="note,name,start_beats,duration_beats,velocity,hand\n";for(const auto&n:notes)b+=QByteArray::number(n.note)+','+noteName(n.note).toUtf8()+','+QByteArray::number(n.start,'f',3)+','+QByteArray::number(n.duration,'f',3)+','+QByteArray::number(n.velocity)+','+(n.hand==Hand::Left?"left":"right")+'\n';return writeAll(path,b,error);}

bool MusicDocument::saveWav(const QString&path,const QVector<NoteEvent>&notes,int tempo,QString*error){const int sr=44100;double spb=60.0/qMax(1,tempo),length=1;for(const auto&n:notes)length=qMax(length,(n.start+n.duration)*spb+1);int frames=int(length*sr);QVector<float>mix(frames);for(const auto&n:notes){int a=int(n.start*spb*sr),z=qMin(frames,int((n.start+n.duration)*spb*sr));double phase=0,inc=2*M_PI*440.0*qPow(2.0,(n.note-69)/12.0)/sr,amp=.14*n.velocity/127.0;for(int i=a;i<z;++i){double t=(i-a)/double(sr),remain=(z-i)/double(sr);double env=qMin(1.0,t/.01)*qMin(1.0,remain/.08)*qExp(-t*.35);mix[i]+=float((qSin(phase)+.22*qSin(phase*2))*amp*env);phase+=inc;}}QByteArray pcm;pcm.reserve(frames*4);for(float f:mix){qint16 v=qBound(-32767,int(f*32767),32767);putLe16(pcm,v);putLe16(pcm,v);}QByteArray out="RIFF";putLe32(out,36+pcm.size());out+="WAVEfmt ";putLe32(out,16);putLe16(out,1);putLe16(out,2);putLe32(out,sr);putLe32(out,sr*4);putLe16(out,4);putLe16(out,16);out+="data";putLe32(out,pcm.size());out+=pcm;return writeAll(path,out,error);}

bool MusicDocument::detectTempo(const QString &path,int &tempo,double *confidence,QString *error,
                                std::function<void(int)> progress,std::function<bool()> canceled){
    DecodedAudio audio;OnsetAnalysis onsets;
    if(progress)progress(0);
    if(!decodeMonoAudio(path,audio,error,canceled))return false;
    if(!buildOnsetAnalysis(audio,onsets,error,progress,canceled))return false;
    const TempoEstimate estimate=estimateTempoFromOnsets(onsets);
    if(!estimate.bpm){setError(error,"The soundtrack did not contain enough steady musical attacks to estimate its tempo.");return false;}
    tempo=estimate.bpm;if(confidence)*confidence=estimate.confidence;return true;
}

bool MusicDocument::transcribeMedia(const QString &path,int tempo,QVector<NoteEvent> &notes,
                                    QString *error,std::function<void(int)> progress,
                                    std::function<bool()> canceled,TranscriptionInfo *info){
    notes.clear();DecodedAudio audio;
    if(!decodeMonoAudio(path,audio,error,canceled))return false;
    TempoEstimate tempoEstimate;const bool automaticTempo=tempo<=0;
    if(automaticTempo){
        OnsetAnalysis onsets;QString tempoError;
        auto tempoProgress=[&](int value){if(progress)progress(value*18/100);};
        if(buildOnsetAnalysis(audio,onsets,&tempoError,tempoProgress,canceled))tempoEstimate=estimateTempoFromOnsets(onsets);
    }
    const int resolvedTempo=automaticTempo?(tempoEstimate.bpm?tempoEstimate.bpm:96):qBound(30,tempo,260);
    if(info){info->tempo=resolvedTempo;info->tempoEstimated=automaticTempo&&tempoEstimate.bpm>0;info->tempoConfidence=automaticTempo?tempoEstimate.confidence:1.0;info->audiovisualAgreement=-1;info->renderedAgreement=-1;info->audioOffsetSeconds=0;info->sourceStartSeconds=0;info->refinementPasses=0;}
    const int sampleRate=audio.sampleRate;
    constexpr int frameSize=8192,hop=512,lowestMidi=21,highestMidi=108,pitchCount=88;
    const int sampleCount=audio.samples.size();
    if(sampleCount<frameSize){setError(error,"The audio is too short to identify stable piano notes.");return false;}
    const int totalFrames=1+(sampleCount-frameSize)/hop;

    QVector<float> window(frameSize),magnitudes(frameSize/2+1);
    for(int i=0;i<frameSize;++i)window[i]=float(.5-.5*std::cos(2.0*M_PI*i/(frameSize-1)));
    cv::Mat timeDomain(frameSize,1,CV_32F),spectrum;
    struct Lane{bool active=false;int hits=0,misses=0,candidateFrame=0,startFrame=0;float lastStrength=0,peakStrength=0;};
    struct Gate{int midi,startFrame,endFrame;float peakStrength;};
    QVector<Lane> lanes(pitchCount);QVector<Gate> gates;

    auto frequency=[](int midi){return 440.0*std::pow(2.0,(midi-69)/12.0);};
    auto bandPeak=[&](double hz){
        const int center=qRound(hz*frameSize/sampleRate);float peak=0;
        for(int bin=qMax(1,center-1);bin<=qMin(frameSize/2,center+1);++bin)peak=qMax(peak,magnitudes[bin]);
        return peak;
    };
    for(int frameIndex=0;frameIndex<totalFrames;++frameIndex){
        if(canceled&&canceled()){setError(error,"Transcription canceled.");return false;}
        const int offset=frameIndex*hop;double squared=0;
        for(int i=0;i<frameSize;++i){const float value=audio.samples[offset+i];squared+=value*value;timeDomain.at<float>(i)=value*window[i];}
        const double rms=std::sqrt(squared/frameSize);
        cv::dft(timeDomain,spectrum,cv::DFT_COMPLEX_OUTPUT);
        for(int bin=0;bin<=frameSize/2;++bin){const cv::Vec2f value=spectrum.at<cv::Vec2f>(bin);magnitudes[bin]=4.0f*std::hypot(value[0],value[1])/frameSize;}

        QVector<float> fundamentals(pitchCount),strengths(pitchCount),sortedFundamentals;
        float strongest=0;
        for(int pitch=0;pitch<pitchCount;++pitch){const double hz=frequency(lowestMidi+pitch);fundamentals[pitch]=bandPeak(hz);strongest=qMax(strongest,fundamentals[pitch]);}
        sortedFundamentals=fundamentals;std::sort(sortedFundamentals.begin(),sortedFundamentals.end());
        const float spectralFloor=sortedFundamentals[sortedFundamentals.size()/2];
        const float threshold=qMax(.0008f,qMax(spectralFloor*4.5f,strongest*.045f));
        QVector<QPair<int,float>> candidates;
        if(rms>.0012&&strongest>threshold){
            for(int pitch=0;pitch<pitchCount;++pitch){
                const float fundamental=fundamentals[pitch];
                const float left=pitch?fundamentals[pitch-1]:0,right=pitch+1<pitchCount?fundamentals[pitch+1]:0;
                if(fundamental<threshold||fundamental<qMax(left,right)*.94f)continue;
                bool harmonicGhost=false;
                for(const auto relation:QVector<QPair<int,float>>{{12,.58f},{19,.38f},{24,.28f},{28,.24f},{31,.20f},{34,.18f},{36,.16f}}){
                    float lowerStrength=0;
                    for(int detune=-1;detune<=1;++detune){const int lower=pitch-relation.first+detune;if(lower>=0&&lower<pitchCount)lowerStrength=qMax(lowerStrength,fundamentals[lower]);}
                    if(lowerStrength>threshold&&fundamental<lowerStrength*relation.second){harmonicGhost=true;break;}
                }
                if(harmonicGhost)continue;
                const double hz=frequency(lowestMidi+pitch);
                float strength=fundamental;
                for(int harmonic=2;harmonic<=6&&hz*harmonic<sampleRate*.48;++harmonic)strength+=bandPeak(hz*harmonic)*(0.24f/harmonic);
                strengths[pitch]=strength;candidates.append({pitch,strength});
            }
            std::sort(candidates.begin(),candidates.end(),[](const auto &a,const auto &b){return a.second>b.second;});
            QVector<QPair<int,float>> separated;
            for(const auto &candidate:candidates){bool neighbor=false;for(const auto &kept:separated)if(qAbs(candidate.first-kept.first)<=1){neighbor=true;break;}if(!neighbor)separated.append(candidate);if(separated.size()>=10)break;}
            candidates=separated;
        }
        QSet<int> activePitches;for(const auto &candidate:candidates)activePitches.insert(candidate.first);
        for(int pitch=0;pitch<pitchCount;++pitch){
            auto &lane=lanes[pitch];const bool present=activePitches.contains(pitch);const float strength=strengths[pitch];
            if(present){
                lane.misses=0;
                if(lane.active){
                    const bool reattack=frameIndex-lane.startFrame>5&&lane.lastStrength>0&&strength>lane.lastStrength*1.85f&&strength>threshold*2.5f;
                    if(reattack){if(frameIndex-lane.startFrame>=3)gates.append({lowestMidi+pitch,lane.startFrame,frameIndex,lane.peakStrength});lane.startFrame=frameIndex;lane.peakStrength=strength;lane.lastStrength=strength;}
                    else{lane.peakStrength=qMax(lane.peakStrength,strength);lane.lastStrength=lane.lastStrength*.72f+strength*.28f;}
                }else{
                    if(lane.hits==0)lane.candidateFrame=frameIndex;
                    if(++lane.hits>=2){lane.active=true;lane.startFrame=lane.candidateFrame;lane.peakStrength=strength;lane.lastStrength=strength;}
                }
            }else if(lane.active){
                if(++lane.misses>4){const int endFrame=frameIndex-lane.misses+1;if(endFrame-lane.startFrame>=3)gates.append({lowestMidi+pitch,lane.startFrame,endFrame,lane.peakStrength});lane=Lane{};}
            }else lane.hits=0;
        }
        if(progress&&frameIndex%20==0)progress((automaticTempo?18:0)+frameIndex*(automaticTempo?82:100)/totalFrames);
    }
    for(int pitch=0;pitch<pitchCount;++pitch)if(lanes[pitch].active&&totalFrames-lanes[pitch].startFrame>=3)gates.append({lowestMidi+pitch,lanes[pitch].startFrame,totalFrames,lanes[pitch].peakStrength});
    if(gates.isEmpty()){setError(error,"No stable piano notes were found in the sound. Try a cleaner solo-piano section or use falling-notes mode.");return false;}

    std::sort(gates.begin(),gates.end(),[](const Gate &a,const Gate &b){return a.startFrame==b.startFrame?a.midi<b.midi:a.startFrame<b.startFrame;});
    const double secondsPerFrame=double(hop)/sampleRate,secondsPerBeat=60.0/resolvedTempo;
    const double firstSecond=gates.first().startFrame*secondsPerFrame;
    float loudestGate=0;for(const auto &gate:gates)loudestGate=qMax(loudestGate,gate.peakStrength);
    for(const auto &gate:gates){
        const double startSecond=gate.startFrame*secondsPerFrame-firstSecond;
        const double durationSecond=qMax(secondsPerFrame,(gate.endFrame-gate.startFrame)*secondsPerFrame);
        constexpr double ticksPerBeat=24.0; // preserves eighth-note triplets
        const int startTick=qMax(0,qRound(startSecond/secondsPerBeat*ticksPerBeat));
        const int durationTick=qMax(1,qRound(durationSecond/secondsPerBeat*ticksPerBeat));
        const int velocity=qBound(48,qRound(58+55*std::sqrt(gate.peakStrength/qMax(.0001f,loudestGate))),116);
        notes.append({gate.midi,startTick/ticksPerBeat,durationTick/ticksPerBeat,velocity,gate.midi<60?Hand::Left:Hand::Right});
    }
    // Merge threshold flicker without merging genuine repeated attacks.
    std::sort(notes.begin(),notes.end(),[](const NoteEvent &a,const NoteEvent &b){return a.note==b.note?a.start<b.start:a.note<b.note;});QVector<NoteEvent>cleaned;
    for(const auto &note:notes){
        if(!cleaned.isEmpty()&&cleaned.last().note==note.note&&note.start<=cleaned.last().start+cleaned.last().duration+.01){cleaned.last().duration=qMax(cleaned.last().duration,note.start+note.duration-cleaned.last().start);cleaned.last().velocity=qMax(cleaned.last().velocity,note.velocity);}
        else cleaned.append(note);
    }
    notes=sortNotes(cleaned);
    if(info)info->sourceStartSeconds=firstSecond;
    if(progress)progress(100);
    if(notes.isEmpty()){setError(error,"No stable piano notes were found in the sound.");return false;}
    return true;
}

namespace {
bool transcribeDetailedPianoRoll(const QString &path,int tempo,QVector<NoteEvent> &notes,
                                 QString *error,const std::function<void(int)> &progress,
                                 const std::function<bool()> &canceled,TranscriptionInfo *info){
    QProcess probe;
    probe.start("ffprobe",{"-v","error","-select_streams","v:0","-show_entries",
                           "stream=width,height,r_frame_rate:format=duration","-of","json",path});
    if(!probe.waitForFinished(10000)||probe.exitCode()!=0){setError(error,"FFmpeg could not inspect this video.");return false;}
    const QJsonObject root=QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
    const QJsonArray streams=root.value("streams").toArray();
    if(streams.isEmpty()){setError(error,"No video stream was found.");return false;}
    const QJsonObject stream=streams.first().toObject();
    const int width=stream.value("width").toInt(),height=stream.value("height").toInt();
    const double durationSeconds=root.value("format").toObject().value("duration").toString().toDouble();
    const QStringList rate=stream.value("r_frame_rate").toString().split('/');
    double sourceFps=rate.value(0).toDouble()/qMax(1.0,rate.value(1).toDouble());
    if(sourceFps<5||sourceFps>240)sourceFps=60;
    const double fps=qMin(60.0,sourceFps);
    if(width<320||height<240||durationSeconds<=0){setError(error,"The video timing or resolution is not suitable for falling-note recognition.");return false;}

    // Tutorials often show titles or warnings before the piano appears. Probe
    // several moments and accept the first frame that has both a strong
    // horizontal strike edge and the repeating light/dark pattern of piano
    // keys below it. The earlier one-frame probe at five seconds incorrectly
    // rejected otherwise clear tutorials whose keyboard entered later.
    const qsizetype fullFrameBytes=qsizetype(width)*height*3;
    QVector<double> referenceTimes;
    const double lastProbe=qMin(durationSeconds-.1,qMin(48.0,durationSeconds*.45));
    for(double second=1.5;second<=lastProbe;second+=2.5)referenceTimes.append(second);
    for(double fraction:{.55,.70}){const double second=qMin(durationSeconds-.1,durationSeconds*fraction);if(second>0)referenceTimes.append(second);}
    std::sort(referenceTimes.begin(),referenceTimes.end());
    referenceTimes.erase(std::unique(referenceTimes.begin(),referenceTimes.end(),[](double a,double b){return qAbs(a-b)<.2;}),referenceTimes.end());
    const QVector<double> strikeDistances{.040,.055,.080,.105,.130,.160,.200,.240,.280};
    struct KeyboardProbe{int y;double score,second;QVector<int> strikeRuns;};
    QVector<KeyboardProbe> keyboardProbes;
    for(double candidateSecond:referenceTimes){
        QProcess referenceDecoder;
        referenceDecoder.start("ffmpeg",{"-v","error","-ss",QString::number(candidateSecond,'f',3),
                                         "-i",path,"-frames:v","1","-f","rawvideo","-pix_fmt","bgr24","pipe:1"});
        if(!referenceDecoder.waitForFinished(30000)||referenceDecoder.exitCode()!=0)continue;
        const QByteArray referenceBytes=referenceDecoder.readAllStandardOutput();
        if(referenceBytes.size()<fullFrameBytes)continue;
        cv::Mat reference(height,width,CV_8UC3,const_cast<char*>(referenceBytes.constData()));
        cv::Mat grayReference,gradient;
        cv::cvtColor(reference,grayReference,cv::COLOR_BGR2GRAY);
        cv::Sobel(grayReference,gradient,CV_32F,0,1,3);
        int frameKeyboardTop=-1;double frameScore=-1;
        for(int y=int(height*.24);y<int(height*.88);++y){
            const double edge=cv::mean(gradient.row(y))[0];
            if(edge<3)continue;
            const int sampleY=qMin(height-1,y+qMax(6,qRound(height*.085)));
            cv::Scalar rowMean,rowDeviation;cv::meanStdDev(grayReference.row(sampleY),rowMean,rowDeviation);
            if(rowDeviation[0]<4)continue;
            cv::Mat keyPattern;cv::threshold(grayReference.row(sampleY),keyPattern,0,255,cv::THRESH_BINARY|cv::THRESH_OTSU);
            const uchar *row=keyPattern.ptr<uchar>(0);
            int bright=0,transitions=0;bool previous=row[0]!=0;
            for(int x=0;x<width;++x){const bool light=row[x]!=0;if(light)++bright;if(x&&light!=previous)++transitions;previous=light;}
            const double brightFraction=double(bright)/width;
            if(brightFraction<.32||transitions<24)continue;
            const double score=edge+brightFraction*38.0+qMin(transitions,120)*.18;
            if(score>frameScore){frameScore=score;frameKeyboardTop=y;}
        }
        if(frameKeyboardTop>=0){
            QVector<int> strikeRuns;const double laneWidth=width/52.0;
            for(double distance:strikeDistances){
                const int scanHeight=qBound(4,qRound(height*8.0/720.0),16);
                const int scanY=qBound(0,frameKeyboardTop-qRound(height*distance),height-scanHeight);
                const cv::Mat band=reference.rowRange(scanY,scanY+scanHeight);
                cv::Mat gray,hsv,saturation,value,grayMean,saturationMean,valueMean;
                cv::cvtColor(band,gray,cv::COLOR_BGR2GRAY);cv::cvtColor(band,hsv,cv::COLOR_BGR2HSV);
                cv::extractChannel(hsv,saturation,1);cv::extractChannel(hsv,value,2);
                cv::reduce(gray,grayMean,0,cv::REDUCE_AVG,CV_32F);cv::reduce(saturation,saturationMean,0,cv::REDUCE_AVG,CV_32F);cv::reduce(value,valueMean,0,cv::REDUCE_AVG,CV_32F);
                QVector<bool> mask(width,false);
                for(int x=0;x<width;++x){const float light=grayMean.at<float>(0,x),sat=saturationMean.at<float>(0,x),val=valueMean.at<float>(0,x);mask[x]=light>=185||(val>=155&&sat>=65);}
                int runs=0;
                for(int x=0;x<width;){while(x<width&&!mask[x])++x;const int begin=x;while(x<width&&mask[x])++x;const int runWidth=x-begin;if(runWidth>=laneWidth*.28&&runWidth<=laneWidth*1.65)++runs;}
                strikeRuns.append(runs);
            }
            keyboardProbes.append({frameKeyboardTop,frameScore,candidateSecond,strikeRuns});
        }
    }
    int keyboardTop=-1,bestClusterCount=0;double bestKeyboardScore=-1,referenceSecond=0,geometrySecond=0;
    const int clusterTolerance=qMax(4,qRound(height*.025));
    for(const auto &probe:keyboardProbes){
        int count=0;double score=0,weightedY=0,earliest=durationSeconds;QVector<double> clusterTimes;
        for(const auto &other:keyboardProbes)if(qAbs(other.y-probe.y)<=clusterTolerance){++count;score+=other.score;weightedY+=other.y*other.score;earliest=qMin(earliest,other.second);clusterTimes.append(other.second);}
        if(count>bestClusterCount||(count==bestClusterCount&&score>bestKeyboardScore)){std::sort(clusterTimes.begin(),clusterTimes.end());bestClusterCount=count;bestKeyboardScore=score;keyboardTop=qRound(weightedY/qMax(1.0,score));referenceSecond=earliest;geometrySecond=clusterTimes.value(clusterTimes.size()/2,earliest);}
    }
    const int requiredRepeats=durationSeconds>=12?2:1;
    if(keyboardTop<0||bestClusterCount<requiredRepeats||bestKeyboardScore/qMax(1,bestClusterCount)<12){setError(error,"A stable piano keyboard could not be located in the video frames that were checked.");return false;}
    QVector<int> strikeScores(strikeDistances.size()),strikeFrames(strikeDistances.size());
    for(const auto &probe:keyboardProbes)if(qAbs(probe.y-keyboardTop)<=clusterTolerance)for(int i=0;i<probe.strikeRuns.size();++i){strikeScores[i]+=probe.strikeRuns[i];if(probe.strikeRuns[i]>0)++strikeFrames[i];}
    const double targetStrikeDistance=qBound(.13,.16+(keyboardTop/double(height)-.45)*.23,.24);
    int strongestReference=0;for(int i=1;i<strikeScores.size();++i)if(strikeScores[i]>strikeScores[strongestReference])strongestReference=i;
    // Test several lines in one video pass. Synthesia-style videos place the
    // playable strike just above the keyboard (about .04 H in Moonlight), while
    // other tutorials have particles there and need a higher clean row. The old
    // geometry-only formula picked .24 H for Moonlight and missed most triplets.
    QVector<double> candidateDistances{.010,.020,.040,.055,.080,targetStrikeDistance,strikeDistances[strongestReference]};
    std::sort(candidateDistances.begin(),candidateDistances.end());
    candidateDistances.erase(std::unique(candidateDistances.begin(),candidateDistances.end(),[](double a,double b){return qAbs(a-b)<.009;}),candidateDistances.end());
    const int probeHeight=qBound(4,qRound(height*8.0/720.0),16);

    auto isBlack=[](int midi){const int pc=((midi%12)+12)%12;return pc==1||pc==3||pc==6||pc==8||pc==10;};
    QVector<double> centers(109,std::numeric_limits<double>::quiet_NaN());
    double whiteWidth=width/52.0;
    int visibleLow=21,visibleHigh=108,visibleWhiteCount=52,keyboardBottom=height;

    // Calibrate the keyboard that is actually visible.  Some tutorials show
    // 49 or 61 keys stretched across the whole frame; treating those pixels as
    // an 88-key piano shifts every pitch and can turn a clean melody into a
    // wall of ledger lines.  The lower part of a piano contains one repeating
    // vertical separator per white key.  Its period gives the visible key
    // count, while the two/three black-key pattern identifies every C.
    QProcess geometryDecoder;
    geometryDecoder.start("ffmpeg",{"-v","error","-ss",QString::number(geometrySecond,'f',3),
                                     "-i",path,"-frames:v","1","-f","rawvideo","-pix_fmt","bgr24","pipe:1"});
    if(geometryDecoder.waitForFinished(30000)&&geometryDecoder.exitCode()==0){
        const QByteArray bytes=geometryDecoder.readAllStandardOutput();
        if(bytes.size()>=fullFrameBytes){
            cv::Mat frame(height,width,CV_8UC3,const_cast<char*>(bytes.constData()));
            cv::Mat gray;cv::cvtColor(frame,gray,cv::COLOR_BGR2GRAY);
            // Some uploads letterbox the keyboard and leave a black strip under
            // it.  Sampling at 84% of the remaining video height then reads the
            // strip instead of a key. Locate a sustained drop in horizontal key
            // texture; if none exists, the keyboard really does reach the frame
            // bottom and the old fallback remains correct.
            QVector<double> rowDeviation(height);
            for(int y=keyboardTop;y<height;++y){cv::Scalar mean,deviation;cv::meanStdDev(gray.row(y),mean,deviation);rowDeviation[y]=deviation[0];}
            const int bottomSearchStart=qMin(height-1,keyboardTop+qMax(36,qRound(height*.055)));
            for(int y=bottomSearchStart;y+8<height;++y){
                double before=0,after=0;int beforeCount=0;
                for(int row=qMax(keyboardTop,y-16);row<y;++row){before+=rowDeviation[row];++beforeCount;}
                for(int row=y;row<qMin(height,y+8);++row)after+=rowDeviation[row];
                before/=qMax(1,beforeCount);after/=8.0;
                if(before>12.0&&after<qMax(6.0,before*.25))keyboardBottom=y;
            }
            const int keyHeight=qMax(24,keyboardBottom-keyboardTop);
            const int whiteY=qBound(0,keyboardTop+qRound(keyHeight*.84),height-1);
            const int halfBand=qBound(2,qRound(height/360.0),6);
            cv::Mat whiteRow;cv::reduce(gray.rowRange(qMax(0,whiteY-halfBand),qMin(height,whiteY+halfBand+1)),whiteRow,0,cv::REDUCE_AVG,CV_32F);
            QVector<double> edge(width);double edgeMean=0;
            for(int x=1;x+1<width;++x){edge[x]=qAbs(double(whiteRow.at<float>(0,x+1)-whiteRow.at<float>(0,x-1)));edgeMean+=edge[x];}
            edgeMean/=qMax(1,width-2);for(double &value:edge)value-=edgeMean;
            const int minLag=qMax(10,width/90),maxLag=qMin(140,width/14);
            QVector<double> periodScore(maxLag+1);
            int strongestLag=minLag;
            for(int lag=minLag;lag<=maxLag;++lag){double score=0;for(int x=1;x+lag+1<width;++x)score+=edge[x]*edge[x+lag];score/=qMax(1,width-lag);periodScore[lag]=score;if(score>periodScore[strongestLag])strongestLag=lag;}
            int period=strongestLag;
            for(int lag=minLag+1;lag<maxLag;++lag)if(periodScore[lag]>=periodScore[lag-1]&&periodScore[lag]>=periodScore[lag+1]&&periodScore[lag]>=periodScore[strongestLag]*.55){period=lag;break;}
            if(period>=10){
                whiteWidth=period;
                double bestPhase=0,bestPhaseScore=-1;
                const int phaseSteps=qMax(1,qRound(whiteWidth*4));
                for(int step=0;step<phaseSteps;++step){const double phase=step/4.0;double score=0;for(double x=phase;x<width;x+=whiteWidth){const int xi=qRound(x);for(int dx=-2;dx<=2;++dx)if(xi+dx>=0&&xi+dx<edge.size())score=qMax(score,0.0)+qMax(0.0,edge[xi+dx]);}if(score>bestPhaseScore){bestPhaseScore=score;bestPhase=phase;}}
                double firstBoundary=bestPhase;while(firstBoundary-whiteWidth>=-whiteWidth)firstBoundary-=whiteWidth;
                QVector<double> whiteCenters;
                for(double boundary=firstBoundary;boundary<width+whiteWidth;boundary+=whiteWidth){const double center=boundary+whiteWidth*.5;if(center>=-whiteWidth*.15&&center<=width+whiteWidth*.15)whiteCenters.append(center);}
                while(!whiteCenters.isEmpty()&&whiteCenters.first()<0)whiteCenters.removeFirst();
                while(!whiteCenters.isEmpty()&&whiteCenters.last()>width)whiteCenters.removeLast();
                visibleWhiteCount=whiteCenters.size();
                if(visibleWhiteCount>=12&&visibleWhiteCount<=60){
                    const int blackY=qBound(0,keyboardTop+qRound(keyHeight*.30),height-1);
                    QVector<double> boundaryLight;
                    for(int i=0;i+1<whiteCenters.size();++i){const int x=qRound((whiteCenters[i]+whiteCenters[i+1])*.5);const int xRadius=qMax(2,qRound(whiteWidth*.18));const cv::Rect roi(qMax(0,x-xRadius),qMax(0,blackY-halfBand),qMin(width,x+xRadius+1)-qMax(0,x-xRadius),qMin(height,blackY+halfBand+1)-qMax(0,blackY-halfBand));boundaryLight.append(cv::mean(gray(roi))[0]);}
                    double dark=*std::min_element(boundaryLight.begin(),boundaryLight.end()),light=*std::max_element(boundaryLight.begin(),boundaryLight.end());
                    for(int pass=0;pass<8;++pass){double darkSum=0,lightSum=0;int darkCount=0,lightCount=0;const double middle=(dark+light)*.5;for(double value:boundaryLight)if(value<middle){darkSum+=value;++darkCount;}else{lightSum+=value;++lightCount;}if(darkCount)dark=darkSum/darkCount;if(lightCount)light=lightSum/lightCount;}
                    const double blackThreshold=(dark+light)*.5;
                    int firstWhiteClass=0,bestErrors=std::numeric_limits<int>::max();
                    for(int candidate=0;candidate<7;++candidate){int errors=0;for(int i=0;i<boundaryLight.size();++i){const int whiteClass=(candidate+i)%7;const bool predicted=whiteClass==0||whiteClass==1||whiteClass==3||whiteClass==4||whiteClass==5;const bool observed=boundaryLight[i]<blackThreshold;if(predicted!=observed)++errors;}if(errors<bestErrors){bestErrors=errors;firstWhiteClass=candidate;}}
                    int middleCIndex=-1;double middleDistance=std::numeric_limits<double>::max();
                    for(int i=0;i<whiteCenters.size();++i)if((firstWhiteClass+i)%7==0){const double distance=qAbs(whiteCenters[i]-width*.48);if(distance<middleDistance){middleDistance=distance;middleCIndex=i;}}
                    if(middleCIndex>=0){
                        static const int whiteSemitones[]{0,2,4,5,7,9,11};QVector<int> whiteMidi(whiteCenters.size());
                        for(int i=0;i<whiteCenters.size();++i){const int offset=i-middleCIndex;const int octave=offset>=0?offset/7:-((-offset+6)/7);const int degree=((offset%7)+7)%7;whiteMidi[i]=60+octave*12+whiteSemitones[degree];if(whiteMidi[i]>=21&&whiteMidi[i]<=108)centers[whiteMidi[i]]=whiteCenters[i];}
                        for(int i=0;i+1<whiteCenters.size();++i){const int midi=whiteMidi[i]+1;if(midi>=21&&midi<=108&&isBlack(midi))centers[midi]=(whiteCenters[i]+whiteCenters[i+1])*.5;}
                        visibleLow=108;visibleHigh=21;for(int midi=21;midi<=108;++midi)if(std::isfinite(centers[midi])){visibleLow=qMin(visibleLow,midi);visibleHigh=qMax(visibleHigh,midi);}
                    }
                }
            }
        }
    }
    // If the geometry frame is dark or covered by the opening animation, it
    // may still reveal the keys but not the keyboard's lower edge.  Recheck a
    // later stable frame and use the last sustained texture-to-black drop so
    // an internal white/black-key gap cannot truncate the keyboard body.
    if(keyboardBottom==height){
        const double bottomSecond=qMin(durationSeconds-.1,qMax(geometrySecond,qMin(30.0,durationSeconds*.25)));
        QProcess bottomDecoder;bottomDecoder.start("ffmpeg",{"-v","error","-ss",QString::number(bottomSecond,'f',3),"-i",path,"-frames:v","1","-f","rawvideo","-pix_fmt","bgr24","pipe:1"});
        if(bottomDecoder.waitForFinished(30000)&&bottomDecoder.exitCode()==0){const QByteArray bytes=bottomDecoder.readAllStandardOutput();if(bytes.size()>=fullFrameBytes){
            cv::Mat frame(height,width,CV_8UC3,const_cast<char*>(bytes.constData())),gray;cv::cvtColor(frame,gray,cv::COLOR_BGR2GRAY);QVector<double> rowDeviation(height);
            for(int y=keyboardTop;y<height;++y){cv::Scalar mean,deviation;cv::meanStdDev(gray.row(y),mean,deviation);rowDeviation[y]=deviation[0];}
            const int searchStart=qMin(height-9,keyboardTop+qMax(40,qRound(height*.12)));
            for(int y=searchStart;y+8<height;++y){double before=0,after=0;int count=0;for(int row=qMax(keyboardTop,y-16);row<y;++row){before+=rowDeviation[row];++count;}for(int row=y;row<y+8;++row)after+=rowDeviation[row];before/=qMax(1,count);after/=8.0;if(before>12.0&&after<qMax(6.0,before*.25))keyboardBottom=y;}
        }}
    }
    if(!std::isfinite(centers[60])){
        centers.fill(std::numeric_limits<double>::quiet_NaN());int whiteIndex=0;whiteWidth=width/52.0;visibleWhiteCount=52;visibleLow=21;visibleHigh=108;
        for(int midi=21;midi<=108;++midi){if(isBlack(midi))centers[midi]=whiteIndex*whiteWidth;else{centers[midi]=(whiteIndex+.5)*whiteWidth;++whiteIndex;}}
    }
    if(qEnvironmentVariableIsSet("KEYFLOW_TRANSCRIPTION_DEBUG"))std::fprintf(stderr,"keyboard %d white keys, MIDI %d-%d, width %.2f, top/bottom %d/%d, geometry %.2fs\n",visibleWhiteCount,visibleLow,visibleHigh,whiteWidth,keyboardTop,keyboardBottom,geometrySecond);
    const int visibleKeyHeight=qMax(24,keyboardBottom-keyboardTop);
    const int keyBlackY=qBound(0,keyboardTop+qRound(visibleKeyHeight*.30),height-probeHeight);
    const int keyWhiteY=qBound(0,keyboardTop+qRound(visibleKeyHeight*.84),height-probeHeight);

    struct Lane{bool active=false;int hits=0,misses=0,startFrame=0,candidateFrame=0;};
    struct RawBoxSample{int frame=0;float x0=0,y0=0,x1=0,y1=0;};
    struct Gate{int midi,startFrame,endFrame;int hand=-1;float hue=-1;float x0=-1,x1=-1;QVector<RawBoxSample> samples;};
    struct Detection{double distance=0;QVector<Lane> brightLanes,colorLanes;QVector<Gate> brightGates,colorGates,gates;};
    QVector<Detection> detections;for(double distance:candidateDistances)detections.append({distance,QVector<Lane>(109),QVector<Lane>(109),{},{},{}});
    Detection highlightedKeysRaw{-2.0,QVector<Lane>(109),QVector<Lane>(109),{},{},{}};
    Detection highlightedKeys{-1.0,QVector<Lane>(109),QVector<Lane>(109),{},{},{}};
    QVector<cv::Vec3f> keyBaseline(109);QVector<bool> keyBaselineReady(109,false);
    QVector<cv::Vec3f> previousKeyColors(109);QVector<float> previousKeySaturation(109),previousKeyValue(109);
    QVector<bool> previousKeyReady(109,false);
    qint64 frameNumber=0;
    const double analysisStartSecond=qMax(0.0,referenceSecond-3.0);
    const qint64 expectedFrames=qMax<qint64>(1,qRound64((durationSeconds-analysisStartSecond)*fps));
    const int stackedInputs=detections.size()+2;
    const qsizetype stackedHeight=probeHeight*stackedInputs;
    const qsizetype frameBytes=qsizetype(width)*stackedHeight*3;

    QProcess decoder;decoder.setProcessChannelMode(QProcess::SeparateChannels);
    QString filter=QString("fps=%1,split=%2").arg(fps,0,'f',6).arg(stackedInputs);
    for(int i=0;i<stackedInputs;++i)filter+=QString("[v%1]").arg(i);
    for(int i=0;i<detections.size();++i){const int y=qBound(0,keyboardTop-qRound(height*detections[i].distance),height-probeHeight);filter+=QString(";[v%1]crop=%2:%3:0:%4[c%1]").arg(i).arg(width).arg(probeHeight).arg(y);}
    filter+=QString(";[v%1]crop=%2:%3:0:%4[kb]").arg(detections.size()).arg(width).arg(probeHeight).arg(keyBlackY);
    filter+=QString(";[v%1]crop=%2:%3:0:%4[kw]").arg(detections.size()+1).arg(width).arg(probeHeight).arg(keyWhiteY);
    filter+=';';for(int i=0;i<detections.size();++i)filter+=QString("[c%1]").arg(i);filter+="[kb][kw]";filter+=QString("vstack=inputs=%1").arg(stackedInputs);
    decoder.start("ffmpeg",{"-v","error","-ss",QString::number(analysisStartSecond,'f',3),"-i",path,"-vf",filter,
                            "-f","rawvideo","-pix_fmt","bgr24","pipe:1"});
    if(!decoder.waitForStarted(10000)){setError(error,"FFmpeg could not start detailed video decoding.");return false;}
    QByteArray raw;
    while(decoder.state()!=QProcess::NotRunning||decoder.bytesAvailable()>0){
        if(canceled&&canceled()){decoder.kill();decoder.waitForFinished();setError(error,"Transcription canceled.");return false;}
        if(decoder.bytesAvailable()==0)decoder.waitForReadyRead(1000);
        raw+=decoder.readAllStandardOutput();qsizetype offset=0;
        while(raw.size()-offset>=frameBytes){
            auto detectRuns=[&](const QVector<bool> &mask){
                QSet<int> active;
                for(int x=0;x<width;){
                    while(x<width&&!mask[x])++x;const int begin=x;while(x<width&&mask[x])++x;const int runWidth=x-begin;
                    if(runWidth<whiteWidth*.30||runWidth>whiteWidth*1.55)continue;
                    const double center=begin+(runWidth-1)*.5;int bestMidi=21;double bestCost=std::numeric_limits<double>::max();
                    for(int midi=21;midi<=108;++midi){if(!std::isfinite(centers[midi]))continue;const double expectedWidth=isBlack(midi)?whiteWidth*.62:whiteWidth;const double cost=qAbs(centers[midi]-center)+.6*qAbs(runWidth-expectedWidth);if(cost<bestCost){bestCost=cost;bestMidi=midi;}}
                    if(std::isfinite(centers[bestMidi])&&qAbs(centers[bestMidi]-center)<=whiteWidth*.38&&bestCost<=whiteWidth*.46)active.insert(bestMidi);
                }
                return active;
            };
            auto updateLanes=[&](const QSet<int> &active,QVector<Lane> &lanes,QVector<Gate> &gates){
                for(int midi=21;midi<=108;++midi){auto &lane=lanes[midi];if(active.contains(midi)){lane.misses=0;if(!lane.active){if(lane.hits==0)lane.candidateFrame=frameNumber;if(++lane.hits>=3){lane.active=true;lane.startFrame=lane.candidateFrame;}}}else if(lane.active){if(++lane.misses>2){const int endFrame=int(frameNumber)-lane.misses+1;if(endFrame-lane.startFrame>=3)gates.append({midi,lane.startFrame,endFrame});lane=Lane{};}}else lane.hits=0;}
            };
            cv::Mat stacked(int(stackedHeight),width,CV_8UC3,raw.data()+offset);
            for(int candidateIndex=0;candidateIndex<detections.size();++candidateIndex){
                cv::Mat band=stacked.rowRange(candidateIndex*probeHeight,(candidateIndex+1)*probeHeight);
                cv::Mat gray,hsv,grayMean,saturation,saturationMean,value,valueMean;
                cv::cvtColor(band,gray,cv::COLOR_BGR2GRAY);cv::cvtColor(band,hsv,cv::COLOR_BGR2HSV);
                cv::extractChannel(hsv,saturation,1);cv::extractChannel(hsv,value,2);
                cv::reduce(gray,grayMean,0,cv::REDUCE_AVG,CV_32F);cv::reduce(saturation,saturationMean,0,cv::REDUCE_AVG,CV_32F);cv::reduce(value,valueMean,0,cv::REDUCE_AVG,CV_32F);
                QVector<bool> bright(width,false),color(width,false);
                for(int x=0;x<width;++x){const float light=grayMean.at<float>(0,x),sat=saturationMean.at<float>(0,x),val=valueMean.at<float>(0,x);bright[x]=light>=220;color[x]=val>=190&&sat>=90;}
                for(int x=1;x+1<width;++x)if(!color[x]&&color[x-1]&&color[x+1])color[x]=true;
                auto &d=detections[candidateIndex];updateLanes(detectRuns(bright),d.brightLanes,d.brightGates);updateLanes(detectRuns(color),d.colorLanes,d.colorGates);
            }
            // Read the piano keys themselves as a color-independent fallback.
            // Each key is compared with its own unpressed appearance, so a
            // highlight may be pink, blue, white, grayscale, or any other
            // color.  This is deliberately based on change, not saturation.
            const cv::Mat blackBand=stacked.rowRange(detections.size()*probeHeight,(detections.size()+1)*probeHeight);
            const cv::Mat whiteBand=stacked.rowRange((detections.size()+1)*probeHeight,(detections.size()+2)*probeHeight);
            QVector<float> keyChanges(109),keySaturation(109),keyValue(109);QVector<cv::Vec3f> currentKeyColors(109);QVector<bool> currentKeyReady(109,false);QVector<float> finiteChanges,whiteSaturations,blackSaturations,blackValues;
            for(int midi=visibleLow;midi<=visibleHigh;++midi){
                if(!std::isfinite(centers[midi]))continue;
                const cv::Mat &band=isBlack(midi)?blackBand:whiteBand;
                const int radius=qMax(2,qRound(whiteWidth*(isBlack(midi)?.18:.24)));
                const int center=qRound(centers[midi]);const int left=qMax(0,center-radius),right=qMin(width,center+radius+1);
                if(right<=left)continue;
                const cv::Scalar mean=cv::mean(band.colRange(left,right));const cv::Vec3f current{float(mean[0]),float(mean[1]),float(mean[2])};currentKeyColors[midi]=current;currentKeyReady[midi]=true;
                const float maximum=qMax(current[0],qMax(current[1],current[2])),minimum=qMin(current[0],qMin(current[1],current[2]));
                keyValue[midi]=maximum;keySaturation[midi]=maximum>1e-3f?255.0f*(maximum-minimum)/maximum:0.0f;
                if(isBlack(midi)){blackSaturations.append(keySaturation[midi]);blackValues.append(keyValue[midi]);}else whiteSaturations.append(keySaturation[midi]);
                if(!keyBaselineReady[midi]){keyBaseline[midi]=current;keyBaselineReady[midi]=true;continue;}
                const cv::Vec3f difference=current-keyBaseline[midi];const float change=std::sqrt(difference.dot(difference));keyChanges[midi]=change;finiteChanges.append(change);
            }
            auto vectorMedian=[](QVector<float> values){if(values.isEmpty())return 0.0f;const int middle=values.size()/2;std::nth_element(values.begin(),values.begin()+middle,values.end());return values[middle];};
            const float medianChange=vectorMedian(finiteChanges),medianWhiteSaturation=vectorMedian(whiteSaturations),medianBlackSaturation=vectorMedian(blackSaturations),medianBlackValue=vectorMedian(blackValues);
            // A pressed coloured key is saturated across the lower key body;
            // sparks reflected from the strike line normally are not.  The old
            // 18-point BGR threshold treated those reflections as notes and
            // could then discard the genuinely pressed neighbouring white key.
            const float changeThreshold=qMax(32.0f,medianChange*2.8f+10.0f);QSet<int> changedKeys,solidSurfaceKeys;
            int visibleKeyCount=0;for(int midi=visibleLow;midi<=visibleHigh;++midi)if(std::isfinite(centers[midi]))++visibleKeyCount;
            for(int midi=visibleLow;midi<=visibleHigh;++midi){
                if(!keyBaselineReady[midi]||!std::isfinite(centers[midi]))continue;
                const float threshold=highlightedKeys.brightLanes[midi].active?changeThreshold*.55f:changeThreshold;
                const bool solidWhite=!isBlack(midi)&&keySaturation[midi]>=qMax(60.0f,medianWhiteSaturation+42.0f);
                const bool solidBlack=isBlack(midi)&&keySaturation[midi]>=medianBlackSaturation+24.0f&&keyValue[midi]>=medianBlackValue+28.0f&&keyChanges[midi]>=threshold*.70f;
                if(frameNumber>2&&(solidWhite||solidBlack||keyChanges[midi]>=threshold)){changedKeys.insert(midi);if(solidWhite||solidBlack)solidSurfaceKeys.insert(midi);}
                else if(!highlightedKeys.brightLanes[midi].active){
                    if(currentKeyReady[midi])keyBaseline[midi]=keyBaseline[midi]*.985f+currentKeyColors[midi]*.015f;
                }
            }
            // A lit white key can spill a little color under the neighboring
            // black-key edge (and vice versa).  Keep the stronger, already
            // stable key in an adjacent pair instead of inventing a semitone
            // cluster that is not visible in the video.
            for(int midi=visibleLow;midi<visibleHigh;++midi)if(isBlack(midi)!=isBlack(midi+1)&&changedKeys.contains(midi)&&changedKeys.contains(midi+1)){
                const bool firstSolid=solidSurfaceKeys.contains(midi),secondSolid=solidSurfaceKeys.contains(midi+1);
                if(firstSolid!=secondSolid)changedKeys.remove(firstSolid?midi+1:midi);
                else {const bool firstActive=highlightedKeys.brightLanes[midi].active,secondActive=highlightedKeys.brightLanes[midi+1].active;
                if(firstActive!=secondActive)changedKeys.remove(firstActive?midi+1:midi);
                else changedKeys.remove(keyChanges[midi]>=keyChanges[midi+1]?midi+1:midi);
                }
            }
            // Fast repeated notes often re-light a key before its previous glow
            // has completely returned to the neutral keyboard colour.  Comparing
            // only with a frozen baseline consequently merged a run of repeated
            // strikes into one several-second note.  A fresh upward flash in
            // saturation or value is a new attack; close the old gate before the
            // ordinary debounce starts the replacement.  Downward fade steps do
            // not qualify, which avoids splitting one decaying highlight.
            for(int midi:std::as_const(changedKeys)){
                auto &lane=highlightedKeys.brightLanes[midi];
                if(!lane.active||!previousKeyReady[midi]||frameNumber-lane.startFrame<5)continue;
                const cv::Vec3f frameDifference=currentKeyColors[midi]-previousKeyColors[midi];
                const float frameChange=std::sqrt(frameDifference.dot(frameDifference));
                const float saturationRise=keySaturation[midi]-previousKeySaturation[midi];
                const float valueRise=keyValue[midi]-previousKeyValue[midi];
                const bool freshColourFlash=keySaturation[midi]>=75.0f&&saturationRise>=38.0f;
                const bool freshNeutralFlash=valueRise>=32.0f&&keySaturation[midi]<=75.0f;
                if(frameChange>=42.0f&&(freshColourFlash||freshNeutralFlash)){
                    if(frameNumber-lane.startFrame>=3)highlightedKeys.brightGates.append({midi,lane.startFrame,int(frameNumber)});
                    lane=Lane{};
                }
            }
            // A fade, cut, or global exposure change can alter every black key
            // together.  A pianist cannot press a quarter of the visible
            // keyboard at once, so learn that frame as the new neutral piano
            // instead of engraving it as a giant opening chord.
            if(changedKeys.size()>qMax(8,visibleKeyCount/4)){
                changedKeys.clear();
                for(int midi=visibleLow;midi<=visibleHigh;++midi)if(currentKeyReady[midi]){keyBaseline[midi]=currentKeyColors[midi];highlightedKeys.brightLanes[midi]=Lane{};highlightedKeysRaw.brightLanes[midi]=Lane{};}
            }
            // Retain the original continuous key-state interpretation alongside
            // the reattack-aware one.  It supplies a conservative baseline for
            // deciding which newly separated flashes are real soundtrack attacks.
            updateLanes(changedKeys,highlightedKeysRaw.brightLanes,highlightedKeysRaw.brightGates);
            updateLanes(changedKeys,highlightedKeys.brightLanes,highlightedKeys.brightGates);
            for(int midi=visibleLow;midi<=visibleHigh;++midi)if(currentKeyReady[midi]){
                previousKeyColors[midi]=currentKeyColors[midi];previousKeySaturation[midi]=keySaturation[midi];
                previousKeyValue[midi]=keyValue[midi];previousKeyReady[midi]=true;
            }
            ++frameNumber;offset+=frameBytes;
            if(progress&&frameNumber%60==0)progress(qMin(99,int(frameNumber*100/expectedFrames)));
        }
        if(offset)raw.remove(0,offset);
        QCoreApplication::processEvents();
    }
    decoder.waitForFinished(5000);
    if(decoder.exitStatus()!=QProcess::NormalExit||decoder.exitCode()!=0){setError(error,"FFmpeg stopped during detailed falling-note decoding: "+QString::fromLocal8Bit(decoder.readAllStandardError()).trimmed());return false;}

    // Track the complete falling rectangles, not just a few scan lines or the
    // illuminated key surface.  A bar receives an identity while it is still
    // high above the noisy strike effects.  Its lower edge crossing the play
    // line is note-on and its upper edge crossing is note-off.  This preserves
    // long holds and keeps successive bars on the same key as separate notes.
    // It follows the central idea of LumaChords' note-rain tracker while using
    // Keyflow's already calibrated key centres and soundtrack validation.
    Detection fullBarDetection{-3.0,QVector<Lane>(109),QVector<Lane>(109),{},{},{}};
    // Short repeated blocks in 60 fps tutorials can appear and separate
    // entirely between two 12 fps samples.  Thirty samples per second keeps
    // those visible gaps while still scaling the frame before analysis.
    const double barFps=qMin(30.0,qMax(12.0,sourceFps));
    const double barScale=qMin(1.0,960.0/width);
    const int barWidth=qMax(320,qRound(width*barScale));
    const int barHeight=qMax(80,qRound(keyboardTop*barScale));
    const double scaledWhiteWidth=whiteWidth*barScale;
    // Some tutorials (notably the monochrome HOI4 video) materialize bars
    // well below the top of the falling-note area.  Admitting tracks only in
    // the first 30% discarded every one of those bars and forced the noisy
    // strike-line fallback.  Track them from the middle of the field, but
    // still measure before the particle-heavy keyboard edge.
    const int admissionY=qRound(barHeight*.62);
    const int playY=qRound(barHeight*.78);
    const int trackingEndY=qRound(barHeight*.98);
    struct BarBox{int midi=60;float x0=0,y0=0,x1=0,y1=0,hue=-1;};
    struct BarTrack{
        int id=0,midi=60,lastFrame=0,age=1,misses=0;
        float x0=0,y0=0,x1=0,y1=0,velocity=0,hue=-1;
        bool on=false,done=false;double onFrame=0,predictedOffFrame=-1;
        QVector<RawBoxSample> samples;
    };
    QVector<BarTrack> barTracks;QVector<float> recentBarVelocities,allBarVelocities;int nextBarId=1;int barFrameNumber=0;
    auto medianFloat=[](QVector<float> values){if(values.isEmpty())return 0.0f;const int middle=values.size()/2;std::nth_element(values.begin(),values.begin()+middle,values.end());return values[middle];};
    auto velocityConsensus=[&](){return medianFloat(recentBarVelocities);};
    auto toMainFrame=[&](double trackerFrame){return qMax(0,qRound(trackerFrame*fps/barFps));};
    auto closeBarTrack=[&](BarTrack &track,double endTrackerFrame){
        if(!track.on||track.done)return;
        const float velocity=track.velocity>.25f?track.velocity:velocityConsensus();
        // onFrame/endTrackerFrame are crossings at the clean tracking line,
        // not at the piano.  Advance both edges by their remaining trip to
        // keyboardTop so raw Director bars and note attacks land on the
        // source video's actual strike line.
        const double strikeAdvance=velocity>.25f?(barHeight-playY)/velocity:0.0;
        const int startFrame=toMainFrame(track.onFrame+strikeAdvance),endFrame=qMax(startFrame+1,toMainFrame(endTrackerFrame+strikeAdvance));
        QVector<float> sampleCenters,sampleWidths;sampleCenters.reserve(track.samples.size());sampleWidths.reserve(track.samples.size());
        for(const auto &sample:track.samples){sampleCenters.append((sample.x0+sample.x1)*.5f);sampleWidths.append(sample.x1-sample.x0);}
        const float robustCenter=sampleCenters.isEmpty()?(track.x0+track.x1)*.5f/float(barScale):medianFloat(sampleCenters);
        const float robustWidth=sampleWidths.isEmpty()?(track.x1-track.x0)/float(barScale):medianFloat(sampleWidths);
        Gate gate{track.midi,startFrame,endFrame,-1,track.hue,robustCenter-robustWidth*.5f,robustCenter+robustWidth*.5f};gate.samples=track.samples;
        fullBarDetection.gates.append(std::move(gate));track.done=true;track.on=false;
    };
    auto rememberBarRelease=[&](BarTrack &track,double currentFrame){
        const float velocity=track.velocity>.25f?track.velocity:velocityConsensus();
        if(velocity<=.25f||track.y0>playY)return;
        const double prediction=currentFrame+(playY-track.y0)/velocity;
        // A genuinely clipped long bar reveals about one more frame of length
        // per decoded frame.  Ignore implausible upward leaps caused when glow
        // or particles temporarily connect to the component.
        if(track.predictedOffFrame<0||prediction<=track.predictedOffFrame+2.5)
            track.predictedOffFrame=qMax(track.predictedOffFrame,prediction);
    };
    auto crossBarPlayLine=[&](BarTrack &track,float oldY0,float oldY1,float newY0,float newY1,double currentFrame){
        const float consensus=velocityConsensus();
        const float velocity=track.velocity>.25f?track.velocity:consensus;
        if(velocity<=.25f)return;
        if(!track.on&&!track.done&&track.age>=2&&oldY1<playY&&newY1>=playY){
            const double crossing=currentFrame-(newY1-playY)/velocity;
            // A distinct lower-edge crossing is a new attack.  The old code
            // threw it away whenever an earlier same-key release prediction
            // overlapped it; that merged the visibly separated repeat blocks
            // in HOI4.  Only crossings within one tracker frame can be the
            // same fragment.  Otherwise close the old hold at the retrigger.
            for(auto &other:barTracks)if(other.id!=track.id&&other.midi==track.midi&&other.on&&!other.done){
                if(qAbs(other.onFrame-crossing)<=1.0){track.done=true;return;}
                closeBarTrack(other,crossing);
            }
            track.on=true;track.onFrame=crossing;
            rememberBarRelease(track,currentFrame);
        }
        if(track.on&&!track.done&&oldY0<playY&&newY0>=playY){
            const double crossing=currentFrame-(newY0-playY)/velocity;closeBarTrack(track,crossing);
        }
    };

    QProcess barDecoder;barDecoder.setProcessChannelMode(QProcess::SeparateChannels);
    const QString barFilter=QString("fps=%1,crop=%2:%3:0:0,scale=%4:%5:flags=area")
        .arg(barFps,0,'f',6).arg(width).arg(keyboardTop).arg(barWidth).arg(barHeight);
    barDecoder.start("ffmpeg",{"-v","error","-ss",QString::number(analysisStartSecond,'f',3),"-i",path,
                                "-vf",barFilter,"-f","rawvideo","-pix_fmt","bgr24","pipe:1"});
    if(barDecoder.waitForStarted(10000)){
        const qsizetype barFrameBytes=qsizetype(barWidth)*barHeight*3;QByteArray barRaw;
        while(barDecoder.state()!=QProcess::NotRunning||barDecoder.bytesAvailable()>0){
            if(canceled&&canceled()){barDecoder.kill();barDecoder.waitForFinished();setError(error,"Transcription canceled.");return false;}
            if(barDecoder.bytesAvailable()==0)barDecoder.waitForReadyRead(1000);
            barRaw+=barDecoder.readAllStandardOutput();qsizetype barOffset=0;
            while(barRaw.size()-barOffset>=barFrameBytes){
                cv::Mat frame(barHeight,barWidth,CV_8UC3,barRaw.data()+barOffset),hsv,saturation,value,colorMask,brightMask,mask;
                cv::cvtColor(frame,hsv,cv::COLOR_BGR2HSV);cv::extractChannel(hsv,saturation,1);cv::extractChannel(hsv,value,2);
                cv::threshold(saturation,colorMask,54,255,cv::THRESH_BINARY);cv::Mat valueFloor;cv::threshold(value,valueFloor,64,255,cv::THRESH_BINARY);cv::bitwise_and(colorMask,valueFloor,colorMask);
                cv::threshold(value,brightMask,204,255,cv::THRESH_BINARY);cv::bitwise_or(colorMask,brightMask,mask);
                const int morphologyWidth=qMax(3,(qRound(scaledWhiteWidth*.16)|1));
                const cv::Mat kernel=cv::getStructuringElement(cv::MORPH_RECT,cv::Size(morphologyWidth,3));
                cv::morphologyEx(mask,mask,cv::MORPH_CLOSE,kernel);cv::morphologyEx(mask,mask,cv::MORPH_OPEN,cv::getStructuringElement(cv::MORPH_RECT,cv::Size(3,3)));
                cv::Mat labels,stats,centroids;const int componentCount=cv::connectedComponentsWithStats(mask,labels,stats,centroids,8,CV_32S);
                QVector<BarBox> boxes;boxes.reserve(componentCount*2);
                const int minWidth=qMax(3,qRound(scaledWhiteWidth*.24)),maxWidth=qMax(minWidth+1,qRound(scaledWhiteWidth*1.58));
                const int minHeight=qMax(3,qRound(scaledWhiteWidth*.18)),minArea=qMax(9,qRound(scaledWhiteWidth*scaledWhiteWidth*.08));
                for(int component=1;component<componentCount;++component){
                    const int x=stats.at<int>(component,cv::CC_STAT_LEFT),y=stats.at<int>(component,cv::CC_STAT_TOP);
                    const int w=stats.at<int>(component,cv::CC_STAT_WIDTH),h=stats.at<int>(component,cv::CC_STAT_HEIGHT),area=stats.at<int>(component,cv::CC_STAT_AREA);
                    if(h<minHeight||area<minArea)continue;

                    // Keep the proven centroid path for an ordinary isolated
                    // rectangle.  Lane splitting is only needed for a wide or
                    // sparse component that can contain several chord tones;
                    // applying it to every normal white-key bar also sampled
                    // the neighbouring black-key centre at the antialiased
                    // edge and doubled the note count.
                    if(w>=minWidth&&w<=maxWidth&&area>=double(w*h)*.42){
                        const double center=centroids.at<double>(component,0);int bestMidi=-1;double bestCost=std::numeric_limits<double>::max();
                        for(int midi=visibleLow;midi<=visibleHigh;++midi){
                            if(!std::isfinite(centers[midi]))continue;const double expectedCenter=centers[midi]*barScale;
                            const double expectedWidth=(isBlack(midi)?whiteWidth*.62:whiteWidth)*barScale;
                            const double cost=qAbs(expectedCenter-center)+.20*qAbs(w-expectedWidth);
                            if(cost<bestCost){bestCost=cost;bestMidi=midi;}
                        }
                        if(bestMidi>=0&&qAbs(centers[bestMidi]*barScale-center)<=scaledWhiteWidth*.48&&bestCost<=scaledWhiteWidth*.72){
                            int hueHistogram[180]{},colouredPixels=0;
                            for(int yy=y;yy<y+h;++yy){const int *labelRow=labels.ptr<int>(yy);const cv::Vec3b *hsvRow=hsv.ptr<cv::Vec3b>(yy);for(int xx=x;xx<x+w;++xx)if(labelRow[xx]==component&&hsvRow[xx][1]>54&&hsvRow[xx][2]>64){++hueHistogram[hsvRow[xx][0]];++colouredPixels;}}
                            int dominantHue=0;for(int hue=1;hue<180;++hue)if(hueHistogram[hue]>hueHistogram[dominantHue])dominantHue=hue;
                            const float boxHue=colouredPixels>=qMax(3,area/8)?float(dominantHue):-1.0f;
                            boxes.append({bestMidi,float(x),float(y),float(x+w),float(y+h),boxHue});continue;
                        }
                    }

                    // A chord is often joined into one connected component by
                    // anti-aliased glow, strike particles, or the horizontal
                    // morphology pass.  Assigning that component by its single
                    // centroid collapsed the whole chord onto one MIDI lane;
                    // rejecting a component wider than one key discarded the
                    // chord altogether.  Decode solid vertical runs through
                    // every calibrated key centre instead.  The narrow core is
                    // deliberately well inside a note bar, so a white bar that
                    // merely touches the neighbouring black-key centre does not
                    // invent an extra semitone.
                    int splitBoxes=0;
                    for(int midi=visibleLow;midi<=visibleHigh;++midi){
                        if(!std::isfinite(centers[midi]))continue;
                        const float expectedCenter=float(centers[midi]*barScale);
                        if(expectedCenter<x-2||expectedCenter>x+w+2)continue;
                        const float expectedWidth=float((isBlack(midi)?whiteWidth*.62:whiteWidth)*barScale);
                        const int centerX=qBound(0,qRound(expectedCenter),barWidth-1);
                        const int coreHalf=qMax(1,qRound(expectedWidth*.18f));
                        const int coreLeft=qMax(x,qMax(0,centerX-coreHalf));
                        const int coreRight=qMin(x+w-1,qMin(barWidth-1,centerX+coreHalf));
                        const int coreWidth=coreRight-coreLeft+1;
                        if(coreWidth<2)continue;
                        const int rowThreshold=qMax(2,qRound(coreWidth*.62));

                        struct SolidRun{int top=0,bottom=0,solidRows=0,pixels=0;};
                        QVector<SolidRun> runs;int runTop=-1,lastSolid=-1,solidRows=0,runPixels=0;
                        auto finishRun=[&](){
                            if(runTop>=0&&lastSolid-runTop+1>=minHeight&&solidRows>=qMax(2,minHeight-1))
                                runs.append({runTop,lastSolid+1,solidRows,runPixels});
                            runTop=-1;lastSolid=-1;solidRows=0;runPixels=0;
                        };
                        for(int yy=y;yy<y+h;++yy){
                            const int *labelRow=labels.ptr<int>(yy);int count=0;
                            for(int xx=coreLeft;xx<=coreRight;++xx)if(labelRow[xx]==component)++count;
                            if(count>=rowThreshold){
                                if(runTop<0)runTop=yy;
                                lastSolid=yy;++solidRows;runPixels+=count;
                            }else if(runTop>=0&&yy-lastSolid>1)finishRun();
                        }
                        finishRun();

                        for(const auto &run:runs){
                            // Sparse effects can touch a lane for a few rows.
                            // Real note rectangles fill most of the lane core
                            // throughout their visible vertical run.
                            const int runHeight=run.bottom-run.top;
                            if(runHeight<minHeight||run.pixels<double(run.solidRows*coreWidth)*.68)continue;
                            const int probeHalf=qMax(coreHalf+1,qRound(expectedWidth*.58f));
                            const int probeLeft=qMax(x,qMax(0,centerX-probeHalf));
                            const int probeRight=qMin(x+w-1,qMin(barWidth-1,centerX+probeHalf));
                            QVector<float> leftEdges,rightEdges;leftEdges.reserve(run.solidRows);rightEdges.reserve(run.solidRows);
                            int hueHistogram[180]{},colouredPixels=0;
                            for(int yy=run.top;yy<run.bottom;++yy){
                                const int *labelRow=labels.ptr<int>(yy);const cv::Vec3b *hsvRow=hsv.ptr<cv::Vec3b>(yy);
                                int left=-1,right=-1;
                                for(int xx=probeLeft;xx<=probeRight;++xx)if(labelRow[xx]==component){if(left<0)left=xx;right=xx;
                                    if(hsvRow[xx][1]>54&&hsvRow[xx][2]>64){++hueHistogram[hsvRow[xx][0]];++colouredPixels;}}
                                if(left>=0){leftEdges.append(float(left));rightEdges.append(float(right+1));}
                            }
                            if(leftEdges.size()<qMax(2,minHeight-1))continue;
                            float boxLeft=medianFloat(leftEdges),boxRight=medianFloat(rightEdges);
                            // A horizontal bridge to the next chord tone must
                            // not make this lane's Director outline span both
                            // notes.  Keep the measured edge when plausible,
                            // otherwise clamp it to one expected key width.
                            boxLeft=qMax(boxLeft,expectedCenter-expectedWidth*.58f);
                            boxRight=qMin(boxRight,expectedCenter+expectedWidth*.58f);
                            if(boxRight-boxLeft<qMax(2.0f,expectedWidth*.20f))continue;
                            int dominantHue=0;for(int hue=1;hue<180;++hue)if(hueHistogram[hue]>hueHistogram[dominantHue])dominantHue=hue;
                            const float boxHue=colouredPixels>=qMax(3,run.pixels/8)?float(dominantHue):-1.0f;
                            boxes.append({midi,boxLeft,float(run.top),boxRight,float(run.bottom),boxHue});++splitBoxes;
                        }
                    }
                    if(splitBoxes)continue;

                    // Fallback for a narrow bar whose calibrated centre lands
                    // just outside its solid core.  Wide or hollow components
                    // are intentionally not accepted here: they are precisely
                    // the connected-chord case handled lane by lane above.
                    if(w<minWidth||w>maxWidth||area<double(w*h)*.42)continue;
                    const double center=centroids.at<double>(component,0);int bestMidi=-1;double bestCost=std::numeric_limits<double>::max();
                    for(int midi=visibleLow;midi<=visibleHigh;++midi){
                        if(!std::isfinite(centers[midi]))continue;const double expectedCenter=centers[midi]*barScale;
                        const double expectedWidth=(isBlack(midi)?whiteWidth*.62:whiteWidth)*barScale;
                        const double cost=qAbs(expectedCenter-center)+.20*qAbs(w-expectedWidth);
                        if(cost<bestCost){bestCost=cost;bestMidi=midi;}
                    }
                    if(bestMidi<0||qAbs(centers[bestMidi]*barScale-center)>scaledWhiteWidth*.48||bestCost>scaledWhiteWidth*.72)continue;
                    int hueHistogram[180]{},colouredPixels=0;
                    for(int yy=y;yy<y+h;++yy){const int *labelRow=labels.ptr<int>(yy);const cv::Vec3b *hsvRow=hsv.ptr<cv::Vec3b>(yy);for(int xx=x;xx<x+w;++xx)if(labelRow[xx]==component&&hsvRow[xx][1]>54&&hsvRow[xx][2]>64){++hueHistogram[hsvRow[xx][0]];++colouredPixels;}}
                    int dominantHue=0;for(int hue=1;hue<180;++hue)if(hueHistogram[hue]>hueHistogram[dominantHue])dominantHue=hue;
                    const float boxHue=colouredPixels>=qMax(3,area/8)?float(dominantHue):-1.0f;
                    boxes.append({bestMidi,float(x),float(y),float(x+w),float(y+h),boxHue});
                }
                std::sort(boxes.begin(),boxes.end(),[](const BarBox &a,const BarBox &b){return a.midi==b.midi?(a.y0+a.y1)<(b.y0+b.y1):a.midi<b.midi;});
                QVector<bool> usedTracks(barTracks.size(),false);
                for(const auto &box:boxes){
                    int bestTrack=-1;double bestCost=std::numeric_limits<double>::max();const float boxCenter=(box.y0+box.y1)*.5f;
                    for(int i=0;i<barTracks.size();++i){auto &track=barTracks[i];if(track.done||usedTracks[i]||track.midi!=box.midi||barFrameNumber-track.lastFrame>2)continue;
                        const float trackCenter=(track.y0+track.y1)*.5f;const float expected=trackCenter+(track.velocity>.25f?track.velocity:velocityConsensus());
                        const float dy=boxCenter-trackCenter;const double cost=qAbs(boxCenter-expected)+qAbs((box.y1-box.y0)-(track.y1-track.y0))*.08;
                        const float xOverlap=qMin(track.x1,box.x1)-qMax(track.x0,box.x0);
                        const float minimumWidth=qMin(track.x1-track.x0,box.x1-box.x0);
                        const float centerDelta=qAbs((track.x0+track.x1-box.x0-box.x1)*.5f);
                        const bool sameHorizontalPath=xOverlap>=minimumWidth*.25f||centerDelta<=qMax(2.0,scaledWhiteWidth*.24);
                        if(sameHorizontalPath&&dy>=-3&&dy<=qMax(18.0,scaledWhiteWidth*1.7)&&cost<bestCost){bestCost=cost;bestTrack=i;}
                    }
                    if(bestTrack>=0){
                        auto &track=barTracks[bestTrack];usedTracks[bestTrack]=true;const float oldY0=track.y0,oldY1=track.y1;
                        const float oldReference=oldY0<2?(track.y1):(track.y0+track.y1)*.5f;
                        const float newReference=box.y0<2?(box.y1):(box.y0+box.y1)*.5f;const float dy=newReference-oldReference;
                        if(dy>.25f&&dy<qMax(24.0,scaledWhiteWidth*2.0)){track.velocity=track.velocity>.25f?track.velocity*.72f+dy*.28f:dy;recentBarVelocities.append(dy);allBarVelocities.append(dy);const int keep=qMax(12,qRound(barFps*2));if(recentBarVelocities.size()>keep)recentBarVelocities.remove(0,recentBarVelocities.size()-keep);}
                        track.x0=box.x0;track.y0=box.y0;track.x1=box.x1;track.y1=box.y1;if(box.hue>=0)track.hue=box.hue;track.lastFrame=barFrameNumber;track.misses=0;++track.age;
                        track.samples.append({barFrameNumber,box.x0/float(barScale),box.y0/float(barScale),box.x1/float(barScale),box.y1/float(barScale)});
                        rememberBarRelease(track,barFrameNumber);
                        crossBarPlayLine(track,oldY0,oldY1,track.y0,track.y1,barFrameNumber);
                    }else if(box.y0<=admissionY){
                        BarTrack track{nextBarId++,box.midi,barFrameNumber,1,0,box.x0,box.y0,box.x1,box.y1,0,box.hue,false,false,0,-1};
                        track.samples.append({barFrameNumber,box.x0/float(barScale),box.y0/float(barScale),box.x1/float(barScale),box.y1/float(barScale)});
                        barTracks.append(std::move(track));usedTracks.append(true);
                    }
                }
                for(int i=0;i<barTracks.size();++i){auto &track=barTracks[i];if(track.done||usedTracks.value(i))continue;
                    const float velocity=track.velocity>.25f?track.velocity:velocityConsensus();if(velocity<=.25f){if(++track.misses>4)track.done=true;continue;}
                    const float oldY0=track.y0,oldY1=track.y1;track.y0+=velocity;track.y1+=velocity;++track.misses;
                    crossBarPlayLine(track,oldY0,oldY1,track.y0,track.y1,barFrameNumber);
                    if((track.y0>trackingEndY&&track.done)||(track.misses>qRound(barFps*.75)&&!track.on)||track.y0>barHeight)track.done=true;
                }
                ++barFrameNumber;barOffset+=barFrameBytes;
            }
            if(barOffset)barRaw.remove(0,barOffset);QCoreApplication::processEvents();
        }
        barDecoder.waitForFinished(5000);
        const float finalVelocity=qMax(.5f,velocityConsensus());
        for(auto &track:barTracks)if(track.on&&!track.done){const double estimated=barFrameNumber+qMax(1.0f,(playY-track.y0)/finalVelocity);closeBarTrack(track,estimated);}
        std::sort(fullBarDetection.gates.begin(),fullBarDetection.gates.end(),[](const Gate&a,const Gate&b){return a.startFrame==b.startFrame?a.midi<b.midi:a.startFrame<b.startFrame;});
        fullBarDetection.gates.erase(std::remove_if(fullBarDetection.gates.begin(),fullBarDetection.gates.end(),[](const Gate &gate){return gate.endFrame-gate.startFrame<2;}),fullBarDetection.gates.end());
        // Two-colour tutorials normally use one colour per hand. Cluster hue
        // on a circle (so red at 0 and red at 179 remain neighbours), then map
        // the lower-pitched colour group to the bass staff. This avoids the
        // fixed-pitch split breaking chords that cross middle C.
        QVector<int> colouredGates;for(int i=0;i<fullBarDetection.gates.size();++i)if(fullBarDetection.gates[i].hue>=0)colouredGates.append(i);
        if(colouredGates.size()>=8){
            auto huePoint=[&](float hue){const double angle=hue*M_PI/90.0;return std::array<double,2>{std::cos(angle),std::sin(angle)};};
            auto c0=huePoint(fullBarDetection.gates[colouredGates.first()].hue),c1=c0;double farthest=-1;
            for(int index:colouredGates){const auto point=huePoint(fullBarDetection.gates[index].hue);const double distance=std::pow(point[0]-c0[0],2)+std::pow(point[1]-c0[1],2);if(distance>farthest){farthest=distance;c1=point;}}
            QVector<int> groups(colouredGates.size());
            if(farthest>.10){
                for(int pass=0;pass<8;++pass){double sx[2]{},sy[2]{};int count[2]{};
                    for(int j=0;j<colouredGates.size();++j){const auto point=huePoint(fullBarDetection.gates[colouredGates[j]].hue);const double d0=std::pow(point[0]-c0[0],2)+std::pow(point[1]-c0[1],2),d1=std::pow(point[0]-c1[0],2)+std::pow(point[1]-c1[1],2);const int group=d1<d0;groups[j]=group;sx[group]+=point[0];sy[group]+=point[1];++count[group];}
                    if(count[0]){const double norm=std::hypot(sx[0],sy[0]);if(norm>0)c0={sx[0]/norm,sy[0]/norm};}if(count[1]){const double norm=std::hypot(sx[1],sy[1]);if(norm>0)c1={sx[1]/norm,sy[1]/norm};}
                }
                double midiSum[2]{};int count[2]{};for(int j=0;j<colouredGates.size();++j){const int group=groups[j];midiSum[group]+=fullBarDetection.gates[colouredGates[j]].midi;++count[group];}
                if(count[0]>=3&&count[1]>=3){const int leftGroup=(midiSum[0]/count[0])<=(midiSum[1]/count[1])?0:1;for(int j=0;j<colouredGates.size();++j)fullBarDetection.gates[colouredGates[j]].hand=groups[j]==leftGroup?0:1;}
            }
        }
        if(qEnvironmentVariableIsSet("KEYFLOW_TRANSCRIPTION_DEBUG")){int left=0,right=0,unknown=0;for(const auto &gate:fullBarDetection.gates)gate.hand==0?++left:(gate.hand==1?++right:++unknown);std::fprintf(stderr,"full-bar tracker frames %d tracks %lld gates %lld velocity %.2f playY %d hands %d/%d/%d\n",barFrameNumber,qint64(barTracks.size()),qint64(fullBarDetection.gates.size()),velocityConsensus(),playY,left,right,unknown);}
        if(info){
            info->videoWidth=width;info->videoHeight=height;info->keyboardTop=keyboardTop;info->keyboardBottom=keyboardBottom;
            info->visibleLow=visibleLow;info->visibleHigh=visibleHigh;info->whiteKeyWidth=whiteWidth;
            info->fallPixelsPerSecond=velocityConsensus()/qMax(.0001,barScale)*barFps;
            info->laneCenters.resize(centers.size());info->laneBarWidths.fill(-1.0,centers.size());
            for(int midi=0;midi<centers.size();++midi)
                info->laneCenters[midi]=std::isfinite(centers[midi])?centers[midi]:-1.0;
            for(int midi=visibleLow;midi<=visibleHigh;++midi){QVector<float> widths;for(const auto &track:barTracks)if(track.midi==midi&&track.x1>track.x0)widths.append((track.x1-track.x0)/barScale);if(!widths.isEmpty())info->laneBarWidths[midi]=medianFloat(widths);}
        }
    }
    if(fullBarDetection.gates.size()>=8)detections.append(std::move(fullBarDetection));
    detections.append(std::move(highlightedKeysRaw));
    detections.append(std::move(highlightedKeys));
    int viableDetections=0;
    for(auto &d:detections){
        for(int midi=21;midi<=108;++midi){if(d.brightLanes[midi].active&&frameNumber-d.brightLanes[midi].startFrame>=3)d.brightGates.append({midi,d.brightLanes[midi].startFrame,int(frameNumber)});if(d.colorLanes[midi].active&&frameNumber-d.colorLanes[midi].startFrame>=3)d.colorGates.append({midi,d.colorLanes[midi].startFrame,int(frameNumber)});}
        // Saturated bars include both hands in the common two-colour tutorial
        // layout (for example red bass plus yellow treble).  The old order
        // preferred luminance as soon as it found eight notes, which silently
        // discarded darker red/blue bars and kept only the brighter hand.
        // Use colour when it is available and reserve luminance for genuinely
        // monochrome falling-note videos.
        if(d.distance>=-2.5)d.gates=d.colorGates.size()>=8?d.colorGates:d.brightGates;
        d.gates.erase(std::remove_if(d.gates.begin(),d.gates.end(),[](const Gate &gate){return gate.startFrame<=1;}),d.gates.end());
        std::sort(d.gates.begin(),d.gates.end(),[](const Gate&a,const Gate&b){return a.startFrame==b.startFrame?a.midi<b.midi:a.startFrame<b.startFrame;});
        if(!d.gates.isEmpty())++viableDetections;
    }
    if(!viableDetections){setError(error,"No solid falling-note bars were found above the keyboard. The video needs a fixed, full-width piano keyboard and visible falling bars.");return false;}

    // Use the densest viable row for the visual tempo fallback. Final row
    // selection happens below after each draft is rendered and compared with
    // the original soundtrack.
    int tempoDetection=0;for(int i=1;i<detections.size();++i)if(detections[i].gates.size()>detections[tempoDetection].gates.size())tempoDetection=i;
    QMap<int,int> visualGroups;for(const auto &gate:detections[tempoDetection].gates)++visualGroups[gate.startFrame];
    OnsetAnalysis visualOnsets;visualOnsets.framesPerSecond=fps;visualOnsets.envelope.fill(0.0,int(frameNumber));
    for(auto it=visualGroups.cbegin();it!=visualGroups.cend();++it)if(it.key()>=0&&it.key()<visualOnsets.envelope.size())visualOnsets.envelope[it.key()]=std::sqrt(double(it.value()));
    if(visualOnsets.envelope.size()>=3){const auto impulses=visualOnsets.envelope;for(int i=1;i+1<impulses.size();++i)visualOnsets.envelope[i]=.25*impulses[i-1]+.5*impulses[i]+.25*impulses[i+1];}
    const TempoEstimate visualTempo=estimateTempoFromOnsets(visualOnsets);
    DecodedAudio soundtrack;OnsetAnalysis audioOnsets;TempoEstimate audioTempo;QString audioAnalysisError;
    if(decodeMonoAudio(path,soundtrack,&audioAnalysisError,canceled)&&buildOnsetAnalysis(soundtrack,audioOnsets,&audioAnalysisError,{},canceled))audioTempo=estimateTempoFromOnsets(audioOnsets);
    const PitchOnsetEvidence pitchOnsets=soundtrack.samples.isEmpty()?PitchOnsetEvidence{}:buildPitchOnsetEvidence(soundtrack,canceled);
    if(canceled&&canceled()){setError(error,"Transcription canceled.");return false;}
    const bool automaticTempo=tempo<=0;int resolvedTempo=automaticTempo?(audioTempo.bpm?audioTempo.bpm:(visualTempo.bpm?visualTempo.bpm:96)):qBound(30,tempo,260);
    double tempoConfidence=automaticTempo?(audioTempo.bpm?audioTempo.confidence:visualTempo.confidence):1.0;
    if(automaticTempo&&audioTempo.bpm&&visualTempo.bpm){
        const double difference=qAbs(audioTempo.preciseBpm-visualTempo.preciseBpm)/qMax(1.0,audioTempo.preciseBpm);
        if(difference<.04)tempoConfidence=qMin(1.0,qMax(audioTempo.confidence,visualTempo.confidence)+.12);
        else tempoConfidence*=.65;
    }
    constexpr double ticksPerBeat=24.0;
    const double beatsPerSecond=resolvedTempo/60.0;
    auto makeDraft=[&](const QVector<Gate> &gates,int minimumDurationTicks=1){
        QVector<NoteEvent> draft;if(gates.isEmpty())return draft;
        const int firstFrame=gates.first().startFrame;
        struct Candidate{int midi,startTick;double rawDurationTicks;Hand hand;};
        QVector<Candidate> candidates;QVector<int> onsetTicks[2];
        for(const auto &gate:gates){const int startTick=qMax(0,qRound(((gate.startFrame-firstFrame)/fps)*beatsPerSecond*ticksPerBeat));const double rawTicks=((gate.endFrame-gate.startFrame)/fps)*beatsPerSecond*ticksPerBeat;const Hand hand=gate.hand==0?Hand::Left:(gate.hand==1?Hand::Right:(gate.midi<55?Hand::Left:Hand::Right));candidates.append({gate.midi,startTick,rawTicks,hand});onsetTicks[hand==Hand::Left?0:1].append(startTick);}
        // Brightness and particle effects can make notes from one chord appear
        // a frame apart. Collapse those sub-grid differences before engraving
        // so the chord remains a chord in Live Follow and MusicXML.
        QVector<int> anchors;for(const auto &candidate:candidates)anchors.append(candidate.startTick);std::sort(anchors.begin(),anchors.end());
        QVector<int> clustered;for(int tick:anchors){if(clustered.isEmpty()||tick-clustered.last()>1)clustered.append(tick);}
        for(auto &candidate:candidates){auto nearest=std::lower_bound(clustered.cbegin(),clustered.cend(),candidate.startTick);if(nearest!=clustered.cend()&&qAbs(*nearest-candidate.startTick)<=1)candidate.startTick=*nearest;else if(nearest!=clustered.cbegin()&&qAbs(*(nearest-1)-candidate.startTick)<=1)candidate.startTick=*(nearest-1);}
        for(auto &ticks:onsetTicks)ticks.clear();for(const auto &candidate:candidates)onsetTicks[candidate.hand==Hand::Left?0:1].append(candidate.startTick);
        for(auto &ticks:onsetTicks){std::sort(ticks.begin(),ticks.end());ticks.erase(std::unique(ticks.begin(),ticks.end()),ticks.end());}
        const QVector<int> notatedDurations{3,4,6,8,9,12,16,18,24,32,36,48,72,96,144,192};
        auto snapDuration=[&](int wanted,int limit){
            // Durations beyond eight beats are valid held notes.  They are tied
            // across measures by the score renderer and must not be silently
            // shortened to the largest item in this short-note snap table.
            if(wanted>notatedDurations.last()){
                const int longGrid=12;const int snapped=qMax(notatedDurations.last(),qRound(wanted/double(longGrid))*longGrid);
                return qMax(1,qMin(snapped,limit));
            }
            int best=3,bestDistance=std::numeric_limits<int>::max();for(int value:notatedDurations){if(value>limit)continue;const int distance=qAbs(value-wanted);if(distance<bestDistance){bestDistance=distance;best=value;}}
            return qMax(1,qMin(best,limit));
        };
        for(const auto &candidate:candidates){
            const auto &staffTicks=onsetTicks[candidate.hand==Hand::Left?0:1];const auto later=std::upper_bound(staffTicks.begin(),staffTicks.end(),candidate.startTick);const int nextDelta=later==staffTicks.end()?std::numeric_limits<int>::max():*later-candidate.startTick;
            int durationTicks=0;if(candidate.rawDurationTicks<6.6)durationTicks=nextDelta<=6?nextDelta:(candidate.rawDurationTicks>=3.3?6:3);else durationTicks=qMax(1,qRound(candidate.rawDurationTicks));
            int nextSame=std::numeric_limits<int>::max();for(const auto &other:candidates)if(other.midi==candidate.midi&&other.startTick>candidate.startTick)nextSame=qMin(nextSame,other.startTick);if(nextSame!=std::numeric_limits<int>::max())durationTicks=qMin(durationTicks,nextSame-candidate.startTick);
            // The complete-bar tracker runs at 8-12 fps.  It can locate an
            // attack accurately by interpolating the moving edge, but a very
            // short rectangle is not measured finely enough to justify a
            // 1/32-note release.  Those 60-90 ms cutoffs made otherwise-correct
            // repeated chords sound like distant, ghostly plucks.  Preserve at
            // least a sixteenth note unless the same key genuinely retriggers
            // sooner; higher-rate key-surface passes keep their finer limit.
            durationTicks=qMax(durationTicks,qMin(minimumDurationTicks,nextSame));
            // A held key may continue under later notes in the same hand.  Its
            // own next attack is the only hard limit; truncating at every
            // same-staff onset made bubbles and notation show false staccato.
            durationTicks=snapDuration(durationTicks,nextSame);
            if(durationTicks>0)draft.append({candidate.midi,candidate.startTick/ticksPerBeat,durationTicks/ticksPerBeat,94,candidate.hand});
        }
        return sortNotes(draft);
    };
    struct DraftQuality{QVector<NoteEvent> notes;TimingAgreement timing;double rendered=-1,score=-1,sourceStart=0;int detection=-1;};
    QVector<DraftQuality> attempts;
    for(int detectionIndex=0;detectionIndex<detections.size();++detectionIndex){
        const auto &gates=detections[detectionIndex].gates;if(gates.isEmpty())continue;
        QMap<int,int> groups;for(const auto &gate:gates)++groups[gate.startFrame];QVector<double> times;times.reserve(groups.size());for(auto it=groups.cbegin();it!=groups.cend();++it)times.append(analysisStartSecond+it.key()/fps);
        // A key-surface highlight is the played instant, so it may only differ
        // from the soundtrack by ordinary A/V latency. Falling bars sampled
        // above the keyboard legitimately need a longer travel-time offset.
        const bool keySurfaceDetection=detections[detectionIndex].distance<0&&detections[detectionIndex].distance> -2.5;
        const TimingAgreement timing=keySurfaceDetection
            ?compareOnsets(times,audioOnsets,-.25,.35)
            :compareOnsets(times,audioOnsets);
        const bool fullBarDetection=detections[detectionIndex].distance< -2.5;
        auto draft=makeDraft(gates,fullBarDetection?6:1);
        const double sourceStart=qMax(0.0,analysisStartSecond+gates.first().startFrame/fps+(timing.score>=0?timing.offsetSeconds:0.0));
        const double rendered=soundtrack.samples.isEmpty()?-1.0:compareRenderedDraft(draft,resolvedTempo,soundtrack,sourceStart);
        const double span=qMax(1.0,(gates.last().startFrame-gates.first().startFrame)/fps);const double density=gates.size()/span;
        const double densityScore=1.0-std::exp(-density/1.6);const double coverage=qBound(0.0,(gates.last().endFrame-gates.first().startFrame)/double(qMax<qint64>(1,frameNumber)),1.0);
        int tiny=0,fragments=0;QVector<NoteEvent> byPitch=draft;for(const auto &note:draft)if(note.duration<=.13)++tiny;
        std::sort(byPitch.begin(),byPitch.end(),[](const NoteEvent&a,const NoteEvent&b){return a.note==b.note?a.start<b.start:a.note<b.note;});
        for(int i=1;i<byPitch.size();++i)if(byPitch[i].note==byPitch[i-1].note&&byPitch[i].start-(byPitch[i-1].start+byPitch[i-1].duration)<=.09)++fragments;
        const double fragmentPenalty=draft.isEmpty()?0.0:.18*tiny/draft.size()+.24*fragments/draft.size();
        // Dense sparkle at the strike line is not rewarded blindly: hundreds
        // of unsupported 1/8-beat fragments are penalized.
        // Dense repeated-note tutorials can legitimately exceed ten individual
        // key attacks per second once both hands and chords are counted.  The
        // former limit rejected the repaired surface draft precisely because it
        // recovered those attacks.  Only truly implausible sparkle storms are
        // penalized here; fragment and soundtrack checks still apply below it.
        const double excessDensityPenalty=.02*qMax(0.0,density-20.0);
        double score=.65*(rendered>=0?rendered:.45)+.15*(timing.score>=0?timing.score:.25)+.12*densityScore+.08*coverage-fragmentPenalty-excessDensityPenalty;
        // Full tracked rectangles encode both the attack and the release and
        // are immune to decorative flashes at the keyboard. Prefer that
        // evidence when the soundtrack checks are at least credible.
        if(detections[detectionIndex].distance< -2.5&&rendered>=.22&&timing.score>=.17)score+=.13;
        if(qEnvironmentVariableIsSet("KEYFLOW_TRANSCRIPTION_DEBUG"))std::fprintf(stderr,"row %.3f gates %lld draft %lld timing %.3f render %.3f density %.2f coverage %.2f fragments %.3f quality %.3f start %.2f\n",detections[detectionIndex].distance,qint64(gates.size()),qint64(draft.size()),timing.score,rendered,density,coverage,fragmentPenalty,score,sourceStart);
        attempts.append({std::move(draft),timing,rendered,score,sourceStart,detectionIndex});
    }
    // The conservative surface pass knows which keys stayed down, while the
    // flash-aware pass separates rapid restrikes.  Keep every conservative
    // attack and admit an additional split only when the soundtrack has a real
    // onset at that instant.  This repairs repeated notes without engraving
    // every sparkle animation as another piano attack.
    int rawSurface=-1,splitSurface=-1;
    for(int i=0;i<attempts.size();++i){
        const double distance=detections[attempts[i].detection].distance;
        if(qAbs(distance+2.0)<.05)rawSurface=i;else if(qAbs(distance+1.0)<.05)splitSurface=i;
    }
    if(rawSurface>=0&&splitSurface>=0&&!audioOnsets.envelope.isEmpty()){
        const auto &rawAttempt=attempts[rawSurface];const auto &splitAttempt=attempts[splitSurface];
        const double secondsPerBeat=60.0/resolvedTempo;QVector<NoteEvent> refined;refined.reserve(splitAttempt.notes.size());
        QVector<bool> rawUsed(rawAttempt.notes.size(),false);
        auto sourceTime=[&](const DraftQuality &attempt,const NoteEvent &note){return attempt.sourceStart+note.start*secondsPerBeat;};
        auto onsetStrength=[&](double second){
            const int center=qRound(second*audioOnsets.framesPerSecond),radius=qMax(2,qRound(audioOnsets.framesPerSecond*.085));double strongest=0;
            for(int i=qMax(0,center-radius);i<qMin(audioOnsets.envelope.size(),center+radius+1);++i)strongest=qMax(strongest,audioOnsets.envelope[i]);
            return strongest;
        };
        for(const auto &note:splitAttempt.notes){
            const double absoluteStart=sourceTime(splitAttempt,note);int closest=-1;double closestDistance=.095;
            for(int i=0;i<rawAttempt.notes.size();++i)if(!rawUsed[i]&&rawAttempt.notes[i].note==note.note){
                const double distance=qAbs(sourceTime(rawAttempt,rawAttempt.notes[i])-absoluteStart);
                if(distance<closestDistance){closestDistance=distance;closest=i;}
            }
            if(closest>=0){rawUsed[closest]=true;refined.append(note);}
            else if(onsetStrength(absoluteStart)>=.12||pitchOnsets.strength(absoluteStart,note.note)>=.15)refined.append(note);
        }
        // If rejected flashes divided one genuinely held key, extend the last
        // accepted segment to the conservative gate end.  A supported restrike
        // still remains a separate attack and therefore sounds/prints correctly.
        for(const auto &rawNote:rawAttempt.notes){
            const double rawStart=sourceTime(rawAttempt,rawNote),rawEnd=rawStart+rawNote.duration*secondsPerBeat;
            int first=-1,last=-1;double lastStart=-1;
            for(int i=0;i<refined.size();++i)if(refined[i].note==rawNote.note){
                const double start=sourceTime(splitAttempt,refined[i]);
                if(start>=rawStart-.10&&start<=rawEnd+.10){if(first<0)first=i;if(start>lastStart){lastStart=start;last=i;}}
            }
            if(first<0){
                NoteEvent restored=rawNote;restored.start=qMax(0.0,(rawStart-splitAttempt.sourceStart)/secondsPerBeat);refined.append(restored);
            }else if(last>=0){
                const double wanted=qMax(1.0/24.0,(rawEnd-lastStart)/secondsPerBeat);
                refined[last].duration=qMax(refined[last].duration,wanted);
            }
        }
        refined=sortNotes(refined);
        const double rendered=compareRenderedDraft(refined,resolvedTempo,soundtrack,splitAttempt.sourceStart);
        const double span=qMax(1.0,(refined.last().start+refined.last().duration-refined.first().start)*secondsPerBeat);
        const double density=refined.size()/span,densityScore=1.0-std::exp(-density/1.6);
        int fragments=0;QVector<NoteEvent> byPitch=refined;std::sort(byPitch.begin(),byPitch.end(),[](const NoteEvent&a,const NoteEvent&b){return a.note==b.note?a.start<b.start:a.note<b.note;});
        for(int i=1;i<byPitch.size();++i)if(byPitch[i].note==byPitch[i-1].note&&byPitch[i].start-(byPitch[i-1].start+byPitch[i-1].duration)<=.09)++fragments;
        const double fragmentPenalty=refined.isEmpty()?0.0:.12*fragments/refined.size();
        const double excessDensityPenalty=.02*qMax(0.0,density-20.0);
        double score=.65*(rendered>=0?rendered:.45)+.15*(splitAttempt.timing.score>=0?splitAttempt.timing.score:.25)+.12*densityScore+.08-fragmentPenalty-excessDensityPenalty;
        // This is the actual retry pass: it retains the raw key identities but
        // restores only restrikes corroborated by the source.  Prefer it over
        // the latched baseline when its rendered pitch match is no worse and it
        // stays within a credible piano density.
        if(rendered>=.45&&rendered>=rawAttempt.rendered-.015&&refined.size()<=rawAttempt.notes.size()*1.75)
            score=qMax(score,rawAttempt.score+.015);
        if(qEnvironmentVariableIsSet("KEYFLOW_TRANSCRIPTION_DEBUG"))std::fprintf(stderr,"refined surface draft %lld timing %.3f render %.3f density %.2f fragments %.3f quality %.3f start %.2f\n",qint64(refined.size()),splitAttempt.timing.score,rendered,density,fragmentPenalty,score,splitAttempt.sourceStart);
        attempts.append({std::move(refined),splitAttempt.timing,rendered,score,splitAttempt.sourceStart,splitAttempt.detection});
    }
    // The full-rectangle pass has the best durations, but effects can erase a
    // rectangle for a frame or two exactly as it reaches the keyboard.  The
    // strike-line pass sees many of those attacks.  Previously the attempts
    // competed and the winning full-bar draft discarded every note found only
    // at the strike line.  Build a conservative union: add a missing surface
    // note when it belongs to an already-detected chord, or when both the audio
    // onset and pitch evidence support it.
    int fullAttempt=-1,bestSurfaceAttempt=-1;
    for(int i=0;i<attempts.size();++i){
        const double distance=detections[attempts[i].detection].distance;
        if(distance< -2.5&&(fullAttempt<0||attempts[i].score>attempts[fullAttempt].score))fullAttempt=i;
        if(qAbs(distance+1.0)<.05&&(bestSurfaceAttempt<0||attempts[i].score>attempts[bestSurfaceAttempt].score))bestSurfaceAttempt=i;
    }
    if(fullAttempt>=0&&bestSurfaceAttempt>=0&&!audioOnsets.envelope.isEmpty()){
        const auto &full=attempts[fullAttempt],&surface=attempts[bestSurfaceAttempt];
        const double secondsPerBeat=60.0/resolvedTempo;
        auto absoluteTime=[&](const DraftQuality &attempt,const NoteEvent &note){return attempt.sourceStart+note.start*secondsPerBeat;};
        auto onsetStrength=[&](double second){
            const int center=qRound(second*audioOnsets.framesPerSecond),radius=qMax(2,qRound(audioOnsets.framesPerSecond*.070));double strongest=0;
            for(int i=qMax(0,center-radius);i<qMin(audioOnsets.envelope.size(),center+radius+1);++i)strongest=qMax(strongest,audioOnsets.envelope[i]);
            return strongest;
        };
        QVector<NoteEvent> hybrid=full.notes;int added=0;
        for(const auto &candidate:surface.notes){
            const double when=absoluteTime(surface,candidate);bool samePitch=false,chordMate=false;
            for(const auto &existing:full.notes){
                const double delta=qAbs(absoluteTime(full,existing)-when);
                if(existing.note==candidate.note&&delta<=.095){samePitch=true;break;}
                if(delta<=.060)chordMate=true;
            }
            if(samePitch)continue;
            const double audio=onsetStrength(when),pitch=pitchOnsets.strength(when,candidate.note);
            Q_UNUSED(audio);Q_UNUSED(pitch);Q_UNUSED(chordMate);
            // This surface attempt is already the refined pass: conservative
            // held-key identities plus only those extra flashes corroborated
            // by the soundtrack. Filtering it a second time discarded a final
            // set of isolated, visibly real notes in the Director overlay.
            NoteEvent restored=candidate;
            restored.start=qMax(0.0,qRound(((when-full.sourceStart)/secondsPerBeat)*24.0)/24.0);
            // Surface colour can stay illuminated long after the bar ends.
            // It is reliable attack evidence, not reliable release evidence.
            // Keep its ordinary short duration but never let a latched glow
            // become a many-measure held note. Genuine long bars already come
            // from the full-rectangle draft and are left untouched.
            restored.duration=qBound(1.0/24.0,qRound(restored.duration*24.0)/24.0,4.0);
            if(restored.start<0||restored.start>durationSeconds*resolvedTempo/60.0)continue;
            hybrid.append(restored);++added;
        }
        // If the strike-line fallback wants to add nearly as many notes as the
        // entire rectangle tracker, it is not filling holes; it is reading
        // particles/key glow as a second performance.  That was the 3,515-note
        // "haunted" HOI4 result.  Keep the union only when it is a bounded
        // repair (Upside Down is about 43%, while the bad HOI4 pass was 278%).
        if(added<=qMax(12,qRound(full.notes.size()*.75))){
        hybrid=sortNotes(hybrid);
        // Do not allow a restored gate to sustain through a later strike of the
        // same key; that would replace missed notes with impossible overlaps.
        for(int i=0;i<hybrid.size();++i)for(int j=i+1;j<hybrid.size();++j)if(hybrid[j].note==hybrid[i].note&&hybrid[j].start>hybrid[i].start){
            hybrid[i].duration=qMin(hybrid[i].duration,qMax(1.0/24.0,hybrid[j].start-hybrid[i].start));break;
        }
        const double rendered=compareRenderedDraft(hybrid,resolvedTempo,soundtrack,full.sourceStart);
        const double density=hybrid.size()/qMax(1.0,(hybrid.last().start+hybrid.last().duration)*secondsPerBeat);
        double score=.65*(rendered>=0?rendered:.45)+.15*(full.timing.score>=0?full.timing.score:.25)+.12*(1.0-std::exp(-density/1.6))+.08+.13;
        if(added>0&&added<=full.notes.size()*.35&&rendered>=full.rendered-.02)score=qMax(score,full.score+.02);
        if(qEnvironmentVariableIsSet("KEYFLOW_TRANSCRIPTION_DEBUG"))std::fprintf(stderr,"hybrid full+surface draft %lld added %d timing %.3f render %.3f quality %.3f start %.2f\n",qint64(hybrid.size()),added,full.timing.score,rendered,score,full.sourceStart);
        attempts.append({std::move(hybrid),full.timing,rendered,score,full.sourceStart,full.detection});
        }else if(qEnvironmentVariableIsSet("KEYFLOW_TRANSCRIPTION_DEBUG"))std::fprintf(stderr,"hybrid full+surface rejected: %d additions for %lld tracked bars\n",added,qint64(full.notes.size()));
    }
    const int minimumPlausibleNotes=qMax(8,qRound((durationSeconds-analysisStartSecond)*.45));
    attempts.erase(std::remove_if(attempts.begin(),attempts.end(),[&](const DraftQuality &attempt){
        QSet<int> pitches;for(const auto &note:attempt.notes)pitches.insert(note.note);
        return attempt.notes.size()<minimumPlausibleNotes||pitches.size()<3;
    }),attempts.end());
    if(attempts.isEmpty()){setError(error,"The falling notes did not produce a playable draft.");return false;}
    const QString dumpDirectory=qEnvironmentVariable("KEYFLOW_TRANSCRIPTION_DUMP_DIR");
    if(!dumpDirectory.isEmpty()){
        QDir().mkpath(dumpDirectory);
        for(int i=0;i<attempts.size();++i){
            const int detectionIndex=attempts[i].detection;
            const double distance=detections[detectionIndex].distance;
            const QString row=distance< -2.5?QStringLiteral("full-bars"):(distance<0?QStringLiteral("surface"):QString::number(distance,'f',3));
            QString ignored;
            MusicDocument::saveJson(QDir(dumpDirectory).filePath(QString("candidate-%1-%2.keyflow").arg(i).arg(row)),attempts[i].notes,resolvedTempo,QString("debug candidate %1").arg(row),&ignored);
        }
    }
    const auto bestAttempt=std::max_element(attempts.cbegin(),attempts.cend(),[](const auto&a,const auto&b){return a.score<b.score;});
    int selectedPitchShift=0;double selectedRendered=bestAttempt->rendered;
    if(!soundtrack.samples.isEmpty()&&bestAttempt->rendered>=0){
        int candidateShift=0;double candidateRendered=bestAttempt->rendered;
        // The visible-key pattern identifies lanes reliably, but an unlabeled
        // cropped keyboard can leave its absolute pitch class a semitone or
        // two ambiguous.  Perform the requested play-back/source comparison
        // over a deliberately small range and accept a correction only when
        // the rendered notes improve materially.
        for(int semitones=-3;semitones<=3;++semitones)if(semitones){
            QVector<NoteEvent> shifted=bestAttempt->notes;bool valid=true;
            for(auto &note:shifted){note.note+=semitones;if(note.note<21||note.note>108){valid=false;break;}}
            if(valid){const double agreement=compareRenderedDraft(shifted,resolvedTempo,soundtrack,bestAttempt->sourceStart);if(agreement>candidateRendered){candidateRendered=agreement;candidateShift=semitones;}}
        }
        if(candidateShift&&candidateRendered>=bestAttempt->rendered+.025){selectedPitchShift=candidateShift;selectedRendered=candidateRendered;}
        if(qEnvironmentVariableIsSet("KEYFLOW_TRANSCRIPTION_DEBUG"))std::fprintf(stderr,"source pitch retry selected %+d (%.3f -> %.3f)\n",selectedPitchShift,bestAttempt->rendered,selectedRendered);
    }
    if(qEnvironmentVariableIsSet("KEYFLOW_TRANSCRIPTION_DEBUG")&&!soundtrack.samples.isEmpty()){
        for(int semitones=-12;semitones<=12;++semitones){
            QVector<NoteEvent> shifted=bestAttempt->notes;bool valid=true;
            for(auto &note:shifted){note.note+=semitones;if(note.note<21||note.note>108){valid=false;break;}}
            if(valid)std::fprintf(stderr,"pitch-shift %+d render %.3f\n",semitones,compareRenderedDraft(shifted,resolvedTempo,soundtrack,bestAttempt->sourceStart));
        }
    }
    notes=bestAttempt->notes;
    if(selectedPitchShift)for(auto &note:notes)note.note+=selectedPitchShift;
    if(!soundtrack.samples.isEmpty()){
        const double audioEnd=lastAudibleSecond(soundtrack);
        if(audioEnd>=0){
            const double secondsPerBeat=60.0/resolvedTempo;
            notes.erase(std::remove_if(notes.begin(),notes.end(),[&](const NoteEvent &note){
                return bestAttempt->sourceStart+note.start*secondsPerBeat>audioEnd+.20;
            }),notes.end());
        }
        applySourceDynamics(notes,resolvedTempo,soundtrack,bestAttempt->sourceStart);
    }
    // A piano key cannot begin a second strike while its previous synthesized
    // instance is still held.  Quantization and briefly duplicated visual
    // tracks could otherwise overlap same-pitch events, which makes both the
    // notation and Director rectangles look like one clumped bar.
    notes=sortNotes(notes);QVector<NoteEvent> separated;separated.reserve(notes.size());QVector<int> previousByPitch(109,-1);
    for(const auto &note:notes){
        const int previous=note.note>=0&&note.note<previousByPitch.size()?previousByPitch[note.note]:-1;
        if(previous>=0){auto &prior=separated[previous];
            if(qAbs(note.start-prior.start)<1e-6){prior.duration=qMax(prior.duration,note.duration);prior.velocity=qMax(prior.velocity,note.velocity);continue;}
            prior.duration=qMin(prior.duration,qMax(1.0/24.0,note.start-prior.start));
        }
        separated.append(note);if(note.note>=0&&note.note<previousByPitch.size())previousByPitch[note.note]=separated.size()-1;
    }
    notes=std::move(separated);
    if(info){
        info->tempo=resolvedTempo;info->tempoEstimated=automaticTempo&&(audioTempo.bpm>0||visualTempo.bpm>0);info->tempoConfidence=tempoConfidence;info->audiovisualAgreement=bestAttempt->timing.score;info->renderedAgreement=selectedRendered;info->audioOffsetSeconds=bestAttempt->timing.offsetSeconds;info->sourceStartSeconds=bestAttempt->sourceStart;info->refinementPasses=attempts.size();
        info->directorNotes.clear();info->directorBarCenters.clear();info->directorBarWidths.clear();info->directorStartSeconds=0.0;
        // Director must show the raw visual rectangles, not the audio-aligned,
        // beat-quantized staff approximation. Match every accepted final note
        // back to its original full-bar gate when possible; restored strike-line
        // notes retain their refined timing as the fallback.
        const auto &visualGates=detections[bestAttempt->detection].gates;
        if(!visualGates.isEmpty()){
            const int firstFrame=visualGates.first().startFrame;const double secondsPerBeat=60.0/resolvedTempo;
            info->directorStartSeconds=analysisStartSecond+firstFrame/fps;
            QVector<NoteEvent> rawVisual;QVector<double> rawCenters,rawWidths;rawVisual.reserve(visualGates.size());rawCenters.reserve(visualGates.size());rawWidths.reserve(visualGates.size());
            for(const auto &gate:visualGates){const Hand hand=gate.hand==0?Hand::Left:(gate.hand==1?Hand::Right:(gate.midi<55?Hand::Left:Hand::Right));rawVisual.append({gate.midi+selectedPitchShift,(gate.startFrame-firstFrame)/fps/secondsPerBeat,(gate.endFrame-gate.startFrame)/fps/secondsPerBeat,94,hand});
                const bool exact=gate.x1>gate.x0;rawCenters.append(exact?(gate.x0+gate.x1)*.5:info->laneCenters.value(gate.midi,-1));rawWidths.append(exact?gate.x1-gate.x0:info->laneBarWidths.value(gate.midi,-1));}
            QVector<bool> used(rawVisual.size(),false);const double visualScoreStart=bestAttempt->sourceStart-bestAttempt->timing.offsetSeconds;
            for(const auto &note:notes){const double wanted=visualScoreStart+note.start*secondsPerBeat;int closest=-1;double distance=.12;
                for(int i=0;i<rawVisual.size();++i)if(!used[i]&&rawVisual[i].note==note.note){const double candidate=info->directorStartSeconds+rawVisual[i].start*secondsPerBeat,delta=qAbs(candidate-wanted);if(delta<distance){distance=delta;closest=i;}}
                if(closest>=0){used[closest]=true;info->directorNotes.append(rawVisual[closest]);info->directorBarCenters.append(rawCenters[closest]);info->directorBarWidths.append(rawWidths[closest]);}
                else {NoteEvent restored=note;restored.start=qMax(0.0,(wanted-info->directorStartSeconds)/secondsPerBeat);const int visualPitch=note.note-selectedPitchShift;info->directorNotes.append(restored);info->directorBarCenters.append(info->laneCenters.value(visualPitch,-1));info->directorBarWidths.append(info->laneBarWidths.value(visualPitch,-1));}
            }
            QVector<int> previousDirectorByPitch(109,-1);
            for(int i=0;i<info->directorNotes.size();++i){auto &note=info->directorNotes[i];const int previous=note.note>=0&&note.note<previousDirectorByPitch.size()?previousDirectorByPitch[note.note]:-1;
                if(previous>=0){auto &prior=info->directorNotes[previous];if(note.start>prior.start&&prior.start+prior.duration>note.start)prior.duration=qMax(1.0/24.0,note.start-prior.start);}
                if(note.note>=0&&note.note<previousDirectorByPitch.size())previousDirectorByPitch[note.note]=i;
            }
        }
    }
    if(progress)progress(100);return true;
}
}

bool MusicDocument::transcribeFallingNotesVideo(const QString &path, int tempo,
                                                QVector<NoteEvent> &notes, QString *error,
                                                std::function<void(int)> progress,
                                                std::function<bool()> canceled,
                                                TranscriptionInfo *info) {
    return transcribeDetailedPianoRoll(path,tempo,notes,error,progress,canceled,info);
    QProcess probe;
    probe.start("ffprobe", {"-v","error","-select_streams","v:0","-show_entries",
                            "stream=width,height,r_frame_rate:format=duration","-of","json",path});
    if (!probe.waitForFinished(10000) || probe.exitCode() != 0) {
        setError(error, "FFmpeg could not inspect this video."); return false;
    }
    const QJsonObject probeRoot = QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
    const QJsonArray streams = probeRoot.value("streams").toArray();
    if (streams.isEmpty()) { setError(error, "No video stream was found."); return false; }
    const QJsonObject stream = streams.at(0).toObject();
    const int width = stream.value("width").toInt();
    const int height = stream.value("height").toInt();
    const double durationSeconds = probeRoot.value("format").toObject().value("duration").toString().toDouble();
    if (durationSeconds <= 0 || width < 320 || height < 240) {
        setError(error, "The video timing or resolution is not suitable for falling-note recognition."); return false;
    }

    // Learn the fixed keyboard strike line from a clean early frame. A full-width keyboard
    // creates a much stronger horizontal edge than the moving note blocks above it.
    const double referenceSecond = qMin(5.0, durationSeconds * .08);
    QProcess referenceDecoder;
    referenceDecoder.start("ffmpeg", {"-v","error","-ss",QString::number(referenceSecond,'f',3),
                                      "-i",path,"-frames:v","1","-f","rawvideo","-pix_fmt","bgr24","pipe:1"});
    if (!referenceDecoder.waitForFinished(30000) || referenceDecoder.exitCode() != 0) {
        setError(error, "FFmpeg could not decode a reference video frame."); return false;
    }
    const QByteArray referenceBytes = referenceDecoder.readAllStandardOutput();
    const qsizetype frameBytes = qsizetype(width) * height * 3;
    if (referenceBytes.size() < frameBytes) { setError(error, "The decoded reference frame was incomplete."); return false; }
    cv::Mat reference(height, width, CV_8UC3,
                      const_cast<char*>(referenceBytes.constData()));
    reference = reference.clone();
    cv::Mat grayReference;
    cv::cvtColor(reference, grayReference, cv::COLOR_BGR2GRAY);
    cv::Mat verticalGradient;
    cv::Sobel(grayReference, verticalGradient, CV_32F, 0, 1, 3);
    int keyboardTop = int(height * .44); double bestEdge = -1;
    for (int y = int(height * .30); y < int(height * .70); ++y) {
        // The strike line is the upper edge where the usually brighter keyboard begins.
        // Keeping the sign avoids mistakenly choosing the stronger lower edge of the keys.
        const cv::Scalar strength = cv::mean(verticalGradient.row(y));
        if (strength[0] > bestEdge) { bestEdge = strength[0]; keyboardTop = y; }
    }
    if (bestEdge < 3.0) { setError(error, "A stable horizontal piano keyboard could not be located."); return false; }

    const int bandHeight = qBound(24, int(height * .065), 72);
    const int bandTop = qMax(0, keyboardTop - bandHeight);
    const cv::Rect strikeBand(0, bandTop, width, keyboardTop - bandTop);
    cv::Mat background = reference(strikeBand).clone();
    cv::Mat backgroundGray, backgroundHsv;
    cv::cvtColor(background, backgroundGray, cv::COLOR_BGR2GRAY);
    cv::cvtColor(background, backgroundHsv, cv::COLOR_BGR2HSV);

    // Map horizontal lanes to an 88-key piano (A0–C8). White and black key centers have
    // different spacing, so a simple 88-way division would assign accidentals incorrectly.
    QVector<double> centers(109, 0.0); int whiteCount = 0, totalWhites = 0;
    for (int midi = 21; midi <= 108; ++midi) {
        const int pc = midi % 12; const bool black = pc==1 || pc==3 || pc==6 || pc==8 || pc==10;
        if (!black) ++totalWhites;
    }
    const double whiteWidth = width / double(totalWhites);
    for (int midi = 21; midi <= 108; ++midi) {
        const int pc = midi % 12; const bool black = pc==1 || pc==3 || pc==6 || pc==8 || pc==10;
        if (black) centers[midi] = whiteCount * whiteWidth;
        else { centers[midi] = (whiteCount + .5) * whiteWidth; ++whiteCount; }
    }

    struct LaneState { bool active=false; int hits=0; int misses=0; double candidate=0; double start=0; };
    QVector<LaneState> lane(109);
    struct TimedNote { int midi; double start; double end; };
    QVector<TimedNote> detected;
    const double targetFps = 20.0;
    const double dt = 1.0 / targetFps;
    cv::Mat frame, hsv, gray, diff, mask, saturation, backgroundSaturation;
    cv::extractChannel(backgroundHsv, backgroundSaturation, 1);
    qint64 analyzed = 0;
    const qint64 expectedAnalyzed = qMax<qint64>(1, qint64(qMin(300.0, durationSeconds) * targetFps));

    auto analyzeFrame = [&](const cv::Mat &decodedFrame) {
        frame = decodedFrame;
        cv::Mat band = frame(strikeBand);
        cv::cvtColor(band, gray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(band, hsv, cv::COLOR_BGR2HSV);
        cv::absdiff(gray, backgroundGray, diff);
        cv::threshold(diff, mask, 32, 255, cv::THRESH_BINARY);
        cv::extractChannel(hsv, saturation, 1);
        cv::Mat saturated, saturationDifference;
        cv::absdiff(saturation, backgroundSaturation, saturationDifference);
        cv::threshold(saturationDifference, saturated, 38, 255, cv::THRESH_BINARY);
        cv::bitwise_or(mask, saturated, mask);
        cv::morphologyEx(mask, mask, cv::MORPH_OPEN,
                         cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3,3)));
        cv::morphologyEx(mask, mask, cv::MORPH_CLOSE,
                         cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3,5)));

        // Read horizontal solid runs immediately above the strike line. Spark effects can
        // connect a note to a large glow component, but they do not form a key-width run
        // across several adjacent rows. Voting by row isolates the actual falling block.
        QHash<int,int> laneVotes;
        const int firstRow = qMax(0, mask.rows - qMax(18, int(height * .026)));
        for (int row = firstRow; row < mask.rows - 1; ++row) {
            const uchar *pixels = mask.ptr<uchar>(row);
            int x = 0;
            while (x < mask.cols) {
                while (x < mask.cols && pixels[x] == 0) ++x;
                const int begin = x;
                while (x < mask.cols && pixels[x] != 0) ++x;
                const int runWidth = x - begin;
                if (runWidth < whiteWidth * .38 || runWidth > whiteWidth * 1.55) continue;
                const double cx = begin + runWidth * .5;
                int nearest = 21; double distance = std::numeric_limits<double>::max();
                for (int midi = 21; midi <= 108; ++midi) {
                    const double d = qAbs(centers[midi] - cx);
                    if (d < distance) { distance = d; nearest = midi; }
                }
                if (distance <= whiteWidth * .72) laneVotes[nearest]++;
            }
        }
        QSet<int> activeNow;
        for (auto it = laneVotes.cbegin(); it != laneVotes.cend(); ++it)
            if (it.value() >= 4) activeNow.insert(it.key());

        const double time = analyzed * dt;
        for (int midi = 21; midi <= 108; ++midi) {
            auto &state = lane[midi];
            if (activeNow.contains(midi)) {
                state.misses = 0;
                if (!state.active) {
                    if (state.hits == 0) state.candidate = time;
                    if (++state.hits >= 2) { state.active = true; state.start = state.candidate; }
                }
            } else if (state.active) {
                if (++state.misses > 2) {
                    const double end = time - (state.misses - 1) * dt;
                    if (end - state.start >= .07) detected.append({midi, state.start, end});
                    state = LaneState{};
                }
            } else state.hits = 0;
        }
        ++analyzed;
        if (progress && analyzed % 20 == 0) {
            progress(qMin(99, int(analyzed * 100 / expectedAnalyzed)));
            QCoreApplication::processEvents();
        }
    };

    QProcess decoder;
    decoder.setProcessChannelMode(QProcess::SeparateChannels);
    decoder.start("ffmpeg", {"-v","error","-i",path,"-t","300","-vf",
                             "fps="+QString::number(targetFps,'f',0),"-f","rawvideo",
                             "-pix_fmt","bgr24","pipe:1"});
    if (!decoder.waitForStarted(10000)) { setError(error, "FFmpeg could not start video decoding."); return false; }
    QByteArray raw;
    while (decoder.state() != QProcess::NotRunning || decoder.bytesAvailable() > 0) {
        if (decoder.bytesAvailable() == 0) decoder.waitForReadyRead(1000);
        raw += decoder.readAllStandardOutput();
        qsizetype offset = 0;
        while (raw.size() - offset >= frameBytes) {
            cv::Mat decoded(height, width, CV_8UC3, raw.data() + offset);
            analyzeFrame(decoded);
            offset += frameBytes;
        }
        if (offset) raw.remove(0, offset);
        QCoreApplication::processEvents();
    }
    decoder.waitForFinished(5000);
    if (decoder.exitStatus() != QProcess::NormalExit || decoder.exitCode() != 0) {
        setError(error, "FFmpeg stopped while decoding the falling-note video: " +
                        QString::fromLocal8Bit(decoder.readAllStandardError()).trimmed());
        return false;
    }
    const double videoEnd = analyzed * dt;
    for (int midi = 21; midi <= 108; ++midi)
        if (lane[midi].active && videoEnd - lane[midi].start >= .07)
            detected.append({midi, lane[midi].start, videoEnd});

    if (detected.isEmpty()) {
        setError(error, "No solid falling-note blocks were detected at the keyboard strike line. Try audio melody mode or a video with a fixed full keyboard.");
        return false;
    }
    std::sort(detected.begin(), detected.end(), [](const auto &a, const auto &b) {
        return a.start == b.start ? a.midi < b.midi : a.start < b.start;
    });
    const double first = detected.first().start;
    const double beatsPerSecond = qMax(30, tempo) / 60.0;
    for (const auto &n : detected) {
        const double startBeat = qRound((n.start - first) * beatsPerSecond * 4.0) / 4.0;
        const double durationBeat = qMax(.25, qRound((n.end - n.start) * beatsPerSecond * 4.0) / 4.0);
        notes.append({n.midi, startBeat, durationBeat, 94, n.midi < 60 ? Hand::Left : Hand::Right});
    }
    notes = sortNotes(notes);
    if (progress) progress(100);
    return true;
}
