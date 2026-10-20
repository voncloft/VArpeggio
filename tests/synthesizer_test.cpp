#include "synthesizer.h"
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
#include "pulseaudiooutput.h"
#endif

#include <QByteArray>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>

// Test-only access to the same worker lifecycle without opening any audio
// device. The fixture supplies a timer-driven pull consumer on that worker.
template<class Tag,typename Tag::type Member>struct SynthTestAccess{
    friend typename Tag::type get(Tag){return Member;}
};
struct StartOutputThreadTag{
    using type=void(Synthesizer::*)();friend type get(StartOutputThreadTag);
};
template struct SynthTestAccess<StartOutputThreadTag,&Synthesizer::startOutputThread>;
struct OutputContextTag{
    using type=QObject* Synthesizer::*;friend type get(OutputContextTag);
};
template struct SynthTestAccess<OutputContextTag,&Synthesizer::m_outputContext>;
struct OutputStreamTag{
    using type=SynthStream Synthesizer::*;friend type get(OutputStreamTag);
};
template struct SynthTestAccess<OutputStreamTag,&Synthesizer::m_stream>;

static QVector<float> render(SynthStream &stream, int frames) {
    QByteArray bytes(frames * 2 * int(sizeof(float)), Qt::Uninitialized);
    stream.readData(bytes.data(), bytes.size());
    QVector<float> samples(frames);
    for (int i = 0; i < frames; ++i)
        std::memcpy(&samples[i], bytes.constData() + i * 2 * int(sizeof(float)), sizeof(float));
    return samples;
}

static QVector<float> renderStereo(SynthStream &stream, int frames) {
    QByteArray bytes(frames * 2 * int(sizeof(float)), Qt::Uninitialized);
    stream.readData(bytes.data(), bytes.size());
    QVector<float> samples(frames*2);
    std::memcpy(samples.data(),bytes.constData(),bytes.size());
    return samples;
}

static bool check(bool value, const char *message) {
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}

static double rms(const QVector<float> &samples, int first = 0) {
    double sum = 0.0;
    for (int i = first; i < samples.size(); ++i) sum += samples[i] * samples[i];
    return std::sqrt(sum / std::max<qsizetype>(1, samples.size() - first));
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    SynthStream stream;
    bool ok = true;
    ok &= check(stream.recordedPianoAvailable(),"CC0 Kawai recording library is loaded");

    // Exercise QIODevice::read(), as QAudioSink does, rather than calling the
    // generator directly. A buffered device silently rendered 16 KiB for an
    // eight-byte request and carried that cached audio across score restarts.
    SynthStream exactReads;
    ok &= check(exactReads.isSequential()&&!exactReads.atEnd(),
                "a continuous audio generator never advertises EOF while silent");
    const auto quietFrame=exactReads.read(8);
    ok &= check(quietFrame.size()==8&&exactReads.renderedFrameCount()==1&&exactReads.bytesAvailable()==0,
                "live sink reads render exactly the requested frames without hidden read-ahead");
    exactReads.startSequence({{72,0,1,110,Hand::Right}},120);
    const auto liveAttack=exactReads.read(4096);
    float livePeak=0;
    for(int i=0;i<liveAttack.size()/int(sizeof(float));++i){float value=0;std::memcpy(&value,liveAttack.constData()+i*sizeof(float),sizeof(float));livePeak=std::max(livePeak,std::abs(value));}
    ok &= check(livePeak>.005f&&exactReads.renderedFrameCount()==513,
                "a restarted live score delivers its complete first attack instead of cached silence");
    exactReads.stopSequence();
    ok &= check(!exactReads.atEnd()&&exactReads.read(8).size()==8,
                "stopping a score leaves the live device available for future samples");
#ifdef VARPEGGIO_HAVE_PULSEAUDIO
    // A missing native server must be a bounded, harmless fallback, not an
    // attempt to spawn a sound daemon or an unbounded wait. This path never
    // opens a playback stream and must not consume any generated samples.
    if(qEnvironmentVariableIsSet("VARPEGGIO_TEST_NATIVE_PULSE_UNAVAILABLE")){
        QTemporaryDir noServerDirectory;
        const bool hadServer=qEnvironmentVariableIsSet("PULSE_SERVER");
        const QByteArray previousServer=qgetenv("PULSE_SERVER");
        qputenv("PULSE_SERVER",("unix:"+noServerDirectory.path()+"/missing.sock").toUtf8());
        const qint64 beforeAbsentServer=exactReads.renderedFrameCount();
        QElapsedTimer absentServerTime;absentServerTime.start();
        {
            PulseAudioOutput absentServer(exactReads,48000,98304);
            ok &= check(!absentServer.available()&&!absentServer.start()
                        &&!absentServer.processedFrames(),
                        "unavailable native output exposes no fabricated playback clock");
            absentServer.reset();
        }
        if(hadServer)qputenv("PULSE_SERVER",previousServer);else qunsetenv("PULSE_SERVER");
        ok &= check(absentServerTime.elapsed()<4000
                    &&exactReads.renderedFrameCount()==beforeAbsentServer,
                    "native fallback and teardown are bounded and consume no source samples");
    }else std::cout<<"SKIP: native missing-server fixture (set VARPEGGIO_TEST_NATIVE_PULSE_UNAVAILABLE=1 with local-socket access).\n";
#endif

    // Exercise the production worker, dispatcher and shutdown with no sound
    // device. A deliberate GUI-thread sleep must not starve its refill timer.
    // This is not evidence about physical output; live backend checks are
    // separate. It guards the event-loop isolation that those checks require.
    QPointer<QObject> deletedOutputContext;
    bool streamDestroyedOnOwner=false,contextDestroyedOnWorker=false;
    QThread *ownerThread=QThread::currentThread();
    {
        const bool hadNoAudio=qEnvironmentVariableIsSet("VARPEGGIO_NO_AUDIO");
        const QByteArray previousNoAudio=qgetenv("VARPEGGIO_NO_AUDIO");
        qputenv("VARPEGGIO_NO_AUDIO","1");
        Synthesizer threaded;
        if(hadNoAudio)qputenv("VARPEGGIO_NO_AUDIO",previousNoAudio);else qunsetenv("VARPEGGIO_NO_AUDIO");
        (threaded.*get(StartOutputThreadTag{}))();
        QObject *context=threaded.*get(OutputContextTag{});
        SynthStream &workerStream=threaded.*get(OutputStreamTag{});
        deletedOutputContext=context;
        ok &= check(threaded.thread()==ownerThread&&context->thread()!=ownerThread
                    &&workerStream.parent()==nullptr&&workerStream.thread()==context->thread(),
                    "only the sink context and parentless source move off the GUI thread");
        QObject::connect(&workerStream,&QObject::destroyed,[&]{streamDestroyedOnOwner=QThread::currentThread()==ownerThread;});
        QObject::connect(context,&QObject::destroyed,[&]{contextDestroyedOnWorker=QThread::currentThread()!=ownerThread;});
        threaded.startSequence({{60,0,8,100,Hand::Right}},120);
        QTimer *pullTimer=nullptr;
        QByteArray workerPcm;
        std::atomic<int> pulls{0};
        std::atomic<bool> readWrongThread{false};
        QMetaObject::invokeMethod(context,[&]{
            pullTimer=new QTimer(context);
            pullTimer->setTimerType(Qt::PreciseTimer);
            QObject::connect(pullTimer,&QTimer::timeout,context,[&]{
                if(QThread::currentThread()!=workerStream.thread())readWrongThread=true;
                const QByteArray bytes=workerStream.read(512*2*sizeof(float));
                if(bytes.size()==512*2*sizeof(float)){workerPcm.append(bytes);++pulls;}
            });
            pullTimer->start(2);
        },Qt::BlockingQueuedConnection);
        QThread::msleep(80); // Intentionally do not process GUI events.
        ok &= check(pulls.load()>=3&&workerStream.renderedFrameCount()>=1536&&!readWrongThread.load(),
                    "audio-thread sample pulls continue while the GUI event loop is blocked");
        QMetaObject::invokeMethod(context,[&]{pullTimer->stop();},Qt::BlockingQueuedConnection);
        SynthStream offlineWorkerReference;
        offlineWorkerReference.startSequence({{60,0,8,100,Hand::Right}},120);
        ok &= check(!workerPcm.isEmpty()&&offlineWorkerReference.read(workerPcm.size())==workerPcm,
                    "worker-thread scheduling preserves the exact offline piano PCM bytes");
        bool directDispatchWorked=false;
        const auto workerReference=ReferenceAudioData::fromSamples(48000,QVector<float>(128,0.0f));
        QMetaObject::invokeMethod(context,[&]{
            // Calling public transport/getters from the owner must execute
            // directly rather than deadlocking on a blocking self-invocation.
            threaded.startReference(workerReference,120,0.0,1.0);
            threaded.stopSequence();
            directDispatchWorked=!threaded.outputFailed()&&!threaded.sequencePlaybackSeconds();
            pullTimer->stop();delete pullTimer;pullTimer=nullptr;
        },Qt::BlockingQueuedConnection);
        ok &= check(directDispatchWorked&&!threaded.available(),
                    "owner-thread transport and clock calls dispatch without a sink or self-deadlock");
    }
    ok &= check(deletedOutputContext.isNull()&&contextDestroyedOnWorker&&streamDestroyedOnOwner,
                "shutdown deletes the worker context and returns the source to its owning thread");

    // A dense chord used to hit the old hard limiter and sound like static.
    for (int note = 36; note <= 96; note += 3) stream.noteOn(note, 112);
    const auto chord = render(stream, 8192);
    float peak = 0.0f;
    for (float sample : chord) {
        ok &= check(std::isfinite(sample), "audio samples stay finite");
        peak = std::max(peak, std::abs(sample));
    }
    ok &= check(peak > .05f, "dense chord produces audible output");
    ok &= check(peak < .925f, "dense chord is soft-limited without hard clipping");

    // Ending part of a chord changes the polyphony compensation. That gain
    // transition must be gradual or it produces a pop between notes.
    for (int note = 36; note <= 96; note += 6) stream.noteOff(note);
    const auto chordRelease = render(stream, 24000);
    float largestStep = 0.0f;
    for (int i = 1; i < chordRelease.size(); ++i)
        largestStep = std::max(largestStep, std::abs(chordRelease[i] - chordRelease[i - 1]));
    if(largestStep>=.12f)std::cerr<<"release largest step "<<largestStep<<'\n';
    ok &= check(largestStep < .12f, "chord release has no abrupt gain step");

    // Retriggering a sounding note must not reset a waveform abruptly.
    stream.allOff();
    render(stream, 24000);
    stream.noteOn(60, 100);
    const auto before = render(stream, 4096);
    stream.noteOn(60, 100);
    const auto after = render(stream, 1);
    ok &= check(std::abs(after.first() - before.last()) < .12f, "note retrigger remains click-free");

    // Imported score playback is now queued on the audio sample clock. Rapid
    // repeated notes must remain continuous without GUI-timer jitter or one
    // note-off accidentally killing a newer voice of the same pitch.
    stream.allOff();render(stream,4096);
    QVector<NoteEvent> sequence{{60,0,.25,100,Hand::Right},{60,.25,.25,100,Hand::Right},{64,.5,.5,96,Hand::Right}};
    stream.startSequence(sequence,120);
    const auto scheduled=render(stream,30000);
    float scheduledPeak=0,scheduledStep=0;
    for(int i=1;i<scheduled.size();++i){scheduledPeak=std::max(scheduledPeak,std::abs(scheduled[i]));scheduledStep=std::max(scheduledStep,std::abs(scheduled[i]-scheduled[i-1]));}
    ok &= check(scheduledPeak>.03f,"sample-clock score playback produces audio");
    ok &= check(scheduledStep<.08f,"sample-clock repeated notes remain click-free");
    stream.stopSequence();

    // Sequence time zero is an absolute rendered-stream frame. This is the
    // contract used to align QAudioSink's processed clock with the orange
    // playhead even when the sink already has silence queued ahead of a song.
    SynthStream clocked;
    render(clocked,4096);
    const qint64 beforeSequence=clocked.renderedFrameCount();
    clocked.startSequence({{60,0,.5,100,Hand::Right}},120);
    ok &= check(clocked.sequenceOriginFrame()==beforeSequence,
                "sequence clock begins at the next absolute rendered frame");
    const quint64 clockSerial=clocked.sequenceSerial();
    render(clocked,2048);
    ok &= check(clocked.renderedFrameCount()==beforeSequence+2048&&clocked.sequenceOriginFrame()==beforeSequence,
                "rendering advances the absolute audio clock without moving sequence time zero");
    clocked.startSequence({{64,0,.5,100,Hand::Right}},120);
    ok &= check(clocked.sequenceSerial()==clockSerial+1&&clocked.sequenceOriginFrame()==clocked.renderedFrameCount(),
                "restarting a score creates a fresh audio-clock origin");

    SynthStream primed;
    primed.startSequence({{72,0,.5,110,Hand::Right}},120,0.0,false,4096);
    ok &= check(primed.sequenceOriginFrame()==4096&&rms(render(primed,4096))<.0001,
                "device priming stays silent until the exact musical audio-clock origin");
    ok &= check(rms(render(primed,4096))>.005,
                "the complete first note begins after device priming without dropping its attack");

    // Original-recording playback uses the same exact sample clock. Preserve
    // decoded stereo PCM, seek into source time, and resample linearly when a
    // playback-rate change advances by more than one source frame at a time.
    QVector<float> referenceSamples;
    for(int frame=0;frame<16;++frame){referenceSamples.append(float(.02+.01*frame));referenceSamples.append(float(-.04-.005*frame));}
    const auto referenceAudio=ReferenceAudioData::fromSamples(48000,std::move(referenceSamples));
    SynthStream referenceRate;
    referenceRate.setOutputSampleRate(48000);
    referenceRate.startReference(referenceAudio,120,2.0/48000.0,2.0);
    const auto rateOutput=renderStereo(referenceRate,3);
    constexpr double defaultVolume=1.0;
    ok &= check(std::abs(rateOutput[0]-float(.04*defaultVolume))<.00001f
                &&std::abs(rateOutput[1]-float(-.05*defaultVolume))<.00001f
                &&std::abs(rateOutput[2]-float(.06*defaultVolume))<.00001f
                &&std::abs(rateOutput[3]-float(-.06*defaultVolume))<.00001f
                &&std::abs(rateOutput[4]-float(.08*defaultVolume))<.00001f
                &&std::abs(rateOutput[5]-float(-.07*defaultVolume))<.00001f,
                "reference PCM preserves stereo while seek and playback rate select source frames");

    SynthStream referenceFraction;
    referenceFraction.setOutputSampleRate(48000);
    referenceFraction.startReference(referenceAudio,120,.5/48000.0,1.0);
    const auto fractionOutput=renderStereo(referenceFraction,1);
    ok &= check(std::abs(fractionOutput[0]-float(.025*defaultVolume))<.00001f
                &&std::abs(fractionOutput[1]-float(-.0425*defaultVolume))<.00001f,
                "reference PCM uses linear interpolation between decoded source frames");

    QVector<float> hardSeekSamples;
    for(int frame=0;frame<80;++frame){hardSeekSamples.append(1.0f);hardSeekSamples.append(-1.0f);}
    const auto hardSeekAudio=ReferenceAudioData::fromSamples(48000,std::move(hardSeekSamples));
    SynthStream hardSeek;
    hardSeek.startReference(hardSeekAudio,120,0.0,1.0);
    const auto guardedSeek=renderStereo(hardSeek,65);
    bool startupBounded=std::abs(guardedSeek[0])<=.07501f&&std::abs(guardedSeek[1])<=.07501f;
    for(int frame=1;frame<64;++frame){
        startupBounded=startupBounded
            &&std::abs(guardedSeek[frame*2]-guardedSeek[(frame-1)*2])<=.07501f
            &&std::abs(guardedSeek[frame*2+1]-guardedSeek[(frame-1)*2+1])<=.07501f;
    }
    ok &= check(startupBounded,
                "reference seek begins with a bounded source-only transport edge");
    ok &= check(std::abs(guardedSeek[128]-1.0f)<.00001f
                &&std::abs(guardedSeek[129]+1.0f)<.00001f,
                "reference PCM is untouched after the 64-frame startup guard");
    hardSeek.stopSequence();hardSeek.startReference(hardSeekAudio,120,0.0,1.0);
    const auto restartedSeek=renderStereo(hardSeek,1);
    ok &= check(std::abs(restartedSeek[0])<=.07501f&&std::abs(restartedSeek[1])<=.07501f,
                "restarting reference playback resets the transport-edge guard");

    SynthStream primedReference;
    primedReference.setOutputSampleRate(48000);
    primedReference.startReference(referenceAudio,120,0.0,1.0,false,0.0,4);
    ok &= check(primedReference.sequenceOriginFrame()==4&&rms(renderStereo(primedReference,4))<.000001,
                "reference PCM stays silent through device priming on the shared clock");
    const auto firstReferenceFrame=renderStereo(primedReference,1);
    ok &= check(std::abs(firstReferenceFrame[0]-float(.02*defaultVolume))<.00001f
                &&std::abs(firstReferenceFrame[1]-float(-.04*defaultVolume))<.00001f,
                "reference PCM begins at its complete first frame after priming");

    SynthStream stoppedReference;
    stoppedReference.startReference(referenceAudio,120,0.0,1.0);
    stoppedReference.stopSequence();
    ok &= check(rms(renderStereo(stoppedReference,32))<.000001,
                "transport stop releases the decoded reference recording");
    stoppedReference.startReference(referenceAudio,120,0.0,1.0);
    stoppedReference.allOff();
    ok &= check(rms(renderStereo(stoppedReference,32))<.000001,
                "all-notes-off also releases the decoded reference recording");

    SynthStream replacedReference;
    replacedReference.startReference(referenceAudio,120,0.0,1.0);
    replacedReference.startSequence({{60,4,.5,100,Hand::Right}},120);
    ok &= check(rms(renderStereo(replacedReference,32))<.000001,
                "starting generated score audio cannot leak an older reference recording");

    QVector<float> silentReferenceSamples(48000*2,0.0f);
    const auto silentReference=ReferenceAudioData::fromSamples(48000,std::move(silentReferenceSamples));
    SynthStream referenceMetro;
    referenceMetro.startReference(silentReference,120,.25,1.0,true,.25);
    ok &= check(rms(renderStereo(referenceMetro,4096))>.001,
                "reference metronome and trimmed score origin share the first output frame");

    // A transport stop is a hard output boundary. Old sequenced voices must
    // not keep leaking into the buffer which follows Pause, Stop, or a seek.
    SynthStream transported;
    transported.startSequence({{60,0,8,110,Hand::Right}},120);
    ok &= check(rms(render(transported,4096))>.005,"transport fixture begins with audible score audio");
    transported.stopSequence();
    const auto stoppedTransport=render(transported,2048);
    ok &= check(rms(stoppedTransport,128)<.0001,"transport stop discards the previous score audio immediately");

    // Internal metronome attacks share the sample-clock schedule. They are not
    // injected later by the GUI after the orange line has already crossed the
    // beat (which would put them one complete output buffer behind).
    const QVector<NoteEvent> delayedPiano{{60,4,.5,100,Hand::Right}};
    SynthStream noMetro;noMetro.startSequence(delayedPiano,120);
    ok &= check(rms(render(noMetro,4096))<.0001,"delayed piano stays silent before its written attack");
    SynthStream sampleMetro;sampleMetro.startSequence(delayedPiano,120,0.0,true);
    const auto firstMetronomeBeat=render(sampleMetro,4096);
    ok &= check(rms(firstMetronomeBeat)>.001,"metronome is rendered at the same sample-clock beat as the score cursor");
    noMetro.setSequenceMetronomeEnabled(true);
    render(noMetro,24000-4096);
    ok &= check(rms(render(noMetro,4096))>.001,
                "enabling the metronome during playback joins the existing sample clock");
    noMetro.setSequenceMetronomeEnabled(false);
    render(noMetro,24000-4096);
    ok &= check(rms(render(noMetro,4096))<.0001,
                "disabling the metronome suppresses later scheduled clicks");

    // If the desktop audio server starves the sink, restarting at full sample
    // amplitude creates a second crack when sound resumes. Recovery begins at
    // zero and fades back in while preserving the active piano voice.
    SynthStream recovered;
    recovered.noteOn(67,108);
    render(recovered,4096);
    recovered.prepareAudioRecovery();
    const auto recovery=render(recovered,4096);
    float recoveryStep=0;
    for(int index=1;index<recovery.size();++index)recoveryStep=std::max(recoveryStep,std::abs(recovery[index]-recovery[index-1]));
    ok &= check(std::abs(recovery.first())<.002f,"underrun recovery restarts at silence");
    ok &= check(recoveryStep<.08f,"underrun recovery fades in without a crack");
    ok &= check(rms(recovery,recovery.size()-1024)>.001,"underrun recovery keeps the piano voice alive");

    // Real engraved scores keep adding attacks while bass notes, ties and
    // damper releases are still sounding.  Exercise that sustained density,
    // not merely one chord at time zero: this is the pattern that exposed the
    // former static, limiter pumping and voice-stealing clicks.
    SynthStream denseScore;
    QVector<NoteEvent> denseNotes;
    for(int beat=0;beat<48;++beat){
        if(beat%4==0)denseNotes.append({36+(beat/4)%5, double(beat), 4.0, 108, Hand::Left});
        const int root=55+(beat%8);
        denseNotes.append({root,beat*.5,.48,104,Hand::Right});
        denseNotes.append({root+4,beat*.5,.48,100,Hand::Right});
        denseNotes.append({root+7,beat*.5,.48,100,Hand::Right});
        if(beat%3==0)denseNotes.append({root+12,beat*.5,2.5,96,Hand::Right});
    }
    denseScore.startSequence(denseNotes,101);
    const auto denseRender=render(denseScore,720000);
    float densePeak=0,denseStep=0;qsizetype nearLimit=0;
    for(int i=0;i<denseRender.size();++i){
        ok &= check(std::isfinite(denseRender[i]),"dense score samples stay finite");
        densePeak=std::max(densePeak,std::abs(denseRender[i]));
        if(std::abs(denseRender[i])>=.88f)++nearLimit;
        if(i)denseStep=std::max(denseStep,std::abs(denseRender[i]-denseRender[i-1]));
    }
    ok &= check(densePeak<.925f,"dense sustained score avoids hard clipping");
    ok &= check(denseStep<.071f,"dense sustained score has no discontinuous pops");
    ok &= check(nearLimit<denseRender.size()/1000,"dense sustained score does not ride the limiter");

    // Wide leaps followed by a very fast descending flourish reproduce the
    // burst that sounded like crackle in imported performance MIDIs. The
    // previous notes are still releasing, so the mixer must create headroom
    // smoothly rather than repeatedly striking the peak shaper.
    SynthStream jumpScore;
    QVector<NoteEvent> jumpNotes{{28,0,.9,118,Hand::Left},{91,.5,.35,118,Hand::Right},{35,1,.9,116,Hand::Left},{96,1.5,.35,120,Hand::Right}};
    for(int index=0;index<14;++index)jumpNotes.append({96-index*2,2.0+index*.03,.05,116,Hand::Right});
    jumpScore.startSequence(jumpNotes,202);
    const auto jumpRender=render(jumpScore,96000);
    float jumpPeak=0,jumpStep=0;qsizetype jumpNearLimit=0;
    for(int index=0;index<jumpRender.size();++index){jumpPeak=std::max(jumpPeak,std::abs(jumpRender[index]));if(std::abs(jumpRender[index])>=.88f)++jumpNearLimit;if(index)jumpStep=std::max(jumpStep,std::abs(jumpRender[index]-jumpRender[index-1]));}
    ok &= check(jumpPeak<.925f,"wide-jump flourish keeps peak headroom");
    ok &= check(jumpStep<.076f,"wide-jump flourish has no crackling discontinuity");
    ok &= check(jumpNearLimit<jumpRender.size()/2000,"wide-jump flourish does not grind against the peak shaper");

    // Tutorial piano audio commonly carries a short damper-pedal overlap even
    // though falling rectangles describe only physical key-down time.  Score
    // playback should keep a natural piano tail through the space before the
    // next attack instead of cutting to a conspicuous hole.
    SynthStream pedalContinuity;
    pedalContinuity.setInstrument(SynthInstrument::BrightPiano);
    pedalContinuity.startSequence({{60,0,.25,100,Hand::Right},{64,1,.25,100,Hand::Right}},120);
    const auto pedalPhrase=render(pedalContinuity,24000);
    ok &= check(rms(pedalPhrase,12000)>.0015,"short score notes retain quiet pedal resonance between attacks");

    // Retiming while Listen is already running restarts the sample-clock
    // schedule at the same musical beat, including a note held across it.
    SynthStream retimed;
    QVector<NoteEvent> heldSequence{{48,0,1,100,Hand::Left},{60,1,4,100,Hand::Right},{72,8,1,100,Hand::Right}};
    retimed.startSequence(heldSequence,84,2.5);
    const auto resumed=render(retimed,4096);
    ok &= check(rms(resumed)>.01,"live tempo change resumes the score at the current beat");
    retimed.stopSequence();

    // A long written/tied score note remains held until its endpoint, while
    // retaining a real piano's natural decay instead of an organ-like gain
    // boost.  Its quiet tail must still exist before the scheduled release.
    SynthStream longHold;
    longHold.startSequence({{60,0,16,104,Hand::Right}},120);
    const auto longAttack=render(longHold,24000);
    const auto longBody=render(longHold,336000);
    ok &= check(rms(longBody,longBody.size()-12000)>rms(longAttack)*.01,
                "long score note retains its naturally decaying sampled tail");
    const auto longRelease=render(longHold,96000);
    ok &= check(rms(longRelease,longRelease.size()-24000)<rms(longBody,longBody.size()-12000)*.25,
                "long score note releases after its written endpoint");

    // Independently stop overlapping events of the same pitch. A short
    // restrike must neither kill the long voice nor remain haunted afterwards.
    SynthStream samePitch;
    samePitch.startSequence({{60,0,4,100,Hand::Right},{60,1,.5,96,Hand::Right}},120);
    const auto overlapMiddle=render(samePitch,60000);
    ok &= check(rms(overlapMiddle,48000)>.005,"short same-pitch note does not cut the long hold");
    const auto overlapEnd=render(samePitch,180000);
    ok &= check(rms(overlapEnd,overlapEnd.size()-24000)<.001,"same-pitch voices stop without a haunted remainder");

    // The no-sample fallback must obey the same duration contract. It used
    // to compact a still-held sequenced voice after its natural gain became
    // small, causing a literal early cutoff on long notes.
    qputenv("VARPEGGIO_DISABLE_PIANO_SAMPLES","1");
    SynthStream fallbackHold;
    fallbackHold.startSequence({{60,0,16,104,Hand::Right}},120);
    const auto fallbackAttack=render(fallbackHold,24000);
    const auto fallbackBody=render(fallbackHold,336000);
    ok &= check(rms(fallbackBody,fallbackBody.size()-12000)>rms(fallbackAttack)*.04,
                "fallback engine retains a long note until scheduled note-off");
    const auto fallbackRelease=render(fallbackHold,96000);
    ok &= check(rms(fallbackRelease,fallbackRelease.size()-24000)<.001,
                "fallback long note stops after its endpoint");
    qunsetenv("VARPEGGIO_DISABLE_PIANO_SAMPLES");

    // A held acoustic-piano key should naturally fade. The previous oscillator
    // stayed at one level and sounded like a church organ.
    SynthStream acoustic;
    acoustic.setInstrument(SynthInstrument::AcousticGrand);
    acoustic.noteOn(60, 104);
    const auto earlyPiano = render(acoustic, 12000);
    const auto laterPiano = render(acoustic, 84000);
    ok &= check(rms(laterPiano, 72000) < rms(earlyPiano) * .75,
                "acoustic piano decays instead of sustaining like an organ");

    SynthStream bright;
    bright.setInstrument(SynthInstrument::BrightPiano);
    bright.noteOn(60, 104);
    const auto brightTone = render(bright, 4096);
    SynthStream soft;
    soft.setInstrument(SynthInstrument::SoftPiano);
    soft.noteOn(60, 104);
    const auto softTone = render(soft, 4096);
    double timbreDifference = 0.0;
    for (int i=0;i<brightTone.size();++i) timbreDifference += std::abs(brightTone[i]-softTone[i]);
    ok &= check(timbreDifference/brightTone.size()>.002,
                "piano selector changes the rendered timbre");

    // Preserve the two microphones in the original Kawai recording without
    // collapsing them or manufacturing additional reflections.
    SynthStream spatial;
    spatial.noteOn(64,104);
    const auto stereo=renderStereo(spatial,24000);
    double midEnergy=0,sideEnergy=0;
    for(int i=0;i<stereo.size()/2;++i){const double mid=(stereo[i*2]+stereo[i*2+1])*.5,side=(stereo[i*2]-stereo[i*2+1])*.5;midEnergy+=mid*mid;sideEnergy+=side*side;}
    ok &= check(sideEnergy>midEnergy*.0001,"grand piano preserves its recorded stereo channels");
    ok &= check(sideEnergy<midEnergy*2.0,"recorded piano remains mono-compatible");

    QTemporaryDir exportDir;
    const QString exportedWav=exportDir.filePath("sampled-score.wav");QString exportError;
    ok &= check(Synthesizer::saveWav(exportedWav,sequence,120,&exportError),"export score through the sampled piano engine");
    QFile exportedFile(exportedWav);exportedFile.open(QIODevice::ReadOnly);
    ok &= check(exportedFile.read(4)=="RIFF"&&exportedFile.size()>10000,"sampled WAV export has audio data");
    const QString brightWav=exportDir.filePath("bright-score.wav"),softWav=exportDir.filePath("soft-score.wav");
    ok &= check(Synthesizer::saveWav(brightWav,sequence,120,&exportError,SynthInstrument::BrightPiano),"export selected bright piano");
    ok &= check(Synthesizer::saveWav(softWav,sequence,120,&exportError,SynthInstrument::SoftPiano),"export selected mellow piano");
    QFile brightFile(brightWav),softFile(softWav);brightFile.open(QIODevice::ReadOnly);softFile.open(QIODevice::ReadOnly);
    ok &= check(brightFile.readAll()!=softFile.readAll(),"WAV export honors the selected piano timbre");

    stream.setMuted(true);
    const auto muted = render(stream, 1024);
    ok &= check(std::abs(muted.last()) < .0001f, "mute uses a short fade and reaches silence");

    if (!ok) return 1;
    std::cout << "All VArpeggio synthesizer tests passed.\n";
    return 0;
}
