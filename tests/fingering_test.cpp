#include "musicwidgets.h"

#include <QApplication>
#include <QDirIterator>
#include <QFileInfo>
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

int auditScore(const QString &path){
    QVector<NoteEvent> source;QString title,error;int tempo=0;
    if(!MusicDocument::importFile(path,source,title,tempo,&error)){
        std::cerr<<"IMPORT\t"<<path.toStdString()<<"\t"<<error.toStdString()<<'\n';return 1;
    }
    ScoreWidget score;score.setNotes(source);auto notes=score.notes();
    int failures=0;
    for(const Hand hand:{Hand::Left,Hand::Right}){
        QVector<NoteEvent> handNotes;for(const auto &note:notes)if(note.hand==hand)handNotes.append(note);
        std::sort(handNotes.begin(),handNotes.end(),[](const auto &a,const auto &b){return a.start==b.start?a.note<b.note:a.start<b.start;});
        for(int cursor=0;cursor<handNotes.size();){
            int end=cursor+1;while(end<handNotes.size()&&qAbs(handNotes[end].start-handNotes[cursor].start)<.015)++end;
            QMap<int,int> pitchFingers;
            for(int index=cursor;index<end;++index){
                const auto &note=handNotes[index];
                if(note.finger<1||note.finger>5||(pitchFingers.contains(note.note)&&pitchFingers.value(note.note)!=note.finger)){
                    std::cerr<<"CHORD\t"<<path.toStdString()<<'\t'<<(hand==Hand::Left?'L':'R')<<'\t'<<note.start<<'\t'<<note.note<<'\t'<<note.finger<<'\n';++failures;
                }
                pitchFingers[note.note]=note.finger;
            }
            if(pitchFingers.size()<=5){
                QSet<int> used;int previousFinger=hand==Hand::Left?6:0;
                for(auto it=pitchFingers.cbegin();it!=pitchFingers.cend();++it){
                    const bool bad=used.contains(it.value())||(hand==Hand::Right?it.value()<=previousFinger:it.value()>=previousFinger);
                    if(bad){std::cerr<<"CHORD\t"<<path.toStdString()<<'\t'<<(hand==Hand::Left?'L':'R')<<'\t'<<handNotes[cursor].start<<'\t'<<it.key()<<'\t'<<it.value()<<'\n';++failures;}
                    used.insert(it.value());previousFinger=it.value();
                }
            }
            if(end-cursor==1){
                int next=end,nextEnd=next+1;while(nextEnd<handNotes.size()&&qAbs(handNotes[nextEnd].start-handNotes[next].start)<.015)++nextEnd;
                if(next<handNotes.size()&&nextEnd-next==1){
                    const auto &a=handNotes[cursor],&b=handNotes[next];const int interval=b.note-a.note,span=qAbs(interval),fingerSpan=qAbs(b.finger-a.finger);
                    const double delta=b.start-a.start;const int fingerDirection=b.finger-a.finger;
                    const int expectedDirection=hand==Hand::Right?interval:-interval;
                    const bool adjacentExtreme=delta<=1.25&&span<=4&&fingerSpan==4;
                    const bool badDirection=delta<=1.25&&span>=3&&span<=7&&(fingerDirection==0||(expectedDirection>0?fingerDirection<0:fingerDirection>0));
                    const bool badRepeat=delta<=1.25&&span==0&&a.finger!=b.finger;
                    if(adjacentExtreme||badDirection||badRepeat){
                        std::cerr<<"MOVE\t"<<path.toStdString()<<'\t'<<(hand==Hand::Left?'L':'R')<<'\t'<<a.start<<'\t'<<a.note<<':'<<a.finger<<"->"<<b.note<<':'<<b.finger<<"\tdt="<<delta<<"\t"<<(badDirection?"direction":badRepeat?"repeat":"adjacent")<<'\n';++failures;
                    }
                }
            }
            cursor=end;
        }
    }
    std::cout<<"AUDIT\t"<<path.toStdString()<<"\tnotes="<<notes.size()<<"\tfailures="<<failures<<'\n';
    return failures;
}

int dumpScore(const QString &path){
    QVector<NoteEvent> source;QString title,error;int tempo=0;
    if(!MusicDocument::importFile(path,source,title,tempo,&error)){std::cerr<<error.toStdString()<<'\n';return 1;}
    ScoreWidget score;score.setNotes(source);auto notes=score.notes();
    std::sort(notes.begin(),notes.end(),[](const auto &a,const auto &b){return a.start==b.start?(a.hand==b.hand?a.note<b.note:int(a.hand)<int(b.hand)):a.start<b.start;});
    for(const auto &note:notes)std::cout<<note.start<<'\t'<<(note.hand==Hand::Left?'L':'R')<<'\t'<<note.note<<'\t'<<note.finger<<'\t'<<note.duration<<'\n';
    return 0;
}
}

int main(int argc,char **argv){
    QApplication app(argc,argv);bool ok=true;

    if(argc==3&&QString::fromLocal8Bit(argv[1])=="--audit-tree"){
        int files=0,failures=0;QDirIterator iterator(QString::fromLocal8Bit(argv[2]),QDir::Files,QDirIterator::Subdirectories);
        while(iterator.hasNext()){
            const QString path=iterator.next();const QString suffix=QFileInfo(path).suffix().toLower();
            if(suffix!="mid"&&suffix!="midi"&&suffix!="musicxml"&&suffix!="mxl"&&suffix!="varpeggio")continue;
            ++files;failures+=auditScore(path);
        }
        std::cout<<"TOTAL\tfiles="<<files<<"\tfailures="<<failures<<'\n';return failures?2:0;
    }
    if(argc==3&&QString::fromLocal8Bit(argv[1])=="--dump")return dumpScore(QString::fromLocal8Bit(argv[2]));

    ok&=equals(suggested(phrase({60,62,64,65,67},Hand::Right)),{1,2,3,4,5},"right-hand ascending five-finger position");
    ok&=equals(suggested(phrase({67,65,64,62},Hand::Right)),{5,4,3,2},"right-hand descending short phrase");
    ok&=equals(suggested(phrase({48,50,52,53,55},Hand::Left)),{5,4,3,2,1},"left-hand ascending five-finger position");
    ok&=equals(suggested(phrase({55,53,52,50},Hand::Left)),{1,2,3,4},"left-hand descending short phrase");
    ok&=equals(suggested(phrase({48,55},Hand::Left)),{5,1},"left-hand C-to-G leap uses little finger to thumb");
    ok&=equals(suggested(phrase({48,52,55},Hand::Left)),{5,3,1},"left-hand C-E-G outline uses its actual interval spacing");
    ok&=equals(suggested(phrase({45,48,55},Hand::Left)),{5,3,1},"left-hand A-C-G outline anchors pinky low and thumb high");
    ok&=equals(suggested(phrase({60,67},Hand::Right)),{1,5},"right-hand C-to-G leap uses thumb to little finger");
    const auto hallelujahContour=suggested(phrase({48,55,47,45,52,57,45,48,55},Hand::Left));
    ok&=check(hallelujahContour.size()==9&&hallelujahContour[0]==5&&hallelujahContour[1]==1&&
                  hallelujahContour[6]==5&&hallelujahContour[7]==3&&hallelujahContour[8]==1,
              "left hand uses 5-1 for a fifth and 5-3-1 when that fifth belongs to A-C-G");

    QVector<NoteEvent> octaveReset{{57,0,.8,80,Hand::Left},{45,2,.4,80,Hand::Left},{48,2.5,.4,80,Hand::Left},{55,3,.4,80,Hand::Left}};
    ok&=equals(suggested(octaveReset),{3,5,3,1},"left hand resets an octave-down A-C-G phrase onto pinky-middle-thumb");
    ok&=equals(suggested(phrase({47,45,52,57},Hand::Left)),{4,5,2,1},"left-hand B-A-E-A accompaniment spans the hand instead of moving one thumb");

    ok&=equals(suggested(phrase({60,62,64,65,67,69,71,72},Hand::Right)),{1,2,3,1,2,3,4,5},"right-hand one-octave C-major scale");
    ok&=equals(suggested(phrase({48,50,52,53,55,57,59,60},Hand::Left)),{5,4,3,2,1,3,2,1},"left-hand one-octave C-major scale");
    ok&=equals(suggested(phrase({21,24,28},Hand::Left)),{5,3,1},"lowest-register A0-C1-E1 outline uses pinky-middle-thumb");
    ok&=equals(suggested(phrase({28,24,21},Hand::Left)),{1,3,5},"lowest-register E1-C1-A0 descent mirrors the physical hand");
    ok&=equals(suggested(phrase({21,33},Hand::Left)),{5,1},"lowest-register octave rises from pinky to thumb");
    ok&=equals(suggested(phrase({33,21},Hand::Left)),{1,5},"lowest-register octave falls from thumb to pinky");

    ScoreWidget bFlatScore;bFlatScore.setNotes(MusicDocument::scaleRoutine(10));
    const auto bFlatRoutine=bFlatScore.notes();
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
    QVector<NoteEvent> lowChord{{21,0,1,80,Hand::Left},{24,0,1,80,Hand::Left},{28,0,1,80,Hand::Left}};
    ok&=equals(suggested(lowChord),{5,3,1},"A0-C1-E1 chord remains physically ordered at the bottom of the piano");
    ScoreWidget compactFrame;compactFrame.setNotes(phrase({60,62,64},Hand::Right));compactFrame.setShowFingering(true);
    ok&=check(compactFrame.recommendedFrameHeight()==212,"single-note fingering keeps the compact score frame");
    ScoreWidget dyadFrame;dyadFrame.setNotes({{48,0,2,80,Hand::Left},{55,0,2,80,Hand::Left}});dyadFrame.setShowFingering(true);
    ScoreWidget largeChordFrame;largeChordFrame.setNotes({{48,0,2,80,Hand::Left},{52,0,2,80,Hand::Left},{55,0,2,80,Hand::Left},{60,0,2,80,Hand::Left}});largeChordFrame.setShowFingering(true);
    ok&=check(dyadFrame.recommendedFrameHeight()>212&&largeChordFrame.recommendedFrameHeight()>dyadFrame.recommendedFrameHeight(),"vertical chord fingering grows with its actual stack instead of clipping either edge");
    largeChordFrame.setShowFingering(false);ok&=check(largeChordFrame.recommendedFrameHeight()==212,"hiding fingering restores the compact score frame");

    QVector<NoteEvent> rightDyadContext{{60,0,.45,88,Hand::Right},{67,0,.45,88,Hand::Right},
                                        {64,.5,.45,88,Hand::Right},{67,.5,.45,88,Hand::Right}};
    ok&=equals(suggested(rightDyadContext),{1,5,3,5},"right-hand C-G to E-G retains G on 5 and moves E to 3");
    QVector<NoteEvent> leftDyadContext{{48,0,.45,80,Hand::Left},{55,0,.45,80,Hand::Left},
                                       {48,.5,.45,80,Hand::Left},{52,.5,.45,80,Hand::Left}};
    ok&=equals(suggested(leftDyadContext),{5,1,5,3},"left-hand C-G to C-E retains C on 5 and moves E to 3");
    QVector<NoteEvent> firstInversion{{64,0,1,88,Hand::Right},{67,0,1,88,Hand::Right},{72,0,1,88,Hand::Right}};
    ok&=equals(suggested(firstInversion),{1,2,5},"recognized right-hand first-inversion C major uses 1-2-5");
    QVector<NoteEvent> secondInversion{{43,0,1,80,Hand::Left},{48,0,1,80,Hand::Left},{52,0,1,80,Hand::Left}};
    ok&=equals(suggested(secondInversion),{5,2,1},"recognized left-hand second-inversion C major uses 5-2-1");
    QVector<NoteEvent> chordToMelody{{60,0,.45,88,Hand::Right},{67,0,.45,88,Hand::Right},
                                     {64,.5,.45,88,Hand::Right},{67,.5,.45,88,Hand::Right},
                                     {60,1,.45,88,Hand::Right},{67,1.5,.45,88,Hand::Right},
                                     {69,2,.45,88,Hand::Right},{69,2.5,.45,88,Hand::Right}};
    ok&=equals(suggested(chordToMelody),{1,5,3,5,1,4,5,5},"right hand follows the physical search from C through G into repeated A notes");
    QVector<NoteEvent> outerBandMelody{{64,0,.45,88,Hand::Right},{71,0,.45,88,Hand::Right},
                                       {71,.5,.45,88,Hand::Right},{69,1,.45,88,Hand::Right}};
    ok&=equals(suggested(outerBandMelody),{1,5,5,4},"short B-to-A phrase keeps the conventional neighboring-finger position");
    QVector<NoteEvent> parallelThirds{{60,0,.45,88,Hand::Right},{64,0,.45,88,Hand::Right},
                                      {62,.5,.45,88,Hand::Right},{65,.5,.45,88,Hand::Right},
                                      {64,1,.45,88,Hand::Right},{67,1,.45,88,Hand::Right}};
    ok&=equals(suggested(parallelThirds),{1,3,2,4,3,5},"successive right-hand thirds move through 1-3, 2-4, and 3-5 as one hand position");
    QVector<NoteEvent> leftParallelThirds{{48,0,.45,80,Hand::Left},{52,0,.45,80,Hand::Left},
                                          {50,.5,.45,80,Hand::Left},{53,.5,.45,80,Hand::Left},
                                          {52,1,.45,80,Hand::Left},{55,1,.45,80,Hand::Left}};
    ok&=equals(suggested(leftParallelThirds),{5,3,4,2,3,1},"successive left-hand thirds mirror one hand position");

    for(int variation=0;variation<3;++variation){
        ScoreWidget drill;drill.setNotes(MusicDocument::chordRoutine(variation));
        int bassCount=0,pinkyCount=0;
        for(const auto &note:drill.notes()){
            ok&=check(note.finger>=1&&note.finger<=5,"every chord-drill note passes through the shared fingering optimizer");
            if(note.hand==Hand::Left){++bassCount;if(note.finger==5)++pinkyCount;}
        }
        ok&=check(bassCount==4&&pinkyCount<bassCount,"daily chord drill does not hard-code every bass note to the pinky");
    }
    ok&=equals(suggested(MusicDocument::chordRoutine(1)),
               {5,1,3,5,1,1,3,5,3,1,3,5,2,1,3,5},
               "C-A-F-G chord drill uses a connected 5-1-3-2 bass phrase and conventional right-hand triads");

    QVector<NoteEvent> authored=phrase({60,62,64},Hand::Right);authored[0].finger=2;authored[1].finger=4;authored[2].finger=5;
    ok&=equals(suggested(authored),{2,4,5},"authored fingering is never overwritten");

    QVector<NoteEvent> stress=phrase({36,38,40,41,43,45,47,48,47,45,43,41,40,38,36},Hand::Left);
    const auto stressFingers=suggested(stress);for(int finger:stressFingers)ok&=check(finger>=1&&finger<=5,"stress phrase assigns every note a valid finger");
    ok&=avoidsAdjacentExtremeSwap(stress,"long scale stress test avoids adjacent extreme swaps");

    QVector<NoteEvent> boundaryStress{{52,0,.2,80,Hand::Left},{56,.25,.2,80,Hand::Left},{55,.5,.2,80,Hand::Left},{59,.75,.2,80,Hand::Left},
                                      {41,2,.2,80,Hand::Left},{40,2.5,.2,80,Hand::Left},{48,4,.2,80,Hand::Left},{48,4.5,.2,80,Hand::Left}};
    const auto repaired=suggested(boundaryStress);
    ok&=check(qAbs(repaired[1]-repaired[0])<4&&qAbs(repaired[3]-repaired[2])<4&&qAbs(repaired[5]-repaired[4])<4,
              "left-hand boundary repair removes nearby thumb-pinky teleports");
    ok&=check(repaired[6]==repaired[7],"left-hand repeated key keeps one stable finger across a phrase boundary");

    QVector<NoteEvent> rightBoundaryStress{{60,0,.2,80,Hand::Right},{64,.25,.2,80,Hand::Right},{63,.5,.2,80,Hand::Right},{67,.75,.2,80,Hand::Right},
                                           {79,2,.2,80,Hand::Right},{78,2.5,.2,80,Hand::Right},{72,4,.2,80,Hand::Right},{72,4.5,.2,80,Hand::Right}};
    const auto rightRepaired=suggested(rightBoundaryStress);
    ok&=check(qAbs(rightRepaired[1]-rightRepaired[0])<4&&qAbs(rightRepaired[3]-rightRepaired[2])<4&&qAbs(rightRepaired[5]-rightRepaired[4])<4,
              "right-hand boundary repair removes nearby thumb-pinky teleports");
    ok&=check(rightRepaired[6]==rightRepaired[7],"right-hand repeated key keeps one stable finger across a phrase boundary");
    ok&=equals(suggested(phrase({96,100,103},Hand::Right)),{1,3,5},"highest-register C7-E7-G7 outline uses thumb-middle-pinky");
    ok&=equals(suggested(phrase({103,100,96},Hand::Right)),{5,3,1},"highest-register G7-E7-C7 descent mirrors the physical hand");

    ScoreWidget meterDrill;
    meterDrill.setTimeSignatures({{0.0,3,4},{6.0,4,4}});
    const auto secondThreeFour=meterDrill.measureRangeAt(4.25);
    ok&=check(qAbs(secondThreeFour.first-3.0)<.001&&qAbs(secondThreeFour.second-3.0)<.001,
              "measure drill resolves a complete 3/4 measure");
    ok&=check(qAbs(meterDrill.measureEndAt(4.25)-6.0)<.001,
              "later-measure rewind converts start plus length into the true measure end");
    ok&=check(meterDrill.measureNumberAt(4.25)==2&&meterDrill.measureNumberAt(6.0)==3,
              "measure drill numbers bars correctly across a meter change");

    ScoreWidget anchoredDrill;
    anchoredDrill.resize(1100,440);
    anchoredDrill.setNotes(MusicDocument::morningLight());
    anchoredDrill.setTimeSignatures({{0.0,4,4}});
    anchoredDrill.setFollowBeat(4.0);
    ok&=check(qAbs(anchoredDrill.viewStartBeat()-4.0)<.001,
              "score normally pages to the followed measure");
    anchoredDrill.holdViewStart(0.0);
    anchoredDrill.setDrillRanges({{4.0,8.0}});
    ok&=check(qAbs(anchoredDrill.viewStartBeat())<.001,
              "selecting a drill measure keeps the visible grand staff anchored");
    anchoredDrill.setDrillRanges({});
    ok&=check(qAbs(anchoredDrill.viewStartBeat())<.001,
              "deselecting a drill measure keeps the visible grand staff anchored");
    anchoredDrill.setFollowBeat(4.0);
    ok&=check(qAbs(anchoredDrill.viewStartBeat())<.001,
              "changing the expected note within the visible page does not repage the staff");
    anchoredDrill.setDrillRanges({{0.0,4.0},{4.0,8.0}});
    anchoredDrill.setDrillRanges({{4.0,8.0}});
    anchoredDrill.setFollowBeat(4.0);
    ok&=check(qAbs(anchoredDrill.viewStartBeat())<.001,
              "deselecting measure 1 while measure 2 remains selected still displays measures 1 onward");
    anchoredDrill.setFollowBeat(8.0);
    ok&=check(qAbs(anchoredDrill.viewStartBeat()-8.0)<.001,
              "normal score following resumes after practice leaves the anchored page");

    ScoreWidget octaveFrame;
    octaveFrame.setNotes({{22,0.0,.25,90,Hand::Left},{106,.5,.25,90,Hand::Right}});
    ok&=check(octaveFrame.recommendedFrameHeight()>212,
              "extreme Free Play notes expand the compact frame enough to retain octave notation");

    QVector<NoteEvent> denseMeasure;
    for(int index=0;index<16;++index)denseMeasure.append({60+(index%5),index*.25,.20,90,Hand::Right});
    ScoreWidget densityPage;densityPage.resize(1100,440);densityPage.setTimeSignatures({{0.0,4,4}});densityPage.setNotes(denseMeasure);densityPage.setFollowBeat(0);
    ok&=check(qAbs(densityPage.viewWindowBeats()-4.0)<.001,
              "a dense measure receives the full score width instead of compressing two measures into illegibility");
    ScoreWidget sparsePage;sparsePage.resize(1100,440);sparsePage.setTimeSignatures({{0.0,4,4}});sparsePage.setNotes({{60,0,1,90,Hand::Right},{64,4,1,90,Hand::Right}});sparsePage.setFollowBeat(0);
    ok&=check(qAbs(sparsePage.viewWindowBeats()-8.0)<.001,
              "sparse music continues to fill the panel with multiple measures");

    auto renderScore=[](ScoreWidget &score){
        QImage image(1100,440,QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);score.paintScore(painter,QRectF(0,0,1100,440));painter.end();
        return image;
    };
    auto checkOpaquePainting=[&](QWidget &widget,const char *name){
        const bool opaqueByDefault=widget.testAttribute(Qt::WA_OpaquePaintEvent);
        auto renderWidget=[&]{
            QImage image(widget.size(),QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);widget.render(&image);return image;
        };
        const QImage opaqueImage=renderWidget();
        widget.setAttribute(Qt::WA_OpaquePaintEvent,false);
        const QImage ordinaryImage=renderWidget();
        widget.setAttribute(Qt::WA_OpaquePaintEvent,opaqueByDefault);
        bool allPixelsOpaque=true;
        for(int y=0;y<opaqueImage.height()&&allPixelsOpaque;++y)
            for(int x=0;x<opaqueImage.width();++x)
                if(qAlpha(opaqueImage.pixel(x,y))!=255
                        ||qAlpha(ordinaryImage.pixel(x,y))!=255){allPixelsOpaque=false;break;}
        if(!opaqueByDefault||opaqueImage!=ordinaryImage||!allPixelsOpaque)
            std::cerr<<"OPAQUE WIDGET: "<<name<<'\n';
        ok&=check(opaqueByDefault,"each fully painted music widget enables opaque repaint optimization by default");
        ok&=check(opaqueImage==ordinaryImage,"opaque repaint optimization does not change any rendered music-widget pixels");
        ok&=check(allPixelsOpaque,"each music widget completely fills its rectangle with opaque pixels");
    };
    ScoreWidget opaqueScore;opaqueScore.resize(1100,440);
    const QVector<NoteEvent> opaqueNotes{{48,0,1,90,Hand::Left},
                                         {60,0,1,90,Hand::Right},
                                         {61,1,.5,90,Hand::Right}};
    opaqueScore.setNotes(opaqueNotes);opaqueScore.setShowNames(true);
    opaqueScore.setShowFingering(true);opaqueScore.setExpected({48,60},0.0);
    checkOpaquePainting(opaqueScore,"score");
    PianoWidget opaquePiano;opaquePiano.resize(1100,168);
    opaquePiano.setKeyRange(36,96);opaquePiano.setActive(60,true);
    opaquePiano.setActive(61,true);opaquePiano.setFingerHints({{60,1},{61,2},{67,5}});
    checkOpaquePainting(opaquePiano,"keyboard");
    BubbleWidget opaqueBubbles;opaqueBubbles.resize(1100,185);
    opaqueBubbles.setNotes(opaqueNotes);opaqueBubbles.setPlayhead(.5);
    opaqueBubbles.setExpected({48,60},0.0);
    // No pulses are introduced, so both real widget paints have identical
    // static contents even though the bubble widget owns an animation timer.
    checkOpaquePainting(opaqueBubbles,"bubbles");

    ScoreWidget cursorVisibility;cursorVisibility.resize(1100,440);
    cursorVisibility.setNotes({{60,0,1,90,Hand::Right},{64,4,1,90,Hand::Right}});
    cursorVisibility.setFollowBeat(1.0);
    const QImage cursorShown=renderScore(cursorVisibility);
    cursorVisibility.setPlaybackCursorVisible(false);
    const QImage cursorHidden=renderScore(cursorVisibility);
    cursorVisibility.setPlaybackCursorVisible(true);
    const QImage cursorRestored=renderScore(cursorVisibility);
    int cursorChangedPixels=0;
    for(int y=0;y<cursorShown.height();++y)for(int x=0;x<cursorShown.width();++x)
        if(cursorShown.pixel(x,y)!=cursorHidden.pixel(x,y))++cursorChangedPixels;
    ok&=check(cursorChangedPixels>100,
              "the explicit cursor visibility control can intentionally hide the playback line");
    ok&=check(cursorShown==cursorRestored,
              "the orange playback cursor returns at the identical score position once output is ready");

    auto cursorDifference=[](const QImage &shown,const QImage &hidden){
        qint64 pixels=0,sumX=0;
        for(int y=0;y<shown.height();++y)for(int x=0;x<shown.width();++x)
            if(shown.pixel(x,y)!=hidden.pixel(x,y)){++pixels;sumX+=x;}
        return qMakePair(pixels,pixels?double(sumX)/pixels:-1.0);
    };
    ScoreWidget preparingCursor;preparingCursor.resize(1100,440);
    preparingCursor.setNotes({{60,0,2,90,Hand::Right},{48,0,2,90,Hand::Left},
                              {64,4,2,90,Hand::Right}});
    preparingCursor.setFollowBeat(0.0);preparingCursor.setPlayedBeat(-1);
    preparingCursor.setExpected({60,48},0.0);
    preparingCursor.setPlaybackWaiting(0.0);
    const QImage preparingAtStart=renderScore(preparingCursor);
    preparingCursor.setExpected({},0.0);
    ok&=check(preparingAtStart==renderScore(preparingCursor),
              "a parked startup cursor does not mark the first note or chord expected before audio begins");
    preparingCursor.setPlayedBeat(5.0);
    ok&=check(preparingAtStart==renderScore(preparingCursor),
              "the preparing cursor neither follows a stale played beat nor prematurely marks notes played");
    preparingCursor.setPlayedBeat(-1);
    preparingCursor.setPlaybackCursorVisible(false);
    const auto preparingStartLine=cursorDifference(preparingAtStart,renderScore(preparingCursor));
    ok&=check(preparingStartLine.first>50,
              "the orange cursor remains visibly parked at the beginning throughout audio priming");
    preparingCursor.setPlaybackCursorVisible(true);
    preparingCursor.setFollowBeat(1.0);
    ok&=check(preparingAtStart==renderScore(preparingCursor),
              "history anchoring cannot move the parked startup cursor away from its requested start");

    preparingCursor.setFollowBeat(4.0);
    preparingCursor.setPlaybackWaiting(4.25);
    const QImage preparingAtResume=renderScore(preparingCursor);
    preparingCursor.setFollowBeat(5.0);
    ok&=check(preparingAtResume==renderScore(preparingCursor),
              "a nonzero resume or tempo-change target stays parked while the audio output is primed");
    preparingCursor.setPlaybackCursorVisible(false);
    const auto preparingResumeLine=cursorDifference(preparingAtResume,renderScore(preparingCursor));
    ok&=check(preparingResumeLine.first>50,
              "the preparing cursor remains visible at a nonzero requested playback position");
    preparingCursor.setPlaybackCursorVisible(true);
    preparingCursor.setPlayedBeat(4.25);
    preparingCursor.setPlaybackWaiting(-1);
    const QImage resumedAtOutputBeat=renderScore(preparingCursor);
    preparingCursor.setPlaybackCursorVisible(false);
    const auto resumedLine=cursorDifference(resumedAtOutputBeat,renderScore(preparingCursor));
    ok&=check(resumedLine.first>100&&qAbs(preparingResumeLine.second-resumedLine.second)<1.0,
              "the first ready output frame changes the parked line to active without shifting its beat");
    preparingCursor.setPlaybackCursorVisible(true);
    preparingCursor.setPlayedBeat(-1);preparingCursor.setFollowBeat(0.0);
    preparingCursor.setPlaybackWaiting(0.0);
    preparingCursor.setPedalEvents({{0.0,true},{2.0,false}});
    preparingCursor.setExpectedPedal(1,0.0);
    const QImage preparingPedal=renderScore(preparingCursor);
    preparingCursor.setExpectedPedal(-1,-1);
    ok&=check(preparingPedal==renderScore(preparingCursor),
              "preparing playback does not show a pedal press as currently due before output begins");
    preparingCursor.setPedalEvents({});
    preparingCursor.setExpected({60,48},0.0);
    const QImage waitingFirstChord=renderScore(preparingCursor);
    preparingCursor.setPlaybackWaiting(-1);
    ok&=check(waitingFirstChord!=renderScore(preparingCursor),
              "note highlighting is restored when the shared output clock actually starts");

    ScoreWidget audioClockCursor;audioClockCursor.resize(1100,440);
    audioClockCursor.setTimeSignatures({{0.0,4,4}});
    audioClockCursor.setNotes({{60,0,1,90,Hand::Right},{64,4,1,90,Hand::Right}});
    audioClockCursor.setPlayedBeat(2.0);audioClockCursor.setFollowBeat(0.0);
    const QImage cursorAtAudioBeat=renderScore(audioClockCursor);
    audioClockCursor.setFollowBeat(1.0);
    ok&=check(cursorAtAudioBeat==renderScore(audioClockCursor),
              "scroll-history anchoring cannot move the orange cursor away from the audio-clock beat");

    ScoreWidget stackedFrame;stackedFrame.setShowNames(true);stackedFrame.setShowFingering(true);
    stackedFrame.setNotes({{96,0,.5,90,Hand::Right,AccidentalMark::Automatic,1},{100,0,.5,90,Hand::Right,AccidentalMark::Automatic,3},{103,0,.5,90,Hand::Right,AccidentalMark::Automatic,5}});
    ok&=check(stackedFrame.recommendedFrameHeight()>313,
              "stacked treble fingering reserves canvas above zero instead of clipping its top digits");

    // An optional visual artifact exercises the default 61-key physical range
    // within the full 88-key renderer. Outer keys remain visible but inactive;
    // the centre chord verifies synchronized fingering badges.
    if(argc>1){
        PianoWidget piano;piano.resize(1760,168);piano.setFingerHints({{60,1},{64,3},{67,5}});piano.setActive(60,true);
        QImage image(piano.size(),QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);piano.render(&painter);painter.end();
        ok&=check(image.save(QString::fromLocal8Bit(argv[1])),"keyboard fingering screenshot was saved");
    }
    if(argc>2){
        auto notes=MusicDocument::odeToJoy();for(auto &note:notes)note.note+=9;
        ScoreWidget score;score.setNotes(notes);score.setKeySignatures({{0.0,3}});score.setShowNames(true);score.setShowFingering(true);score.setFollowBeat(0);score.resize(1100,440);
        QImage image(1100,440,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);score.paintScore(painter,QRectF(0,0,1100,440));painter.end();
        ok&=check(image.save(QString::fromLocal8Bit(argv[2])),"dense left-hand fingering screenshot was saved");
    }
    if(argc>3){
        auto notes=MusicDocument::odeToJoy();
        ScoreWidget score;score.setNotes(notes);score.setShowNames(true);score.setShowFingering(true);score.setDrillRanges({{4.0,8.0},{12.0,16.0}});score.setFollowBeat(4.0);score.resize(2200,440);
        QImage image(2200,440,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);score.paintScore(painter,QRectF(0,0,2200,440));painter.end();
        ok&=check(image.save(QString::fromLocal8Bit(argv[3])),"measure drill screenshot was saved");
    }

    if(!ok)return 1;
    std::cout<<"All VArpeggio fingering tests passed.\n";
    return 0;
}
