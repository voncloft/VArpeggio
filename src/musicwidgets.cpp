#include "musicwidgets.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>
#include <limits>

namespace {
bool blackKey(int midi){int p=((midi%12)+12)%12;return p==1||p==3||p==6||p==8||p==10;}
int diatonicIndex(int midi){static const int step[]={0,0,1,1,2,3,3,4,4,5,5,6};return (midi/12-1)*7+step[((midi%12)+12)%12];}
int fifthsAt(const QVector<KeySignatureEvent>&signatures,double beat){int fifths=0;for(const auto &signature:signatures){if(signature.beat>beat+.001)break;fifths=signature.fifths;}return fifths;}
double nextMeasureBoundary(QVector<TimeSignatureEvent> signatures,double beat){
    if(signatures.isEmpty())signatures.append({0.0,4,4});
    std::sort(signatures.begin(),signatures.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});
    if(signatures.first().beat>.001)signatures.prepend({0.0,4,4});
    TimeSignatureEvent active=signatures.first();double nextChange=std::numeric_limits<double>::max();
    for(int index=0;index<signatures.size();++index){if(signatures[index].beat<=beat+.001)active=signatures[index];else{nextChange=signatures[index].beat;break;}}
    const double length=qMax(.125,active.beats*4.0/qMax(1,active.beatType));
    const double local=qMax(0.0,beat-active.beat);double boundary=active.beat+(std::floor(local/length)+1.0)*length;
    if(nextChange<boundary)boundary=nextChange;return boundary;
}
int staffDiatonicIndex(int midi,int fifths,AccidentalMark accidental){static const int sharpStep[]={0,0,1,1,2,3,3,4,4,5,5,6};static const int flatStep[]={0,1,1,2,2,3,4,4,5,5,6,6};const int pitch=((midi%12)+12)%12;const bool useFlats=accidental==AccidentalMark::Flat||(accidental!=AccidentalMark::Sharp&&fifths<0);return (midi/12-1)*7+(useFlats?flatStep[pitch]:sharpStep[pitch]);}
QString staffPitchName(int midi,int fifths,AccidentalMark accidental){static const char*sharpNames[]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};static const char*flatNames[]={"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};const bool useFlats=accidental==AccidentalMark::Flat||(accidental!=AccidentalMark::Sharp&&fifths<0);return QString::fromLatin1((useFlats?flatNames:sharpNames)[((midi%12)+12)%12]);}
bool included(const NoteEvent &n,int filter){return filter<0||(filter==0&&n.hand==Hand::Left)||(filter==1&&n.hand==Hand::Right);}
}

ScoreWidget::ScoreWidget(QWidget *parent):QWidget(parent){setMinimumHeight(300);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
void ScoreWidget::setNotes(const QVector<NoteEvent>&n){m_notes=n;update();}
void ScoreWidget::setExpected(const QSet<int>&n,double start){m_expected=n;m_groupStart=start;update();}
void ScoreWidget::setPlayedBeat(double b){m_playedBeat=b;update();}
void ScoreWidget::setFollowBeat(double b){m_followBeat=qMax(0.0,b);update();}
void ScoreWidget::setLiveWritingMode(bool live){m_liveWriting=live;update();}
void ScoreWidget::setEditEnabled(bool enabled){m_editEnabled=enabled;if(!enabled)m_editCursorBeat=-1;setCursor(enabled?Qt::CrossCursor:Qt::ArrowCursor);update();}
void ScoreWidget::setEditCursorBeat(double beat){m_editCursorBeat=beat;update();}
void ScoreWidget::setTimeSignatures(const QVector<TimeSignatureEvent>&signatures){m_timeSignatures=signatures;std::sort(m_timeSignatures.begin(),m_timeSignatures.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});update();}
void ScoreWidget::setKeySignatures(const QVector<KeySignatureEvent>&signatures){m_keySignatures=signatures;std::sort(m_keySignatures.begin(),m_keySignatures.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});update();}
void ScoreWidget::setRests(const QVector<StaffRestEvent>&rests){m_rests=rests;std::sort(m_rests.begin(),m_rests.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});update();}
void ScoreWidget::setRepeatEnds(const QVector<double>&beats){m_repeatEnds=beats;std::sort(m_repeatEnds.begin(),m_repeatEnds.end());update();}
void ScoreWidget::setShowNames(bool s){m_showNames=s;update();}
void ScoreWidget::setHandFilter(int f){m_handFilter=f;update();}
void ScoreWidget::paintEvent(QPaintEvent*){QPainter p(this);paintScore(p,rect());}

QPair<double,double> ScoreWidget::viewRange() const {
    double maxBeat=0;for(const auto &note:m_notes)if(included(note,m_handFilter))maxBeat=qMax(maxBeat,note.start+note.duration);for(const auto &rest:m_rests)if(m_handFilter<0||(m_handFilter==0&&rest.hand==Hand::Left)||(m_handFilter==1&&rest.hand==Hand::Right))maxBeat=qMax(maxBeat,rest.beat+rest.duration);for(const auto &signature:m_timeSignatures)maxBeat=qMax(maxBeat,signature.beat);for(const auto &signature:m_keySignatures)maxBeat=qMax(maxBeat,signature.beat);for(double beat:m_repeatEnds)maxBeat=qMax(maxBeat,beat);
    const double window=m_liveWriting?12.0:qMax(8.0,qMin(16.0,std::ceil((maxBeat+1)/4.0)*4.0));
    const double followed=m_liveWriting?qMax(0.0,m_followBeat):qBound(0.0,m_followBeat,maxBeat);
    double startBeat=0;
    if(m_liveWriting){startBeat=qMax(0.0,followed-window*.68);startBeat=std::floor(startBeat*8.0)/8.0;}
    else{startBeat=qMax(0.0,std::floor(followed/4.0-1.0)*4.0);if(startBeat+window>maxBeat+1)startBeat=qMax(0.0,maxBeat+1-window);startBeat=std::floor(startBeat/4.0)*4.0;}
    return {startBeat,window};
}

void ScoreWidget::paintScore(QPainter &p,const QRectF &target) const {
    p.save();p.setRenderHint(QPainter::Antialiasing);p.fillRect(target,QColor("#fffefa"));
    constexpr double verticalOffset=30.0,canvasHeight=310.0;
    const double sx=target.width()/1100.0,sy=target.height()/canvasHeight;p.translate(target.left(),target.top());p.scale(sx,sy);
    p.setPen(QPen(QColor("#a7b0ad"),1));for(int s:{70,80,90,100,110,160,170,180,190,200})p.drawLine(QPointF(54,s+verticalOffset),QPointF(1064,s+verticalOffset));
    p.setPen(QPen(QColor("#445a55"),2));p.drawLine(QPointF(54,70+verticalOffset),QPointF(54,200+verticalOffset));QPainterPath brace;brace.moveTo(46,70+verticalOffset);brace.cubicTo(30,85+verticalOffset,47,115+verticalOffset,31,134+verticalOffset);brace.cubicTo(47,151+verticalOffset,31,184+verticalOffset,46,200+verticalOffset);p.drawPath(brace);
    QFont clef("Noto Music",42);if(!QFontInfo(clef).exactMatch())clef=QFont("serif",42);p.setFont(clef);p.setPen(QColor("#29433d"));p.drawText(QPointF(65,108+verticalOffset),QString::fromUtf8("𝄞"));p.setFont(QFont("Noto Music",34));p.drawText(QPointF(68,194+verticalOffset),QString::fromUtf8("𝄢"));
    p.setFont(QFont("Manrope",7,QFont::DemiBold));p.setPen(QColor("#99a5a1"));p.drawText(QPointF(23,92+verticalOffset),"RIGHT");p.drawText(QPointF(27,182+verticalOffset),"LEFT");
    QVector<NoteEvent> visible;for(const auto&n:m_notes)if(included(n,m_handFilter))visible<<n;
    double maxBeat=0;for(const auto&n:visible)maxBeat=qMax(maxBeat,n.start+n.duration);
    const auto range=viewRange();const double startBeat=range.first,window=range.second;
    const double followed=m_liveWriting?qMax(0.0,m_followBeat):qBound(0.0,m_followBeat,maxBeat);
    // Beat zero must begin after the clef, key signature, and time signature.
    // Keeping those symbols in a dedicated header also prevents the first
    // recorded note from looking as though it belongs before the meter.
    const double musicLeft=170.0+qAbs(fifthsAt(m_keySignatures,startBeat))*8.0;
    const double musicRight=1020.0;
    const double scale=(musicRight-musicLeft)/window;
    QVector<TimeSignatureEvent> signatures=m_timeSignatures;
    if(signatures.isEmpty()||signatures.first().beat>.001)signatures.prepend({0.0,4,4});
    int barNumber=1;
    for(int signatureIndex=0;signatureIndex<signatures.size();++signatureIndex){
        const auto &signature=signatures[signatureIndex];
        const double segmentEnd=signatureIndex+1<signatures.size()?signatures[signatureIndex+1].beat:startBeat+window+16.0;
        const double barLength=qMax(.125,signature.beats*4.0/qMax(1,signature.beatType));
        for(double barBeat=signature.beat;barBeat<segmentEnd-.001;barBeat+=barLength,++barNumber){
            if(barBeat<startBeat-.001||barBeat>startBeat+window+.001)continue;
            // Notes that begin a measure occupy the beat position itself. Put
            // the barline just before that position so it cannot disappear
            // underneath the first notehead of the new measure.
            const bool viewportStart=qAbs(barBeat-startBeat)<.001;
            const double beatX=musicLeft+(barBeat-startBeat)*scale;
            const double x=viewportStart?120.0:beatX-10.0;
            p.setPen(QPen(QColor("#8f9d98"),1.5));p.drawLine(QPointF(x,70+verticalOffset),QPointF(x,200+verticalOffset));
            p.setPen(QColor("#71807b"));p.setFont(QFont("DM Mono",7,QFont::DemiBold));p.drawText(QPointF(x+4,57+verticalOffset),QString("M%1").arg(barNumber));
        }
    }
    QVector<TimeSignatureEvent> shownSignatures=signatures;if(startBeat>.001){TimeSignatureEvent active=signatures.first();for(const auto &signature:signatures){if(signature.beat>startBeat+.001)break;active=signature;}if(qAbs(active.beat-startBeat)>.04)shownSignatures.append({startBeat,active.beats,active.beatType});std::sort(shownSignatures.begin(),shownSignatures.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});}
    for(const auto &signature:shownSignatures){
        if(signature.beat<startBeat-.001||signature.beat>startBeat+window+.001)continue;
        const double x=qAbs(signature.beat-startBeat)<.04?120.0:musicLeft+(signature.beat-startBeat)*scale;
        const int keyFifths=fifthsAt(m_keySignatures,signature.beat);
        const double keyWidth=qAbs(keyFifths)*8.0;
        p.setPen(QColor("#29433d"));p.setFont(QFont("Noto Serif",13,QFont::Bold));
        for(double center:{105.0+verticalOffset,195.0+verticalOffset}){
            p.drawText(QRectF(x+8+keyWidth,center-22,24,18),Qt::AlignCenter,QString::number(signature.beats));
            p.drawText(QRectF(x+8+keyWidth,center-5,24,18),Qt::AlignCenter,QString::number(signature.beatType));
        }
    }
    const QVector<int> trebleSharps{77,72,79,74,69,76,71},trebleFlats{71,76,69,74,67,72,65};
    const QVector<int> bassSharps{53,48,55,50,45,52,47},bassFlats{47,52,45,50,43,48,41};
    QVector<KeySignatureEvent> shownKeys=m_keySignatures;if(shownKeys.isEmpty())shownKeys.append({0.0,0});if(startBeat>.001){KeySignatureEvent active=shownKeys.first();for(const auto &key:shownKeys){if(key.beat>startBeat+.001)break;active=key;}if(qAbs(active.beat-startBeat)>.04)shownKeys.append({startBeat,active.fifths});std::sort(shownKeys.begin(),shownKeys.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});}
    for(const auto &key:shownKeys){
        if(key.beat<startBeat-.001||key.beat>startBeat+window+.001)continue;
        const double x=qAbs(key.beat-startBeat)<.04?120.0:musicLeft+(key.beat-startBeat)*scale;p.setPen(QColor("#29433d"));p.setFont(QFont("Noto Music",17));
        const bool sharp=key.fifths>0;const int count=qMin(7,qAbs(key.fifths));const auto &treble=sharp?trebleSharps:trebleFlats;const auto &bass=sharp?bassSharps:bassFlats;
        for(int index=0;index<count;++index){
            const double sx=x+5+index*8,ty=120-(diatonicIndex(treble[index])-diatonicIndex(60))*5+verticalOffset;
            const double by=150-(diatonicIndex(bass[index])-diatonicIndex(60))*5+verticalOffset;
            p.drawText(QPointF(sx,ty+6),sharp?QString::fromUtf8("♯"):QString::fromUtf8("♭"));p.drawText(QPointF(sx,by+6),sharp?QString::fromUtf8("♯"):QString::fromUtf8("♭"));
        }
    }
    for(double repeatBeat:m_repeatEnds){
        if(repeatBeat<startBeat-.001||repeatBeat>startBeat+window+.001)continue;
        const double x=musicLeft+(repeatBeat-startBeat)*scale;
        p.setPen(QPen(QColor("#29433d"),1.5));p.drawLine(QPointF(x-4,70+verticalOffset),QPointF(x-4,200+verticalOffset));
        p.setPen(QPen(QColor("#29433d"),4));p.drawLine(QPointF(x+2,70+verticalOffset),QPointF(x+2,200+verticalOffset));
        p.setPen(Qt::NoPen);p.setBrush(QColor("#29433d"));
        for(double y:{85.0+verticalOffset,95.0+verticalOffset,175.0+verticalOffset,185.0+verticalOffset})p.drawEllipse(QPointF(x-10,y),2.2,2.2);
    }
    for(const auto &rest:m_rests){
        if(rest.beat<startBeat-.001||rest.beat>startBeat+window+.001||!(m_handFilter<0||(m_handFilter==0&&rest.hand==Hand::Left)||(m_handFilter==1&&rest.hand==Hand::Right)))continue;
        const double x=musicLeft+(rest.beat-startBeat)*scale,y=(rest.hand==Hand::Right?90.0:180.0)+verticalOffset;
        const QString symbol=rest.duration>=3.5?QString::fromUtf8("𝄻"):rest.duration>=1.75?QString::fromUtf8("𝄼"):rest.duration>=.75?QString::fromUtf8("𝄽"):QString::fromUtf8("𝄾");
        p.setPen(QColor("#263f39"));p.setFont(QFont("Noto Music",21));p.drawText(QRectF(x-11,y-17,22,34),Qt::AlignCenter,symbol);
    }
    if(followed>=startBeat&&followed<=startBeat+window){const double playX=musicLeft+(followed-startBeat)*scale;p.fillRect(QRectF(playX-2,8,4,294),QColor(239,119,95,70));p.setPen(QColor("#bd5945"));p.drawLine(QPointF(playX,8),QPointF(playX,302));}
    if(m_editEnabled&&m_editCursorBeat>=startBeat&&m_editCursorBeat<=startBeat+window){
        const double editX=musicLeft+(m_editCursorBeat-startBeat)*scale;p.fillRect(QRectF(editX-2.5,48,5,190),QColor("#ef775f"));
        p.setPen(QColor("#bd5945"));p.setBrush(QColor("#ef775f"));QPolygonF marker;marker<<QPointF(editX-7,48)<<QPointF(editX+7,48)<<QPointF(editX,58);p.drawPolygon(marker);
        p.setFont(QFont("Manrope",7,QFont::Bold));p.drawText(QPointF(editX+6,47),"ADD");
    }
    if(visible.isEmpty()){p.setPen(QColor("#93a09c"));p.setFont(QFont("Manrope",13));p.drawText(QRectF(150,90+verticalOffset,800,70),Qt::AlignCenter,m_editEnabled?"Click to insert notes · Right-click empty staff for rests, key, meter, or repeat":"Play or import notes to begin your score");p.restore();return;}
    struct RenderNote {
        const NoteEvent *event = nullptr;
        double y = 0;
        double headX = 0;
        QColor color;
    };
    struct RenderChord {
        Hand hand = Hand::Right;
        double start = 0;
        double x = 0;
        double rhythm = 1;
        QVector<RenderNote> notes;
        bool stemUp = true;
        int flags = 0;
        double stemX = 0;
        double stemBaseY = 0;
        double stemEndY = 0;
        QColor stemColor;
        bool beamed = false;
    };
    struct BeamGroup { QVector<int> chordIndexes; bool stemUp = true; };

    auto flagsForDuration=[](double duration){
        // A third of a beat is normally an eighth-note triplet, not a flagged
        // sixteenth. Keep the dotted values on their conventional level too.
        if(duration<.19)return 3;
        if(duration<.31||qAbs(duration-.375)<.045)return 2;
        return duration<.82?1:0;
    };
    auto noteColor=[&](const NoteEvent &note){
        const bool expected=qAbs(note.start-m_groupStart)<.04&&m_expected.contains(note.note);
        const bool played=m_playedBeat>=note.start&&!expected;
        return expected?QColor("#ef775f"):(played?QColor("#2d7d70"):QColor("#263f39"));
    };

    // First collapse simultaneous pitches on the same staff into a chord. A
    // small tolerance absorbs import/transcription rounding without merging an
    // audible arpeggio into a block chord.
    QVector<const NoteEvent*> ordered;
    for(const auto &note:visible)
        if(note.start+note.duration>=startBeat&&note.start<=startBeat+window)
            ordered<<&note;
    std::sort(ordered.begin(),ordered.end(),[](const NoteEvent *a,const NoteEvent *b){
        if(a->hand!=b->hand)return int(a->hand)<int(b->hand);
        if(qAbs(a->start-b->start)>.001)return a->start<b->start;
        return a->note<b->note;
    });
    QVector<RenderChord> chords;
    constexpr double chordTolerance=.04;
    const int c4=diatonicIndex(60);
    for(const NoteEvent *note:ordered){
        if(chords.isEmpty()||chords.last().hand!=note->hand||qAbs(chords.last().start-note->start)>chordTolerance){
            RenderChord chord;
            chord.hand=note->hand;chord.start=note->start;
            chord.x=musicLeft+(note->start-startBeat)*scale;
            chord.rhythm=qMax(.0625,note->duration);
            chords<<chord;
        }
        auto &chord=chords.last();
        chord.rhythm=qMin(chord.rhythm,qMax(.0625,note->duration));
        const int idx=staffDiatonicIndex(note->note,fifthsAt(m_keySignatures,note->start),note->accidental);
        const double y=(note->hand==Hand::Left?150-(idx-c4)*5:120-(idx-c4)*5)+verticalOffset;
        chord.notes<<RenderNote{note,y,chord.x,noteColor(*note)};
        chord.flags=qMax(chord.flags,flagsForDuration(note->duration));
    }
    for(auto &chord:chords){
        double averageY=0;for(const auto &note:chord.notes)averageY+=note.y;
        averageY/=qMax(1,chord.notes.size());
        chord.stemUp=averageY>=(chord.hand==Hand::Right?90.0+verticalOffset:180.0+verticalOffset);
    }

    // Beam consecutive short values within one beat. Bar boundaries are
    // therefore boundaries too, and a visible rest breaks the group.
    QVector<BeamGroup> beams;
    for(Hand hand:{Hand::Left,Hand::Right}){
        QVector<int> run;
        auto flush=[&](){
            if(run.size()>1){
                double averageY=0;int count=0;
                for(int ci:run)for(const auto &note:chords[ci].notes){averageY+=note.y;++count;}
                const bool up=(averageY/qMax(1,count))>=(hand==Hand::Right?90.0+verticalOffset:180.0+verticalOffset);
                for(int ci:run)chords[ci].stemUp=up;
                beams<<BeamGroup{run,up};
            }
            run.clear();
        };
        for(int ci=0;ci<chords.size();++ci){
            const auto &chord=chords[ci];
            if(chord.hand!=hand)continue;
            bool joins=chord.flags>0;
            if(joins&&!run.isEmpty()){
                const auto &previous=chords[run.last()];
                const bool sameBeat=int(std::floor(previous.start+1e-4))==int(std::floor(chord.start+1e-4));
                const double gap=chord.start-previous.start;
                joins=sameBeat&&gap>chordTolerance&&gap<=qMax(.40,previous.rhythm*1.55)+.03;
            }
            if(!joins)flush();
            if(chord.flags>0)run<<ci;
        }
        flush();
    }

    // Chord seconds need offset noteheads. Work out the shared stem only after
    // the beam pass has selected one direction for the entire rhythmic group.
    for(auto &chord:chords){
        std::sort(chord.notes.begin(),chord.notes.end(),[](const RenderNote &a,const RenderNote &b){return a.y>b.y;});
        bool previousShifted=false;
        for(int ni=1;ni<chord.notes.size();++ni){
            if(qAbs(chord.notes[ni].y-chord.notes[ni-1].y)<7.0){
                previousShifted=!previousShifted;
                if(previousShifted)chord.notes[ni].headX+=chord.stemUp?6.0:-6.0;
            }else previousShifted=false;
        }
        double minY=std::numeric_limits<double>::max(),maxY=std::numeric_limits<double>::lowest();
        double minX=std::numeric_limits<double>::max(),maxX=std::numeric_limits<double>::lowest();
        bool anyExpected=false,allWhole=true,allPlayed=true;
        for(const auto &note:chord.notes){
            minY=qMin(minY,note.y);maxY=qMax(maxY,note.y);minX=qMin(minX,note.headX);maxX=qMax(maxX,note.headX);
            anyExpected|=note.color==QColor("#ef775f");
            allPlayed&=note.color==QColor("#2d7d70");
            allWhole&=note.event->duration>=3.5;
        }
        chord.stemColor=anyExpected?QColor("#ef775f"):(allPlayed?QColor("#2d7d70"):QColor("#263f39"));
        if(allWhole){chord.flags=0;chord.stemX=chord.x;chord.stemBaseY=chord.stemEndY=0;continue;}
        chord.stemX=chord.stemUp?maxX+6:minX-6;
        chord.stemBaseY=chord.stemUp?maxY:minY;
        chord.stemEndY=chord.stemUp?minY-30:maxY+30;
    }

    // Lay out beam slopes and extend each chord's one shared stem to meet it.
    for(auto &beam:beams){
        auto &first=chords[beam.chordIndexes.first()];
        auto &last=chords[beam.chordIndexes.last()];
        double slope=qBound(-.16,(last.stemEndY-first.stemEndY)/qMax(1.0,last.stemX-first.stemX),.16);
        double intercept=first.stemEndY;
        double correction=0;
        for(int ci:beam.chordIndexes){
            const auto &chord=chords[ci];
            double minY=std::numeric_limits<double>::max(),maxY=std::numeric_limits<double>::lowest();
            for(const auto &note:chord.notes){minY=qMin(minY,note.y);maxY=qMax(maxY,note.y);}
            const double y=intercept+slope*(chord.stemX-first.stemX);
            if(beam.stemUp)correction=qMax(correction,y-(minY-25));
            else correction=qMax(correction,(maxY+25)-y);
        }
        intercept+=beam.stemUp?-correction:correction;
        for(int ci:beam.chordIndexes){
            auto &chord=chords[ci];chord.beamed=true;
            chord.stemEndY=intercept+slope*(chord.stemX-first.stemX);
        }
    }

    // Ledger lines sit behind the noteheads.
    for(const auto &chord:chords)for(const auto &render:chord.notes){
        const auto &note=*render.event;const double x=render.headX,y=render.y;
        if((note.hand==Hand::Right&&y>=115+verticalOffset)||(note.hand==Hand::Left&&y<=155+verticalOffset)){
            p.setPen(QPen(QColor("#52645f"),1));const double ledger=(note.hand==Hand::Right?120:150)+verticalOffset;
            const double step=y>ledger?10:-10;
            for(double ly=ledger;(step>0?ly<=y:ly>=y);ly+=step)p.drawLine(QPointF(x-10,ly),QPointF(x+10,ly));
        }
    }
    // One stem per chord; flags are used only when that chord is not beamed.
    for(const auto &chord:chords){
        if(chord.stemBaseY==0&&chord.stemEndY==0)continue;
        p.setPen(QPen(chord.stemColor,1.7));
        p.drawLine(QPointF(chord.stemX,chord.stemBaseY),QPointF(chord.stemX,chord.stemEndY));
        if(!chord.beamed){
            for(int flag=0;flag<chord.flags;++flag){
                const double fy=chord.stemEndY+(chord.stemUp?flag*6:-flag*6);
                QPainterPath tail;tail.moveTo(chord.stemX,fy);
                if(chord.stemUp)tail.cubicTo(chord.stemX+10,fy+3,chord.stemX+10,fy+10,chord.stemX+4,fy+14);
                else tail.cubicTo(chord.stemX-10,fy-3,chord.stemX-10,fy-10,chord.stemX-4,fy-14);
                p.drawPath(tail);
            }
        }
    }
    // Draw connected beam levels. Mixed eighth/sixteenth groups get a short
    // secondary hook instead of another detached flag.
    for(const auto &beam:beams){
        const auto &indexes=beam.chordIndexes;
        int maximum=1;for(int ci:indexes)maximum=qMax(maximum,chords[ci].flags);
        for(int level=1;level<=maximum;++level){
            const double offset=(level-1)*(beam.stemUp?7.0:-7.0);
            for(int pos=0;pos+1<indexes.size();++pos){
                const auto &a=chords[indexes[pos]],&b=chords[indexes[pos+1]];
                if(a.flags<level||b.flags<level)continue;
                QLinearGradient gradient(QPointF(a.stemX,a.stemEndY+offset),QPointF(b.stemX,b.stemEndY+offset));
                gradient.setColorAt(0,a.stemColor);gradient.setColorAt(1,b.stemColor);
                p.setPen(QPen(QBrush(gradient),4,Qt::SolidLine,Qt::FlatCap));
                p.drawLine(QPointF(a.stemX,a.stemEndY+offset),QPointF(b.stemX,b.stemEndY+offset));
            }
            if(level==1)continue;
            for(int pos=0;pos<indexes.size();++pos){
                const auto &chord=chords[indexes[pos]];
                if(chord.flags<level)continue;
                const bool linkedLeft=pos>0&&chords[indexes[pos-1]].flags>=level;
                const bool linkedRight=pos+1<indexes.size()&&chords[indexes[pos+1]].flags>=level;
                if(linkedLeft||linkedRight)continue;
                const int neighbor=pos+1<indexes.size()?indexes[pos+1]:indexes[pos-1];
                const auto &other=chords[neighbor];
                const double hookX=chord.stemX+(other.stemX-chord.stemX)*.36;
                const double hookY=chord.stemEndY+(other.stemEndY-chord.stemEndY)*.36+offset;
                p.setPen(QPen(chord.stemColor,4,Qt::SolidLine,Qt::FlatCap));
                p.drawLine(QPointF(chord.stemX,chord.stemEndY+offset),QPointF(hookX,hookY));
            }
        }
    }
    // Keep sustains explicit in Live follow.  Bar-spanning notes need ordinary
    // notation ties, but a visually transcribed hold can also run underneath
    // several shorter attacks without crossing a bar.  In that case the one
    // duration-shaped notehead was too easy to mistake for a short note.  Draw
    // a tied continuation through the release point as well, so the live view
    // shows the same hold that the synthesizer is actually playing.
    for(const auto &chord:chords)for(const auto &render:chord.notes){
        const auto &note=*render.event;const double end=note.start+note.duration;
        // One beat or less already has an unambiguous standard note value.
        // Longer detected holds get the explicit live sustain treatment.
        const bool singleWrittenValue=qAbs(note.duration-1.5)<.07||qAbs(note.duration-2.0)<.07||qAbs(note.duration-3.0)<.10||qAbs(note.duration-4.0)<.10||qAbs(note.duration-6.0)<.12;
        const bool showSustainTie=note.duration>1.05&&!singleWrittenValue;
        QVector<double> stops;
        double boundary=nextMeasureBoundary(m_timeSignatures,note.start);
        while(boundary<end-.001){stops<<boundary;boundary=nextMeasureBoundary(m_timeSignatures,boundary+.002);}
        if(showSustainTie||!stops.isEmpty())stops<<end;
        double previousBeat=note.start;
        for(const double stop:stops){
            const double previousX=musicLeft+(previousBeat-startBeat)*scale;
            const double stopX=musicLeft+(stop-startBeat)*scale;
            const bool startVisible=previousBeat>=startBeat-.001&&previousBeat<=startBeat+window+.001;
            const bool stopVisible=stop>=startBeat-.001&&stop<=startBeat+window+.001;
            const double left=qMax(musicLeft,previousX+(startVisible?8.0:0.0));
            const double right=qMin(musicRight,stopX-(stopVisible?8.0:0.0));
            if(right-left>8.0&&stop>=startBeat-.001&&previousBeat<=startBeat+window+.001){
                const double direction=chord.stemUp?1.0:-1.0;
                const double bow=qBound(6.0,(right-left)*.10,13.0);
                QPainterPath tie;tie.moveTo(left,render.y+direction*4.0);
                tie.cubicTo(left+(right-left)*.28,render.y+direction*(4.0+bow),right-(right-left)*.28,render.y+direction*(4.0+bow),right,render.y+direction*4.0);
                p.setBrush(Qt::NoBrush);p.setPen(QPen(render.color,2.2,Qt::SolidLine,Qt::RoundCap));p.drawPath(tie);
                if(stopVisible){
                    p.save();p.translate(stopX,render.y);p.rotate(-18);p.setPen(QPen(render.color,1.8));p.setBrush(QColor("#fffefa"));p.drawEllipse(QRectF(-7,-4.5,14,9));p.restore();
                }
            }
            previousBeat=stop;
        }
        if(showSustainTie&&followed>=note.start&&followed<end&&followed>=startBeat&&followed<=startBeat+window){
            const double activeX=musicLeft+(followed-startBeat)*scale;
            p.setPen(QPen(QColor("#bd5945"),1.3));p.setBrush(QColor("#ef775f"));p.drawEllipse(QPointF(activeX,render.y),3.8,3.8);
        }
    }
    // Noteheads, augmentation dots, and optional names remain pitch-specific.
    for(const auto &chord:chords)for(const auto &render:chord.notes){
        const auto &note=*render.event;const double x=render.headX,y=render.y;const QColor color=render.color;
        AccidentalMark accidental=note.accidental;
        if(accidental==AccidentalMark::Automatic){int fifths=0;for(const auto &key:m_keySignatures){if(key.beat>note.start+.001)break;fifths=key.fifths;}const QVector<int> sharpNaturals{5,0,7,2,9,4,11},flatNaturals{11,4,9,2,7,0,5};const auto &naturals=fifths>0?sharpNaturals:flatNaturals;for(int index=0;index<qMin(7,qAbs(fifths));++index)if(note.note%12==naturals[index]){accidental=AccidentalMark::Natural;break;}}
        if(accidental!=AccidentalMark::Automatic){const QString symbol=accidental==AccidentalMark::Natural?QString::fromUtf8("♮"):accidental==AccidentalMark::Sharp?QString::fromUtf8("♯"):QString::fromUtf8("♭");p.setPen(color);p.setFont(QFont("Noto Music",15));p.drawText(QRectF(x-24,y-13,14,26),Qt::AlignCenter,symbol);}
        const bool openHead=note.duration>=1.75;
        p.save();p.translate(x,y);p.rotate(-18);p.setPen(openHead?QPen(color,1.8):Qt::NoPen);p.setBrush(openHead?QColor("#fffefa"):color);p.drawEllipse(QRectF(-7,-4.5,14,9));p.restore();
        const bool dotted=qAbs(note.duration-.375)<.045||qAbs(note.duration-.75)<.07||qAbs(note.duration-1.5)<.1||qAbs(note.duration-3.0)<.15;
        if(dotted){p.setPen(Qt::NoPen);p.setBrush(color);p.drawEllipse(QPointF(x+11,y),1.8,1.8);}
        if(m_showNames){p.setPen(openHead?color:Qt::white);p.setFont(QFont("Manrope",6,QFont::Bold));p.drawText(QRectF(x-8,y-5,16,10),Qt::AlignCenter,staffPitchName(note.note,fifthsAt(m_keySignatures,note.start),note.accidental));}
    }
    p.restore();
}

void ScoreWidget::mousePressEvent(QMouseEvent *event){
    if(!m_editEnabled||width()<=0||height()<=0){QWidget::mousePressEvent(event);return;}
    const double x=event->position().x()*1100.0/width(),y=event->position().y()*310.0/height();
    const auto range=viewRange();const double startBeat=range.first,window=range.second;
    const double musicLeft=170.0+qAbs(fifthsAt(m_keySignatures,startBeat))*8.0,musicRight=1020.0,scale=(musicRight-musicLeft)/window;
    if(event->button()==Qt::LeftButton&&x>=musicLeft&&x<=musicRight){
        const double beat=qMax(0.0,qRound((startBeat+(x-musicLeft)/scale)*8.0)/8.0);m_editCursorBeat=beat;update();emit editCursorChanged(beat);event->accept();return;
    }
    if(event->button()==Qt::RightButton){
        int closest=-1;double closestDistance=std::numeric_limits<double>::max();const int c4=diatonicIndex(60);
        for(int index=0;index<m_notes.size();++index){const auto &note=m_notes[index];if(!included(note,m_handFilter)||note.start<startBeat-.001||note.start>startBeat+window+.001)continue;const double noteX=musicLeft+(note.start-startBeat)*scale;const int diatonic=staffDiatonicIndex(note.note,fifthsAt(m_keySignatures,note.start),note.accidental);const double noteY=(note.hand==Hand::Left?150-(diatonic-c4)*5:120-(diatonic-c4)*5)+30.0;const double dx=qAbs(x-noteX),dy=qAbs(y-noteY);if(dx<=18&&dy<=13&&dx+dy<closestDistance){closest=index;closestDistance=dx+dy;}}
        if(closest>=0){emit noteContextRequested(closest,event->globalPosition().toPoint());event->accept();return;}
        int closestRest=-1;closestDistance=std::numeric_limits<double>::max();for(int index=0;index<m_rests.size();++index){const auto &rest=m_rests[index];if(rest.beat<startBeat-.001||rest.beat>startBeat+window+.001)continue;const double restX=musicLeft+(rest.beat-startBeat)*scale,restY=(rest.hand==Hand::Right?90.0:180.0)+30.0;const double dx=qAbs(x-restX),dy=qAbs(y-restY);if(dx<=18&&dy<=18&&dx+dy<closestDistance){closestRest=index;closestDistance=dx+dy;}}
        if(closestRest>=0){emit restDeleteRequested(closestRest);event->accept();return;}
        if(x>=musicLeft&&x<=musicRight){const double beat=qMax(0.0,qRound((startBeat+(x-musicLeft)/scale)*8.0)/8.0);m_editCursorBeat=beat;update();emit editCursorChanged(beat);emit staffContextRequested(beat,event->globalPosition().toPoint());event->accept();return;}
    }
    QWidget::mousePressEvent(event);
}

PianoWidget::PianoWidget(QWidget *parent):QWidget(parent){setMinimumHeight(130);setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);}
QRectF PianoWidget::noteRect(int note) const {int whites=0,total=0;for(int n=m_low;n<=m_high;++n)if(!blackKey(n))++total;double ww=width()/double(total);for(int n=m_low;n<=note;++n)if(!blackKey(n))++whites;if(blackKey(note))return QRectF((whites-.34)*ww,0,ww*.68,height()*.62);return QRectF((whites-1)*ww,0,ww,height());}
int PianoWidget::noteAt(const QPointF&p)const{for(int n=m_low;n<=m_high;++n)if(blackKey(n)&&noteRect(n).contains(p))return n;for(int n=m_low;n<=m_high;++n)if(!blackKey(n)&&noteRect(n).contains(p))return n;return-1;}
void PianoWidget::setActive(int n,bool a){if(a)m_active.insert(n);else m_active.remove(n);update(noteRect(n).adjusted(-5,-5,5,5).toRect());}
void PianoWidget::paintEvent(QPaintEvent*){QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#20332f"));auto draw=[&](bool black){for(int n=m_low;n<=m_high;++n){if(blackKey(n)!=black)continue;QRectF r=noteRect(n).adjusted(1,0,-1,-2);QPainterPath path;path.addRoundedRect(r,black?3:4,black?3:4);if(m_active.contains(n))p.fillPath(path,QColor(black?"#ef775f":"#9ee0d0"));else{QLinearGradient g(r.topLeft(),r.bottomLeft());if(black){g.setColorAt(0,QColor("#2d3c38"));g.setColorAt(1,QColor("#07110f"));}else{g.setColorAt(0,Qt::white);g.setColorAt(1,QColor("#e0dfd7"));}p.fillPath(path,g);}p.setPen(QPen(QColor(black?"#07110f":"#adb5b2"),1));p.drawPath(path);if(!black&&n%12==0){p.setPen(QColor("#8a9692"));p.setFont(QFont("DM Mono",7));p.drawText(r.adjusted(0,0,0,-7),Qt::AlignHCenter|Qt::AlignBottom,MusicDocument::noteName(n));}}};draw(false);draw(true);}
void PianoWidget::mousePressEvent(QMouseEvent*e){m_mouseNote=noteAt(e->position());if(m_mouseNote>=0)emit notePressed(m_mouseNote,100);}
void PianoWidget::mouseMoveEvent(QMouseEvent*e){if(!(e->buttons()&Qt::LeftButton))return;int n=noteAt(e->position());if(n!=m_mouseNote){if(m_mouseNote>=0)emit noteReleased(m_mouseNote);m_mouseNote=n;if(n>=0)emit notePressed(n,100);}}
void PianoWidget::mouseReleaseEvent(QMouseEvent*){if(m_mouseNote>=0)emit noteReleased(m_mouseNote);m_mouseNote=-1;}
void PianoWidget::leaveEvent(QEvent*){if(m_mouseNote>=0){emit noteReleased(m_mouseNote);m_mouseNote=-1;}}

BubbleWidget::BubbleWidget(QWidget *parent):QWidget(parent){setMinimumHeight(185);m_clock.start();m_timer.setInterval(16);connect(&m_timer,&QTimer::timeout,this,qOverload<>(&QWidget::update));m_timer.start();}
void BubbleWidget::setNotes(const QVector<NoteEvent>&n){m_notes=n;update();}void BubbleWidget::setPlayhead(double b){m_playhead=b;update();}void BubbleWidget::setExpected(const QSet<int>&n,double s){m_expected=n;m_groupStart=s;update();}void BubbleWidget::setBubblesVisible(bool v){m_showBubbles=v;update();}void BubbleWidget::setWaitMode(bool waiting){m_waitMode=waiting;update();}void BubbleWidget::setHandFilter(int f){m_handFilter=f;update();}void BubbleWidget::pulseNote(int n){m_pulses<<Pulse{n,m_clock.elapsed()};update();}
void BubbleWidget::paintEvent(QPaintEvent*){QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QLinearGradient bg(0,0,0,height());bg.setColorAt(0,QColor("#17342e"));bg.setColorAt(1,QColor("#102721"));p.fillRect(rect(),bg);int low=21,high=108;auto xFor=[&](int n){return (n-low+.5)*width()/double(high-low+1);};p.setPen(QPen(QColor(255,255,255,12),1));for(int n=low;n<=high;++n)if(n%12==0)p.drawLine(QPointF(xFor(n),0),QPointF(xFor(n),height()));double strike=height()-23;p.setPen(QPen(QColor("#66c4ae"),1));p.drawLine(QPointF(0,strike),QPointF(width(),strike));
    if(m_showBubbles){for(const auto&n:m_notes){if(!included(n,m_handFilter))continue;double reference=m_waitMode?m_groupStart:m_playhead;constexpr double pixelsPerBeat=42.0;double y=strike-(n.start-reference)*pixelsPerBeat;double w=qMax(12.0,width()/92.0),h=qMax(15.0,n.duration*pixelsPerBeat);if(y<-40||y-h>height()+30)continue;bool ex=m_waitMode&&qAbs(n.start-m_groupStart)<.01&&m_expected.contains(n.note);QColor c=ex?QColor("#ef775f"):QColor(n.hand==Hand::Left?"#86aebd":"#77cbb8");QRectF r(xFor(n.note)-w/2,y-h,w,h);p.setPen(Qt::NoPen);p.setBrush(c);p.drawRoundedRect(r,w/2,w/2);if(ex){p.setBrush(QColor(239,119,95,38));p.drawEllipse(QPointF(xFor(n.note),strike),15,15);}}}
    qint64 now=m_clock.elapsed();m_pulses.erase(std::remove_if(m_pulses.begin(),m_pulses.end(),[&](const Pulse&a){return now-a.born>700;}),m_pulses.end());for(const auto&a:m_pulses){double age=(now-a.born)/700.0;p.setPen(QPen(QColor(111,220,193,int(200*(1-age))),2));p.setBrush(Qt::NoBrush);p.drawEllipse(QPointF(xFor(a.note),strike),8+age*22,8+age*22);}
}
