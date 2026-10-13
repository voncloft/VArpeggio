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

    const QString midi = temp.filePath("roundtrip.mid");
    ok &= check(MusicDocument::saveMidi(midi, source, tempo, &error), "write MIDI");
    QVector<NoteEvent> loaded;
    ok &= check(MusicDocument::importFile(midi, loaded, title, tempo, &error), "read MIDI");
    ok &= check(loaded.size() == source.size(), "MIDI preserves note count");

    const QString duplicateMidi=temp.filePath("duplicate-unison.mid");
    const QVector<NoteEvent> duplicateUnison{{60,0,1,72,Hand::Left},{60,0,1,104,Hand::Right}};
    ok &= check(MusicDocument::saveMidi(duplicateMidi,duplicateUnison,100,&error),"write duplicate-channel MIDI fixture");
    loaded.clear();tempo=90;
    ok &= check(MusicDocument::importFile(duplicateMidi,loaded,title,tempo,&error),"read duplicate-channel MIDI fixture");
    ok &= check(loaded.size()==1&&loaded.first().velocity==104,"MIDI import merges identical notes duplicated across channels");

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
<direction><sound tempo="110"/></direction>
<note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration><voice>1</voice><type>eighth</type><staff>1</staff></note>
<note><pitch><step>D</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><type>16th</type><tie type="start"/><staff>1</staff></note>
<note><pitch><step>D</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><type>16th</type><tie type="stop"/><staff>1</staff></note>
<backup><duration>8</duration></backup>
<note><pitch><step>C</step><octave>3</octave></pitch><duration>8</duration><voice>2</voice><type>quarter</type><staff>2</staff></note>
</measure></part></score-partwise>)XML");
    detailedFile.close();
    loaded.clear(); tempo = 72; title.clear();
    ok &= check(MusicDocument::importFile(detailedXml, loaded, title, tempo, &error), "read detailed MusicXML");
    ok &= check(title == "Detailed rhythm test" && tempo == 110, "MusicXML preserves title and tempo");
    ok &= check(loaded.size() == 3, "MusicXML merges tied fragments without losing both staves");
    auto durationFor=[&](int midi){for(const auto &note:loaded)if(note.note==midi)return note.duration;return -1.0;};
    ok &= check(qAbs(durationFor(60) - .5) < .001, "eighth-note duration is preserved");
    ok &= check(qAbs(durationFor(62) - .5) < .001, "tied 16th notes become one sustained note");

    const QString project = temp.filePath("roundtrip.varpeggio");
    ok &= check(MusicDocument::saveJson(project, source, 72, "Chord drill", &error), "write VArpeggio project");
    loaded.clear(); tempo = 90;
    ok &= check(MusicDocument::importFile(project, loaded, title, tempo, &error), "read VArpeggio project");
    ok &= check(loaded.size() == source.size() && title == "Chord drill" && tempo == 72, "project metadata round trip");

    const QString annotatedProject=temp.filePath("annotated.varpeggio");QVector<NoteEvent> annotated=source;if(!annotated.isEmpty())annotated[0].accidental=AccidentalMark::Natural;
    ok &= check(MusicDocument::saveJson(annotatedProject,annotated,72,"Annotated",&error,{{0,3,4},{3,4,4}},{3},{{0,-1}},{{2,1,Hand::Left}}),"write editable notation metadata");
    QFile annotatedFile(annotatedProject);annotatedFile.open(QIODevice::ReadOnly);const QJsonObject annotatedJson=QJsonDocument::fromJson(annotatedFile.readAll()).object();
    ok &= check(annotatedJson.value("timeSignatures").toArray().size()==2&&annotatedJson.value("keySignatures").toArray().size()==1&&annotatedJson.value("repeatEnds").toArray().size()==1&&annotatedJson.value("rests").toArray().size()==1,"project preserves meter, key, repeat, and rest data");
    ok &= check(annotatedJson.value("notes").toArray().first().toObject().value("accidental").toInt()==int(AccidentalMark::Natural),"project preserves note accidental");
    QVector<TimeSignatureEvent> loadedMeters;QVector<double> loadedRepeats;QVector<KeySignatureEvent> loadedKeys;QVector<StaffRestEvent> loadedRests;loaded.clear();
    ok &= check(MusicDocument::importFile(annotatedProject,loaded,title,tempo,&error,&loadedMeters,&loadedRepeats,&loadedKeys,&loadedRests),"reopen editable notation project");
    ok &= check(loadedMeters.size()==2&&loadedKeys.size()==1&&loadedRepeats.size()==1&&loadedRests.size()==1&&loaded.first().accidental==AccidentalMark::Natural,"editable notation metadata round trip");

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
