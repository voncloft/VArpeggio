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
    const auto dMinorScale=MusicDocument::scaleRoutine(2,true);QVector<int> dNatural,dHarmonic,dMelodicUp,dMelodicDown;AccidentalMark harmonicLeading=AccidentalMark::Automatic;
    for(const auto &note:dMinorScale)if(note.hand==Hand::Right){
        if(note.start<4.0)dNatural.append(note.note);
        else if(note.start>=8.0&&note.start<12.0){dHarmonic.append(note.note);if(qAbs(note.start-11.0)<.01)harmonicLeading=note.accidental;}
        else if(note.start>=16.0&&note.start<20.0)dMelodicUp.append(note.note);
        else if(note.start>=20.0&&note.start<24.0)dMelodicDown.append(note.note);
    }
    ok &= check(dNatural==QVector<int>({62,64,65,67,69,70,72,74}),"D natural minor ascends with B-flat and C natural");
    ok &= check(dHarmonic==QVector<int>({62,64,65,67,69,70,73,74})&&harmonicLeading==AccidentalMark::Sharp,"D harmonic minor keeps B-flat and writes C-sharp as the raised seventh");
    ok &= check(dMelodicUp==QVector<int>({62,64,65,67,69,71,73,74}),"D melodic minor ascending raises both B and C");
    ok &= check(dMelodicDown==QVector<int>({72,70,69,67,65,64,62}),"D melodic minor descends through C natural and B-flat");
    ok &= check(dMinorScale.size()==90,"minor scale chapter contains natural, harmonic, and melodic forms for both hands");
    const auto dNaturalOnly=MusicDocument::scaleRoutine(2,true,0,-1);const auto dHarmonicOnly=MusicDocument::scaleRoutine(2,true,1,-1);const auto dMelodicOnly=MusicDocument::scaleRoutine(2,true,2,-1);
    ok &= check(dNaturalOnly.size()==30&&dHarmonicOnly.size()==30&&dMelodicOnly.size()==30,"daily natural, harmonic, and melodic minor lessons are three separate one-form chapters");
    QVector<int> dHarmonicOnlyUp,dHarmonicOnlyDown,dHarmonicLeftOut,dHarmonicLeftReturn;AccidentalMark harmonicDownLeading=AccidentalMark::Automatic;
    for(const auto &note:dHarmonicOnly){
        if(note.hand==Hand::Right&&note.start<4.0)dHarmonicOnlyUp.append(note.note);
        else if(note.hand==Hand::Right&&note.start>=4.0){dHarmonicOnlyDown.append(note.note);if(qAbs(note.start-4.0)<.01)harmonicDownLeading=note.accidental;}
        else if(note.hand==Hand::Left&&note.start<4.0)dHarmonicLeftOut.append(note.note);
        else if(note.hand==Hand::Left&&note.start>=4.0)dHarmonicLeftReturn.append(note.note);
    }
    ok &= check(dHarmonicOnlyUp==QVector<int>({62,64,65,67,69,70,73,74}),"standalone D harmonic minor ascends with C-sharp");
    ok &= check(dHarmonicOnlyDown==QVector<int>({73,70,69,67,65,64,62})&&harmonicDownLeading==AccidentalMark::Sharp,"standalone D harmonic minor descends with C-sharp too");
    ok &= check(dHarmonicLeftOut==QVector<int>({50,49,46,45,43,41,40,38})&&dHarmonicLeftReturn==QVector<int>({40,41,43,45,46,49,50}),"left-hand D harmonic minor keeps C-sharp in both directions");
    const auto aSharpHarmonic=MusicDocument::scaleRoutine(10,true,1,7);const NoteEvent *aSharpLeading=nullptr;
    for(const auto &note:aSharpHarmonic)if(note.hand==Hand::Right&&qAbs(note.start-3.0)<.01){aSharpLeading=&note;break;}
    ok &= check(aSharpLeading&&aSharpLeading->writtenStep==4&&aSharpLeading->writtenAlter==2&&aSharpLeading->accidental==AccidentalMark::DoubleSharp&&MusicDocument::spelledNoteName(aSharpLeading->note,7,aSharpLeading->accidental,true,aSharpLeading->writtenStep,aSharpLeading->writtenAlter)==QString::fromUtf8("G𝄪5"),"A-sharp harmonic minor writes its leading tone as G-double-sharp");
    const auto cFlatMajor=MusicDocument::scaleRoutine(11,false,-1,-7);const auto cFlatFirst=std::find_if(cFlatMajor.cbegin(),cFlatMajor.cend(),[](const auto &note){return note.hand==Hand::Right&&note.start<.01;});
    ok &= check(cFlatFirst!=cFlatMajor.cend()&&cFlatFirst->writtenStep==0&&cFlatFirst->writtenAlter==-1&&MusicDocument::spelledNoteName(cFlatFirst->note,-7,cFlatFirst->accidental,true,cFlatFirst->writtenStep,cFlatFirst->writtenAlter)==QString::fromUtf8("C♭5"),"C-flat major remains a distinct written key instead of being renamed B major");
    struct RotationKey{int majorTonic,minorTonic,fifths;};
    const QVector<RotationKey> rotationKeys{
        {0,9,0},{7,4,1},{5,2,-1},{2,11,2},{10,7,-2},
        {9,6,3},{3,0,-3},{4,1,4},{8,5,-4},{11,8,5},
        {1,10,-5},{6,3,6},{6,3,-6},{1,10,7},{11,8,-7}
    };
    const QVector<int> majorRight{0,2,4,5,7,9,11,12,11,9,7,5,4,2,0};
    const QVector<int> naturalRight{0,2,3,5,7,8,10,12,10,8,7,5,3,2,0};
    const QVector<int> harmonicRight{0,2,3,5,7,8,11,12,11,8,7,5,3,2,0};
    const QVector<int> melodicRight{0,2,3,5,7,9,11,12,10,8,7,5,3,2,0};
    const QVector<int> harmonicLeft{0,-1,-4,-5,-7,-9,-10,-12,-10,-9,-7,-5,-4,-1,0};
    auto relativeLine=[](const QVector<NoteEvent>&notes,Hand hand,int base){QVector<int> result;for(const auto &note:notes)if(note.hand==hand)result.append(note.note-base);return result;};
    auto degreeAt=[](const QVector<NoteEvent>&notes,double beat)->const NoteEvent*{for(const auto &note:notes)if(note.hand==Hand::Right&&qAbs(note.start-beat)<.01)return &note;return nullptr;};
    bool completeRotationCorrect=true;
    for(const auto &key:rotationKeys){
        const auto major=MusicDocument::scaleRoutine(key.majorTonic,false,-1,key.fifths);
        const auto natural=MusicDocument::scaleRoutine(key.minorTonic,true,0,key.fifths);
        const auto harmonic=MusicDocument::scaleRoutine(key.minorTonic,true,1,key.fifths);
        const auto melodic=MusicDocument::scaleRoutine(key.minorTonic,true,2,key.fifths);
        completeRotationCorrect&=relativeLine(major,Hand::Right,60+key.majorTonic)==majorRight;
        completeRotationCorrect&=relativeLine(natural,Hand::Right,60+key.minorTonic)==naturalRight;
        completeRotationCorrect&=relativeLine(harmonic,Hand::Right,60+key.minorTonic)==harmonicRight;
        completeRotationCorrect&=relativeLine(melodic,Hand::Right,60+key.minorTonic)==melodicRight;
        completeRotationCorrect&=relativeLine(harmonic,Hand::Left,48+key.minorTonic)==harmonicLeft;
        const auto *naturalSixth=degreeAt(natural,2.5),*naturalSeventh=degreeAt(natural,3.0);
        const auto *harmonicSeventhUp=degreeAt(harmonic,3.0),*harmonicSeventhDown=degreeAt(harmonic,4.0);
        const auto *melodicSixthUp=degreeAt(melodic,2.5),*melodicSeventhUp=degreeAt(melodic,3.0);
        const auto *melodicSeventhDown=degreeAt(melodic,4.0),*melodicSixthDown=degreeAt(melodic,4.5);
        completeRotationCorrect&=naturalSixth&&naturalSeventh&&harmonicSeventhUp&&harmonicSeventhDown&&melodicSixthUp&&melodicSeventhUp&&melodicSeventhDown&&melodicSixthDown;
        if(naturalSixth&&naturalSeventh&&harmonicSeventhUp&&harmonicSeventhDown&&melodicSixthUp&&melodicSeventhUp&&melodicSeventhDown&&melodicSixthDown){
            completeRotationCorrect&=harmonicSeventhUp->writtenStep==naturalSeventh->writtenStep&&harmonicSeventhUp->writtenAlter==naturalSeventh->writtenAlter+1;
            completeRotationCorrect&=harmonicSeventhDown->writtenStep==naturalSeventh->writtenStep&&harmonicSeventhDown->writtenAlter==naturalSeventh->writtenAlter+1;
            completeRotationCorrect&=melodicSixthUp->writtenStep==naturalSixth->writtenStep&&melodicSixthUp->writtenAlter==naturalSixth->writtenAlter+1;
            completeRotationCorrect&=melodicSeventhUp->writtenStep==naturalSeventh->writtenStep&&melodicSeventhUp->writtenAlter==naturalSeventh->writtenAlter+1;
            completeRotationCorrect&=melodicSeventhDown->writtenStep==naturalSeventh->writtenStep&&melodicSeventhDown->writtenAlter==naturalSeventh->writtenAlter;
            completeRotationCorrect&=melodicSixthDown->writtenStep==naturalSixth->writtenStep&&melodicSixthDown->writtenAlter==naturalSixth->writtenAlter;
        }
    }
    ok &= check(completeRotationCorrect,"all 60 scale days preserve the correct major and three distinct minor forms in both directions");
    const auto aMinorArpeggio=MusicDocument::arpeggioRoutine(9,true);QSet<int> aMinorArpeggioClasses;for(const auto &note:aMinorArpeggio)aMinorArpeggioClasses.insert((note.note%12+12)%12);
    ok &= check(aMinorArpeggioClasses==QSet<int>({9,0,4}),"minor daily arpeggio uses the minor third instead of silently reverting to major");
    const auto aMinorInversions=MusicDocument::inversionRoutine(9,true);QSet<int> aMinorInversionClasses;for(const auto &note:aMinorInversions)aMinorInversionClasses.insert((note.note%12+12)%12);
    ok &= check(aMinorInversionClasses==QSet<int>({9,0,4}),"minor daily inversions remain minor in every position");
    const auto aMinorCadence=MusicDocument::cadenceRoutine(9,0,true);QSet<int> aMinorCadenceClasses;for(const auto &note:aMinorCadence)aMinorCadenceClasses.insert((note.note%12+12)%12);
    ok &= check(aMinorCadenceClasses.contains(0)&&aMinorCadenceClasses.contains(8)&&aMinorCadenceClasses.contains(9),"minor daily cadence uses i-iv-V-i harmony");
    const auto dMinorCadence=MusicDocument::cadenceRoutine(2,0,true);QVector<QSet<int>> dCadenceChords(4);
    for(const auto &note:dMinorCadence)if(note.hand==Hand::Right)dCadenceChords[qBound(0,int(note.start/2),3)].insert((note.note%12+12)%12);
    ok &= check(dCadenceChords[0]==QSet<int>({2,5,9})&&dCadenceChords[1]==QSet<int>({7,10,2})&&dCadenceChords[2]==QSet<int>({9,1,4})&&dCadenceChords[3]==QSet<int>({2,5,9}),"D-minor cadence is exactly i-iv-V-i with C-sharp only in the dominant");
    const auto dMinorPedal=MusicDocument::pedalChangeRoutine(2,true);QVector<QSet<int>> dPedalChords(4);
    for(const auto &note:dMinorPedal)if(note.hand==Hand::Right)dPedalChords[qBound(0,int(note.start/2),3)].insert((note.note%12+12)%12);
    ok &= check(dPedalChords==dCadenceChords,"minor pedal chapter uses the same correct i-iv-V-i harmony as the cadence chapter");
    const auto dMajorCadence=MusicDocument::cadenceRoutine(2,0,false);QVector<QSet<int>> dMajorChords(4);
    for(const auto &note:dMajorCadence)if(note.hand==Hand::Right)dMajorChords[qBound(0,int(note.start/2),3)].insert((note.note%12+12)%12);
    ok &= check(dMajorChords[0]==QSet<int>({2,6,9})&&dMajorChords[1]==QSet<int>({7,11,2})&&dMajorChords[2]==QSet<int>({9,1,4})&&dMajorChords[3]==QSet<int>({2,6,9}),"D-major cadence remains entirely major and does not borrow minor harmony");

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
    const QString theoreticalXml=temp.filePath("a-sharp-harmonic.musicxml");
    ok &= check(MusicDocument::saveMusicXml(theoreticalXml,aSharpHarmonic,72,"A-sharp harmonic minor",&error),"write theoretical MusicXML spelling");
    QFile theoreticalFile(theoreticalXml);theoreticalFile.open(QIODevice::ReadOnly);const QByteArray theoreticalBytes=theoreticalFile.readAll();
    ok &= check(theoreticalBytes.contains("<step>G</step><alter>2</alter><octave>5</octave>"),"MusicXML export preserves G-double-sharp instead of renaming it A natural");

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

    const QString annotatedProject=temp.filePath("annotated.varpeggio");QVector<NoteEvent> annotated=source;if(!annotated.isEmpty()){annotated[0].accidental=AccidentalMark::Sharp;annotated[0].finger=3;annotated[0].writtenStep=6;annotated[0].writtenAlter=1;}
    ok &= check(MusicDocument::saveJson(annotatedProject,annotated,72,"Annotated",&error,{{0,3,4},{3,4,4}},{3},{{0,-1}},{{2,1,Hand::Left}},sourcePedals),"write editable notation metadata");
    QFile annotatedFile(annotatedProject);annotatedFile.open(QIODevice::ReadOnly);const QJsonObject annotatedJson=QJsonDocument::fromJson(annotatedFile.readAll()).object();
    ok &= check(annotatedJson.value("timeSignatures").toArray().size()==2&&annotatedJson.value("keySignatures").toArray().size()==1&&annotatedJson.value("repeatEnds").toArray().size()==1&&annotatedJson.value("rests").toArray().size()==1&&annotatedJson.value("pedalEvents").toArray().size()==2,"project preserves meter, key, repeat, rest, and pedal data");
    ok &= check(annotatedJson.value("notes").toArray().first().toObject().value("accidental").toInt()==int(AccidentalMark::Sharp),"project preserves note accidental");
    ok &= check(annotatedJson.value("notes").toArray().first().toObject().value("writtenStep").toInt()==6&&annotatedJson.value("notes").toArray().first().toObject().value("writtenAlter").toInt()==1,"project preserves exact enharmonic spelling");
    ok &= check(annotatedJson.value("notes").toArray().first().toObject().value("finger").toInt()==3,"project preserves lesson fingering");
    QVector<TimeSignatureEvent> loadedMeters;QVector<double> loadedRepeats;QVector<KeySignatureEvent> loadedKeys;QVector<StaffRestEvent> loadedRests;QVector<PedalEvent> loadedPedals;loaded.clear();
    ok &= check(MusicDocument::importFile(annotatedProject,loaded,title,tempo,&error,&loadedMeters,&loadedRepeats,&loadedKeys,&loadedRests,&loadedPedals),"reopen editable notation project");
    ok &= check(loadedMeters.size()==2&&loadedKeys.size()==1&&loadedRepeats.size()==1&&loadedRests.size()==1&&loadedPedals.size()==2&&loadedPedals.first().down&&!loadedPedals.last().down&&loaded.first().accidental==AccidentalMark::Sharp&&loaded.first().finger==3&&loaded.first().writtenStep==6&&loaded.first().writtenAlter==1,"editable notation metadata round trip");

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
