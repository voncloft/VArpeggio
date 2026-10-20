#include "synthesizer.h"
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
#include "pulseaudiooutput.h"
#endif

#include <QAudioDevice>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaDevices>
#include <QMetaObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sndfile.h>

struct RecordedPianoLibrary {
    struct Sample {
        QVector<float> stereo;
        qint64 frames = 0;
        int sampleRate = 48000;
    };
    struct Region {
        int loKey = 21;
        int hiKey = 108;
        int rootKey = 60;
        int loVelocity = 1;
        int hiVelocity = 127;
        int sampleIndex = -1;
        qint64 loopStart = 0;
        qint64 loopEnd = 0;
        bool loop = false;
    };
    QVector<Sample> samples;
    QVector<Region> regions;
};

namespace {
struct RegionDefinition {
    int loKey=21,hiKey=108,rootKey=60,loVelocity=1,hiVelocity=127;
    qint64 loopStart=0,loopEnd=0;
    bool loop=false;
    QString sample;
};

QHash<QString,QString> sfzAttributes(const QString &line){
    QHash<QString,QString> values;
    static const QRegularExpression expression(R"(([A-Za-z0-9_]+)=([^\s]+))");
    auto matches=expression.globalMatch(line);
    while(matches.hasNext()){const auto match=matches.next();values.insert(match.captured(1).toLower(),match.captured(2));}
    return values;
}

void applySfzAttributes(RegionDefinition &definition,const QHash<QString,QString> &values){
    auto number=[&](const char *name,int current){bool ok=false;const int value=values.value(QLatin1String(name)).toInt(&ok);return ok?value:current;};
    if(values.contains("key")){const int key=number("key",definition.rootKey);definition.loKey=key;definition.hiKey=key;definition.rootKey=key;}
    definition.loKey=number("lokey",definition.loKey);definition.hiKey=number("hikey",definition.hiKey);
    definition.rootKey=number("pitch_keycenter",definition.rootKey);
    definition.loVelocity=number("lovel",definition.loVelocity);definition.hiVelocity=number("hivel",definition.hiVelocity);
    definition.loopStart=number("loop_start",int(definition.loopStart));definition.loopEnd=number("loop_end",int(definition.loopEnd));
    if(values.contains("loop_mode"))definition.loop=values.value("loop_mode").toLower()!="no_loop";
    if(values.contains("sample"))definition.sample=values.value("sample");
}

std::shared_ptr<const RecordedPianoLibrary> loadRecordedPiano(){
    static QMutex cacheMutex;
    static std::weak_ptr<const RecordedPianoLibrary> cache;
    QMutexLocker cacheLock(&cacheMutex);
    if(const auto existing=cache.lock())return existing;
    QString root=QDir(QCoreApplication::applicationDirPath()).filePath("soundfonts/UprightPianoKW");
#ifdef VARPEGGIO_DATADIR
    if(!QFileInfo::exists(root))root=QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../"+QStringLiteral(VARPEGGIO_DATADIR)+"/soundfonts/UprightPianoKW");
#endif
    QFile mapping(QDir(root).filePath("UprightPianoKW-20220221.sfz"));
    if(!mapping.open(QIODevice::ReadOnly|QIODevice::Text))return {};
    auto library=std::make_shared<RecordedPianoLibrary>();
    QHash<QString,int> loadedSamples;
    RegionDefinition group,region;bool inRegion=false;
    auto flushRegion=[&]{
        if(!inRegion||region.sample.isEmpty())return;
        int sampleIndex=loadedSamples.value(region.sample,-1);
        if(sampleIndex<0){
            const QString samplePath=QDir(root).filePath(region.sample);
            SF_INFO info{};const QByteArray encoded=QFile::encodeName(samplePath);
            SNDFILE *file=sf_open(encoded.constData(),SFM_READ,&info);
            if(!file||info.frames<=0||info.channels<=0){if(file)sf_close(file);return;}
            QVector<float> source(qsizetype(info.frames)*info.channels);
            const sf_count_t readFrames=sf_readf_float(file,source.data(),info.frames);sf_close(file);
            if(readFrames<=0)return;
            RecordedPianoLibrary::Sample decoded;decoded.frames=readFrames;decoded.sampleRate=info.samplerate;
            decoded.stereo.resize(qsizetype(readFrames)*2);
            for(qint64 frame=0;frame<readFrames;++frame){
                const float left=source[qsizetype(frame)*info.channels];
                const float right=info.channels>1?source[qsizetype(frame)*info.channels+1]:left;
                decoded.stereo[qsizetype(frame)*2]=left;decoded.stereo[qsizetype(frame)*2+1]=right;
            }
            sampleIndex=library->samples.size();library->samples.append(std::move(decoded));loadedSamples.insert(region.sample,sampleIndex);
        }
        const auto &sample=library->samples[sampleIndex];
        RecordedPianoLibrary::Region ready;
        ready.loKey=qBound(0,region.loKey,127);ready.hiKey=qBound(0,region.hiKey,127);ready.rootKey=qBound(0,region.rootKey,127);
        ready.loVelocity=qBound(1,region.loVelocity,127);ready.hiVelocity=qBound(1,region.hiVelocity,127);ready.sampleIndex=sampleIndex;
        ready.loopStart=qBound<qint64>(0,region.loopStart,sample.frames-1);ready.loopEnd=qBound<qint64>(0,region.loopEnd,sample.frames-1);
        ready.loop=region.loop&&ready.loopEnd>ready.loopStart+1;
        library->regions.append(ready);
    };
    const QStringList lines=QString::fromUtf8(mapping.readAll()).split('\n');
    for(QString line:lines){
        line=line.section("//",0,0).trimmed();if(line.isEmpty())continue;
        if(line.startsWith("<group>")){flushRegion();inRegion=false;group=RegionDefinition{};applySfzAttributes(group,sfzAttributes(line.mid(7)));continue;}
        if(line.startsWith("<region>")){flushRegion();region=group;inRegion=true;applySfzAttributes(region,sfzAttributes(line.mid(8)));continue;}
        if(inRegion)applySfzAttributes(region,sfzAttributes(line));else applySfzAttributes(group,sfzAttributes(line));
    }
    flushRegion();
    if(library->samples.isEmpty()||library->regions.isEmpty())return {};
    cache=library;return library;
}

double pianoLifetime(SynthInstrument instrument,int midi) {
    const double base=instrument==SynthInstrument::BrightPiano?.95:
                      instrument==SynthInstrument::SoftPiano?2.0:1.35;
    return base*qBound(.62,qPow(2.0,(60-midi)/42.0),1.75);
}

double stringDetune(SynthInstrument instrument) {
    return instrument==SynthInstrument::BrightPiano?1.0007:
           instrument==SynthInstrument::SoftPiano?1.0002:1.0004;
}

double sequencePedalTail(SynthInstrument instrument) {
    Q_UNUSED(instrument);
    // The recording library owns the natural decay and release envelope.
    return 0.0;
}
}

SynthStream::SynthStream(QObject *parent) : QIODevice(parent) {
    for(int i=0;i<=kWaveTableSize;++i)m_sineTable[i]=float(std::sin(2.0*M_PI*i/kWaveTableSize));
    // Keep the old variable as a compatibility alias for older test scripts.
    const bool samplesDisabled=qEnvironmentVariableIsSet("VARPEGGIO_DISABLE_PIANO_SAMPLES")
                               ||qEnvironmentVariableIsSet("VARPEGGIO_DISABLE_SOUNDFONT");
    if(!samplesDisabled)m_recordedPiano=loadRecordedPiano();
    // QIODevice's default 16 KiB read-ahead retained older sound/silence across
    // transport resets, and counted frames which the sink had not requested.
    // A live audio generator has no seekable backing file or read-ahead cache:
    // each read must render exactly the frames delivered to QAudioSink.
    open(QIODevice::ReadOnly|QIODevice::Unbuffered);
}

SynthStream::~SynthStream()=default;

double SynthStream::oscillator(double phase) const {
    const double position=phase*(kWaveTableSize/(2.0*M_PI));
    const int index=qBound(0,int(position),kWaveTableSize-1);
    const double fraction=position-index;
    return m_sineTable[index]+(m_sineTable[index+1]-m_sineTable[index])*fraction;
}

void SynthStream::shapePianoStereo(double &left,double &right) {
    // This shaper is used only by the emergency oscillator when the recorded
    // piano library is unavailable. Recorded piano audio never passes here.
    constexpr double lowPassAlpha=.19; // about 1.8 kHz at 48 kHz
    constexpr double bassPassAlpha=.018; // about 140 Hz at 48 kHz
    m_toneLowLeft+=lowPassAlpha*(left-m_toneLowLeft);
    m_toneLowRight+=lowPassAlpha*(right-m_toneLowRight);
    m_bassLowLeft+=bassPassAlpha*(left-m_bassLowLeft);
    m_bassLowRight+=bassPassAlpha*(right-m_bassLowRight);
    const double brightness=m_instrument==SynthInstrument::BrightPiano?.45:
                            m_instrument==SynthInstrument::SoftPiano?.22:.52;
    const double bass=m_instrument==SynthInstrument::BrightPiano?.68:
                      m_instrument==SynthInstrument::SoftPiano?.28:.45;
    const double midCut=m_instrument==SynthInstrument::BrightPiano?.20:.08;
    const double brightLeft=left+bass*m_bassLowLeft-midCut*(m_toneLowLeft-m_bassLowLeft)+brightness*(left-m_toneLowLeft);
    const double brightRight=right+bass*m_bassLowRight-midCut*(m_toneLowRight-m_bassLowRight)+brightness*(right-m_toneLowRight);
    constexpr double airPassAlpha=.46,airTrim=.40; // control the brittle >5 kHz shelf
    m_airLowLeft+=airPassAlpha*(brightLeft-m_airLowLeft);
    m_airLowRight+=airPassAlpha*(brightRight-m_airLowRight);
    const double shapedLeft=brightLeft-airTrim*(brightLeft-m_airLowLeft);
    const double shapedRight=brightRight-airTrim*(brightRight-m_airLowRight);
    const double mid=(shapedLeft+shapedRight)*.5;
    const double naturalSide=(shapedLeft-shapedRight)*.5;
    auto delayed=[&](int frames){return double(m_pianoRoom[(m_pianoRoomIndex+kPianoRoomFrames-frames)%kPianoRoomFrames]);};
    const double earlyLeft=delayed(336);   // 7 ms
    const double earlyRight=delayed(624);  // 13 ms
    const double roomLeft=delayed(1499);   // 31 ms
    const double roomRight=delayed(2063);  // 43 ms
    // These taps add a little width, but must stay well below the direct
    // sample.  The old values put a loud, opposite-polarity copy in each
    // channel and turned busy passages into a brittle, static-like wash.
    m_pianoRoom[m_pianoRoomIndex]=float(mid+.035*(roomLeft+roomRight));
    m_pianoRoomIndex=(m_pianoRoomIndex+1)%kPianoRoomFrames;
    constexpr double wetSide=.30,room=.04,output=.90;
    left=output*(mid+naturalSide*1.18+wetSide*earlyLeft+room*roomLeft);
    right=output*(mid-naturalSide*1.18-wetSide*earlyRight+room*roomRight);
}

void SynthStream::startRecordedVoice(quint64 id,int note,int velocity,bool sequenced){
    if(!m_recordedPiano)return;
    int layerVelocity=qBound(1,velocity,127);
    if(m_instrument==SynthInstrument::BrightPiano)layerVelocity=qMax(81,layerVelocity);
    else if(m_instrument==SynthInstrument::SoftPiano)layerVelocity=qMin(80,layerVelocity);
    int regionIndex=-1;
    for(int i=0;i<m_recordedPiano->regions.size();++i){
        const auto &candidate=m_recordedPiano->regions[i];
        if(note>=candidate.loKey&&note<=candidate.hiKey&&layerVelocity>=candidate.loVelocity&&layerVelocity<=candidate.hiVelocity){regionIndex=i;break;}
    }
    if(regionIndex<0)return;
    const auto &region=m_recordedPiano->regions[regionIndex];
    const auto &sample=m_recordedPiano->samples[region.sampleIndex];
    const double pitch=qPow(2.0,(note-region.rootKey)/12.0);
    const double step=pitch*sample.sampleRate/double(m_sampleRate);
    const double touch=.55+.45*qSqrt(qBound(0.0,velocity/127.0,1.0));
    m_recordedVoices.append({id,note,regionIndex,0.0,step,.28*touch,0.0,1.0,false,sequenced});
}

void SynthStream::stopRecordedVoice(quint64 id){
    for(auto &voice:m_recordedVoices)if(voice.id==id&&!voice.releasing){voice.releasing=true;break;}
}

void SynthStream::stopRecordedVoices(bool sequencedOnly){
    for(auto &voice:m_recordedVoices)if(!sequencedOnly||voice.sequenced)voice.releasing=true;
}

quint64 SynthStream::noteOn(int midi, int velocity) {
    QMutexLocker lock(&m_mutex);
    const quint64 id=m_nextVoiceId++;
    if(m_recordedPiano){startRecordedVoice(id,midi,velocity,false);return id;}
    const double frequency=440.0*qPow(2.0,(midi-69)/12.0);
    const double step=2*M_PI*frequency/m_sampleRate;
    const double lifetime=pianoLifetime(m_instrument,midi);
    const double decay=std::exp(-1.0/(m_sampleRate*lifetime));
    const double detune=stringDetune(m_instrument);
    m_voices.append({midi,id,0,0,0,0,step,step*detune,step*2.002,step*3.006,0.0,1.0,1.0,velocity/127.0,false,false,false,decay,m_instrument});
    return id;
}
void SynthStream::noteOff(int midi) { QMutexLocker lock(&m_mutex);if(m_recordedPiano)for(auto &voice:m_recordedVoices)if(voice.note==midi)voice.releasing=true;for(auto &v:m_voices) if(!v.click&&v.note==midi)v.releasing=true; }
void SynthStream::noteOffVoice(quint64 voiceId) { QMutexLocker lock(&m_mutex);if(m_recordedPiano)stopRecordedVoice(voiceId);for(auto &v:m_voices)if(v.id==voiceId){v.releasing=true;break;} }
void SynthStream::click(bool accent) { QMutexLocker lock(&m_mutex);const int note=accent?102:96;const double frequency=440.0*qPow(2.0,(note-69)/12.0);const double step=2*M_PI*frequency/m_sampleRate;m_voices.append({note,m_nextVoiceId++,0,0,0,0,step,step*2,step*3,step*4,1.0,1.0,1.0,accent?.42:.28,true,true,false,accent?.9978:.9968,SynthInstrument::AcousticGrand}); }
void SynthStream::allOff() { QMutexLocker lock(&m_mutex);stopRecordedVoices(false);for(auto &v:m_voices)v.releasing=true;m_referenceActive=false;m_referenceAudio.reset(); }
void SynthStream::setMuted(bool muted) { QMutexLocker lock(&m_mutex); m_muted=muted; }
void SynthStream::setVolume(double volume) { m_volumeTarget.store(qBound(0.0,volume,1.5),std::memory_order_relaxed); }
void SynthStream::setInstrument(SynthInstrument instrument) { QMutexLocker lock(&m_mutex);if(instrument!=m_instrument)stopRecordedVoices(false);m_instrument=instrument; }
void SynthStream::setOutputSampleRate(int sampleRate) { QMutexLocker lock(&m_mutex);if(m_voices.isEmpty()&&m_recordedVoices.isEmpty()&&!m_sequenceActive&&!m_referenceActive)m_sampleRate=qBound(22050,sampleRate,192000); }
void SynthStream::prepareAudioRecovery() { QMutexLocker lock(&m_mutex);m_muteGain=0.0;m_lastOutputLeft=0.0;m_lastOutputRight=0.0; }

void SynthStream::clearTimelineAudio() {
    m_sequenceActive=false;m_sequence.clear();m_sequenceIndex=0;
    m_referenceActive=false;m_referenceAudio.reset();m_referencePosition=0.0;m_referenceStep=1.0;
    m_referenceStartupFrames=0;m_referenceStartupLeft=0.0;m_referenceStartupRight=0.0;
    // A transport jump is a hard boundary. Releasing an old sequenced voice is
    // appropriate during ordinary note-off, but carrying that release into a
    // restarted score makes stale notes audible before the restarted cursor.
    m_recordedVoices.erase(std::remove_if(m_recordedVoices.begin(),m_recordedVoices.end(),[](const RecordedVoice &voice){return voice.sequenced;}),m_recordedVoices.end());
    m_voices.erase(std::remove_if(m_voices.begin(),m_voices.end(),[](const Voice &voice){return voice.sequenced||voice.click;}),m_voices.end());
    m_pianoRoom.fill(0.0f);m_pianoRoomIndex=0;m_toneLowLeft=m_toneLowRight=m_bassLowLeft=m_bassLowRight=m_airLowLeft=m_airLowRight=0.0;
}

void SynthStream::beginTimeline(qint64 preRollFrames) {
    clearTimelineAudio();
    const qint64 primeFrames=qMax<qint64>(0,preRollFrames);
    m_sequenceFrame=-primeFrames;
    // The first frame generated after this point is sequence time zero. The
    // sink may already contain a sizeable block of older silence; recording
    // the absolute frame here lets the visible playhead wait for that queued
    // audio instead of racing ahead on an unrelated wall clock.
    m_sequenceOriginFrame.store(m_totalRenderedFrames.load(std::memory_order_relaxed)+primeFrames,std::memory_order_relaxed);
    m_sequenceSerial.fetch_add(1,std::memory_order_relaxed);
}

void SynthStream::scheduleMetronome(int tempo,double startBeat,double endBeat) {
    if(!std::isfinite(startBeat)||!std::isfinite(endBeat)||endBeat<startBeat)return;
    const double framesPerBeat=m_sampleRate*60.0/qMax(1,tempo);
    const int firstBeat=qCeil(startBeat-.000001),lastBeat=qFloor(endBeat+.000001);
    for(int beat=firstBeat;beat<=lastBeat;++beat){
        const qint64 frame=qMax<qint64>(0,qRound64((beat-startBeat)*framesPerBeat));
        const bool accent=beat%4==0;
        m_sequence.append({frame,true,accent?102:96,accent?1:0,m_nextVoiceId++,true});
    }
}

void SynthStream::startSequence(const QVector<NoteEvent> &notes,int tempo,double startBeat,bool metronome,qint64 preRollFrames) {
    QMutexLocker lock(&m_mutex);
    beginTimeline(preRollFrames);
    const double framesPerBeat=m_sampleRate*60.0/qMax(1,tempo);
    const qint64 pedalTailFrames=qRound64(sequencePedalTail(m_instrument)*m_sampleRate);
    for(const auto &note:notes){
        if(note.start+note.duration<=startBeat)continue;
        const quint64 id=m_nextVoiceId++;
        const double audibleStart=qMax(note.start,startBeat);
        const qint64 start=qMax<qint64>(0,qRound64((audibleStart-startBeat)*framesPerBeat));
        const qint64 end=qMax(start+1,qRound64((note.start+note.duration-startBeat)*framesPerBeat)+pedalTailFrames);
        m_sequence.append({start,true,note.note,note.velocity,id,false});
        m_sequence.append({end,false,note.note,0,id,false});
    }
    m_sequenceMetronomeEnabled=metronome;
    if(!notes.isEmpty()){
        double endBeat=startBeat;for(const auto &note:notes)if(note.start+note.duration>startBeat)endBeat=qMax(endBeat,note.start+note.duration);
        scheduleMetronome(tempo,startBeat,endBeat);
    }
    std::sort(m_sequence.begin(),m_sequence.end(),[](const ScheduledEvent &a,const ScheduledEvent &b){return a.frame==b.frame?a.noteOn<b.noteOn:a.frame<b.frame;});
    m_sequenceActive=!m_sequence.isEmpty();
}

void SynthStream::startReference(ReferenceAudioPtr audio,int tempo,double startSeconds,
                                 double rate,bool metronome,double scoreStartSeconds,
                                 qint64 preRollFrames) {
    QMutexLocker lock(&m_mutex);
    beginTimeline(preRollFrames);
    m_sequenceMetronomeEnabled=metronome;
    if(!audio||audio->sampleRate<=0||audio->frameCount<=0||!audio->samples)return;

    const double duration=double(audio->frameCount)/audio->sampleRate;
    const double playbackRate=std::isfinite(rate)&&rate>0.0?rate:1.0;
    const double sourceStart=qBound(0.0,std::isfinite(startSeconds)?startSeconds:0.0,duration);
    const double scoreOrigin=qBound(0.0,std::isfinite(scoreStartSeconds)?scoreStartSeconds:0.0,duration);
    m_referenceAudio=std::move(audio);
    m_referencePosition=sourceStart*m_referenceAudio->sampleRate;
    m_referenceStep=playbackRate*m_referenceAudio->sampleRate/double(m_sampleRate);
    if(!std::isfinite(m_referenceStep)||m_referenceStep<=0.0)
        m_referenceStep=m_referenceAudio->sampleRate/double(m_sampleRate);
    m_referenceActive=m_referencePosition<m_referenceAudio->frameCount;
    m_referenceStartupFrames=m_referenceActive?64:0;

    if(m_referenceActive){
        // Source seconds advance `rate` times faster than output seconds.  The
        // score origin removes a video's non-musical intro, so beat zero and
        // the first metronome pulse stay tied to the same decoded PCM frame.
        const double sourceStartBeat=(sourceStart-scoreOrigin)*qMax(1,tempo)/playbackRate/60.0;
        const double sourceEndBeat=(duration-scoreOrigin)*qMax(1,tempo)/playbackRate/60.0;
        scheduleMetronome(tempo,sourceStartBeat,sourceEndBeat);
        std::sort(m_sequence.begin(),m_sequence.end(),[](const ScheduledEvent &a,const ScheduledEvent &b){return a.frame==b.frame?a.noteOn<b.noteOn:a.frame<b.frame;});
        m_sequenceActive=!m_sequence.isEmpty();
    }
}

void SynthStream::stopSequence() {
    QMutexLocker lock(&m_mutex);
    clearTimelineAudio();
}

void SynthStream::setSequenceMetronomeEnabled(bool enabled) {
    QMutexLocker lock(&m_mutex);m_sequenceMetronomeEnabled=enabled;
}

qint64 SynthStream::readData(char *data, qint64 maxlen) {
    const int channels=2; const int frameBytes=int(sizeof(float))*channels; const int frames=int(maxlen/frameBytes);
    auto *out=reinterpret_cast<float*>(data);
    // Do not hold the voice lock for an entire device buffer. With a large,
    // dense score that starved note input and made the on-screen keyboard and
    // volume control appear to jump or freeze. Short chunks keep the callback
    // safe while bounding UI-side waits to a few milliseconds.
    constexpr int chunkFrames=512;
    for(int chunkStart=0;chunkStart<frames;chunkStart+=chunkFrames){
      const int chunkEnd=qMin(frames,chunkStart+chunkFrames);
      QMutexLocker lock(&m_mutex);
      auto processEvent=[&](const ScheduledEvent &event){
          if(event.metronome){
              if(!m_sequenceMetronomeEnabled)return;
              const double frequency=440.0*qPow(2.0,(event.note-69)/12.0);
              const double step=2*M_PI*frequency/m_sampleRate;
              const bool accent=event.velocity!=0;
              m_voices.append({event.note,event.voiceId,0,0,0,0,step,step*2,step*3,step*4,1.0,1.0,1.0,accent?.42:.28,true,true,true,accent?.9978:.9968,SynthInstrument::AcousticGrand});
              return;
          }
          if(m_recordedPiano){
              if(event.noteOn)startRecordedVoice(event.voiceId,event.note,event.velocity,true);
              else stopRecordedVoice(event.voiceId);
          }else if(event.noteOn){
              const double frequency=440.0*qPow(2.0,(event.note-69)/12.0);
              const double step=2*M_PI*frequency/m_sampleRate;
              const double lifetime=pianoLifetime(m_instrument,event.note);
              const double decay=std::exp(-1.0/(m_sampleRate*lifetime));
              const double detune=stringDetune(m_instrument);
              m_voices.append({event.note,event.voiceId,0,0,0,0,step,step*detune,step*2.002,step*3.006,0.0,1.0,1.0,event.velocity/127.0,false,false,true,decay,m_instrument});
          }else for(auto &voice:m_voices)if(voice.id==event.voiceId){voice.releasing=true;break;}
      };

      for(int i=chunkStart;i<chunkEnd;++i){double melodyLeft=0,melodyRight=0,clickSample=0,voiceLoad=0;
        double referenceLeft=0.0,referenceRight=0.0;
        while(m_sequenceActive&&m_sequenceIndex<m_sequence.size()&&m_sequence[m_sequenceIndex].frame<=m_sequenceFrame){
            processEvent(m_sequence[m_sequenceIndex++]);
            if(m_sequenceIndex>=m_sequence.size())m_sequenceActive=false;
        }
        if(m_referenceActive&&m_referenceAudio&&m_sequenceFrame>=0){
            const qint64 frame=qBound<qint64>(0,qint64(m_referencePosition),m_referenceAudio->frameCount-1);
            const qint64 next=qMin(frame+1,m_referenceAudio->frameCount-1);
            const double fraction=m_referencePosition-frame;
            referenceLeft=m_referenceAudio->samples[frame*2]
                          +(m_referenceAudio->samples[next*2]-m_referenceAudio->samples[frame*2])*fraction;
            referenceRight=m_referenceAudio->samples[frame*2+1]
                           +(m_referenceAudio->samples[next*2+1]-m_referenceAudio->samples[frame*2+1])*fraction;
            if(m_referenceStartupFrames>0){
                // A seek can land at any waveform phase. Bound only this new
                // transport edge so arbitrary nonzero PCM cannot jump straight
                // out of the preceding priming silence; ordinary attacks whose
                // sample deltas are already small pass through bit-for-bit.
                m_referenceStartupLeft+=qBound(-.075,referenceLeft-m_referenceStartupLeft,.075);
                m_referenceStartupRight+=qBound(-.075,referenceRight-m_referenceStartupRight,.075);
                referenceLeft=m_referenceStartupLeft;referenceRight=m_referenceStartupRight;
                --m_referenceStartupFrames;
            }
            m_referencePosition+=m_referenceStep;
            if(!std::isfinite(m_referencePosition)||m_referencePosition>=m_referenceAudio->frameCount)
                m_referenceActive=false;
        }
        if(m_recordedPiano){
                constexpr double attackSeconds=.004;
                constexpr double releaseSeconds=.60;
                for(auto &voice:m_recordedVoices){
                    if(voice.releaseGain<=0.0)continue;
                    const auto &region=m_recordedPiano->regions[voice.region];
                    const auto &pianoSample=m_recordedPiano->samples[region.sampleIndex];
                    if(pianoSample.frames<2){voice.releaseGain=0;continue;}
                    if(region.loop){
                        const double loopLimit=region.loopEnd+1.0;
                        while(voice.position>=loopLimit)voice.position=region.loopStart+(voice.position-loopLimit);
                    }else if(voice.position>=pianoSample.frames-1){voice.releaseGain=0;continue;}
                    const qint64 frame=qBound<qint64>(0,qint64(voice.position),pianoSample.frames-1);
                    qint64 next=frame+1;
                    if(region.loop&&next>region.loopEnd)next=region.loopStart;
                    else next=qMin(next,pianoSample.frames-1);
                    const double fraction=voice.position-frame;
                    voice.attackGain=qMin(1.0,voice.attackGain+1.0/(attackSeconds*m_sampleRate));
                    if(voice.releasing)voice.releaseGain=qMax(0.0,voice.releaseGain-1.0/(releaseSeconds*m_sampleRate));
                    const double envelope=voice.gain*voice.attackGain*voice.releaseGain;
                    voiceLoad+=envelope;
                    melodyLeft+=(pianoSample.stereo[frame*2]+(pianoSample.stereo[next*2]-pianoSample.stereo[frame*2])*fraction)*envelope;
                    melodyRight+=(pianoSample.stereo[frame*2+1]+(pianoSample.stereo[next*2+1]-pianoSample.stereo[frame*2+1])*fraction)*envelope;
                    voice.position+=voice.step;
                }
        }else for(const auto &v:m_voices)if(!v.click)voiceLoad+=v.level*v.naturalGain*v.velocity;
        // Rapid octave jumps and glissandi start many recorded samples while
        // earlier strings are still in their release tails. Give those real
        // samples the same kind of smoothly changing headroom as the fallback
        // engine instead of driving the peak shaper with a fixed .82 gain.
        const double loadReference=m_recordedPiano?1.5:4.0;
        const double targetPolyphonyGain=.82/qSqrt(qMax(1.0,voiceLoad/loadReference));
        const double polyphonySlew=targetPolyphonyGain<m_polyphonyGain?.0025:.00020;
        m_polyphonyGain+=polyphonySlew*(targetPolyphonyGain-m_polyphonyGain);
        for(auto &v:m_voices){
            if(v.click){
                clickSample+=oscillator(v.phase)*v.velocity*v.level;
                v.phase+=v.phaseStep;
                v.level*=v.decay;
            }else if(!m_recordedPiano){
                const double attack=v.instrument==SynthInstrument::BrightPiano?.0065:v.instrument==SynthInstrument::SoftPiano?.0025:.0045;
                if(!v.releasing)v.level=qMin(1.0,v.level+attack);else v.level*=.9992;
                v.naturalGain*=v.decay;
                // The oscillator is only a fallback, but it must still honor
                // a written duration.  Preserve a quiet resonant floor for an
                // active sequenced hold instead of fading to numerical zero
                // (and being compacted) before its scheduled note-off.
                if(v.sequenced&&!v.releasing)v.naturalGain=qMax(v.naturalGain,.08);
                v.brightness*=v.instrument==SynthInstrument::BrightPiano?.99990:v.instrument==SynthInstrument::SoftPiano?.99980:.99986;
                double tone=0.0;
                if(v.instrument==SynthInstrument::BrightPiano){
                    tone=.64*oscillator(v.phase)+.10*oscillator(v.phase2)+v.brightness*(.18*oscillator(v.phase3)+.08*oscillator(v.phase4));
                }else if(v.instrument==SynthInstrument::SoftPiano){
                    tone=.80*oscillator(v.phase)+.08*oscillator(v.phase2)+v.brightness*(.09*oscillator(v.phase3)+.03*oscillator(v.phase4));
                }else{
                    tone=.72*oscillator(v.phase)+.10*oscillator(v.phase2)+v.brightness*(.13*oscillator(v.phase3)+.05*oscillator(v.phase4));
                }
                const double voiceSample=tone*v.velocity*v.level*v.naturalGain*.24;
                melodyLeft+=voiceSample;melodyRight+=voiceSample;
                v.phase+=v.phaseStep;
                v.phase2+=v.phase2Step;
                v.phase3+=v.phase3Step;
                v.phase4+=v.phase4Step;
            }
            while(v.phase>2*M_PI)v.phase-=2*M_PI;
            while(v.phase2>2*M_PI)v.phase2-=2*M_PI;
            while(v.phase3>2*M_PI)v.phase3-=2*M_PI;
            while(v.phase4>2*M_PI)v.phase4-=2*M_PI;
        }
        if(!m_recordedPiano)shapePianoStereo(melodyLeft,melodyRight);
        const double mixedLeft=melodyLeft*m_polyphonyGain+clickSample;
        const double mixedRight=melodyRight*m_polyphonyGain+clickSample;
        m_volumeGain+=.0015*(m_volumeTarget.load(std::memory_order_relaxed)-m_volumeGain);
        const double drivenLeft=mixedLeft*.9*m_volumeGain,drivenRight=mixedRight*.9*m_volumeGain;
        // Stay completely linear at normal levels. Only round the rare peak
        // above -1.1 dBFS instead of coloring every chord through tanh.
        auto soften=[](double driven){const double magnitude=qAbs(driven);return magnitude<=.88?driven:std::copysign(.88+.10*std::tanh((magnitude-.88)/.10),driven);};
        // Limit only discontinuous jumps.  A permanent one-pole smoother did
        // suppress clicks, but it also low-passed every legitimate hammer
        // transient and stripped the bright piano harmonics from the result.
        // The slew guard is inactive for ordinary samples and engages only on
        // an impossible single-sample edge.
        auto deClick=[](double target,double &previous){previous+=qBound(-.075,target-previous,.075);return previous;};
        // Most recorded attacks are clean, but switching sample regions during
        // a large leap or wrapping a loop can still expose a one-sample edge.
        // The guard is dormant for normal hammer transients and limits only an
        // impossible discontinuity, so it does not low-pass the piano tone.
        deClick(soften(drivenLeft),m_lastOutputLeft);
        deClick(soften(drivenRight),m_lastOutputRight);
        const double muteTarget=m_muted?0.0:1.0;
        m_muteGain+=qBound(-.0025,muteTarget-m_muteGain,.0025);
        // The decoded original recording is already mastered stereo PCM. Mix
        // it only after the piano-specific EQ, polyphony gain and peak shaper;
        // otherwise a silent synth voice would still recolor the recording.
        // It intentionally shares the same user volume and mute ramps.
        // Match QAudioOutput's previous 0..1 reference-volume range rather
        // than amplifying a mastered recording with the piano's 150% boost.
        const double referenceGain=qMin(1.0,m_volumeGain)*m_muteGain;
        out[i*2]=float(m_lastOutputLeft*m_muteGain*.92+referenceLeft*referenceGain);
        out[i*2+1]=float(m_lastOutputRight*m_muteGain*.92+referenceRight*referenceGain);
        ++m_sequenceFrame;
      }
      // Compact once per short chunk: cheap enough for audio and it prevents a
      // long tail of dead voices from making later chunks progressively slower.
      m_voices.erase(std::remove_if(m_voices.begin(),m_voices.end(),[](const Voice&v){return v.releasing&&v.level*v.naturalGain<.0008;}),m_voices.end());
      m_recordedVoices.erase(std::remove_if(m_recordedVoices.begin(),m_recordedVoices.end(),[](const RecordedVoice &voice){return voice.releaseGain<=0.0;}),m_recordedVoices.end());
      m_totalRenderedFrames.fetch_add(chunkEnd-chunkStart,std::memory_order_relaxed);
    }
    return qint64(frames)*frameBytes;
}

Synthesizer::Synthesizer(QObject *parent) : QObject(parent) {
    if (qEnvironmentVariableIsSet("VARPEGGIO_NO_AUDIO")) return;
    QAudioDevice device=QMediaDevices::defaultAudioOutput();QAudioFormat format;format.setSampleRate(device.preferredFormat().sampleRate());format.setChannelCount(2);format.setSampleFormat(QAudioFormat::Float);
    if(!device.isFormatSupported(format)){format.setSampleRate(48000);if(!device.isFormatSupported(format))return;}
    m_outputSampleRate=format.sampleRate();m_stream.setOutputSampleRate(m_outputSampleRate);
    startOutputThread();
    onOutputThread([this,device,format]{
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
        // QAudioSink's Pulse backend smooths recent sink latencies. That can
        // leave its position tens of milliseconds behind real output after
        // a latency change. The native backend queries a fresh public Pulse
        // presentation clock and independently refills the same sample data.
        m_pulse=new PulseAudioOutput(m_stream,m_outputSampleRate,98304,device.id());
        m_sinkFrameBase=m_stream.renderedFrameCount();
        if(m_pulse->available()&&m_pulse->start()){m_available=true;return;}
        delete m_pulse;m_pulse=nullptr;
#endif
        m_sink=std::make_unique<QAudioSink>(device,format,m_outputContext);
        // Preserve the established headroom, format, sample data and tone.
        // Only ownership of the backend refill event loop has changed.
        m_sink->setBufferSize(98304);
        m_sinkFrameBase=m_stream.renderedFrameCount();
        connect(m_sink.get(),&QAudioSink::stateChanged,m_outputContext,[this](QAudio::State state){
            if(!m_sink||m_recoveringAudio||state!=QAudio::IdleState
                ||m_sink->error()!=QAudio::UnderrunError)return;
            // A continuous source never reaches EOF. Let the backend refill
            // and resume its existing stream; readyRead also wakes a backend
            // which stopped pulling while idle. Resetting here discarded
            // rendered-but-unpresented samples and moved the clock origin.
            // This callback and the refill timer now share the output thread,
            // so neither has to wait for the GUI to paint a score.
            // Leave the backend's state-change callback before waking it;
            // this is one queued wake, not a retry loop or a transport reset.
            QMetaObject::invokeMethod(m_outputContext,[this]{
                if(m_sink&&!m_recoveringAudio&&m_sink->state()==QAudio::IdleState
                    &&m_sink->error()==QAudio::UnderrunError)emit m_stream.readyRead();
            },Qt::QueuedConnection);
        });
        m_sink->start(&m_stream);
        m_available=true;
    });
}

void Synthesizer::startOutputThread() {
    Q_ASSERT(QThread::currentThread()==thread());
    if(m_outputContext)return;
    m_outputThread.setObjectName(QStringLiteral("VArpeggio audio output"));
    m_outputContext=new QObject;
    m_outputContext->moveToThread(&m_outputThread);
    m_stream.moveToThread(&m_outputThread);
    connect(&m_outputThread,&QThread::finished,m_outputContext,&QObject::deleteLater);
    m_outputThread.start();
}

void Synthesizer::onOutputThread(const std::function<void()> &work) const {
    if(!m_outputContext||QThread::currentThread()==&m_outputThread){work();return;}
    // No GUI event pumping: completion is a single owning-thread operation.
    // In particular, clock/state getters may not inspect QAudioSink directly
    // from the GUI while the backend updates it on its own event loop.
    QMetaObject::invokeMethod(m_outputContext,work,Qt::BlockingQueuedConnection);
}

Synthesizer::~Synthesizer() {
    if(!m_outputContext)return;
    Q_ASSERT(QThread::currentThread()==thread());
    QThread *ownerThread=thread();
    onOutputThread([this,ownerThread]{
        m_recoveringAudio=true;
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
        // Its destructor stops the native callback thread before the source
        // can return to the GUI or release a mapped reference recording.
        delete m_pulse;m_pulse=nullptr;
#endif
        if(m_sink){
            disconnect(m_sink.get(),nullptr,m_outputContext,nullptr);
            m_sink->reset();
            m_sink.reset();
        }
        // The sink is gone, so no read can outlive its samples. Return the
        // member QIODevice before its ordinary C++ destruction on the owning
        // thread. Its reference-audio ownership remains intact until then.
        m_stream.moveToThread(ownerThread);
    });
    m_outputThread.quit();
    m_outputThread.wait();
    m_outputContext=nullptr;
}

void Synthesizer::restartSink() {
    Q_ASSERT(!m_outputContext||QThread::currentThread()==&m_outputThread);
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
    if(m_pulse){
        m_sinkFrameBase=m_stream.renderedFrameCount();
        m_pulse->start();return;
    }
#endif
    if(!m_sink)return;
    m_sink->setBufferSize(98304);
    m_sinkFrameBase=m_stream.renderedFrameCount();
    m_sink->start(&m_stream);
}

void Synthesizer::startSequence(const QVector<NoteEvent> &notes,int tempo,double startBeat,bool metronome) {
    onOutputThread([this,&notes,tempo,startBeat,metronome]{
    if(!m_sink&&!m_pulse){m_stream.startSequence(notes,tempo,startBeat,metronome);return;}
    // reset() discards audio which Qt has rendered but the device has not yet
    // presented. Without this flush, Pause/Resume and tempo changes could play
    // as much as one old device buffer while every visual was already waiting
    // at the new score position.
    m_recoveringAudio=true;
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
    if(m_pulse)m_pulse->reset();else
#endif
    m_sink->reset();
    m_stream.prepareAudioRecovery();
    // Prime one device buffer with silence. Starting a note in the first frame
    // of a newly created Pulse stream exposed its attack to startup rewinds and
    // the backend's still-settling latency estimate. Sequence time zero remains
    // the first musical frame, so every visual waits for exactly this same
    // output position; later notes are never shifted relative to one another.
    qsizetype bufferBytes=0;
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
    if(m_pulse)bufferBytes=m_pulse->bufferSize();else
#endif
    bufferBytes=m_sink->bufferSize();
    const qint64 primeFrames=qMax<qint64>(1,bufferBytes/(2*qint64(sizeof(float))));
    m_stream.startSequence(notes,tempo,startBeat,metronome,primeFrames);
    restartSink();
    m_recoveringAudio=false;
    });
}

void Synthesizer::startReference(ReferenceAudioPtr audio,int tempo,double startSeconds,
                                 double rate,bool metronome,double scoreStartSeconds) {
    onOutputThread([this,&audio,tempo,startSeconds,rate,metronome,scoreStartSeconds]{
    if(!m_sink&&!m_pulse){m_stream.startReference(std::move(audio),tempo,startSeconds,rate,metronome,scoreStartSeconds);return;}
    // Use the identical output-boundary reset and silent device priming as
    // generated score playback. Decoded PCM and the visible score can then
    // share one authoritative consumed-sample clock.
    m_recoveringAudio=true;
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
    if(m_pulse)m_pulse->reset();else
#endif
    m_sink->reset();
    m_stream.prepareAudioRecovery();
    qsizetype bufferBytes=0;
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
    if(m_pulse)bufferBytes=m_pulse->bufferSize();else
#endif
    bufferBytes=m_sink->bufferSize();
    const qint64 primeFrames=qMax<qint64>(1,bufferBytes/(2*qint64(sizeof(float))));
    m_stream.startReference(std::move(audio),tempo,startSeconds,rate,metronome,scoreStartSeconds,primeFrames);
    restartSink();
    m_recoveringAudio=false;
    });
}

void Synthesizer::stopSequence() {
    onOutputThread([this]{
    if(!m_sink&&!m_pulse){m_stream.stopSequence();return;}
    // Stop means stop at the output boundary too, not merely stop generating
    // additional samples behind audio which is already queued.
    m_recoveringAudio=true;
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
    if(m_pulse)m_pulse->reset();else
#endif
    m_sink->reset();
    m_stream.stopSequence();
    m_stream.prepareAudioRecovery();
    restartSink();
    m_recoveringAudio=false;
    });
}

std::optional<double> Synthesizer::sequencePlaybackSeconds() const {
    std::optional<double> seconds;
    onOutputThread([this,&seconds]{
    if(m_stream.sequenceSerial()==0||m_stream.sequenceOriginFrame()<0||m_outputSampleRate<=0)return;
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
    if(m_pulse){
        if(const auto processed=m_pulse->processedFrames())
            seconds=double(m_sinkFrameBase+*processed-m_stream.sequenceOriginFrame())/m_outputSampleRate;
        return;
    }
#endif
    if(!m_sink||m_sink->state()==QAudio::StoppedState)return;
    // processedUSecs() is the sink's consumed-output clock. renderedFrameCount
    // is deliberately not used here: it runs ahead whenever Qt fills its
    // buffer and was the source of the visible early-note regression.
    const qint64 processedFrames=qRound64(m_sink->processedUSecs()*double(m_outputSampleRate)/1000000.0);
    const qint64 audibleFrame=m_sinkFrameBase+processedFrames;
    seconds=double(audibleFrame-m_stream.sequenceOriginFrame())/m_outputSampleRate;
    });
    return seconds;
}

bool Synthesizer::outputFailed() const {
    bool failed=false;
    onOutputThread([this,&failed]{
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
        if(m_pulse){failed=m_pulse->failed();return;}
#endif
        failed=m_sink&&m_sink->state()==QAudio::StoppedState
            &&m_sink->error()!=QAudio::NoError;
    });
    return failed;
}

bool Synthesizer::saveWav(const QString &path,const QVector<NoteEvent> &notes,int tempo,QString *error,SynthInstrument instrument){
    if(notes.isEmpty()){if(error)*error="There are no notes to render.";return false;}
    constexpr int sampleRate=48000,channels=2,bytesPerSample=2;
    double endBeat=0;for(const auto &note:notes)endBeat=qMax(endBeat,note.start+note.duration);
    const qint64 frames=qCeil((endBeat*60.0/qMax(1,tempo)+2.0)*sampleRate);
    const qint64 dataBytes=frames*channels*bytesPerSample;
    if(dataBytes>std::numeric_limits<quint32>::max()-36){if(error)*error="This WAV export is too long.";return false;}
    auto le16=[](QByteArray &out,quint16 value){out.append(char(value));out.append(char(value>>8));};
    auto le32=[](QByteArray &out,quint32 value){out.append(char(value));out.append(char(value>>8));out.append(char(value>>16));out.append(char(value>>24));};
    QByteArray header="RIFF";le32(header,quint32(36+dataBytes));header+="WAVEfmt ";le32(header,16);le16(header,1);le16(header,channels);le32(header,sampleRate);le32(header,sampleRate*channels*bytesPerSample);le16(header,channels*bytesPerSample);le16(header,bytesPerSample*8);header+="data";le32(header,quint32(dataBytes));
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)){if(error)*error=file.errorString();return false;}
    if(file.write(header)!=header.size()){if(error)*error=file.errorString();return false;}
    SynthStream renderer;renderer.setInstrument(instrument);renderer.startSequence(notes,tempo);
    constexpr int blockFrames=1024;QByteArray floats(blockFrames*channels*int(sizeof(float)),Qt::Uninitialized);QByteArray pcm(blockFrames*channels*bytesPerSample,Qt::Uninitialized);
    qint64 writtenFrames=0;
    while(writtenFrames<frames){
        const int count=int(qMin<qint64>(blockFrames,frames-writtenFrames));renderer.readData(floats.data(),count*channels*int(sizeof(float)));
        const float *samples=reinterpret_cast<const float*>(floats.constData());
        for(int i=0;i<count*channels;++i){const qint16 value=qint16(qBound(-32767,qRound(samples[i]*32767.0f),32767));pcm[i*2]=char(value);pcm[i*2+1]=char(value>>8);}
        const qint64 bytes=count*channels*bytesPerSample;if(file.write(pcm.constData(),bytes)!=bytes){if(error)*error=file.errorString();return false;}writtenFrames+=count;
    }
    if(!file.commit()){if(error)*error=file.errorString();return false;}return true;
}
