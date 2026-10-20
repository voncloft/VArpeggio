#include "musicwidgets.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <QPainterPath>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double scoreCanvasWidth=1100.0;
constexpr double scoreCanvasHeight=440.0;
constexpr double scoreVerticalOffset=60.0;

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
QPair<double,double> measureAt(QVector<TimeSignatureEvent> signatures,double beat){
    if(signatures.isEmpty())signatures.append({0.0,4,4});
    std::sort(signatures.begin(),signatures.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});
    if(signatures.first().beat>.001)signatures.prepend({0.0,4,4});
    TimeSignatureEvent active=signatures.first();
    for(const auto &signature:signatures){if(signature.beat>beat+.001)break;active=signature;}
    const double length=qMax(.125,active.beats*4.0/qMax(1,active.beatType));
    const double local=qMax(0.0,beat-active.beat);
    return {active.beat+std::floor((local+1e-6)/length)*length,length};
}
int measureOrdinalAt(QVector<TimeSignatureEvent> signatures,double beat){
    if(signatures.isEmpty())signatures.append({0.0,4,4});
    std::sort(signatures.begin(),signatures.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});
    if(signatures.first().beat>.001)signatures.prepend({0.0,4,4});
    int number=1;
    for(int index=0;index<signatures.size();++index){
        const auto &signature=signatures[index];
        const double length=qMax(.125,signature.beats*4.0/qMax(1,signature.beatType));
        const double next=index+1<signatures.size()?signatures[index+1].beat:std::numeric_limits<double>::max();
        if(beat<next-.001)return number+qMax(0,int(std::floor((beat-signature.beat+1e-6)/length)));
        number+=qMax(0,int(qCeil((next-signature.beat-.001)/length)));
    }
    return number;
}
int staffDiatonicIndex(const NoteEvent &note,int fifths){
    if(note.writtenStep>=0&&note.writtenStep<7&&note.writtenAlter>=-2&&note.writtenAlter<=2){
        static const int naturalPitch[]={0,2,4,5,7,9,11};
        const int octave=int(std::floor(double(note.note-naturalPitch[note.writtenStep]-note.writtenAlter)/12.0))-1;
        return octave*7+note.writtenStep;
    }
    static const int sharpStep[]={0,0,1,1,2,3,3,4,4,5,5,6};static const int flatStep[]={0,1,1,2,2,3,4,4,5,5,6,6};const int pitch=((note.note%12)+12)%12;const bool useFlats=note.accidental==AccidentalMark::Flat||note.accidental==AccidentalMark::DoubleFlat||((note.accidental!=AccidentalMark::Sharp&&note.accidental!=AccidentalMark::DoubleSharp)&&fifths<0);return (note.note/12-1)*7+(useFlats?flatStep[pitch]:sharpStep[pitch]);
}
struct StaffPlacement { double y=0; int octaveDisplacement=0; };
StaffPlacement staffPlacement(const NoteEvent &note,int fifths,double verticalOffset){
    const int index=staffDiatonicIndex(note,fifths),c4=diatonicIndex(60);
    StaffPlacement placement{(note.hand==Hand::Left?150-(index-c4)*5:120-(index-c4)*5)+verticalOffset,0};
    // Once more than roughly four ledger lines are required, conventional
    // octave notation is clearer. Move only the written head; playback and
    // labels continue to use the note's real MIDI pitch.
    const double upperLimit=note.hand==Hand::Right?95.0:185.0;
    // Bass notes below C2 become markedly clearer as an 8vb passage. The
    // former 315-unit cutoff left C1-Bb1 underneath the staff, where ledger
    // lines, note names, and finger numbers collapsed into the frame edge.
    const double lowerLimit=note.hand==Hand::Right?215.0:285.0;
    while(placement.y<upperLimit&&placement.octaveDisplacement<2){placement.y+=35.0;++placement.octaveDisplacement;}
    // The 88-key bass end is conventionally readable with one 8vb shift;
    // retaining that single shift also preserves the established A0 layout.
    while(placement.y>lowerLimit&&placement.octaveDisplacement>-1){placement.y-=35.0;--placement.octaveDisplacement;}
    return placement;
}
QString staffPitchName(const NoteEvent &note,int fifths){return MusicDocument::spelledNoteName(note.note,fifths,note.accidental,true,note.writtenStep,note.writtenAlter);}
AccidentalMark automaticAccidental(int midi,int fifths){
    const int pitch=((midi%12)+12)%12;
    const QVector<int> sharpNaturals{5,0,7,2,9,4,11},flatNaturals{11,4,9,2,7,0,5};
    const auto &alteredNaturals=fifths>=0?sharpNaturals:flatNaturals;
    for(int index=0;index<qMin(7,qAbs(fifths));++index){
        const int natural=alteredNaturals[index];
        const int altered=(natural+(fifths>=0?1:-1)+12)%12;
        if(pitch==altered)return AccidentalMark::Automatic; // already supplied by the key signature
        if(pitch==natural)return AccidentalMark::Natural;   // cancel the active key signature
    }
    // A black-key pitch outside the key signature still needs a written
    // accidental. Prefer flats in a flat key and sharps everywhere else.
    if(blackKey(midi))return fifths<0?AccidentalMark::Flat:AccidentalMark::Sharp;
    return AccidentalMark::Automatic;
}
bool included(const NoteEvent &n,int filter){return filter<0||(filter==0&&n.hand==Hand::Left)||(filter==1&&n.hand==Hand::Right);}
QPair<int,int> maximumFingeringChordSizes(const QVector<NoteEvent> &notes,int filter){
    QVector<const NoteEvent*> ordered;for(const auto &note:notes)if(included(note,filter)&&note.finger>=1&&note.finger<=5)ordered.append(&note);
    std::sort(ordered.begin(),ordered.end(),[](const auto *a,const auto *b){return a->hand==b->hand?a->start<b->start:int(a->hand)<int(b->hand);});
    int right=1,left=1;
    for(int cursor=0;cursor<ordered.size();){int end=cursor+1;while(end<ordered.size()&&ordered[end]->hand==ordered[cursor]->hand&&qAbs(ordered[end]->start-ordered[cursor]->start)<=.04)++end;const int count=end-cursor;if(ordered[cursor]->hand==Hand::Right)right=qMax(right,count);else left=qMax(left,count);cursor=end;}
    return {right,left};
}
double fingeringTransitionCost(const NoteEvent &previous,int previousFinger,const NoteEvent &current,int finger,Hand hand){
    const int pitchSteps=diatonicIndex(current.note)-diatonicIndex(previous.note);
    const int semitoneSpan=qAbs(current.note-previous.note);
    const int fingerSteps=finger-previousFinger;
    if(current.note==previous.note)return finger==previousFinger?0.0:1000.0+20.0*qAbs(fingerSteps);
    // The compact diatonic lookup deliberately collapses enharmonic spellings;
    // retain the MIDI direction when two neighboring chromatic notes therefore
    // land on the same lookup step.
    const int movement=pitchSteps!=0?pitchSteps:current.note-previous.note;
    const int expectedDirection=(hand==Hand::Right?movement:-movement);
    const bool naturalDirection=(expectedDirection>0&&fingerSteps>0)||(expectedDirection<0&&fingerSteps<0);
    const bool crossing=semitoneSpan<=2&&(hand==Hand::Right
        ?((movement>0&&(previousFinger==3||previousFinger==4)&&finger==1)
          ||(movement<0&&previousFinger==1&&(finger==3||finger==4)))
        :((movement>0&&previousFinger==1&&(finger==3||finger==4))
          ||(movement<0&&(previousFinger==3||previousFinger==4)&&finger==1)));
    double cost=0.0;
    if(naturalDirection){
        const int wanted=qBound(1,qRound(semitoneSpan*4.0/7.0),4);
        cost=8.0*qAbs(qAbs(fingerSteps)-wanted);
    }else if(crossing){
        // Thumb-under or hand-over crossings normally span two or three
        // fingers. They are useful, but not on every pair of notes.
        cost=3.0+2.0*qAbs(qAbs(fingerSteps)-2);
    }else if(fingerSteps==0)cost=500.0+10.0*semitoneSpan;
    else cost=500.0+10.0*qAbs(fingerSteps);
    // A thumb-to-little-finger swap on an adjacent note is never a sensible
    // automatic suggestion. This was the source of the visible 1/5 pattern.
    if(qAbs(movement)<=1&&qAbs(fingerSteps)==4)cost+=200.0;
    if(blackKey(current.note)&&finger==1)cost+=3.0;
    return cost;
}
// Adapted from PianoPlayer's MIT-licensed Hand optimizer. PianoPlayer models
// the five relaxed finger positions, finger strength, black-key reach and the
// time available for the next movement. VArpeggio solves the complete melodic
// chain with dynamic programming instead of repeatedly committing only the
// first result of a nine-note window, but minimizes the same physical effort.
double physicalTransitionCost(const NoteEvent &previous,int previousFinger,const NoteEvent &current,int finger,Hand hand){
    static constexpr double rest[6]={0.0,-7.0*.82,-2.8*.82,0.0,2.8*.82,5.6*.82};
    static constexpr double weight[6]={1.0,1.1,1.0,1.1,.9,.8};
    static constexpr double blackBias[6]={1.0,.3,1.0,1.1,.8,.7};
    const double direction=hand==Hand::Right?1.0:-1.0;
    const double previousX=direction*previous.note*(16.5/12.0);
    const double currentX=direction*current.note*(16.5/12.0);
    const double waiting=qAbs(current.start-previous.start)+.1;
    const double relaxedFingerPosition=previousX+(rest[finger]-rest[previousFinger]);
    double effort=qAbs(currentX-relaxedFingerPosition)/waiting;
    effort/=weight[finger]*(blackKey(current.note)?blackBias[finger]:1.0);

    const double movement=currentX-previousX;
    if(previous.note!=current.note&&previousFinger==finger&&previous.duration<4.0)return 1e9;
    if(previousFinger>1){
        if(finger>1&&(finger-previousFinger)*movement<0)return 1e9;
        if(finger==1&&blackKey(current.note)&&movement>0)return 1e9;
    }else if(blackKey(previous.note)&&movement<0&&finger>1&&previous.duration<2.0)return 1e9;
    return effort;
}
void fillMelodicFingering(QVector<NoteEvent> &notes,const QVector<int> &run,Hand hand){
    if(run.isEmpty())return;
    bool unannotated=true,ascending=true,descending=true;
    for(int index=0;index<run.size();++index){
        unannotated&=notes[run[index]].finger==0;
        if(index){const int difference=notes[run[index]].note-notes[run[index-1]].note;ascending&=difference>0;descending&=difference<0;}
    }
    // A short one-direction phrase that spans no more than a fifth fits under
    // one hand position. Map the actual diatonic distance to the finger, not
    // merely the event count: C-G is 1-5 in RH and 5-1 in LH, while C-E-G is
    // 1-3-5 / 5-3-1. Treating every next event as the next finger previously
    // produced the unusable LH C-G suggestion 5-4 in imported scores.
    if(unannotated&&run.size()>=2&&run.size()<=5&&(ascending||descending)){
        const int firstPitch=notes[run.first()].note;
        const int semitoneSpan=qAbs(notes[run.last()].note-firstPitch);
        if(semitoneSpan<=12){
            const bool rises=ascending;
            const int start=hand==Hand::Right?(rises?1:5):(rises?5:1);
            const int direction=hand==Hand::Right?(rises?1:-1):(rises?-1:1);
            for(int index=0;index<run.size();++index){
                const int distance=qBound(0,qAbs(diatonicIndex(notes[run[index]].note)-diatonicIndex(firstPitch)),4);
                notes[run[index]].finger=start+direction*distance;
            }
            return;
        }
    }
    constexpr double infinity=1e12;
    QVector<QVector<double>> costs(run.size(),QVector<double>(6,infinity));
    QVector<QVector<int>> parents(run.size(),QVector<int>(6,0));
    int direction=0;for(int index=1;index<run.size();++index)if(notes[run[index]].note!=notes[run[0]].note){direction=notes[run[index]].note-notes[run[0]].note;break;}
    const int preferred=direction==0?3:(hand==Hand::Right?(direction>0?1:5):(direction>0?5:1));
    for(int finger=1;finger<=5;++finger)if(notes[run[0]].finger==0||notes[run[0]].finger==finger)
        costs[0][finger]=qAbs(finger-preferred)+(blackKey(notes[run[0]].note)&&finger==1?3.0:0.0);
    for(int position=1;position<run.size();++position){
        for(int finger=1;finger<=5;++finger){
            if(notes[run[position]].finger!=0&&notes[run[position]].finger!=finger)continue;
            for(int previousFinger=1;previousFinger<=5;++previousFinger){
                if(costs[position-1][previousFinger]>=infinity)continue;
                const double candidate=costs[position-1][previousFinger]+fingeringTransitionCost(notes[run[position-1]],previousFinger,notes[run[position]],finger,hand);
                if(candidate<costs[position][finger]){costs[position][finger]=candidate;parents[position][finger]=previousFinger;}
            }
        }
    }
    int finger=1;for(int candidate=2;candidate<=5;++candidate)if(costs.last()[candidate]<costs.last()[finger])finger=candidate;
    for(int position=run.size()-1;position>=0;--position){notes[run[position]].finger=finger;if(position>0)finger=parents[position][finger];}
}
void repairHandFingering(QVector<NoteEvent> &notes,const QVector<bool> &authored,Hand hand){
    QVector<int> order;for(int index=0;index<notes.size();++index)if(notes[index].hand==hand)order.append(index);
    std::sort(order.begin(),order.end(),[&](int a,int b){return notes[a].start==notes[b].start?notes[a].note<notes[b].note:notes[a].start<notes[b].start;});
    struct Group{int begin=0,end=0;};QVector<Group> groups;
    for(int cursor=0;cursor<order.size();){int end=cursor+1;while(end<order.size()&&qAbs(notes[order[end]].start-notes[order[cursor]].start)<.015)++end;groups.append({cursor,end});cursor=end;}
    // Chords already have simultaneous, ordered finger assignments. Between
    // them, optimize each uninterrupted melodic chain as one physical hand
    // movement. Authored MusicXML fingering remains a hard constraint.
    constexpr double infinity=1e12;
    for(int group=0;group<groups.size();){
        if(groups[group].end-groups[group].begin!=1){++group;continue;}
        int chainEnd=group+1;
        while(chainEnd<groups.size()&&groups[chainEnd].end-groups[chainEnd].begin==1){
            const auto &previous=notes[order[groups[chainEnd-1].begin]];
            const auto &current=notes[order[groups[chainEnd].begin]];
            // Phrase spacing is the silence after the previous key is
            // released, not the distance between attack times. A chord drill
            // can place attacks two beats apart while holding each bass note
            // for 1.6 beats; treating that as a two-beat rest split every
            // note into its own chain and bypassed the physical-hand search.
            // Use the same release-aware boundary for lessons, imports and
            // every playback mode that displays this shared score model.
            const double releaseGap=current.start-(previous.start+previous.duration);
            // A leap is not itself a phrase break: the optimizer needs to see
            // it to choose a reachable hand relocation (for example LH 5-1
            // across a sixth). Only genuine silence starts a new hand phrase.
            if(releaseGap>.50)break;
            ++chainEnd;
        }
        const int length=chainEnd-group;
        // PianoPlayer requires at least three attacks for a meaningful search
        // window. Keep the already-generated conventional interval fingering
        // when there is not enough phrase context to optimize physically.
        if(length<3){group=chainEnd;continue;}
        QVector<QVector<double>> costs(length,QVector<double>(6,infinity));
        QVector<QVector<int>> parents(length,QVector<int>(6,0));
        for(int position=0;position<length;++position){
            const int noteIndex=order[groups[group+position].begin];
            const int originalFinger=notes[noteIndex].finger;
            for(int finger=1;finger<=5;++finger){
                if(authored[noteIndex]&&finger!=originalFinger)continue;
                // The physical search refines a conventional phrase/chord
                // proposal rather than discarding it. PianoPlayer's raw
                // velocity minimum can otherwise prefer unusual but merely
                // fast shapes (for example 5-2-1 over 5-3-1). This uniform
                // prior applies to every score and both hands.
                const double notationPrior=authored[noteIndex]?0.0:2.0*qAbs(finger-originalFinger);
                if(position==0){
                    // Place the relaxed hand around the centre of the next
                    // five attacks. This is the physical-model equivalent of
                    // looking ahead before choosing the first finger.
                    QVector<int> window;
                    for(int look=0;look<qMin(5,length);++look)window.append(notes[order[groups[group+look].begin]].note);
                    std::sort(window.begin(),window.end());
                    static constexpr double rest[6]={0.0,-7.0*.82,-2.8*.82,0.0,2.8*.82,5.6*.82};
                    const double direction=hand==Hand::Right?1.0:-1.0;
                    const double noteX=direction*notes[noteIndex].note*(16.5/12.0);
                    const double handCentre=noteX+(rest[3]-rest[finger]);
                    const double phraseCentre=direction*window[window.size()/2]*(16.5/12.0);
                    costs[position][finger]=qAbs(handCentre-phraseCentre)+notationPrior;
                    continue;
                }
                const int previousIndex=order[groups[group+position-1].begin];
                for(int previousFinger=1;previousFinger<=5;++previousFinger){
                    if(costs[position-1][previousFinger]>=infinity)continue;
                    const double candidate=costs[position-1][previousFinger]
                        +physicalTransitionCost(notes[previousIndex],previousFinger,notes[noteIndex],finger,hand)+notationPrior;
                    if(candidate<costs[position][finger]){costs[position][finger]=candidate;parents[position][finger]=previousFinger;}
                }
            }
        }
        int finger=1;
        for(int candidate=2;candidate<=5;++candidate)if(costs.last()[candidate]<costs.last()[finger])finger=candidate;
        for(int position=length-1;position>=0;--position){
            const int noteIndex=order[groups[group+position].begin];
            notes[noteIndex].finger=finger;
            if(position>0)finger=parents[position][finger];
        }
        group=chainEnd;
    }
}
QVector<int> recognizedChordFingers(const QVector<int> &sourcePitches,Hand hand){
    QVector<int> pitches=sourcePitches;std::sort(pitches.begin(),pitches.end());
    pitches.erase(std::unique(pitches.begin(),pitches.end()),pitches.end());
    QVector<int> pitchClasses;for(int pitch:pitches)if(!pitchClasses.contains((pitch%12+12)%12))pitchClasses.append((pitch%12+12)%12);
    if(pitchClasses.size()!=3&&pitchClasses.size()!=4)return {};
    const QVector<QVector<int>> triads{{0,4,7},{0,3,7},{0,3,6},{0,4,8}};
    const QVector<QVector<int>> sevenths{{0,4,7,10},{0,4,7,11},{0,3,7,10},{0,3,6,10},{0,3,6,9}};
    const auto &patterns=pitchClasses.size()==3?triads:sevenths;
    int root=-1,inversion=0;
    for(int candidate:pitchClasses){
        for(const auto &pattern:patterns){
            bool matches=true;for(int interval:pattern)matches&=pitchClasses.contains((candidate+interval)%12);
            if(!matches)continue;
            root=candidate;inversion=pattern.indexOf(((pitches.first()%12+12)%12-root+12)%12);break;
        }
        if(root>=0)break;
    }
    if(root<0||inversion<0||pitches.size()!=pitchClasses.size())return {};
    if(pitches.size()==3){
        if(hand==Hand::Right)return inversion==1?QVector<int>{1,2,5}:QVector<int>{1,3,5};
        return inversion==2?QVector<int>{5,2,1}:QVector<int>{5,3,1};
    }
    if(hand==Hand::Right)return inversion==0?QVector<int>{1,2,3,5}:QVector<int>{1,2,4,5};
    return inversion==0?QVector<int>{5,3,2,1}:QVector<int>{5,4,2,1};
}
QVector<NoteEvent> withSuggestedFingering(QVector<NoteEvent> notes){
    // Preserve authored fingering, but fill missing values so the optional aid
    // also works for imported MIDI/MusicXML and notes captured in Free Play.
    // Chords get a conventional outside-to-outside hand shape; melodic notes
    // continue by diatonic distance and cross the thumb when the hand runs out.
    QVector<bool> authored;authored.reserve(notes.size());for(const auto &note:notes)authored.append(note.finger>=1&&note.finger<=5);
    for(const Hand hand:{Hand::Right,Hand::Left}){
        QVector<int> order;for(int index=0;index<notes.size();++index)if(notes[index].hand==hand)order.append(index);
        std::sort(order.begin(),order.end(),[&](int a,int b){return qFuzzyCompare(notes[a].start+1.0,notes[b].start+1.0)?notes[a].note<notes[b].note:notes[a].start<notes[b].start;});
        QMap<int,int> previousChordFingers;QVector<int> previousChordPitches;double previousChordStart=-1000.0;
        for(int cursor=0;cursor<order.size();){
            int end=cursor+1;while(end<order.size()&&qAbs(notes[order[end]].start-notes[order[cursor]].start)<.015)++end;
            const int count=end-cursor;
            if(count>1){
                static const QVector<QVector<int>> right{{},{1},{1,5},{1,3,5},{1,2,3,5},{1,2,3,4,5}};
                static const QVector<QVector<int>> left{{},{5},{5,1},{5,3,1},{5,3,2,1},{5,4,3,2,1}};
                QVector<int> pitches;for(int offset=0;offset<count;++offset)if(!pitches.contains(notes[order[cursor+offset]].note))pitches.append(notes[order[cursor+offset]].note);std::sort(pitches.begin(),pitches.end());
                const int uniqueCount=pitches.size();QVector<int> shape=recognizedChordFingers(pitches,hand);
                if(shape.isEmpty())shape=hand==Hand::Right?right[qMin(5,uniqueCount)]:left[qMin(5,uniqueCount)];
                // A dyad is not automatically a full 1-5 stretch. Size it by
                // its actual interval, then retain a shared finger from the
                // immediately preceding chord when possible. For example,
                // RH C-G followed by E-G remains 1-5 then 3-5 instead of
                // needlessly moving E onto the thumb.
                if(uniqueCount==2){
                    const int semitones=qAbs(pitches[1]-pitches[0]);
                    const int distance=semitones<=2?1:semitones<=4?2:semitones<=5?3:4;
                    shape=hand==Hand::Right?QVector<int>{1,1+distance}:QVector<int>{5,5-distance};
                    if(notes[order[cursor]].start-previousChordStart<=1.25&&previousChordPitches.size()==2){
                        QVector<int> harmonicContext=previousChordPitches;
                        for(int pitch:pitches)if(!harmonicContext.contains(pitch))harmonicContext.append(pitch);
                        std::sort(harmonicContext.begin(),harmonicContext.end());
                        const auto contextualShape=recognizedChordFingers(harmonicContext,hand);
                        if(!contextualShape.isEmpty()){
                            QVector<int> contextualDyad;
                            for(int pitch:pitches)contextualDyad.append(contextualShape[harmonicContext.indexOf(pitch)]);
                            if(contextualDyad.size()==2)shape=contextualDyad;
                        }
                        const bool lowerShared=previousChordFingers.contains(pitches[0]);
                        const bool upperShared=previousChordFingers.contains(pitches[1]);
                        const int previousSpan=qAbs(previousChordPitches[1]-previousChordPitches[0]);
                        const int lowerMotion=pitches[0]-previousChordPitches[0];
                        const int upperMotion=pitches[1]-previousChordPitches[1];
                        const bool parallelNeighbors=!lowerShared&&!upperShared
                            &&qAbs(previousSpan-semitones)<=1&&qAbs(lowerMotion)<=2
                            &&qAbs(upperMotion)<=2&&lowerMotion*upperMotion>0;
                        if(parallelNeighbors){
                            const int shift=(lowerMotion>0?1:-1)*(hand==Hand::Right?1:-1);
                            const int lower=previousChordFingers.value(previousChordPitches[0])+shift;
                            const int upper=previousChordFingers.value(previousChordPitches[1])+shift;
                            if(lower>=1&&lower<=5&&upper>=1&&upper<=5)shape={lower,upper};
                        }else if(lowerShared&&upperShared){
                            const int lower=previousChordFingers.value(pitches[0]),upper=previousChordFingers.value(pitches[1]);
                            if((hand==Hand::Right&&lower<upper)||(hand==Hand::Left&&lower>upper))shape={lower,upper};
                        }else if(upperShared){
                            const int upper=previousChordFingers.value(pitches[1]);
                            const int lower=upper+(hand==Hand::Right?-distance:distance);
                            if(lower>=1&&lower<=5)shape={lower,upper};
                        }else if(lowerShared){
                            const int lower=previousChordFingers.value(pitches[0]);
                            const int upper=lower+(hand==Hand::Right?distance:-distance);
                            if(upper>=1&&upper<=5)shape={lower,upper};
                        }
                    }
                }
                for(int offset=0;offset<count;++offset)if(notes[order[cursor+offset]].finger==0){
                    const int pitchIndex=pitches.indexOf(notes[order[cursor+offset]].note);
                    int authored=0;for(int peer=cursor;peer<end;++peer)if(notes[order[peer]].note==notes[order[cursor+offset]].note&&notes[order[peer]].finger>0){authored=notes[order[peer]].finger;break;}
                    notes[order[cursor+offset]].finger=authored>0?authored:(uniqueCount<=5?shape[pitchIndex]:qBound(1,hand==Hand::Right?1+qRound(pitchIndex*4.0/(uniqueCount-1)):5-qRound(pitchIndex*4.0/(uniqueCount-1)),5));
                }
                previousChordFingers.clear();
                for(int offset=0;offset<count;++offset)previousChordFingers[notes[order[cursor+offset]].note]=notes[order[cursor+offset]].finger;
                previousChordPitches=pitches;
                previousChordStart=notes[order[cursor]].start;
            }
            cursor=end;
        }
        // Optimize each monophonic phrase as a whole. Chords form phrase
        // boundaries because their hand shape was assigned above.
        QVector<int> run;
        for(int cursor=0;cursor<order.size();){
            int end=cursor+1;while(end<order.size()&&qAbs(notes[order[end]].start-notes[order[cursor]].start)<.015)++end;
            if(end-cursor>1){fillMelodicFingering(notes,run,hand);run.clear();}
            else{
                if(!run.isEmpty()){
                    const auto &previous=notes[run.last()];const auto &current=notes[order[cursor]];
                    const int registerSpan=qAbs(current.note-previous.note);
                    // Keep an exact octave together so it receives the normal
                    // outside-finger 1/5 reach. Other leaps larger than a fifth
                    // start a fresh hand position.
                    const bool registerLeap=registerSpan>7&&registerSpan!=12;
                    const bool phraseGap=current.start-previous.start>qMax(1.25,previous.duration+.75);
                    if(registerLeap||phraseGap){fillMelodicFingering(notes,run,hand);run.clear();}
                }
                run.append(order[cursor]);
            }
            cursor=end;
        }
        fillMelodicFingering(notes,run,hand);
    }
    repairHandFingering(notes,authored,Hand::Right);
    repairHandFingering(notes,authored,Hand::Left);
    return notes;
}
}

ScoreWidget::ScoreWidget(QWidget *parent):QWidget(parent){setAttribute(Qt::WA_OpaquePaintEvent);setMinimumHeight(190);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
void ScoreWidget::setNotes(const QVector<NoteEvent>&n){m_viewStartOverride=-1;m_notes=withSuggestedFingering(n);updateGeometry();update();emit frameHeightHintChanged(recommendedFrameHeight());}
void ScoreWidget::setExpected(const QSet<int>&n,double start){m_expected=n;m_groupStart=start;update();}
void ScoreWidget::setPlayedBeat(double b){m_playedBeat=b;update();}
void ScoreWidget::setPlaybackCursorVisible(bool visible){if(m_playbackCursorVisible==visible)return;m_playbackCursorVisible=visible;update();}
void ScoreWidget::setPlaybackWaiting(double beat){
    const double next=std::isfinite(beat)&&beat>=0.0?beat:-1.0;
    if(m_playbackWaitingBeat==next&&(next<0.0||m_playbackCursorVisible))return;
    m_playbackWaitingBeat=next;
    if(next>=0.0)m_playbackCursorVisible=true;
    update();
}
void ScoreWidget::setFollowBeat(double b){
    const double next=qMax(0.0,b);
    if(m_viewStartOverride>=0){
        const auto visible=viewRange();
        // Drill selection changes the expected note, but an expected note
        // already on the displayed page must not become a new left edge.
        // Release the anchor only when following genuinely leaves the page.
        if(next<visible.first-.001||next>=visible.first+visible.second-.001)m_viewStartOverride=-1;
    }
    m_followBeat=next;update();
}
void ScoreWidget::setLiveWritingMode(bool live){m_liveWriting=live;update();}
void ScoreWidget::setEditEnabled(bool enabled){m_editEnabled=enabled;if(!enabled)m_editCursorBeat=-1;setCursor(enabled?Qt::CrossCursor:Qt::ArrowCursor);update();}
void ScoreWidget::setSeekEnabled(bool enabled){m_seekEnabled=enabled;m_seekDragging=false;m_seekPreviewBeat=-1;if(!m_editEnabled)setCursor(enabled?Qt::SizeHorCursor:Qt::ArrowCursor);update();}
void ScoreWidget::setEditCursorBeat(double beat){m_editCursorBeat=beat;update();}
void ScoreWidget::setTimeSignatures(const QVector<TimeSignatureEvent>&signatures){m_timeSignatures=signatures;std::sort(m_timeSignatures.begin(),m_timeSignatures.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});update();}
void ScoreWidget::setKeySignatures(const QVector<KeySignatureEvent>&signatures){m_keySignatures=signatures;std::sort(m_keySignatures.begin(),m_keySignatures.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});update();}
QString ScoreWidget::noteNameForBeat(int midi,double beat,AccidentalMark accidental)const{return MusicDocument::spelledNoteName(midi,fifthsAt(m_keySignatures,beat),accidental);}
void ScoreWidget::setRests(const QVector<StaffRestEvent>&rests){m_rests=rests;std::sort(m_rests.begin(),m_rests.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});update();}
void ScoreWidget::setRepeatEnds(const QVector<double>&beats){m_repeatEnds=beats;std::sort(m_repeatEnds.begin(),m_repeatEnds.end());update();}
void ScoreWidget::setPedalEvents(const QVector<PedalEvent>&events){m_pedalEvents=events;std::sort(m_pedalEvents.begin(),m_pedalEvents.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});update();}
void ScoreWidget::setExpectedPedal(int state,double beat){m_expectedPedal=state;m_expectedPedalBeat=beat;update();}
void ScoreWidget::setShowNames(bool s){m_showNames=s;updateGeometry();update();emit frameHeightHintChanged(recommendedFrameHeight());}
void ScoreWidget::setShowFingering(bool show){m_showFingering=show;updateGeometry();update();emit frameHeightHintChanged(recommendedFrameHeight());}
void ScoreWidget::setLessonOpeningInset(bool enabled){m_lessonOpeningInset=enabled;update();}
void ScoreWidget::setHandFilter(int f){m_handFilter=f;updateGeometry();update();emit frameHeightHintChanged(recommendedFrameHeight());}
void ScoreWidget::setDrillRange(double startBeat,double endBeat){
    m_drillRanges.clear();if(startBeat>=0&&endBeat>startBeat)m_drillRanges.append({startBeat,endBeat});
    update();
}
void ScoreWidget::setDrillRanges(const QVector<QPair<double,double>>&ranges){
    m_drillRanges.clear();for(const auto &range:ranges)if(range.first>=0&&range.second>range.first)m_drillRanges.append(range);
    std::sort(m_drillRanges.begin(),m_drillRanges.end(),[](const auto&a,const auto&b){return a.first<b.first;});
    update();
}
QPair<double,double> ScoreWidget::measureRangeAt(double beat)const{return measureAt(m_timeSignatures,qMax(0.0,beat));}
double ScoreWidget::measureEndAt(double beat)const{const auto measure=measureRangeAt(beat);return measure.first+measure.second;}
int ScoreWidget::measureNumberAt(double beat)const{return measureOrdinalAt(m_timeSignatures,qMax(0.0,beat));}
double ScoreWidget::viewStartBeat()const{return viewRange().first;}
double ScoreWidget::viewWindowBeats()const{return viewRange().second;}
void ScoreWidget::holdViewStart(double beat){m_viewStartOverride=qMax(0.0,beat);update();}
void ScoreWidget::paintEvent(QPaintEvent*){QPainter p(this);paintScore(p,rect());}

QPair<double,double> ScoreWidget::viewRange() const {
    double maxBeat=0;for(const auto &note:m_notes)if(included(note,m_handFilter))maxBeat=qMax(maxBeat,note.start+note.duration);for(const auto &rest:m_rests)if(m_handFilter<0||(m_handFilter==0&&rest.hand==Hand::Left)||(m_handFilter==1&&rest.hand==Hand::Right))maxBeat=qMax(maxBeat,rest.beat+rest.duration);for(const auto &signature:m_timeSignatures)maxBeat=qMax(maxBeat,signature.beat);for(const auto &signature:m_keySignatures)maxBeat=qMax(maxBeat,signature.beat);for(double beat:m_repeatEnds)maxBeat=qMax(maxBeat,beat);for(const auto &pedal:m_pedalEvents)maxBeat=qMax(maxBeat,pedal.beat);
    const double requestedFollow=m_seekDragging&&m_seekPreviewBeat>=0?m_seekPreviewBeat:m_followBeat;
    const double followed=m_liveWriting?qMax(0.0,requestedFollow):qBound(0.0,requestedFollow,maxBeat);
    double startBeat=0,window=6.0;
    if(m_liveWriting){startBeat=qMax(0.0,followed-window*.68);startBeat=std::floor(startBeat*8.0)/8.0;}
    else{
        // Page by whole measures: the current bar owns the left slot and the
        // next bar is the preview on the right. Crossing a barline replaces
        // the left slot instead of continuously sliding beneath the clefs.
        const double pageBeat=m_viewStartOverride>=0?m_viewStartOverride:followed;
        const auto measure=measureAt(m_timeSignatures,pageBeat);startBeat=measure.first;
        // Fill a wide score with additional complete measures while retaining
        // enough horizontal room for notes, labels, and fingering. Resizing or
        // maximizing changes the number of visible measures, not the clef or
        // staff scale.
        int visibleMeasures=qBound(2,qMax(1,width())/520,5);
        // Page density follows the actual engraving load. A measure with a
        // fast run, accidentals, labels, and fingering cannot share the same
        // width as a sparse whole-note measure and remain readable. Reduce
        // only the number of measures on this page; never shrink the clefs,
        // noteheads, or teaching aids.
        constexpr double usableNotationWidth=750.0,minAttackSpacing=34.0;
        while(visibleMeasures>1){
            const int attacksPerMeasure=qMax(1,int(std::floor((usableNotationWidth/visibleMeasures)/minAttackSpacing)));
            bool crowded=false;
            for(int slot=0;slot<visibleMeasures&&!crowded;++slot){
                const double slotStart=measure.first+slot*measure.second,slotEnd=slotStart+measure.second;
                QSet<qint64> leftAttacks,rightAttacks;
                for(const auto &note:m_notes){
                    if(!included(note,m_handFilter)||note.start<slotStart-.001||note.start>=slotEnd-.001)continue;
                    (note.hand==Hand::Left?leftAttacks:rightAttacks).insert(qRound64(note.start*1000.0));
                }
                crowded=qMax(leftAttacks.size(),rightAttacks.size())>attacksPerMeasure;
            }
            if(!crowded)break;
            --visibleMeasures;
        }
        window=measure.second*visibleMeasures;
    }
    return {startBeat,window};
}

QRectF ScoreWidget::canvasViewport(const QRectF &) const {
    // Permanent A0-C8 viewport. Its bounds never depend on the current notes,
    // measure, hand filter, fingering, or playback position, so the score can
    // never zoom when the music moves into another register.
    if(m_showFingering||m_showNames){
        const auto sizes=maximumFingeringChordSizes(m_notes,m_handFilter);
        // Low ledger notes and their stems can push the common LH fingering
        // lane below its nominal 320px position (and high RH notation can do
        // the inverse). Once a vertical chord stack is needed, expose the
        // complete safe canvas instead of estimating from finger count alone.
        int rightStack=qMax(1,sizes.first),leftStack=qMax(1,sizes.second);
        if(m_showNames){QVector<NoteEvent> ordered;for(const auto &note:m_notes)if(included(note,m_handFilter))ordered.append(note);std::sort(ordered.begin(),ordered.end(),[](const auto&a,const auto&b){return a.hand==b.hand?(qAbs(a.start-b.start)<.001?a.note<b.note:a.start<b.start):int(a.hand)<int(b.hand);});for(int first=0;first<ordered.size();){int end=first+1;while(end<ordered.size()&&ordered[end].hand==ordered[first].hand&&qAbs(ordered[end].start-ordered[first].start)<=.04)++end;const int count=end-first;if(ordered[first].hand==Hand::Right)rightStack=qMax(rightStack,count);else leftStack=qMax(leftStack,count);first=end;}}
        if(rightStack>1||leftStack>1){
            // Chord fingers and name bubbles stack away from their staff. The
            // old full-canvas fallback still began at y=0, so a high treble
            // stack could be drawn at a negative y and disappear. Reserve
            // explicit overflow on the side where each hand's stack grows.
            const double top=rightStack>1?-((rightStack-1)*16.0+24.0):0.0;
            const double bottom=scoreCanvasHeight+(leftStack>1?(leftStack-1)*16.0+24.0:0.0);
            return QRectF(0.0,top,scoreCanvasWidth,bottom-top);
        }
    }
    bool octaveAbove=false,octaveBelow=false;
    for(const auto &note:m_notes){
        if(!included(note,m_handFilter))continue;
        const int displacement=staffPlacement(note,fifthsAt(m_keySignatures,note.start),scoreVerticalOffset).octaveDisplacement;
        octaveAbove|=displacement>0;octaveBelow|=displacement<0;
    }
    // Keep the normal panel extremely tight, but expose the small outer lane
    // whenever 8va/8vb/15ma/15mb is actually present. Previously the glyphs
    // were drawn correctly and then cropped by the compact frame—most visibly
    // the 8vb beneath A0/Bb0 in Free Play.
    const double top=octaveAbove?4.0:45.0;
    double bottom=!m_pedalEvents.isEmpty()?370.0:343.0;
    if(octaveBelow)bottom=qMax(bottom,380.0);
    return QRectF(0.0,top,scoreCanvasWidth,bottom-top);
}

int ScoreWidget::recommendedFrameHeight() const {
    const double sourceHeight=canvasViewport(QRectF()).height();
    return qMax(212,qCeil(212.0*sourceHeight/298.0));
}

QPointF ScoreWidget::widgetToCanvas(const QPointF &point) const {
    const QRectF target=rect();const QRectF source=canvasViewport(target);
    // Cropping unused canvas makes the panel more compact; it must not make
    // the notation or either clef grow to fill a wide window.
    const double fit=qMin(1.13,qMin(target.width()/source.width(),target.height()/source.height()));
    if(fit<=0)return {};
    const double insetX=(target.width()-source.width()*fit)*.5,insetY=(target.height()-source.height()*fit)*.5;
    return QPointF((point.x()-insetX)/fit+source.left(),(point.y()-insetY)/fit+source.top());
}

void ScoreWidget::paintScore(QPainter &p,const QRectF &target) const {
    p.save();p.setRenderHint(QPainter::Antialiasing);p.fillRect(target,QColor("#fffefa"));
    // Reserve generous ledger-line space below the bass staff and above the
    // treble staff. Dense piano arrangements regularly extend far beyond the
    // five lines; a 310-unit canvas clipped those legitimate low notes.
    // A0 through C8, including ledger lines, stems, flags, and accidentals,
    // must fit inside the existing score panel. The larger internal canvas is
    // fitted into the same widget, so the surrounding KDE layout does not
    // grow or crop the extreme ends of an 88-key piano.
    constexpr double verticalOffset=scoreVerticalOffset,canvasWidth=scoreCanvasWidth,canvasHeight=scoreCanvasHeight;
    // Musical glyphs must retain their proportions.  The old independent X/Y
    // scaling made treble and bass clefs look warped whenever the score card
    // was made taller. Fit one undistorted canvas and center the spare space.
    const QRectF source=canvasViewport(target);
    // Match the established clef size from the former score panel. The score
    // can shrink for a small window, but never enlarges merely because unused
    // vertical canvas was removed.
    const double scaleToTarget=qMin(1.13,qMin(target.width()/source.width(),target.height()/source.height()));
    const double insetX=(target.width()-source.width()*scaleToTarget)*.5,insetY=(target.height()-source.height()*scaleToTarget)*.5;
    p.translate(target.left()+insetX,target.top()+insetY);p.scale(scaleToTarget,scaleToTarget);p.translate(-source.left(),-source.top());
    constexpr double staffLeft=104.0,staffRight=1040.0,headerX=174.0;
    p.setPen(QPen(QColor("#879691"),1.8));for(int s:{70,80,90,100,110,160,170,180,190,200})p.drawLine(QPointF(staffLeft,s+verticalOffset),QPointF(staffRight,s+verticalOffset));
    p.setPen(QPen(QColor("#29433d"),3));p.drawLine(QPointF(staffLeft,70+verticalOffset),QPointF(staffLeft,200+verticalOffset));QPainterPath brace;brace.moveTo(96,70+verticalOffset);brace.cubicTo(80,85+verticalOffset,97,115+verticalOffset,81,134+verticalOffset);brace.cubicTo(97,151+verticalOffset,81,184+verticalOffset,96,200+verticalOffset);p.drawPath(brace);
    // Draw clefs in explicit boxes wholly to the right of the opening
    // barline. Point-baseline drawing let the font's negative left bearing
    // leak outside the measure, especially for the treble clef.
    auto drawClef=[&](const QString &glyph,const QRectF &box,const QPointF &musicalAnchor,double registeredScale){
        QFont font("Noto Music",90);if(!QFontInfo(font).exactMatch())font=QFont("serif",90);
        QPainterPath outline;outline.addText(QPointF(0,0),font,glyph);const QRectF bounds=outline.boundingRect();
        if(bounds.isEmpty())return;
        const double fit=qMin(box.width()/bounds.width(),box.height()/bounds.height());
        QTransform transform;transform.translate(box.center().x(),box.center().y());transform.scale(fit,fit);transform.translate(-bounds.center().x(),-bounds.center().y());
        QTransform registration;registration.translate(musicalAnchor.x(),musicalAnchor.y());registration.scale(registeredScale,registeredScale);registration.translate(-musicalAnchor.x(),-musicalAnchor.y());
        p.setPen(Qt::NoPen);p.setBrush(QColor("#173f39"));p.drawPath(registration.map(transform.map(outline)));
    };
    // One vector path per clef avoids the split/duplicated fragments produced
    // by rectangle-based supplementary-plane text layout on some KDE fonts.
    // These boxes were originally tuned with a 30-unit canvas margin. Move
    // them only by the additional margin, not by the full offset, so each
    // clef's musical anchor remains on its staff.
    constexpr double clefShift=verticalOffset-30.0;
    // G clef: the spiral is registered on G4, the second treble line from the
    // bottom. Scaling around that musical anchor keeps font bounds from
    // moving it away from its line.
    drawClef(QString::fromUtf8("𝄞"),QRectF(110,66+clefShift,58,106),QPointF(139,100+verticalOffset),.78);
    // Register the engraved F-clef glyph to musical positions, not its font
    // baseline. The fitted box keeps its crown at A3 and tail at A2; the
    // glyph's own two dots then straddle F3 in the G3 and E3 spaces.
    const double upperA=160+verticalOffset;
    const double lowerA=195+verticalOffset;
    constexpr double bassCrownInset=1.5;
    constexpr double bassTailInset=1.0;
    drawClef(QString::fromUtf8("𝄢"),QRectF(112,upperA+bassCrownInset,56,lowerA-upperA-bassCrownInset-bassTailInset),QPointF(140,170+verticalOffset),1.0);
    p.setFont(QFont("Manrope",9,QFont::DemiBold));p.setPen(QColor("#71817c"));p.drawText(QPointF(18,92+verticalOffset),"RIGHT");p.drawText(QPointF(25,182+verticalOffset),"LEFT");
    QVector<NoteEvent> visible;for(const auto&n:m_notes)if(included(n,m_handFilter))visible<<n;
    double maxBeat=0;for(const auto&n:visible)maxBeat=qMax(maxBeat,n.start+n.duration);
    const auto range=viewRange();const double startBeat=range.first,window=range.second;
    const double followed=m_liveWriting?qMax(0.0,m_followBeat):qBound(0.0,m_followBeat,maxBeat);
    // m_followBeat also anchors whichever historical page the user is viewing.
    // During playback the orange cursor instead belongs to m_playedBeat, which
    // is driven by the audio-output clock. This prevents scrolling to an older
    // measure from moving the playback line away from the sound it represents.
    const bool playbackWaiting=!m_editEnabled&&m_playbackWaitingBeat>=0.0;
    const double cursorBeat=playbackWaiting?qBound(0.0,m_playbackWaitingBeat,maxBeat)
        :(!m_editEnabled&&m_playedBeat>=0.0)?qBound(0.0,m_playedBeat,maxBeat):followed;
    // Beat zero must begin after the clef, key signature, and time signature.
    // Keeping those symbols in a dedicated header also prevents the first
    // recorded note from looking as though it belongs before the meter.
    const double musicLeft=225.0+qAbs(fifthsAt(m_keySignatures,startBeat))*8.0;
    // Daily lessons and Free Play need a short pickup lane between the fixed
    // header and beat zero. In Free Play this is essential for a first-note
    // accidental: without it, the sharp/flat sits left of the notation clip
    // and appears missing. Imported playback scores retain their established
    // page density.
    const double notationLeft=musicLeft+((m_lessonOpeningInset||m_liveWriting)?24.0:0.0);
    const double musicRight=1000.0;
    const double scale=(musicRight-notationLeft)/window;
    // A barline marks the boundary immediately before its downbeat; it must
    // not occupy the same horizontal ink as a notehead on that beat.
    constexpr double barlineLead=18.0;
    for(const auto &selectedRange:m_drillRanges){
        const double shownStart=qMax(startBeat,selectedRange.first),shownEnd=qMin(startBeat+window,selectedRange.second);
        if(shownEnd>shownStart+.001){
            const double left=qAbs(shownStart-startBeat)<.001?musicLeft-barlineLead:notationLeft+(shownStart-startBeat)*scale-barlineLead;
            const double right=notationLeft+(shownEnd-startBeat)*scale-barlineLead;
            const QRectF drillHighlight(left,70+verticalOffset,qMax(18.0,right-left),130);
            // A drill is a highlighted span of music, not another box around
            // the staff. Use a borderless translucent wash between its
            // barlines so notes, fingering, ledger lines, and clefs retain
            // their normal visual hierarchy.
            QLinearGradient wash(drillHighlight.topLeft(),drillHighlight.topRight());
            wash.setColorAt(0.0,QColor(239,119,95,18));
            wash.setColorAt(0.08,QColor(239,119,95,34));
            wash.setColorAt(0.92,QColor(239,119,95,34));
            wash.setColorAt(1.0,QColor(239,119,95,18));
            p.fillRect(drillHighlight,wash);
        }
    }
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
            const double beatX=notationLeft+(barBeat-startBeat)*scale;
            const double x=viewportStart?musicLeft-barlineLead:beatX-barlineLead;
            p.setPen(QPen(QColor("#71817c"),2.2));p.drawLine(QPointF(x,70+verticalOffset),QPointF(x,200+verticalOffset));
            p.setPen(QColor("#52645f"));p.setFont(QFont("DM Mono",10,QFont::Bold));
            // At the viewport opening the header occupies the left side, so
            // its measure number sits just to the right. Internal labels sit
            // before their barline, away from the downbeat's note-name and
            // fingering columns.
            // Measure numbers need their own guide row. The previous
            // y=43+verticalOffset position was exactly the treble note-name
            // row, so M1/M2 could be painted under a C4/D4 bubble. Keep them
            // above both the fingering and note-name aids instead.
            constexpr double measureLabelTop=48.0;
            const QRectF measureLabel=viewportStart
                ?QRectF(x+4,measureLabelTop,32,17)
                :QRectF(x-34,measureLabelTop,30,17);
            p.drawText(measureLabel,viewportStart?Qt::AlignLeft:Qt::AlignRight,QString("M%1").arg(barNumber));
        }
    }
    QVector<TimeSignatureEvent> shownSignatures=signatures;if(startBeat>.001){TimeSignatureEvent active=signatures.first();for(const auto &signature:signatures){if(signature.beat>startBeat+.001)break;active=signature;}if(qAbs(active.beat-startBeat)>.04)shownSignatures.append({startBeat,active.beats,active.beatType});std::sort(shownSignatures.begin(),shownSignatures.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});}
    for(const auto &signature:shownSignatures){
        if(signature.beat<startBeat-.001||signature.beat>startBeat+window+.001)continue;
        const double x=qAbs(signature.beat-startBeat)<.04?headerX:notationLeft+(signature.beat-startBeat)*scale;
        const int keyFifths=fifthsAt(m_keySignatures,signature.beat);
        const double keyWidth=qAbs(keyFifths)*8.0;
        p.setPen(QColor("#173f39"));p.setFont(QFont("Noto Serif",18,QFont::Bold));
        // Each meter fills its own five-line staff: numerator in the upper
        // half and denominator in the lower half, divided at the middle line.
        // The former centres were 15 units too low after the canvas expansion.
        for(double center:{90.0+verticalOffset,180.0+verticalOffset}){
            p.drawText(QRectF(x+8+keyWidth,center-21,24,21),Qt::AlignCenter,QString::number(signature.beats));
            p.drawText(QRectF(x+8+keyWidth,center,24,21),Qt::AlignCenter,QString::number(signature.beatType));
        }
    }
    const QVector<int> trebleSharps{77,72,79,74,69,76,71},trebleFlats{71,76,69,74,67,72,65};
    const QVector<int> bassSharps{53,48,55,50,45,52,47},bassFlats{47,52,45,50,43,48,41};
    QVector<KeySignatureEvent> shownKeys=m_keySignatures;if(shownKeys.isEmpty())shownKeys.append({0.0,0});if(startBeat>.001){KeySignatureEvent active=shownKeys.first();for(const auto &key:shownKeys){if(key.beat>startBeat+.001)break;active=key;}if(qAbs(active.beat-startBeat)>.04)shownKeys.append({startBeat,active.fifths});std::sort(shownKeys.begin(),shownKeys.end(),[](const auto&a,const auto&b){return a.beat<b.beat;});}
    for(const auto &key:shownKeys){
        if(key.beat<startBeat-.001||key.beat>startBeat+window+.001)continue;
        const double x=qAbs(key.beat-startBeat)<.04?headerX:notationLeft+(key.beat-startBeat)*scale;p.setPen(QColor("#173f39"));p.setFont(QFont("Noto Music",22));
        const bool sharp=key.fifths>0;const int count=qMin(7,qAbs(key.fifths));const auto &treble=sharp?trebleSharps:trebleFlats;const auto &bass=sharp?bassSharps:bassFlats;
        for(int index=0;index<count;++index){
            const double sx=x+5+index*8,ty=120-(diatonicIndex(treble[index])-diatonicIndex(60))*5+verticalOffset;
            const double by=150-(diatonicIndex(bass[index])-diatonicIndex(60))*5+verticalOffset;
            p.drawText(QPointF(sx,ty+6),sharp?QString::fromUtf8("♯"):QString::fromUtf8("♭"));p.drawText(QPointF(sx,by+6),sharp?QString::fromUtf8("♯"):QString::fromUtf8("♭"));
        }
    }
    for(double repeatBeat:m_repeatEnds){
        if(repeatBeat<startBeat-.001||repeatBeat>startBeat+window+.001)continue;
        const double x=notationLeft+(repeatBeat-startBeat)*scale-barlineLead;
        p.setPen(QPen(QColor("#29433d"),1.5));p.drawLine(QPointF(x-4,70+verticalOffset),QPointF(x-4,200+verticalOffset));
        p.setPen(QPen(QColor("#29433d"),4));p.drawLine(QPointF(x+2,70+verticalOffset),QPointF(x+2,200+verticalOffset));
        p.setPen(Qt::NoPen);p.setBrush(QColor("#29433d"));
        for(double y:{85.0+verticalOffset,95.0+verticalOffset,175.0+verticalOffset,185.0+verticalOffset})p.drawEllipse(QPointF(x-10,y),2.2,2.2);
    }
    for(const auto &rest:m_rests){
        if(rest.beat<startBeat-.001||rest.beat>startBeat+window+.001||!(m_handFilter<0||(m_handFilter==0&&rest.hand==Hand::Left)||(m_handFilter==1&&rest.hand==Hand::Right)))continue;
        const double x=notationLeft+(rest.beat-startBeat)*scale,y=(rest.hand==Hand::Right?90.0:180.0)+verticalOffset;
        const QString symbol=rest.duration>=3.5?QString::fromUtf8("𝄻"):rest.duration>=1.75?QString::fromUtf8("𝄼"):rest.duration>=.75?QString::fromUtf8("𝄽"):QString::fromUtf8("𝄾");
        p.setPen(QColor("#263f39"));p.setFont(QFont("Noto Music",27));p.drawText(QRectF(x-15,y-22,30,44),Qt::AlignCenter,symbol);
    }
    // Sustain is performance notation, not a longer written note. Mark each
    // CC64 press with Ped. and its release with *; keep the area between them
    // open so the notation remains uncluttered at large display sizes.
    for(int index=0;index<m_pedalEvents.size();++index){
        const auto &event=m_pedalEvents[index];if(!event.down)continue;
        double release=startBeat+window;bool released=false;
        for(int next=index+1;next<m_pedalEvents.size();++next)if(!m_pedalEvents[next].down){release=m_pedalEvents[next].beat;released=true;break;}
        if(release<startBeat-.001||event.beat>startBeat+window+.001)continue;
        const auto fingerSizes=maximumFingeringChordSizes(m_notes,m_handFilter);
        const double pedalLane=m_showFingering&&fingerSizes.second>1?414.0:286.0+verticalOffset;
        const double x1=notationLeft+(event.beat-startBeat)*scale,x2=notationLeft+(release-startBeat)*scale,y=pedalLane;
        const bool pressExpected=!playbackWaiting&&m_expectedPedal==1&&qAbs(m_expectedPedalBeat-event.beat)<.02;
        const bool releaseExpected=!playbackWaiting&&m_expectedPedal==0&&qAbs(m_expectedPedalBeat-release)<.02;
        p.setPen(pressExpected||releaseExpected?QColor("#ef775f"):QColor("#29433d"));
        if(event.beat>=startBeat-.001){p.setFont(QFont("Noto Serif",13,QFont::Bold));p.drawText(QPointF(x1,y),"Ped.");if(pressExpected){p.setFont(QFont("Manrope",8,QFont::Bold));p.drawText(QPointF(x1,y+14),"PRESS");}}
        if(released&&release<=startBeat+window+.001){p.setFont(QFont("Noto Serif",16,QFont::Bold));p.drawText(QPointF(x2-3,y+2),"*");if(releaseExpected){p.setFont(QFont("Manrope",8,QFont::Bold));p.drawText(QPointF(x2-12,y+16),"RELEASE");}}
    }
    if(m_playbackCursorVisible&&cursorBeat>=startBeat&&cursorBeat<=startBeat+window){
        const double playX=notationLeft+(cursorBeat-startBeat)*scale;
        // Keep the playhead within the musical grand-staff band. The top and
        // bottom fingering guides sit outside this range, so the orange line
        // can no longer run through the finger number the player needs next.
        // The treble note-name guide ends at y=120. Start beneath it so the
        // current-beat line cannot slice through the first label (notably the
        // leading D in D4 at beat zero).
        constexpr double playLineTop=124.0,playLineBottom=272.0;
        p.fillRect(QRectF(playX-2,playLineTop,4,playLineBottom-playLineTop),QColor(239,119,95,70));
        p.setPen(QPen(QColor("#bd5945"),1.0,playbackWaiting?Qt::DashLine:Qt::SolidLine));
        p.drawLine(QPointF(playX,playLineTop),QPointF(playX,playLineBottom));
    }
    if(m_editEnabled&&m_editCursorBeat>=startBeat&&m_editCursorBeat<=startBeat+window){
        const double editX=notationLeft+(m_editCursorBeat-startBeat)*scale;p.fillRect(QRectF(editX-2.5,48,5,190),QColor("#ef775f"));
        p.setPen(QColor("#bd5945"));p.setBrush(QColor("#ef775f"));QPolygonF marker;marker<<QPointF(editX-7,48)<<QPointF(editX+7,48)<<QPointF(editX,58);p.drawPolygon(marker);
        p.setFont(QFont("Manrope",7,QFont::Bold));p.drawText(QPointF(editX+6,47),"ADD");
    }
    if(visible.isEmpty()){p.setPen(QColor("#93a09c"));p.setFont(QFont("Manrope",13));p.drawText(QRectF(150,90+verticalOffset,800,70),Qt::AlignCenter,m_editEnabled?"Click to insert notes · Right-click empty staff for rests, key, meter, or repeat":"Play or import notes to begin your score");p.restore();return;}
    // The clefs, key signature, and time signature form a fixed header while
    // notes scroll behind it. Long notes that began in the previous window
    // still need a continuation tie at the boundary, but their old notehead,
    // stem, ledger lines, and accidentals must never enter the clef column.
    // Clip only horizontally to protect the fixed clef header. Vertically,
    // allow the complete canvas so ledger notes and long stems are not cut.
    p.save();p.setClipRect(QRectF(musicLeft-14,-80,musicRight-musicLeft+28,canvasHeight+160));
    struct RenderNote {
        const NoteEvent *event = nullptr;
        double y = 0;
        double headX = 0;
        QColor color;
        int octaveDisplacement = 0;
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
        const bool expected=m_playbackCursorVisible&&!playbackWaiting&&qAbs(note.start-m_groupStart)<.04&&m_expected.contains(note.note);
        const bool played=!playbackWaiting&&m_playedBeat>=note.start&&!expected;
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
    // A performed MIDI glissando can contain a dozen attacks only a few
    // milliseconds apart. At a normal score zoom those positions are less
    // than a notehead apart, so individual stems become an unreadable wedge.
    // Keep playback events untouched, but engrave a monotonic ultra-fast run
    // as its two endpoint notes joined by a conventional glissando line.
    QVector<QPair<const NoteEvent*,const NoteEvent*>> glissandi;
    QSet<const NoteEvent*> hiddenGlissNotes;
    for(int first=0;first+5<ordered.size();){
        const NoteEvent *a=ordered[first],*b=ordered[first+1];
        const int initialStep=b->note-a->note;const int direction=initialStep>0?1:initialStep<0?-1:0;
        if(direction==0||a->hand!=b->hand||b->start-a->start>.09||a->duration>.14||b->duration>.14){++first;continue;}
        int last=first+1;
        while(last+1<ordered.size()){
            const NoteEvent *previous=ordered[last],*next=ordered[last+1];const int step=next->note-previous->note;
            if(next->hand!=a->hand||next->start-previous->start>.09||next->start<=previous->start+.001||next->duration>.14||step*direction<=0||qAbs(step)>4)break;
            ++last;
        }
        if(last-first+1>=6){glissandi.append(qMakePair(ordered[first],ordered[last]));for(int index=first+1;index<last;++index)hiddenGlissNotes.insert(ordered[index]);first=last+1;}else ++first;
    }
    if(!hiddenGlissNotes.isEmpty())ordered.erase(std::remove_if(ordered.begin(),ordered.end(),[&](const NoteEvent *note){return hiddenGlissNotes.contains(note);}),ordered.end());
    QVector<RenderChord> chords;
    constexpr double chordTolerance=.04;
    for(const NoteEvent *note:ordered){
        if(chords.isEmpty()||chords.last().hand!=note->hand||qAbs(chords.last().start-note->start)>chordTolerance){
            RenderChord chord;
            chord.hand=note->hand;chord.start=note->start;
            chord.x=notationLeft+(note->start-startBeat)*scale;
            chord.rhythm=qMax(.0625,note->duration);
            chords<<chord;
        }
        auto &chord=chords.last();
        const auto placement=staffPlacement(*note,fifthsAt(m_keySignatures,note->start),verticalOffset);
        // Multi-track MIDI files frequently contain the same key twice at
        // effectively the same instant. The synth may retain both events,
        // but printed notation has one notehead and one fingering instruction
        // for that physical key. Keeping both produced misleading stacks such
        // as "1 1 5" that looked like a nonexistent finger 11.
        auto duplicate=std::find_if(chord.notes.begin(),chord.notes.end(),[&](const RenderNote &render){return render.event->note==note->note;});
        if(duplicate==chord.notes.end())chord.notes<<RenderNote{note,placement.y,chord.x,noteColor(*note),placement.octaveDisplacement};
        else if(note->duration>duplicate->event->duration)*duplicate=RenderNote{note,placement.y,chord.x,noteColor(*note),placement.octaveDisplacement};
        chord.rhythm=std::numeric_limits<double>::max();chord.flags=0;
        for(const auto &render:chord.notes){chord.rhythm=qMin(chord.rhythm,qMax(.0625,render.event->duration));chord.flags=qMax(chord.flags,flagsForDuration(render.event->duration));}
    }
    // A continuous extreme-register phrase uses one octave level throughout.
    // Switching between 8va and 15ma note by note is not normal engraving and
    // makes both the bracket and fingering unreadable.
    for(Hand hand:{Hand::Left,Hand::Right}){
        QVector<int> run;
        auto flushOctavePhrase=[&](){
            if(run.isEmpty())return;
            int target=0;for(int chordIndex:run)for(const auto &render:chords[chordIndex].notes)
                if(qAbs(render.octaveDisplacement)>qAbs(target))target=render.octaveDisplacement;
            for(int chordIndex:run)for(auto &render:chords[chordIndex].notes){render.y+=35.0*(target-render.octaveDisplacement);render.octaveDisplacement=target;}
            run.clear();
        };
        int sign=0;double previousStart=-100;
        for(int chordIndex=0;chordIndex<chords.size();++chordIndex){
            auto &chord=chords[chordIndex];if(chord.hand!=hand)continue;
            int chordShift=0;for(const auto &render:chord.notes)if(qAbs(render.octaveDisplacement)>qAbs(chordShift))chordShift=render.octaveDisplacement;
            const int chordSign=chordShift>0?1:chordShift<0?-1:0;
            if(!chordSign){flushOctavePhrase();sign=0;continue;}
            if(!run.isEmpty()&&(chord.start-previousStart>1.01||chordSign!=sign))flushOctavePhrase();
            run.append(chordIndex);sign=chordSign;previousStart=chord.start;
        }
        flushOctavePhrase();
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
                if(previousShifted)chord.notes[ni].headX+=chord.stemUp?8.0:-8.0;
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
        chord.stemX=chord.stemUp?maxX+8:minX-8;
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

    // One consistent fingering guide per visible staff. Base each guide on
    // the outermost note/stem in this viewport, then keep every fingering on
    // that same Y coordinate. Simultaneous chord fingers spread sideways.
    double trebleFingeringY=90.0,bassFingeringY=320.0;
    double trebleNotationTop=112.0,bassNotationBottom=270.0;
    bool haveTreble=false,haveBass=false;
    for(const auto &chord:chords){
        double minY=chord.notes.first().y,maxY=minY;
        for(const auto &render:chord.notes){minY=qMin(minY,render.y);maxY=qMax(maxY,render.y);}
        if(chord.stemEndY!=0){minY=qMin(minY,chord.stemEndY);maxY=qMax(maxY,chord.stemEndY);}
        if(chord.hand==Hand::Right){const double candidate=minY-18.0;trebleFingeringY=haveTreble?qMin(trebleFingeringY,candidate):candidate;trebleNotationTop=haveTreble?qMin(trebleNotationTop,minY):minY;haveTreble=true;}
        else{const double candidate=maxY+28.0;bassFingeringY=haveBass?qMax(bassFingeringY,candidate):candidate;bassNotationBottom=haveBass?qMax(bassNotationBottom,maxY):maxY;haveBass=true;}
    }
    // Measure labels occupy the lane immediately above the treble staff.
    // Keep RH fingering in its own higher lanes so a number on beat one can
    // never merge with M2/M3 or appear clipped by the barline heading.
    trebleFingeringY=qMax(18.0,qMin(92.0,trebleFingeringY));
    // Keep the closest LH digit in a dedicated lane below the bass staff.
    // The old first-candidate assignment discarded the intended 310px floor,
    // allowing the upper digit of a dyad to land back among the staff lines.
    bassFingeringY=qMin(canvasHeight-18.0,qMax(320.0,bassFingeringY));

    // Note names use their own fixed guide rows: treble names above the
    // treble staff, bass names below the bass staff. Chord names form a
    // vertical stack while retaining pitch order. Push fingering farther out
    // when both aids are enabled so neither row can merge with the other.
    // Preserve the teaching order around the real notation bounds:
    // RH finger -> name -> note, and LH note -> name -> finger. Fixed name
    // rows collided with the second-ledger-line C used by low 8vb passages.
    const double trebleNameY=haveTreble?qMin(112.0,trebleNotationTop-14.0):112.0;
    const double bassNameY=haveBass?qMax(288.0,bassNotationBottom+18.0):288.0;
    int trebleNameStack=1,bassNameStack=1;
    if(m_showNames)for(const auto &chord:chords){const int count=chord.notes.size();if(chord.hand==Hand::Right)trebleNameStack=qMax(trebleNameStack,count);else bassNameStack=qMax(bassNameStack,count);}
    if(m_showNames&&m_showFingering){trebleFingeringY=qMin(trebleFingeringY,trebleNameY-trebleNameStack*17.0-8.0);bassFingeringY=qMax(bassFingeringY,bassNameY+bassNameStack*17.0+8.0);}

    // Fingering belongs to the attack, but very fast imported MIDI can place
    // two different attacks only a few canvas units apart. Give each attack a
    // distinct local column so adjacent 1s read as "1  1", never "11". The
    // notes and their timing stay exactly where they were engraved.
    QHash<const NoteEvent*,double> fingeringColumns;
    for(Hand hand:{Hand::Left,Hand::Right}){
        QVector<int> indexes;
        for(int index=0;index<chords.size();++index){
            if(chords[index].hand!=hand)continue;
            bool hasFinger=false;for(const auto &render:chords[index].notes)hasFinger|=render.event->finger>=1&&render.event->finger<=5;
            if(hasFinger)indexes<<index;
        }
        auto placeCluster=[&](int first,int end){
            const int count=end-first;if(count<=0)return;
            constexpr double preferredGap=18.0;
            const double available=musicRight-notationLeft-16.0;
            const double gap=count>1?qMin(preferredGap,available/(count-1)):0.0;
            const double width=gap*(count-1);
            const double centre=(chords[indexes[first]].x+chords[indexes[end-1]].x)*.5;
            const double left=qBound(notationLeft+8.0,centre-width*.5,musicRight-8.0-width);
            for(int offset=0;offset<count;++offset){
                const auto &chord=chords[indexes[first+offset]];const double column=left+offset*gap;
                for(const auto &render:chord.notes)fingeringColumns.insert(render.event,column);
            }
        };
        int first=0;
        while(first<indexes.size()){
            int end=first+1;
            while(end<indexes.size()&&chords[indexes[end]].x-chords[indexes[end-1]].x<18.0)++end;
            placeCluster(first,end);first=end;
        }
    }

    // Ledger lines sit behind the noteheads.
    for(const auto &chord:chords)for(const auto &render:chord.notes){
        const auto &note=*render.event;const double x=render.headX,y=render.y;
        const bool outsideStaff=note.hand==Hand::Right
            ?(y<=65+verticalOffset||y>=115+verticalOffset)
            :(y<=155+verticalOffset||y>=205+verticalOffset);
        if(outsideStaff){
            p.setPen(QPen(QColor("#52645f"),1.8));
            const double ledger=note.hand==Hand::Right
                ?(y<70+verticalOffset?60:120)+verticalOffset
                :(y<160+verticalOffset?150:210)+verticalOffset;
            const double step=y>ledger?10:-10;
            for(double ly=ledger;(step>0?ly<=y:ly>=y);ly+=step)p.drawLine(QPointF(x-13,ly),QPointF(x+13,ly));
        }
    }
    // Group consecutive octave-shifted notes beneath one conventional dashed
    // octave bracket instead of repeating 8va/8vb over every notehead.
    struct OctaveRun { Hand hand=Hand::Right; int displacement=0; double lastBeat=0,x1=0,x2=0,outerY=0; };
    QVector<OctaveRun> octaveRuns;
    for(Hand hand:{Hand::Left,Hand::Right})for(const auto &chord:chords){
        if(chord.hand!=hand)continue;
        QSet<int> shifts;for(const auto &render:chord.notes)if(render.octaveDisplacement)shifts.insert(render.octaveDisplacement);
        for(int shift:shifts){
            double outerY=chord.notes.first().y;for(const auto &render:chord.notes)outerY=shift>0?qMin(outerY,render.y):qMax(outerY,render.y);
            if(chord.stemEndY!=0)outerY=shift>0?qMin(outerY,chord.stemEndY):qMax(outerY,chord.stemEndY);
            if(!octaveRuns.isEmpty()&&octaveRuns.last().hand==hand&&octaveRuns.last().displacement==shift&&chord.start-octaveRuns.last().lastBeat<=1.01){octaveRuns.last().lastBeat=chord.start;octaveRuns.last().x2=chord.x+16;octaveRuns.last().outerY=shift>0?qMin(octaveRuns.last().outerY,outerY):qMax(octaveRuns.last().outerY,outerY);}
            else octaveRuns.append({hand,shift,chord.start,chord.x-11,chord.x+16,outerY});
        }
    }
    for(const auto &run:octaveRuns){
        const bool above=run.displacement>0;
        double y=above?qMax(24.0,run.outerY-42.0):qMin(canvasHeight-20.0,run.outerY+42.0);
        if(above&&run.hand==Hand::Right)y=qMax(18.0,qMin(y,trebleFingeringY-22.0));
        if(!above&&run.hand==Hand::Left)y=qMin(canvasHeight-18.0,qMax(y,bassFingeringY+22.0));
        const QString label=qAbs(run.displacement)>1?(above?"15ma":"15mb"):(above?"8va":"8vb");
        p.setPen(QColor("#52645f"));p.setFont(QFont("Noto Serif",10,QFont::DemiBold));p.drawText(QPointF(run.x1,y),label);
        const double lineY=above?y-4:y+4;
        p.setPen(QPen(QColor("#52645f"),1.3,Qt::DashLine));p.drawLine(QPointF(run.x1+28,lineY),QPointF(run.x2,lineY));
        p.setPen(QPen(QColor("#52645f"),1.3));p.drawLine(QPointF(run.x2,lineY),QPointF(run.x2,lineY+(above?8:-8)));
    }
    // Join the retained endpoints of condensed MIDI flourishes. The short
    // wavy stroke communicates every intermediate pitch without overlapping
    // noteheads and stems at an impossible horizontal scale.
    for(const auto &run:glissandi){
        const NoteEvent *first=run.first,*last=run.second;
        const double x1=notationLeft+(first->start-startBeat)*scale,x2=notationLeft+(last->start-startBeat)*scale;
        const double y1=staffPlacement(*first,fifthsAt(m_keySignatures,first->start),verticalOffset).y,y2=staffPlacement(*last,fifthsAt(m_keySignatures,last->start),verticalOffset).y;
        QPainterPath wave;const double left=x1+8,right=x2-8;wave.moveTo(left,y1);const int segments=qMax(3,int((right-left)/5.0));for(int segment=1;segment<=segments;++segment){const double amount=double(segment)/segments;wave.lineTo(left+(right-left)*amount,y1+(y2-y1)*amount+(segment%2?2.2:-2.2));}
        p.setBrush(Qt::NoBrush);p.setPen(QPen(noteColor(*first),1.4,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.drawPath(wave);
    }
    // One stem per chord; flags are used only when that chord is not beamed.
    for(const auto &chord:chords){
        if(chord.stemBaseY==0&&chord.stemEndY==0)continue;
        p.setPen(QPen(chord.stemColor,2.5));
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
                p.setPen(QPen(QBrush(gradient),6,Qt::SolidLine,Qt::FlatCap));
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
                p.setPen(QPen(chord.stemColor,6,Qt::SolidLine,Qt::FlatCap));
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
            const double previousX=notationLeft+(previousBeat-startBeat)*scale;
            const double stopX=notationLeft+(stop-startBeat)*scale;
            const bool startVisible=previousBeat>=startBeat-.001&&previousBeat<=startBeat+window+.001;
            const bool stopVisible=stop>=startBeat-.001&&stop<=startBeat+window+.001;
            const double left=qMax(notationLeft,previousX+(startVisible?8.0:0.0));
            const double right=qMin(musicRight,stopX-(stopVisible?8.0:0.0));
            if(right-left>8.0&&stop>=startBeat-.001&&previousBeat<=startBeat+window+.001){
                const double direction=chord.stemUp?1.0:-1.0;
                const double bow=qBound(6.0,(right-left)*.10,13.0);
                QPainterPath tie;tie.moveTo(left,render.y+direction*4.0);
                tie.cubicTo(left+(right-left)*.28,render.y+direction*(4.0+bow),right-(right-left)*.28,render.y+direction*(4.0+bow),right,render.y+direction*4.0);
                p.setBrush(Qt::NoBrush);p.setPen(QPen(render.color,2.2,Qt::SolidLine,Qt::RoundCap));p.drawPath(tie);
                if(stopVisible){
                    p.save();p.translate(stopX,render.y);p.rotate(-18);p.setPen(QPen(render.color,2.3));p.setBrush(QColor("#fffefa"));p.drawEllipse(QRectF(-9,-6,18,12));p.restore();
                }
            }
            previousBeat=stop;
        }
        if(m_playbackCursorVisible&&!playbackWaiting&&showSustainTie&&cursorBeat>=note.start&&cursorBeat<end&&cursorBeat>=startBeat&&cursorBeat<=startBeat+window){
            const double activeX=notationLeft+(cursorBeat-startBeat)*scale;
            p.setPen(QPen(QColor("#bd5945"),1.3));p.setBrush(QColor("#ef775f"));p.drawEllipse(QPointF(activeX,render.y),3.8,3.8);
        }
    }
    // Lay note-name helpers on clean staff-specific guide rows. They no longer
    // sit beside the notehead where a sharp/flat, stem, or neighboring label
    // can make the spelling unreadable.
    QHash<const NoteEvent*,QRectF> nameLabels;
    if(m_showNames){
        QHash<int,double> nameColumns;
        for(Hand hand:{Hand::Left,Hand::Right}){
            QVector<int> indexes;for(int index=0;index<chords.size();++index)if(chords[index].hand==hand)indexes<<index;
            for(int first=0;first<indexes.size();){
                int end=first+1;while(end<indexes.size()&&chords[indexes[end]].x-chords[indexes[end-1]].x<34.0)++end;
                const int count=end-first;constexpr double gap=32.0;const double width=gap*qMax(0,count-1);
                const double centre=(chords[indexes[first]].x+chords[indexes[end-1]].x)*.5;
                const double left=qBound(notationLeft+16.0,centre-width*.5,musicRight-16.0-width);
                for(int offset=0;offset<count;++offset)nameColumns.insert(indexes[first+offset],left+offset*gap);
                first=end;
            }
        }
        for(int chordIndex=0;chordIndex<chords.size();++chordIndex){const auto &chord=chords[chordIndex];
            QVector<const RenderNote*> ordered;for(const auto &render:chord.notes)ordered.append(&render);
            std::sort(ordered.begin(),ordered.end(),[](const auto *a,const auto *b){return a->y<b->y;});
            double width=26.0;for(const auto *render:ordered){const QString name=staffPitchName(*render->event,fifthsAt(m_keySignatures,render->event->start));width=qMax(width,8.0+name.size()*6.0);}
            // Beat zero has a dedicated lesson lead-in before it. Use that
            // space to centre the first name bubble over its note instead of
            // pinning the bubble's left edge to the orange playhead.
            const double labelX=qBound(musicLeft+2.0,nameColumns.value(chordIndex,chord.x)-width*.5,musicRight-width);
            for(int index=0;index<ordered.size();++index){const double center=chord.hand==Hand::Right?trebleNameY-(ordered.size()-1-index)*17.0:bassNameY+index*17.0;nameLabels.insert(ordered[index]->event,QRectF(labelX,center-8,width,16));}
        }
    }
    // Noteheads, augmentation dots, optional names, and optional lesson
    // fingering remain pitch-specific.
    for(const auto &chord:chords)for(const auto &render:chord.notes){
        const auto &note=*render.event;const double x=render.headX,y=render.y;const QColor color=render.color;
        AccidentalMark accidental=note.accidental;
        if(accidental==AccidentalMark::Automatic)accidental=automaticAccidental(note.note,fifthsAt(m_keySignatures,note.start));
        if(accidental!=AccidentalMark::Automatic){
            const QString symbol=accidental==AccidentalMark::Natural?QString::fromUtf8("♮"):accidental==AccidentalMark::Sharp?QString::fromUtf8("♯"):accidental==AccidentalMark::Flat?QString::fromUtf8("♭"):accidental==AccidentalMark::DoubleSharp?QString::fromUtf8("𝄪"):QString::fromUtf8("𝄫");
            QFont accidentalFont("Noto Music",22);if(!QFontInfo(accidentalFont).exactMatch())accidentalFont=QFont("serif",22);
            QPainterPath glyph;glyph.addText(QPointF(0,0),accidentalFont,symbol);const QRectF bounds=glyph.boundingRect();
            QTransform placement;placement.translate(x-22.0-bounds.center().x(),y-bounds.center().y());glyph=placement.map(glyph);
            // Fast Free Play attacks can leave less than one glyph of space
            // between consecutive noteheads. A slim paper-coloured keyline
            // separates the accidental from the preceding head, beam, ledger
            // line, or staff line without hiding the surrounding notation.
            p.setPen(QPen(QColor("#fffefa"),3.0,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(color);p.drawPath(glyph);
            p.setPen(Qt::NoPen);p.setBrush(color);p.drawPath(glyph);
        }
        const bool openHead=note.duration>=1.75;
        p.save();p.translate(x,y);p.rotate(-18);p.setPen(openHead?QPen(color,2.3):Qt::NoPen);p.setBrush(openHead?QColor("#fffefa"):color);p.drawEllipse(QRectF(-9,-6,18,12));p.restore();
        // Playback uses a small release gap (for example, a written quarter
        // is held for about .82 beats).  Do not turn that articulation into
        // an augmentation dot; dots are reserved for durations that were
        // actually quantized close to a dotted written value.
        const bool dotted=qAbs(note.duration-.375)<.025||qAbs(note.duration-.75)<.035||qAbs(note.duration-1.5)<.06||qAbs(note.duration-3.0)<.10;
        if(dotted){p.setPen(Qt::NoPen);p.setBrush(color);p.drawEllipse(QPointF(x+14,y),2.5,2.5);}
        if(m_showNames){
            const QString name=staffPitchName(note,fifthsAt(m_keySignatures,note.start));
            const QRectF label=nameLabels.value(&note,QRectF(x+11,y-8,qMax(26.0,8.0+name.size()*6.0),16));
            p.setPen(QPen(QColor(color),.8));p.setBrush(QColor(255,254,250,235));p.drawRoundedRect(label,4,4);
            p.setPen(color);p.setFont(QFont("Manrope",8,QFont::Bold));p.drawText(label,Qt::AlignCenter,name);
        }
        if(m_showFingering&&note.finger>=1&&note.finger<=5){
            // Single notes stay on one guide line. Simultaneous chord fingers
            // stack vertically at the chord's X coordinate; ScoreWidget asks
            // its containing frame for exactly the extra height needed so the
            // outer digits are not clipped.
            int fingerIndex=0,fingerCount=0;for(const auto &candidate:chord.notes)if(candidate.event->finger>=1&&candidate.event->finger<=5){if(candidate.event==&note)fingerIndex=fingerCount;++fingerCount;}
            double fingerY=note.hand==Hand::Right?trebleFingeringY:bassFingeringY;
            if(fingerCount>1)fingerY+=note.hand==Hand::Right?-fingerIndex*16.0:(fingerCount-1-fingerIndex)*16.0;
            p.setPen(QColor("#6c5145"));p.setFont(QFont("Noto Sans",9,QFont::Bold));
            // When note-name help is visible, the finger number belongs to
            // that label visually: use the label's exact centre. Without
            // names, retain the collision-avoiding fingering columns.
            const double fingerX=m_showNames&&nameLabels.contains(&note)
                ?nameLabels.value(&note).center().x()
                :fingeringColumns.value(&note,chord.x);
            p.drawText(QRectF(fingerX-7,fingerY-7,14,14),Qt::AlignCenter,QString::number(note.finger));
        }
    }
    p.restore();
    p.restore();
}

void ScoreWidget::mousePressEvent(QMouseEvent *event){
    if(width()<=0||height()<=0){QWidget::mousePressEvent(event);return;}
    const QPointF canvasPoint=widgetToCanvas(event->position());const double x=canvasPoint.x(),y=canvasPoint.y();
    const auto range=viewRange();const double startBeat=range.first,window=range.second;
    const double musicLeft=225.0+qAbs(fifthsAt(m_keySignatures,startBeat))*8.0;
    const double notationLeft=musicLeft+((m_lessonOpeningInset||m_liveWriting)?24.0:0.0),musicRight=1000.0,scale=(musicRight-notationLeft)/window;
    if(!m_editEnabled&&m_seekEnabled&&event->button()==Qt::LeftButton&&x>=notationLeft&&x<=musicRight){
        m_seekDragging=true;m_seekRangeStart=startBeat;m_seekRangeWindow=window;
        m_seekPreviewBeat=qMax(0.0,qRound((startBeat+(x-notationLeft)/scale)*8.0)/8.0);
        update();emit seekPreviewChanged(m_seekPreviewBeat);event->accept();return;
    }
    if(!m_editEnabled){QWidget::mousePressEvent(event);return;}
    if(event->button()==Qt::LeftButton&&x>=notationLeft&&x<=musicRight){
        const double beat=qMax(0.0,qRound((startBeat+(x-notationLeft)/scale)*8.0)/8.0);m_editCursorBeat=beat;update();emit editCursorChanged(beat);event->accept();return;
    }
    if(event->button()==Qt::RightButton){
        int closest=-1;double closestDistance=std::numeric_limits<double>::max();
        for(int index=0;index<m_notes.size();++index){const auto &note=m_notes[index];if(!included(note,m_handFilter)||note.start<startBeat-.001||note.start>startBeat+window+.001)continue;const double noteX=notationLeft+(note.start-startBeat)*scale;const double noteY=staffPlacement(note,fifthsAt(m_keySignatures,note.start),scoreVerticalOffset).y;const double dx=qAbs(x-noteX),dy=qAbs(y-noteY);if(dx<=18&&dy<=13&&dx+dy<closestDistance){closest=index;closestDistance=dx+dy;}}
        if(closest>=0){emit noteContextRequested(closest,event->globalPosition().toPoint());event->accept();return;}
        int closestRest=-1;closestDistance=std::numeric_limits<double>::max();for(int index=0;index<m_rests.size();++index){const auto &rest=m_rests[index];if(rest.beat<startBeat-.001||rest.beat>startBeat+window+.001)continue;const double restX=notationLeft+(rest.beat-startBeat)*scale,restY=(rest.hand==Hand::Right?90.0:180.0)+scoreVerticalOffset;const double dx=qAbs(x-restX),dy=qAbs(y-restY);if(dx<=18&&dy<=18&&dx+dy<closestDistance){closestRest=index;closestDistance=dx+dy;}}
        if(closestRest>=0){emit restDeleteRequested(closestRest);event->accept();return;}
        if(x>=notationLeft&&x<=musicRight){const double beat=qMax(0.0,qRound((startBeat+(x-notationLeft)/scale)*8.0)/8.0);m_editCursorBeat=beat;update();emit editCursorChanged(beat);emit staffContextRequested(beat,event->globalPosition().toPoint());event->accept();return;}
    }
    QWidget::mousePressEvent(event);
}

void ScoreWidget::mouseDoubleClickEvent(QMouseEvent *event){
    if(m_editEnabled||!m_seekEnabled||event->button()!=Qt::LeftButton||width()<=0||height()<=0){QWidget::mouseDoubleClickEvent(event);return;}
    const double x=widgetToCanvas(event->position()).x();
    const double startBeat=viewRange().first;
    const double musicLeft=225.0+qAbs(fifthsAt(m_keySignatures,startBeat))*8.0;
    const double notationLeft=musicLeft+((m_lessonOpeningInset||m_liveWriting)?24.0:0.0),musicRight=1000.0;
    if(x<notationLeft||x>musicRight){QWidget::mouseDoubleClickEvent(event);return;}
    m_seekDragging=false;m_seekPreviewBeat=-1;update();
    emit measureDrillCancelRequested();
    event->accept();
}

void ScoreWidget::mouseMoveEvent(QMouseEvent *event){
    if(!m_seekDragging||!(event->buttons()&Qt::LeftButton)){QWidget::mouseMoveEvent(event);return;}
    const double x=widgetToCanvas(event->position()).x();
    const double musicLeft=225.0+qAbs(fifthsAt(m_keySignatures,m_seekRangeStart))*8.0;
    const double notationLeft=musicLeft+((m_lessonOpeningInset||m_liveWriting)?24.0:0.0),musicRight=1000.0;
    const double scale=(musicRight-notationLeft)/m_seekRangeWindow;
    m_seekPreviewBeat=qMax(0.0,qRound((m_seekRangeStart+(qBound(notationLeft,x,musicRight)-notationLeft)/scale)*8.0)/8.0);
    update();emit seekPreviewChanged(m_seekPreviewBeat);event->accept();
}

void ScoreWidget::mouseReleaseEvent(QMouseEvent *event){
    if(!m_seekDragging||event->button()!=Qt::LeftButton){QWidget::mouseReleaseEvent(event);return;}
    const double beat=m_seekPreviewBeat;m_seekDragging=false;m_seekPreviewBeat=-1;update();emit seekRequested(beat);event->accept();
}

void ScoreWidget::wheelEvent(QWheelEvent *event){
    if(!m_editEnabled&&m_seekEnabled){
        const int delta=event->angleDelta().x()!=0?event->angleDelta().x():event->angleDelta().y();
        if(delta!=0){emit historyStepRequested(delta>0?-1:1);event->accept();return;}
    }
    QWidget::wheelEvent(event);
}

PianoWidget::PianoWidget(QWidget *parent):QWidget(parent){setAttribute(Qt::WA_OpaquePaintEvent);setMinimumHeight(130);setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);}
QRectF PianoWidget::noteRect(int note) const {int whites=0;const double ww=width()/52.0;for(int n=21;n<=note;++n)if(!blackKey(n))++whites;if(blackKey(note))return QRectF((whites-.34)*ww,0,ww*.68,height()*.62);return QRectF((whites-1)*ww,0,ww,height());}
int PianoWidget::noteAt(const QPointF&p)const{for(int n=21;n<=108;++n)if(blackKey(n)&&noteRect(n).contains(p))return n;for(int n=21;n<=108;++n)if(!blackKey(n)&&noteRect(n).contains(p))return n;return-1;}
void PianoWidget::setActive(int n,bool a){if(m_active.contains(n)==a)return;if(a)m_active.insert(n);else m_active.remove(n);if(n>=21&&n<=108)update(noteRect(n).adjusted(-5,-5,5,5).toRect());}
void PianoWidget::setFingerHints(const QHash<int,int>&hints){if(m_fingerHints==hints)return;m_fingerHints=hints;update();}
void PianoWidget::setKeyRange(int low,int high){low=qBound(21,low,108);high=qBound(low,high,108);if(m_low==low&&m_high==high)return;m_low=low;m_high=high;update();}
void PianoWidget::paintEvent(QPaintEvent*){QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.fillRect(rect(),QColor("#20332f"));auto draw=[&](bool black){for(int n=21;n<=108;++n){if(blackKey(n)!=black)continue;const bool inactive=n<m_low||n>m_high;QRectF r=noteRect(n).adjusted(1,0,-1,-2);QPainterPath path;path.addRoundedRect(r,black?3:4,black?3:4);if(m_active.contains(n))p.fillPath(path,QColor(black?"#ef775f":"#9ee0d0"));else if(inactive)p.fillPath(path,QColor(black?"#303633":"#aeb2ae"));else{QLinearGradient g(r.topLeft(),r.bottomLeft());if(black){g.setColorAt(0,QColor("#2d3c38"));g.setColorAt(1,QColor("#07110f"));}else{g.setColorAt(0,Qt::white);g.setColorAt(1,QColor("#e0dfd7"));}p.fillPath(path,g);}p.setPen(QPen(QColor(inactive?"#747b77":black?"#07110f":"#adb5b2"),1));p.drawPath(path);if(!black&&n%12==0){p.setPen(QColor(inactive?"#757c78":"#8a9692"));p.setFont(QFont("DM Mono",7));p.drawText(r.adjusted(0,0,0,-7),Qt::AlignHCenter|Qt::AlignBottom,MusicDocument::noteName(n));}}};draw(false);draw(true);
    // Fingering is an optional score aid, so keep the keyboard clean unless
    // MainWindow supplies hints for the current score position. Draw the
    // badges after both key layers so black-key hints cannot cover white-key
    // numbers. Each chord tone receives its own synchronized finger.
    QList<int> hinted=m_fingerHints.keys();std::sort(hinted.begin(),hinted.end());
    for(int n:hinted){const int finger=m_fingerHints.value(n);if(n<21||n>108||finger<1||finger>5)continue;const bool black=blackKey(n);const QRectF key=noteRect(n).adjusted(1,0,-1,-2);const double diameter=qBound(13.0,key.width()-3.0,22.0);const QPointF center(key.center().x(),key.bottom()-(black?diameter*.70:diameter*.78));const QRectF badge(center.x()-diameter*.5,center.y()-diameter*.5,diameter,diameter);p.setPen(QPen(QColor("#fffefa"),qMax(1.0,diameter*.08)));p.setBrush(QColor(m_active.contains(n)?"#d95843":"#287f70"));p.drawEllipse(badge);QFont font=QApplication::font();font.setBold(true);font.setPixelSize(qBound(10,qRound(diameter*.70),16));p.setFont(font);p.setPen(Qt::white);p.drawText(badge,Qt::AlignCenter,QString::number(finger));}
}
void PianoWidget::mousePressEvent(QMouseEvent*e){m_mouseNote=noteAt(e->position());if(m_mouseNote>=0)emit notePressed(m_mouseNote,100);}
void PianoWidget::mouseMoveEvent(QMouseEvent*e){if(!(e->buttons()&Qt::LeftButton))return;int n=noteAt(e->position());if(n!=m_mouseNote){if(m_mouseNote>=0)emit noteReleased(m_mouseNote);m_mouseNote=n;if(n>=0)emit notePressed(n,100);}}
void PianoWidget::mouseReleaseEvent(QMouseEvent*){if(m_mouseNote>=0)emit noteReleased(m_mouseNote);m_mouseNote=-1;}
void PianoWidget::leaveEvent(QEvent*){if(m_mouseNote>=0){emit noteReleased(m_mouseNote);m_mouseNote=-1;}}

BubbleWidget::BubbleWidget(QWidget *parent):QWidget(parent){setAttribute(Qt::WA_OpaquePaintEvent);setMinimumHeight(185);m_clock.start();m_timer.setInterval(16);connect(&m_timer,&QTimer::timeout,this,qOverload<>(&QWidget::update));m_timer.start();}
void BubbleWidget::setNotes(const QVector<NoteEvent>&n){m_notes=n;update();}void BubbleWidget::setPlayhead(double b){m_playhead=b;update();}void BubbleWidget::setExpected(const QSet<int>&n,double s){m_expected=n;m_groupStart=s;update();}void BubbleWidget::setBubblesVisible(bool v){m_showBubbles=v;update();}void BubbleWidget::setTimelineVisible(bool v){if(m_timelineVisible==v)return;m_timelineVisible=v;update();}void BubbleWidget::setWaitMode(bool waiting){m_waitMode=waiting;update();}void BubbleWidget::setHandFilter(int f){m_handFilter=f;update();}void BubbleWidget::pulseNote(int n){m_pulses<<Pulse{n,m_clock.elapsed()};update();}
void BubbleWidget::resetState(){m_pulses.clear();m_expected.clear();m_playhead=0;m_groupStart=0;update();}
void BubbleWidget::paintEvent(QPaintEvent*){QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QLinearGradient bg(0,0,0,height());bg.setColorAt(0,QColor("#17342e"));bg.setColorAt(1,QColor("#102721"));p.fillRect(rect(),bg);int low=21,high=108;auto xFor=[&](int n){return (n-low+.5)*width()/double(high-low+1);};p.setPen(QPen(QColor(255,255,255,12),1));for(int n=low;n<=high;++n)if(n%12==0)p.drawLine(QPointF(xFor(n),0),QPointF(xFor(n),height()));double strike=height()-23;p.setPen(QPen(QColor("#66c4ae"),1));p.drawLine(QPointF(0,strike),QPointF(width(),strike));
    if(m_showBubbles&&m_timelineVisible){for(const auto&n:m_notes){if(!included(n,m_handFilter))continue;double reference=m_playhead;constexpr double pixelsPerBeat=42.0;double y=strike-(n.start-reference)*pixelsPerBeat;double w=qMax(12.0,width()/92.0),h=qMax(15.0,n.duration*pixelsPerBeat);if(y<-40||y-h>height()+30)continue;bool ex=m_waitMode&&qAbs(n.start-m_groupStart)<.01&&m_expected.contains(n.note);QColor c=ex?QColor("#ef775f"):QColor(n.hand==Hand::Left?"#86aebd":"#77cbb8");QRectF r(xFor(n.note)-w/2,y-h,w,h);p.setPen(Qt::NoPen);p.setBrush(c);p.drawRoundedRect(r,w/2,w/2);if(ex){p.setBrush(QColor(239,119,95,38));p.drawEllipse(QPointF(xFor(n.note),strike),15,15);}}}
    qint64 now=m_clock.elapsed();m_pulses.erase(std::remove_if(m_pulses.begin(),m_pulses.end(),[&](const Pulse&a){return now-a.born>700;}),m_pulses.end());for(const auto&a:m_pulses){double age=(now-a.born)/700.0;p.setPen(QPen(QColor(111,220,193,int(200*(1-age))),2));p.setBrush(Qt::NoBrush);p.drawEllipse(QPointF(xFor(a.note),strike),8+age*22,8+age*22);}
}
