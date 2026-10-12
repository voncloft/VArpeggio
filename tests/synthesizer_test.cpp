#include "synthesizer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

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
