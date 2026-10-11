#include "synthesizer.h"

#include <QAudioDevice>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaDevices>
#include <QSaveFile>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#define TSF_IMPLEMENTATION
#include "tsf.h"

namespace {
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
    // Falling-note videos encode key-down time, but their piano track is
    // normally recorded with the damper pedal.  Releasing every sample at the
    // exact top edge of every rectangle therefore creates holes that are not
    // in the source performance.  This short overlap models the pedal/string
    // resonance only for score playback; it remains far below a hall-reverb
    // tail and never changes the written duration or animated bar length.
    return instrument==SynthInstrument::BrightPiano?.12:
           instrument==SynthInstrument::SoftPiano?.18:.15;
}

double sequenceReleaseSeconds(SynthInstrument instrument) {
    // TinySoundFont's envelope reaches effectively silent at the end of this
    // interval (roughly -80 dB), so this is not a three-second full-volume
    // hold.  It is a fast initial piano decay with a quiet pedal tail.
    return instrument==SynthInstrument::BrightPiano?3.2:
           instrument==SynthInstrument::SoftPiano?4.0:3.5;
}
}

SynthStream::SynthStream(QObject *parent) : QIODevice(parent) {
    for(int i=0;i<=kWaveTableSize;++i)m_sineTable[i]=float(std::sin(2.0*M_PI*i/kWaveTableSize));
    const QString soundFontPath=QDir(QCoreApplication::applicationDirPath()).filePath("soundfonts/GeneralUser-GS.sf2");
    if(!qEnvironmentVariableIsSet("KEYFLOW_DISABLE_SOUNDFONT")&&QFileInfo::exists(soundFontPath)){
        const QByteArray encoded=QFile::encodeName(soundFontPath);
        m_soundFont=tsf_load_filename(encoded.constData());
        if(m_soundFont){
            tsf_set_output(m_soundFont,TSF_STEREO_INTERLEAVED,m_sampleRate,-5.0f);
            tsf_set_max_voices(m_soundFont,256);
            m_soundFontPresets={qMax(0,tsf_get_presetindex(m_soundFont,0,0)),
                                qMax(0,tsf_get_presetindex(m_soundFont,0,1)),
                                qMax(0,tsf_get_presetindex(m_soundFont,0,0))};
        }
    }
    open(QIODevice::ReadOnly);
}

SynthStream::~SynthStream(){if(m_soundFont)tsf_close(m_soundFont);}

double SynthStream::oscillator(double phase) const {
    const double position=phase*(kWaveTableSize/(2.0*M_PI));
    const int index=qBound(0,int(position),kWaveTableSize-1);
    const double fraction=position-index;
    return m_sineTable[index]+(m_sineTable[index+1]-m_sineTable[index])*fraction;
}

void SynthStream::shapePianoStereo(double &left,double &right) {
    // GeneralUser's compact grand is intentionally conservative and almost
    // mono.  Preserve the sampled body, but restore the brighter hammer edge
    // and short left/right microphone differences heard in modern tutorial
    // pianos.  These are early reflections (7-43 ms), not a church/hall echo.
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
    // The source tutorial piano has a deep fundamental, a restrained wooden
    // box band, and a crisp hammer/string top.  The compact GeneralUser preset
    // is almost the inverse: weak below 160 Hz and crowded around 400 Hz.
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
    // Very short feedback only fills the holes between clipped-looking note
    // blocks.  It falls away in a fraction of a second and cannot become the
    // organ/church sustain that the earlier renderer suffered from.
    m_pianoRoom[m_pianoRoomIndex]=float(mid+.105*(roomLeft+roomRight));
    m_pianoRoomIndex=(m_pianoRoomIndex+1)%kPianoRoomFrames;
    constexpr double wetSide=1.22,room=.11,output=.76;
    left=output*(mid+naturalSide*1.85+wetSide*earlyLeft+room*roomLeft);
    right=output*(mid-naturalSide*1.85-wetSide*earlyRight+room*roomRight);
}

int SynthStream::samplePreset(SynthInstrument instrument) const {
    return m_soundFontPresets[qBound(0,int(instrument),2)];
}

void SynthStream::startSampleVoice(quint64 id,int note,int velocity,bool sequenced){
    if(!m_soundFont)return;
    const int preset=samplePreset(m_instrument);
    const double touch=m_instrument==SynthInstrument::SoftPiano?.70:m_instrument==SynthInstrument::BrightPiano?1.0:.90;
    const unsigned int playIndex=m_soundFont->voicePlayIndex;
    if(!tsf_note_on(m_soundFont,preset,note,float(qBound(0.01,velocity/127.0*touch,1.0))))return;
    m_sampleVoices.insert(id,{note,preset,sequenced,playIndex,0,0.0});
    ++m_sampleNoteCounts[preset*128+note];
}

void SynthStream::stopSampleVoice(quint64 id){
    if(!m_soundFont||!m_sampleVoices.contains(id))return;
    const SampleVoice voice=m_sampleVoices.take(id);const int key=voice.preset*128+voice.note;
    const int remaining=qMax(0,m_sampleNoteCounts.value(key)-1);
    if(remaining==0)m_sampleNoteCounts.remove(key);else m_sampleNoteCounts[key]=remaining;
    // TinySoundFont's public note-off targets the oldest voice sharing a MIDI
    // key.  Score playback tracks each note event separately, so a short
    // repeated note must not cut a longer same-pitch hold (or leave the newer
    // voice ringing).  End precisely the regions created by this event.
    for(int i=0;i<m_soundFont->voiceNum;++i){
        auto &sample=m_soundFont->voices[i];
        if(sample.playingPreset==-1||sample.playIndex!=voice.playIndex||sample.ampenv.segment>=TSF_SEGMENT_RELEASE)continue;
        if(voice.sequenced){
            const float release=float(sequenceReleaseSeconds(m_instrument));
            sample.ampenv.parameters.release=qMax(sample.ampenv.parameters.release,release);
            sample.modenv.parameters.release=qMax(sample.modenv.parameters.release,release);
        }
        tsf_voice_end(m_soundFont,&sample);
    }
}

void SynthStream::prepareSampleSustains(int frames){
    if(!m_soundFont||frames<=0)return;
    // The bundled grand-piano samples have the right attack and timbre, but
    // their recorded tails fall about 20 dB during a long written hold.  Raise
    // only already-decaying sequenced voices, smoothly and with a firm cap.
    // Short notes keep the untouched piano transient; no oscillator or sample
    // retrigger is introduced, so this remains a piano rather than an organ.
    for(auto it=m_sampleVoices.begin();it!=m_sampleVoices.end();++it){
        auto &voice=it.value();if(!voice.sequenced)continue;
        const double ageSeconds=voice.renderedFrames/double(m_sampleRate);
        const double target=qBound(0.0,(ageSeconds-.70)*3.2,14.0);
        const double delta=target-voice.sustainBoostDb;
        if(qAbs(delta)>.0001){
            for(int i=0;i<m_soundFont->voiceNum;++i){auto &sample=m_soundFont->voices[i];if(sample.playingPreset!=-1&&sample.playIndex==voice.playIndex)sample.noteGainDB+=float(delta);}
            voice.sustainBoostDb=target;
        }
        voice.renderedFrames+=frames;
    }
}

void SynthStream::stopSampleVoices(bool sequencedOnly){
    if(!m_soundFont)return;
    const auto ids=m_sampleVoices.keys();
    for(quint64 id:ids)if(!sequencedOnly||m_sampleVoices.value(id).sequenced)stopSampleVoice(id);
}

quint64 SynthStream::noteOn(int midi, int velocity) {
    QMutexLocker lock(&m_mutex);
    const quint64 id=m_nextVoiceId++;
    if(m_soundFont){startSampleVoice(id,midi,velocity,false);return id;}
    const double frequency=440.0*qPow(2.0,(midi-69)/12.0);
    const double step=2*M_PI*frequency/m_sampleRate;
    const double lifetime=pianoLifetime(m_instrument,midi);
    const double decay=std::exp(-1.0/(m_sampleRate*lifetime));
    const double detune=stringDetune(m_instrument);
    m_voices.append({midi,id,0,0,0,0,step,step*detune,step*2.002,step*3.006,0.0,1.0,1.0,velocity/127.0,false,false,false,decay,m_instrument});
    return id;
}
void SynthStream::noteOff(int midi) { QMutexLocker lock(&m_mutex);if(m_soundFont){const auto ids=m_sampleVoices.keys();for(quint64 id:ids)if(m_sampleVoices.value(id).note==midi)stopSampleVoice(id);}for(auto &v:m_voices) if(!v.click&&v.note==midi)v.releasing=true; }
void SynthStream::noteOffVoice(quint64 voiceId) { QMutexLocker lock(&m_mutex);if(m_soundFont)stopSampleVoice(voiceId);for(auto &v:m_voices)if(v.id==voiceId){v.releasing=true;break;} }
void SynthStream::click(bool accent) { QMutexLocker lock(&m_mutex);const int note=accent?102:96;const double frequency=440.0*qPow(2.0,(note-69)/12.0);const double step=2*M_PI*frequency/m_sampleRate;m_voices.append({note,m_nextVoiceId++,0,0,0,0,step,step*2,step*3,step*4,1.0,1.0,1.0,accent?.42:.28,true,true,false,accent?.9978:.9968,SynthInstrument::AcousticGrand}); }
void SynthStream::allOff() { QMutexLocker lock(&m_mutex);if(m_soundFont){tsf_note_off_all(m_soundFont);m_sampleVoices.clear();m_sampleNoteCounts.clear();}for(auto &v:m_voices)v.releasing=true; }
void SynthStream::setMuted(bool muted) { QMutexLocker lock(&m_mutex); m_muted=muted; }
void SynthStream::setVolume(double volume) { m_volumeTarget.store(qBound(0.0,volume,1.5),std::memory_order_relaxed); }
void SynthStream::setInstrument(SynthInstrument instrument) { QMutexLocker lock(&m_mutex);if(m_soundFont&&instrument!=m_instrument){tsf_note_off_all(m_soundFont);m_sampleVoices.clear();m_sampleNoteCounts.clear();}m_instrument=instrument; }

void SynthStream::startSequence(const QVector<NoteEvent> &notes,int tempo,double startBeat) {
    QMutexLocker lock(&m_mutex);
    stopSampleVoices(true);
    for(auto &voice:m_voices)if(voice.sequenced)voice.releasing=true;
    m_sequence.clear();m_sequenceIndex=0;m_sequenceFrame=0;
    const double framesPerBeat=m_sampleRate*60.0/qMax(1,tempo);
    const qint64 pedalTailFrames=qRound64(sequencePedalTail(m_instrument)*m_sampleRate);
    for(const auto &note:notes){
        if(note.start+note.duration<=startBeat)continue;
        const quint64 id=m_nextVoiceId++;
        const double audibleStart=qMax(note.start,startBeat);
        const qint64 start=qMax<qint64>(0,qRound64((audibleStart-startBeat)*framesPerBeat));
        const qint64 end=qMax(start+1,qRound64((note.start+note.duration-startBeat)*framesPerBeat)+pedalTailFrames);
        m_sequence.append({start,true,note.note,note.velocity,id});
        m_sequence.append({end,false,note.note,0,id});
    }
    std::sort(m_sequence.begin(),m_sequence.end(),[](const ScheduledEvent &a,const ScheduledEvent &b){return a.frame==b.frame?a.noteOn<b.noteOn:a.frame<b.frame;});
    m_sequenceActive=!m_sequence.isEmpty();
}

void SynthStream::stopSequence() {
    QMutexLocker lock(&m_mutex);
    m_sequenceActive=false;m_sequence.clear();m_sequenceIndex=0;
    stopSampleVoices(true);
    for(auto &voice:m_voices)if(voice.sequenced)voice.releasing=true;
}

qint64 SynthStream::readData(char *data, qint64 maxlen) {
    const int channels=2; const int frameBytes=int(sizeof(float))*channels; const int frames=int(maxlen/frameBytes);
    auto *out=reinterpret_cast<float*>(data);
    // Do not hold the voice lock for an entire device buffer. With a large,
    // dense score that starved note input and made the on-screen keyboard and
    // volume control appear to jump or freeze. Short chunks keep the callback
    // safe while bounding UI-side waits to a few milliseconds.
    constexpr int chunkFrames=256;
    for(int chunkStart=0;chunkStart<frames;chunkStart+=chunkFrames){
      const int chunkEnd=qMin(frames,chunkStart+chunkFrames);
      const int frameCount=chunkEnd-chunkStart;
      std::array<float,chunkFrames*2> sampled{};
      QMutexLocker lock(&m_mutex);
      auto processEvent=[&](const ScheduledEvent &event){
          if(m_soundFont){
              if(event.noteOn)startSampleVoice(event.voiceId,event.note,event.velocity,true);
              else stopSampleVoice(event.voiceId);
          }else if(event.noteOn){
              const double frequency=440.0*qPow(2.0,(event.note-69)/12.0);
              const double step=2*M_PI*frequency/m_sampleRate;
              const double lifetime=pianoLifetime(m_instrument,event.note);
              const double decay=std::exp(-1.0/(m_sampleRate*lifetime));
              const double detune=stringDetune(m_instrument);
              m_voices.append({event.note,event.voiceId,0,0,0,0,step,step*detune,step*2.002,step*3.006,0.0,1.0,1.0,event.velocity/127.0,false,false,true,decay,m_instrument});
          }else for(auto &voice:m_voices)if(voice.id==event.voiceId){voice.releasing=true;break;}
      };

      if(m_soundFont){
          int rendered=0;
          while(rendered<frameCount){
              while(m_sequenceActive&&m_sequenceIndex<m_sequence.size()&&m_sequence[m_sequenceIndex].frame<=m_sequenceFrame){
                  processEvent(m_sequence[m_sequenceIndex++]);
                  if(m_sequenceIndex>=m_sequence.size())m_sequenceActive=false;
              }
              int segment=frameCount-rendered;
              if(m_sequenceActive&&m_sequenceIndex<m_sequence.size())segment=qMin(segment,int(qMax<qint64>(1,m_sequence[m_sequenceIndex].frame-m_sequenceFrame)));
              prepareSampleSustains(segment);
              tsf_render_float(m_soundFont,sampled.data()+rendered*2,segment,0);
              rendered+=segment;m_sequenceFrame+=segment;
          }
      }
      for(int i=chunkStart;i<chunkEnd;++i){double melodyLeft=0,melodyRight=0,clickSample=0,voiceLoad=0;
        if(m_soundFont){
            const int local=i-chunkStart;
            // GeneralUser's grand is a stereo sampled instrument.  Summing both
            // channels and copying the result back to L/R collapsed its piano
            // image and made exported scores sound much flatter than the source.
            melodyLeft=1.35*sampled[local*2];melodyRight=1.35*sampled[local*2+1];
        }
        else{
            while(m_sequenceActive&&m_sequenceIndex<m_sequence.size()&&m_sequence[m_sequenceIndex].frame<=m_sequenceFrame){
                processEvent(m_sequence[m_sequenceIndex++]);
                if(m_sequenceIndex>=m_sequence.size())m_sequenceActive=false;
            }
            for(const auto &v:m_voices)if(!v.click)voiceLoad+=v.level*v.naturalGain*v.velocity;
        }
        const double targetPolyphonyGain=m_soundFont?.92:.82/qSqrt(qMax(1.0,voiceLoad/4.0));
        m_polyphonyGain+=.0015*(targetPolyphonyGain-m_polyphonyGain);
        for(auto &v:m_voices){
            if(v.click){
                clickSample+=oscillator(v.phase)*v.velocity*v.level;
                v.phase+=v.phaseStep;
                v.level*=v.decay;
            }else if(!m_soundFont){
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
        shapePianoStereo(melodyLeft,melodyRight);
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
        deClick(soften(drivenLeft),m_lastOutputLeft);
        deClick(soften(drivenRight),m_lastOutputRight);
        const double muteTarget=m_muted?0.0:1.0;
        m_muteGain+=qBound(-.0025,muteTarget-m_muteGain,.0025);
        out[i*2]=float(m_lastOutputLeft*m_muteGain*.92);out[i*2+1]=float(m_lastOutputRight*m_muteGain*.92);
        if(!m_soundFont)++m_sequenceFrame;
      }
      // Compact once per short chunk: cheap enough for audio and it prevents a
      // long tail of dead voices from making later chunks progressively slower.
      m_voices.erase(std::remove_if(m_voices.begin(),m_voices.end(),[](const Voice&v){return v.releasing&&v.level*v.naturalGain<.0008;}),m_voices.end());
    }
    return qint64(frames)*frameBytes;
}

Synthesizer::Synthesizer(QObject *parent) : QObject(parent), m_stream(this) {
    if (qEnvironmentVariableIsSet("KEYFLOW_NO_AUDIO")) return;
    QAudioFormat format; format.setSampleRate(48000); format.setChannelCount(2); format.setSampleFormat(QAudioFormat::Float);
    QAudioDevice device=QMediaDevices::defaultAudioOutput(); if(!device.isFormatSupported(format)) format=device.preferredFormat();
    if(format.sampleFormat()!=QAudioFormat::Float || format.channelCount()!=2 || format.sampleRate()!=48000) return;
    m_sink=std::make_unique<QAudioSink>(device,format,this);m_sink->setBufferSize(16384);m_sink->start(&m_stream);
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
