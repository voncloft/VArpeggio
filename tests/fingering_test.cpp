#include "musicwidgets.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QSet>
#include <iostream>

namespace {
bool check(bool condition,const char *message){
    if(!condition)std::cerr<<"FAIL: "<<message<<'\n';
    return condition;
}

QVector<NoteEvent> phrase(const QVector<int> &pitches,Hand hand,double start=0.0){
    QVector<NoteEvent> notes;
    for(int index=0;index<pitches.size();++index)notes.append({pitches[index],start+index*.5,.42,88,hand});
    return notes;
}

QVector<int> suggested(const QVector<NoteEvent> &source){
    ScoreWidget score;score.setNotes(source);
    QVector<int> fingers;for(const auto &note:score.notes())fingers.append(note.finger);
    return fingers;
}

bool equals(const QVector<int> &actual,const QVector<int> &expected,const char *message){
    if(actual==expected)return true;
    std::cerr<<"FAIL: "<<message<<" expected";
    for(int value:expected)std::cerr<<' '<<value;
    std::cerr<<" got";for(int value:actual)std::cerr<<' '<<value;std::cerr<<'\n';
    return false;
}

bool avoidsAdjacentExtremeSwap(const QVector<NoteEvent> &source,const char *message){
    ScoreWidget score;score.setNotes(source);const auto notes=score.notes();
    for(int index=1;index<notes.size();++index){
        if(notes[index].hand!=notes[index-1].hand||qAbs(notes[index].start-notes[index-1].start)<.015)continue;
        if(qAbs(notes[index].note-notes[index-1].note)<=2&&qAbs(notes[index].finger-notes[index-1].finger)==4)
            return check(false,message);
    }
    return true;
}
}

int main(int argc,char **argv){
    QApplication app(argc,argv);bool ok=true;

    ok&=equals(suggested(phrase({60,62,64,65,67},Hand::Right)),{1,2,3,4,5},"right-hand ascending five-finger position");
    ok&=equals(suggested(phrase({67,65,64,62},Hand::Right)),{5,4,3,2},"right-hand descending short phrase");
    ok&=equals(suggested(phrase({48,50,52,53,55},Hand::Left)),{5,4,3,2,1},"left-hand ascending five-finger position");
    ok&=equals(suggested(phrase({55,53,52,50},Hand::Left)),{1,2,3,4},"left-hand descending short phrase");

    ok&=equals(suggested(phrase({60,62,64,65,67,69,71,72},Hand::Right)),{1,2,3,1,2,3,4,5},"right-hand one-octave C-major scale");
    ok&=equals(suggested(phrase({48,50,52,53,55,57,59,60},Hand::Left)),{5,4,3,2,1,3,2,1},"left-hand one-octave C-major scale");

    const auto bFlatRoutine=MusicDocument::scaleRoutine(10);
    QVector<int> bFlatRightFingers;
    QVector<int> bFlatRightPitches;
    for(const auto &note:bFlatRoutine){
        if(note.hand!=Hand::Right)continue;
        bFlatRightFingers.append(note.finger);
        bFlatRightPitches.append(note.note);
    }
    ok&=equals(bFlatRightFingers,{2,1,2,3,1,2,3,4,3,2,1,3,2,1,2},"right-hand B-flat-major scale keeps conventional fingering in both directions");
    ok&=equals(bFlatRightPitches,{70,72,74,75,77,79,81,82,81,79,77,75,74,72,70},"right-hand B-flat-major scale keeps the correct pitch order");

    QVector<NoteEvent> leap=phrase({41,40,38,36},Hand::Left);
    const auto ending=phrase({47,48,50},Hand::Left,2.5);leap+=ending;
    ok&=equals(suggested(leap),{1,2,3,4,5,4,3},"large leap resets B-C-D to a fresh left-hand position");

    ok&=equals(suggested(phrase({46,48,50,51,53},Hand::Left)),{5,4,3,2,1},"B-flat major opening uses one left-hand position");
    ok&=avoidsAdjacentExtremeSwap(phrase({41,43,45,41,43,41,43,41,43},Hand::Left),"contoured left-hand phrase avoids adjacent 1-to-5 swaps");
    ok&=avoidsAdjacentExtremeSwap(phrase({60,62,64,60,62,60,62,60,62},Hand::Right),"contoured right-hand phrase avoids adjacent 1-to-5 swaps");

    QVector<NoteEvent> repeated=phrase({60,60,60,60},Hand::Right);
    const auto repeatedFingers=suggested(repeated);
    ok&=check(QSet<int>(repeatedFingers.begin(),repeatedFingers.end()).size()==1,"repeated key keeps one stable finger");

    QVector<NoteEvent> chords{{60,0,1,88,Hand::Right},{64,0,1,88,Hand::Right},{67,0,1,88,Hand::Right},
                              {48,1,1,80,Hand::Left},{52,1,1,80,Hand::Left},{55,1,1,80,Hand::Left}};
    ok&=equals(suggested(chords),{1,3,5,5,3,1},"triads receive conventional outside-to-outside fingering");

    QVector<NoteEvent> authored=phrase({60,62,64},Hand::Right);authored[0].finger=2;authored[1].finger=4;authored[2].finger=5;
    ok&=equals(suggested(authored),{2,4,5},"authored fingering is never overwritten");

    QVector<NoteEvent> stress=phrase({36,38,40,41,43,45,47,48,47,45,43,41,40,38,36},Hand::Left);
    const auto stressFingers=suggested(stress);for(int finger:stressFingers)ok&=check(finger>=1&&finger<=5,"stress phrase assigns every note a valid finger");
    ok&=avoidsAdjacentExtremeSwap(stress,"long scale stress test avoids adjacent extreme swaps");

    // An optional visual artifact exercises the full 88-key renderer with a
    // synchronized chord. It is used by the screenshot QA pass, while ctest
    // continues to run without writing anything.
    if(argc>1){
        PianoWidget piano;piano.resize(1760,168);piano.setFingerHints({{60,1},{64,3},{67,5}});piano.setActive(60,true);
        QImage image(piano.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);piano.render(&painter);painter.end();
        ok&=check(image.save(QString::fromLocal8Bit(argv[1])),"keyboard fingering screenshot was saved");
    }

    if(!ok)return 1;
    std::cout<<"All VArpeggio fingering tests passed.\n";
    return 0;
}
