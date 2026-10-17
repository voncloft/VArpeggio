#include "musicdocument.h"

#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>
#include <iostream>

static void putLe16(QByteArray &out,quint16 value){out.append(char(value));out.append(char(value>>8));}
static void putLe32(QByteArray &out,quint32 value){out.append(char(value));out.append(char(value>>8));out.append(char(value>>16));out.append(char(value>>24));}

static QByteArray storedMxl(const QByteArray &score) {
    struct Item { QByteArray name,data;quint32 offset=0; };
    QVector<Item> items{
        {"META-INF/container.xml",R"XML(<?xml version="1.0"?><container><rootfiles><rootfile full-path="score.xml" media-type="application/vnd.recordare.musicxml+xml"/></rootfiles></container>)XML"},
        {"score.xml",score}
    };
    QByteArray out;
    for(auto &item:items){
        item.offset=out.size();putLe32(out,0x04034b50);putLe16(out,20);putLe16(out,0);putLe16(out,0);
        putLe16(out,0);putLe16(out,0);putLe32(out,0);putLe32(out,item.data.size());putLe32(out,item.data.size());
        putLe16(out,item.name.size());putLe16(out,0);out+=item.name;out+=item.data;
    }
    const quint32 directoryOffset=out.size();
    for(const auto &item:items){
        putLe32(out,0x02014b50);putLe16(out,20);putLe16(out,20);putLe16(out,0);putLe16(out,0);
        putLe16(out,0);putLe16(out,0);putLe32(out,0);putLe32(out,item.data.size());putLe32(out,item.data.size());
        putLe16(out,item.name.size());putLe16(out,0);putLe16(out,0);putLe16(out,0);putLe16(out,0);putLe32(out,0);putLe32(out,item.offset);out+=item.name;
    }
    const quint32 directorySize=out.size()-directoryOffset;
    putLe32(out,0x06054b50);putLe16(out,0);putLe16(out,0);putLe16(out,items.size());putLe16(out,items.size());
    putLe32(out,directorySize);putLe32(out,directoryOffset);putLe16(out,0);
    return out;
}

static bool check(bool value, const char *message) {
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir temp;
    if (!check(temp.isValid(), "temporary directory")) return 1;
    const auto source = MusicDocument::chordRoutine();
    QString error, title;
    int tempo = 72;
    bool ok = true;

    ok &= check(MusicDocument::spelledNoteName(56, -3) == QString::fromUtf8("A♭3"),
                "flat key spells MIDI 56 as A-flat rather than G-sharp");
    ok &= check(MusicDocument::spelledNoteName(56, 3) == QString::fromUtf8("G♯3"),
                "sharp key spells MIDI 56 as G-sharp");

    const QVector<NoteEvent> fourFlatCollection{
        {60,0,.5,90,Hand::Right},{58,.5,.5,90,Hand::Right},
        {56,1,.5,90,Hand::Right},{55,1.5,.5,90,Hand::Right},
        {53,2,.5,90,Hand::Left},{51,2.5,.5,90,Hand::Left},
        {49,3,.5,90,Hand::Left},{48,3.5,.5,90,Hand::Left}
    };
    ok &= check(MusicDocument::inferKeySignature(fourFlatCollection) == -4,
                "pitch collection becomes a conventional four-flat key signature");
    const QVector<NoteEvent> threeSharpCollection{
        {57,0,.5,90,Hand::Right},{59,.5,.5,90,Hand::Right},
        {61,1,.5,90,Hand::Right},{62,1.5,.5,90,Hand::Right},
        {64,2,.5,90,Hand::Right},{66,2.5,.5,90,Hand::Right},
        {68,3,.5,90,Hand::Right}
    };
    ok &= check(MusicDocument::inferKeySignature(threeSharpCollection) == 3,
                "pitch collection becomes a conventional three-sharp key signature");
    const auto cMajorRoutine=MusicDocument::scaleRoutine();
    ok &= check(MusicDocument::inferKeySignature(cMajorRoutine) == 0,
                "C-major practice material keeps an empty key signature");
    const QSet<int> cMajorPitchClasses{0,2,4,5,7,9,11};
    bool leftHandIsCMajor=true;
    for(const auto &note:cMajorRoutine)if(note.hand==Hand::Left&&!cMajorPitchClasses.contains((note.note%12+12)%12))leftHandIsCMajor=false;
    ok &= check(leftHandIsCMajor,"C-major contrary-motion left hand uses natural C-major notes");
    bool cScaleHasFingering=!cMajorRoutine.isEmpty();for(const auto &note:cMajorRoutine)cScaleHasFingering&=note.finger>=1&&note.finger<=5;
    ok &= check(cScaleHasFingering,"lesson scale supplies a valid finger for every note");
    const auto fMajorRoutine=MusicDocument::scaleRoutine(5);QVector<int> fRight;
    for(const auto &note:fMajorRoutine)if(note.hand==Hand::Right&&note.start<4.0)fRight.append(note.finger);
    ok &= check(fRight==QVector<int>({1,2,3,4,1,2,3,4}),"F-major lesson uses the conventional fourth-finger crossing");

    const auto arpeggio=MusicDocument::arpeggioRoutine();
    ok &= check(arpeggio.size()==14,"daily arpeggio supplies a complete up-and-down figure for both hands");
    const auto inversions=MusicDocument::inversionRoutine();QSet<int> inversionStarts;for(const auto &note:inversions)inversionStarts.insert(qRound(note.start*100));
    ok &= check(inversions.size()==24&&inversionStarts.size()==4,"daily inversion lesson covers four chord positions with both hands");
    const auto cadence=MusicDocument::cadenceRoutine(0,1);QSet<int> cadenceStarts;for(const auto &note:cadence)cadenceStarts.insert(qRound(note.start*100));
    ok &= check(cadence.size()==32&&cadenceStarts.size()==16,"daily broken cadence plays I-IV-V-I as separate chord tones");
    const auto fallingCadence=MusicDocument::cadenceRoutine(0,2);
    ok &= check(cadence.size()==fallingCadence.size()&&cadence[0].note!=fallingCadence[0].note,
                "daily cadence selections generate distinct rising and falling patterns");
    const auto pedalNotes=MusicDocument::pedalChangeRoutine();const auto pedalChanges=MusicDocument::pedalChangeEvents();
    ok &= check(pedalNotes.size()==24&&pedalChanges.size()==8&&pedalChanges.first().down&&!pedalChanges.last().down,"daily pedal lesson pairs four harmonies with exact press and release instructions");
    const auto aMinorScale=MusicDocument::scaleRoutine(9,true);QSet<int> aMinorPitchClasses;for(const auto &note:aMinorScale)aMinorPitchClasses.insert((note.note%12+12)%12);
    ok &= check(aMinorPitchClasses==QSet<int>({9,11,0,2,4,5,8}),"minor daily scale uses one complete harmonic-minor collection");
    const auto aMinorArpeggio=MusicDocument::arpeggioRoutine(9,true);QSet<int> aMinorArpeggioClasses;for(const auto &note:aMinorArpeggio)aMinorArpeggioClasses.insert((note.note%12+12)%12);
    ok &= check(aMinorArpeggioClasses==QSet<int>({9,0,4}),"minor daily arpeggio uses the minor third instead of silently reverting to major");
    const auto aMinorInversions=MusicDocument::inversionRoutine(9,true);QSet<int> aMinorInversionClasses;for(const auto &note:aMinorInversions)aMinorInversionClasses.insert((note.note%12+12)%12);
    ok &= check(aMinorInversionClasses==QSet<int>({9,0,4}),"minor daily inversions remain minor in every position");
    const auto aMinorCadence=MusicDocument::cadenceRoutine(9,0,true);QSet<int> aMinorCadenceClasses;for(const auto &note:aMinorCadence)aMinorCadenceClasses.insert((note.note%12+12)%12);
    ok &= check(aMinorCadenceClasses.contains(0)&&aMinorCadenceClasses.contains(8)&&aMinorCadenceClasses.contains(9),"minor daily cadence uses i-iv-V-i harmony");

    const auto morning=MusicDocument::morningLight();QVector<int> morningBassPitches,morningBassFingers;
    for(const auto &note:morning)if(note.hand==Hand::Left){morningBassPitches.append(note.note);morningBassFingers.append(note.finger);}
    ok &= check(morningBassPitches==QVector<int>({48,48,45,43,48,45}),"Morning Light keeps its authored bass line");
    ok &= check(morningBassFingers==QVector<int>({1,1,3,5,1,3}),"Morning Light uses LH thumb on C3 and conventional 1-3-5 placement");

    const QString midi = temp.filePath("roundtrip.mid");
    const QVector<PedalEvent> sourcePedals{{.25,true},{2.75,false}};
    ok &= check(MusicDocument::saveMidi(midi, source, tempo, &error, sourcePedals), "write MIDI");
    QFile midiFile(midi);midiFile.open(QIODevice::ReadOnly);const QByteArray midiBytes=midiFile.readAll();
    ok &= check(midiBytes.contains(QByteArray{"\xb0\x40\x7f",3})&&midiBytes.contains(QByteArray{"\xb0\x40\x00",3}),"MIDI preserves sustain-pedal CC64 down and release");
    QVector<NoteEvent> loaded;
    QVector<PedalEvent> importedMidiPedals;
    ok &= check(MusicDocument::importFile(midi, loaded, title, tempo, &error,nullptr,nullptr,nullptr,nullptr,&importedMidiPedals), "read MIDI");
    ok &= check(loaded.size() == source.size(), "MIDI preserves note count");
    ok &= check(importedMidiPedals.size()==2&&importedMidiPedals.first().down&&!importedMidiPedals.last().down&&qAbs(importedMidiPedals.first().beat-.25)<.001&&qAbs(importedMidiPedals.last().beat-2.75)<.001,"MIDI import restores CC64 pedal timing for practice validation");

    const QString duplicateMidi=temp.filePath("duplicate-unison.mid");
    const QVector<NoteEvent> duplicateUnison{{60,0,1,72,Hand::Left},{60,0,1,104,Hand::Right}};
    ok &= check(MusicDocument::saveMidi(duplicateMidi,duplicateUnison,100,&error),"write duplicate-channel MIDI fixture");
    loaded.clear();tempo=90;
    ok &= check(MusicDocument::importFile(duplicateMidi,loaded,title,tempo,&error),"read duplicate-channel MIDI fixture");
    ok &= check(loaded.size()==1&&loaded.first().velocity==104,"MIDI import merges identical notes duplicated across channels");

    const QString rollingTextureMidi=temp.filePath("cross-staff-rolling-texture.mid");
    const QVector<NoteEvent> rollingTexture{
        {37,0,9.25,72,Hand::Left},{49,0,9.25,72,Hand::Left},
        {56,0,1,84,Hand::Right},{61,.75,1,84,Hand::Right},{64,1.5,1,84,Hand::Right},
        {56,2.25,1,84,Hand::Right},{61,3,1,84,Hand::Right},{64,3.75,1,84,Hand::Right}
    };
    ok &= check(MusicDocument::saveMidi(rollingTextureMidi,rollingTexture,110,&error),"write cross-staff rolling-texture MIDI fixture");
    loaded.clear();tempo=90;
    ok &= check(MusicDocument::importFile(rollingTextureMidi,loaded,title,tempo,&error),"read cross-staff rolling-texture MIDI fixture");
    bool bassStayedLeft=true,rollingStayedRight=true;
    for(const auto &note:loaded){if(note.note<=49)bassStayedLeft&=note.hand==Hand::Left;else rollingStayedRight&=note.hand==Hand::Right;}
    ok &= check(loaded.size()==rollingTexture.size(),"cross-staff hand inference preserves every MIDI note");
    ok &= check(bassStayedLeft&&rollingStayedRight,"held bass octave stays left while the complete rolling figure stays right");
    const QVector<NoteEvent> visualLegato{
        {37,0,9.25,72,Hand::Left},{49,0,9.25,72,Hand::Left},
        {56,0,1,84,Hand::Right},{61,.75,1,84,Hand::Right},{64,1.5,1,84,Hand::Right},
        {35,9,9.25,72,Hand::Left},{47,9,9.25,72,Hand::Left},{56,9,1,84,Hand::Right}
    };
    const auto visualReleases=MusicDocument::keyboardReleaseBeats(visualLegato);
    ok &= check(qAbs(visualReleases[0]-9.0)<.001&&qAbs(visualReleases[1]-9.0)<.001,"virtual bass keys release at the next bass attack instead of piling up across a barline");
    ok &= check(qAbs(visualReleases[2]-.75)<.001&&qAbs(visualReleases[3]-1.5)<.001,"virtual rolling keys release at the next attack while audio legato remains intact");

    const QString xml = temp.filePath("roundtrip.musicxml");
    ok &= check(MusicDocument::saveMusicXml(xml, source, 72, "Chord drill", &error), "write MusicXML");
    loaded.clear(); tempo = 72;
    ok &= check(MusicDocument::importFile(xml, loaded, title, tempo, &error), "read MusicXML");
    ok &= check(loaded.size() == source.size(), "MusicXML preserves note count");

    QFile xmlSource(xml);xmlSource.open(QIODevice::ReadOnly);
    const QString mxl=temp.filePath("roundtrip.mxl");QFile mxlFile(mxl);mxlFile.open(QIODevice::WriteOnly);mxlFile.write(storedMxl(xmlSource.readAll()));mxlFile.close();
    loaded.clear();tempo=72;
    ok &= check(MusicDocument::importFile(mxl,loaded,title,tempo,&error),"read compressed MusicXML without an external unzip process");
    ok &= check(loaded.size()==source.size(),"compressed MusicXML preserves note count");

    const QString detailedXml = temp.filePath("detailed.musicxml");
    QFile detailedFile(detailedXml);
    ok &= check(detailedFile.open(QIODevice::WriteOnly), "create detailed MusicXML fixture");
    detailedFile.write(R"XML(<?xml version="1.0"?><score-partwise version="4.0">
<work><work-title>Detailed rhythm test</work-title></work>
<part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
<part id="P1"><measure number="1"><attributes><divisions>8</divisions><staves>2</staves></attributes>
<direction><direction-type><pedal type="start"/></direction-type><sound tempo="110"/></direction>
<note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration><voice>1</voice><type>eighth</type><staff>1</staff></note>
<note><pitch><step>D</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><type>16th</type><tie type="start"/><staff>1</staff></note>
<note><pitch><step>D</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><type>16th</type><tie type="stop"/><staff>1</staff></note>
<backup><duration>8</duration></backup>
<note><pitch><step>C</step><octave>3</octave></pitch><duration>8</duration><voice>2</voice><type>quarter</type><staff>2</staff></note>
<direction><direction-type><pedal type="stop"/></direction-type></direction>
</measure></part></score-partwise>)XML");
    detailedFile.close();
    loaded.clear(); tempo = 72; title.clear();
    QVector<PedalEvent> importedXmlPedals;
    ok &= check(MusicDocument::importFile(detailedXml, loaded, title, tempo, &error,nullptr,nullptr,nullptr,nullptr,&importedXmlPedals), "read detailed MusicXML");
    ok &= check(title == "Detailed rhythm test" && tempo == 110, "MusicXML preserves title and tempo");
    ok &= check(loaded.size() == 3, "MusicXML merges tied fragments without losing both staves");
    ok &= check(importedXmlPedals.size()==2&&importedXmlPedals.first().down&&!importedXmlPedals.last().down,"MusicXML imports written pedal press and release marks");
    auto durationFor=[&](int midi){for(const auto &note:loaded)if(note.note==midi)return note.duration;return -1.0;};
    ok &= check(qAbs(durationFor(60) - .5) < .001, "eighth-note duration is preserved");
    ok &= check(qAbs(durationFor(62) - .5) < .001, "tied 16th notes become one sustained note");

    const QString project = temp.filePath("roundtrip.varpeggio");
    ok &= check(MusicDocument::saveJson(project, source, 72, "Chord drill", &error), "write VArpeggio project");
    loaded.clear(); tempo = 90;
    ok &= check(MusicDocument::importFile(project, loaded, title, tempo, &error), "read VArpeggio project");
    ok &= check(loaded.size() == source.size() && title == "Chord drill" && tempo == 72, "project metadata round trip");

    const QString annotatedProject=temp.filePath("annotated.varpeggio");QVector<NoteEvent> annotated=source;if(!annotated.isEmpty()){annotated[0].accidental=AccidentalMark::Natural;annotated[0].finger=3;}
    ok &= check(MusicDocument::saveJson(annotatedProject,annotated,72,"Annotated",&error,{{0,3,4},{3,4,4}},{3},{{0,-1}},{{2,1,Hand::Left}},sourcePedals),"write editable notation metadata");
    QFile annotatedFile(annotatedProject);annotatedFile.open(QIODevice::ReadOnly);const QJsonObject annotatedJson=QJsonDocument::fromJson(annotatedFile.readAll()).object();
    ok &= check(annotatedJson.value("timeSignatures").toArray().size()==2&&annotatedJson.value("keySignatures").toArray().size()==1&&annotatedJson.value("repeatEnds").toArray().size()==1&&annotatedJson.value("rests").toArray().size()==1&&annotatedJson.value("pedalEvents").toArray().size()==2,"project preserves meter, key, repeat, rest, and pedal data");
    ok &= check(annotatedJson.value("notes").toArray().first().toObject().value("accidental").toInt()==int(AccidentalMark::Natural),"project preserves note accidental");
    ok &= check(annotatedJson.value("notes").toArray().first().toObject().value("finger").toInt()==3,"project preserves lesson fingering");
    QVector<TimeSignatureEvent> loadedMeters;QVector<double> loadedRepeats;QVector<KeySignatureEvent> loadedKeys;QVector<StaffRestEvent> loadedRests;QVector<PedalEvent> loadedPedals;loaded.clear();
    ok &= check(MusicDocument::importFile(annotatedProject,loaded,title,tempo,&error,&loadedMeters,&loadedRepeats,&loadedKeys,&loadedRests,&loadedPedals),"reopen editable notation project");
    ok &= check(loadedMeters.size()==2&&loadedKeys.size()==1&&loadedRepeats.size()==1&&loadedRests.size()==1&&loadedPedals.size()==2&&loadedPedals.first().down&&!loadedPedals.last().down&&loaded.first().accidental==AccidentalMark::Natural&&loaded.first().finger==3,"editable notation metadata round trip");

    const QString pdfPath = temp.filePath("playable.pdf");
    QFile projectFile(project); projectFile.open(QIODevice::ReadOnly);
    {
        QPdfWriter pdf(pdfPath);
        pdf.setTitle("Playable VArpeggio test");
        pdf.addFileAttachment("roundtrip.varpeggio", projectFile.readAll(), "application/vnd.voncloft.varpeggio+json");
        QPainter painter(&pdf); painter.drawText(100, 100, "Playable VArpeggio PDF"); painter.end();
    }
    const QString extracted = temp.filePath("extracted.varpeggio");
    QProcess detach;
    detach.start("pdfdetach", {"-savefile", "roundtrip.varpeggio", "-o", extracted, pdfPath});
    ok &= check(detach.waitForFinished(5000) && detach.exitCode() == 0, "extract playable PDF notes");
    loaded.clear(); tempo = 90; title.clear();
    ok &= check(MusicDocument::importFile(extracted, loaded, title, tempo, &error), "read notes embedded in PDF");
    ok &= check(loaded.size() == source.size(), "playable PDF preserves note count");

    const QString wav = temp.filePath("render.wav");
    ok &= check(MusicDocument::saveWav(wav, source, 72, &error), "write WAV");
    QFile wavFile(wav); wavFile.open(QIODevice::ReadOnly);
    ok &= check(wavFile.read(4) == "RIFF" && wavFile.size() > 1000, "WAV has audio data");

    QVector<NoteEvent> pulseTrain;
    for (int beat=0; beat<24; ++beat)
        pulseTrain << NoteEvent{60+(beat%5)*2,double(beat),.18,104,Hand::Right};
    const QString tempoWav=temp.filePath("tempo-110.wav");
    ok &= check(MusicDocument::saveWav(tempoWav,pulseTrain,110,&error), "write tempo fixture");
    int detectedTempo=0;double tempoConfidence=0;
    ok &= check(MusicDocument::detectTempo(tempoWav,detectedTempo,&tempoConfidence,&error), "automatically detect tempo");
    ok &= check(qAbs(detectedTempo-110)<=2, "automatic tempo stays within two BPM");
    ok &= check(tempoConfidence>.2, "automatic tempo reports useful confidence");

    QVector<NoteEvent> audioNotes;
    error.clear();
    ok &= check(MusicDocument::transcribeMedia(wav, 72, audioNotes, &error), "transcribe rendered piano audio");
    QSet<int> openingChord;
    for (const auto &note : audioNotes) if (note.start <= .25) openingChord.insert(note.note);
    ok &= check(openingChord.size() >= 3, "audio transcription retains simultaneous chord pitches");
    bool hasBass = false, hasTreble = false;
    for (const auto &note : audioNotes) { hasBass |= note.note < 60; hasTreble |= note.note >= 60; }
    ok &= check(hasBass && hasTreble, "audio transcription retains bass and treble ranges");

    if (!ok) { std::cerr << error.toStdString() << '\n'; return 1; }
    std::cout << "All VArpeggio music-document tests passed.\n";
    return 0;
}
