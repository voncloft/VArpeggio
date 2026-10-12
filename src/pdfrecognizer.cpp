#include "pdfrecognizer.h"

#include <QImage>
#include <QDir>
#include <QPdfDocument>
#include <QPdfSelection>
#include <QRegularExpression>
#include <QSizeF>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace {
struct Staff {
    QVector<double> lines;
    double spacing=0;
    int left=0;
    int right=0;
};

struct FoundHead { double x=0; double y=0; int midi=60; Hand hand=Hand::Right; };

QVector<Staff> findStaves(const cv::Mat &ink) {
    cv::Mat horizontal;
    const int kernelWidth=qMax(24,ink.cols/35);
    cv::morphologyEx(ink,horizontal,cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_RECT,{kernelWidth,1}));
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(horizontal,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    struct Line { double y; int left; int right; };
    QVector<Line> candidates;
    for(const auto &contour:contours){
        const cv::Rect r=cv::boundingRect(contour);
        if(r.width<ink.cols*.22||r.height>qMax(10,ink.rows/100))continue;
        candidates.append({r.y+(r.height-1)*.5,r.x,r.x+r.width});
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){return a.y<b.y;});
    QVector<Line> lines;
    for(const auto &line:candidates){
        if(!lines.isEmpty()&&qAbs(lines.last().y-line.y)<2.5){
            auto &same=lines.last();same.y=(same.y+line.y)*.5;same.left=qMin(same.left,line.left);same.right=qMax(same.right,line.right);
        }else lines.append(line);
    }
    QVector<Staff> result;
    for(int i=0;i+4<lines.size();){
        QVector<double> gaps;for(int j=0;j<4;++j)gaps<<lines[i+j+1].y-lines[i+j].y;
        QVector<double> sorted=gaps;std::sort(sorted.begin(),sorted.end());const double spacing=(sorted[1]+sorted[2])*.5;
        bool regular=spacing>=5&&spacing<=45;
        for(double gap:gaps)regular=regular&&qAbs(gap-spacing)<=qMax(2.2,spacing*.22);
        int left=0,right=ink.cols;
        for(int j=0;j<5;++j){left=qMax(left,lines[i+j].left);right=qMin(right,lines[i+j].right);}
        if(regular&&right-left>ink.cols*.2){
            Staff staff;staff.spacing=spacing;staff.left=left;staff.right=right;
            for(int j=0;j<5;++j)staff.lines<<lines[i+j].y;
            result<<staff;i+=5;
        }else ++i;
    }
    return result;
}

int naturalMidi(int diatonic) {
    static const int pitchClass[]{0,2,4,5,7,9,11};
    int octave=diatonic/7,degree=diatonic%7;
    if(degree<0){degree+=7;--octave;}
    return octave*12+pitchClass[degree];
}

QVector<FoundHead> findHeads(const cv::Mat &ink,const Staff &staff,Hand hand) {
    const int s=qMax(5,int(qRound(staff.spacing)));
    // Do not let lyrics, chord names, or dynamics between the two staves enter
    // the notehead detector. These ranges still include the conventional
    // ledger territory used by piano music (C4 below treble and C4 above
    // bass), plus a generous outer range away from the inter-staff text.
    const double upperLedgers=hand==Hand::Right?2.7:2.0;
    const double lowerLedgers=hand==Hand::Right?2.0:4.0;
    const int top=qMax(0,int(staff.lines.first()-upperLedgers*s));
    const int bottom=qMin(ink.rows-1,int(staff.lines.last()+lowerLedgers*s));
    const int start=qBound(0,staff.left+qRound(10.5*s),ink.cols-1);
    const int end=qBound(0,staff.right-qRound(.5*s),ink.cols-1);
    if(end<=start||bottom<=top)return {};
    cv::Mat roi=ink(cv::Rect(start,top,end-start+1,bottom-top+1)).clone();
    // Remove the five long staff rules, then repair noteheads split by a rule.
    for(double line:staff.lines){
        const int y=qRound(line)-top;
        cv::rectangle(roi,{0,qMax(0,y-1)},{roi.cols-1,qMin(roi.rows-1,y+1)},cv::Scalar(0),cv::FILLED);
    }
    const int closeSize=qMax(2,int(qRound(s*.28)));
    cv::morphologyEx(roi,roi,cv::MORPH_CLOSE,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE,{closeSize*2+1,closeSize*2+1}));
    cv::Mat thickness;
    cv::distanceTransform(roi,thickness,cv::DIST_L2,3);
    // An oval opening removes stems, beams, digits, and accidental strokes.
    cv::Mat heads;
    int kw=qMax(3,int(qRound(s*.92)));if(!(kw&1))++kw;
    int kh=qMax(3,int(qRound(s*.42)));if(!(kh&1))++kh;
    cv::morphologyEx(roi,heads,cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE,{kw,kh}));
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(heads,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    struct Candidate { FoundHead head; bool stem=false; bool hollow=false; };
    QVector<Candidate> candidates;
    const double bottomLine=staff.lines.last();
    // Absolute diatonic indexes: C-1 is zero; treble bottom is E4 and bass
    // bottom is G2. One staff step is half the inter-line distance.
    const int bottomDiatonic=hand==Hand::Right?37:25;
    QVector<double> chordSeedXs;
    auto addCandidate=[&](double cx,double cy){
        const int step=qRound((bottomLine-cy)/(staff.spacing*.5));
        const int midi=naturalMidi(bottomDiatonic+step);
        if(midi<21||midi>108)return;
        int longestRun=0;
        for(int x=qMax(0,qRound(cx-s));x<=qMin(ink.cols-1,qRound(cx+s));++x){
            int run=0;
            for(int y=qMax(0,qRound(cy-3*s));y<=qMin(ink.rows-1,qRound(cy+3*s));++y){
                if(ink.at<uchar>(y,x)){++run;longestRun=qMax(longestRun,run);}else run=0;
            }
        }
        const int localX=qRound(cx-start),localY=qRound(cy-top);
        int centerInk=0,centerTotal=0,ringInk=0,ringTotal=0;
        for(int dy=-qRound(s*.48);dy<=qRound(s*.48);++dy)for(int dx=-qRound(s*.7);dx<=qRound(s*.7);++dx){
            const int x=localX+dx,y=localY+dy;if(x<0||x>=roi.cols||y<0||y>=roi.rows)continue;
            const double radius=qSqrt(qPow(dx/(s*.7),2)+qPow(dy/(s*.48),2));
            if(radius<.28){centerInk+=roi.at<uchar>(y,x)>0;++centerTotal;}
            else if(radius>.62&&radius<1.08){ringInk+=roi.at<uchar>(y,x)>0;++ringTotal;}
        }
        const bool hollow=centerTotal>0&&ringTotal>0&&centerInk<double(centerTotal)*.32&&ringInk>double(ringTotal)*.16;
        candidates<<Candidate{{cx,cy,midi,hand},longestRun>=s*1.25,hollow};
    };
    for(const auto &contour:contours){
        const cv::Rect r=cv::boundingRect(contour);
        const double cx=start+r.x+r.width*.5,cy=top+r.y+r.height*.5;
        if(r.width<s*.38||r.width>s*2.2||r.height<s*.25||r.height>s*5.5)continue;
        if(cx<start||cx>end)continue;
        if(r.height<=s*1.55)addCandidate(cx,cy);
        else{
            if(r.width>=s*.78)chordSeedXs<<cx;
            // Vertically stacked chord heads often become one connected blob.
            // Sample each legal staff step through that component instead of
            // throwing the whole chord away as an oversized symbol.
            struct RowScore { double y=0; double score=0; };
            QVector<RowScore> rowScores;
            for(double y=staff.lines.first()-upperLedgers*s;y<=staff.lines.last()+lowerLedgers*s;y+=staff.spacing*.5){
                if(y<top+r.y-s*.35||y>top+r.y+r.height+s*.35)continue;
                const int yy=qBound(0,qRound(y-top),heads.rows-1);
                double thickest=0;
                for(int dy=-qRound(s*.25);dy<=qRound(s*.25);++dy)for(int x=qMax(0,r.x-qRound(s*.25));x<qMin(thickness.cols,r.x+r.width+qRound(s*.25));++x){const int sy=yy+dy;if(sy>=0&&sy<thickness.rows)thickest=qMax(thickest,double(thickness.at<float>(sy,x)));}
                rowScores<<RowScore{y,thickest/qMax(1,s)};
            }
            for(int index=0;index<rowScores.size();++index){
                const double score=rowScores[index].score;
                const double before=index?rowScores[index-1].score:-1;
                const double after=index+1<rowScores.size()?rowScores[index+1].score:-1;
                if(score>.16&&score>=before&&score>=after)addCandidate(cx,rowScores[index].y);
            }
        }
    }
    // A connected vertical chord can have only one contour and one projection
    // peak. Distance-transform maxima recover the thick centre of every oval
    // independently; thin stems and the joins between adjacent heads do not
    // create comparable maxima.
    cv::Mat dilated,maximaMask;
    int maximumKernel=qMax(3,int(qRound(s*.45)));if(!(maximumKernel&1))++maximumKernel;
    cv::dilate(thickness,dilated,cv::getStructuringElement(cv::MORPH_ELLIPSE,{maximumKernel,maximumKernel}));
    cv::compare(thickness,dilated,maximaMask,cv::CMP_GE);
    cv::Mat thickMask;cv::threshold(thickness,thickMask,s*.16,255,cv::THRESH_BINARY);thickMask.convertTo(thickMask,CV_8U);
    cv::bitwise_and(maximaMask,thickMask,maximaMask);
    std::vector<std::vector<cv::Point>> maximaContours;cv::findContours(maximaMask,maximaContours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    for(const auto &maximum:maximaContours){
        const cv::Rect r=cv::boundingRect(maximum);const double cx=start+r.x+r.width*.5,rawY=top+r.y+r.height*.5;
        if(cx<start||cx>end)continue;
        bool chordSeed=false;for(double seed:chordSeedXs)if(qAbs(cx-seed)<s*.8){chordSeed=true;break;}
        if(!chordSeed)continue;
        const double snappedY=bottomLine-qRound((bottomLine-rawY)/(staff.spacing*.5))*staff.spacing*.5;
        if(qAbs(snappedY-rawY)>staff.spacing*.38)continue;
        addCandidate(cx,snappedY);
    }
    QVector<FoundHead> found;
    for(int i=0;i<candidates.size();++i){
        bool supported=candidates[i].stem||candidates[i].hollow;
        int aligned=0;
        for(int j=0;j<candidates.size();++j)if(qAbs(candidates[i].head.x-candidates[j].head.x)<s*.75&&qAbs(candidates[i].head.y-candidates[j].head.y)<s*5.0)++aligned;
        bool chordSeed=false;for(double seed:chordSeedXs)if(qAbs(candidates[i].head.x-seed)<s*.8){chordSeed=true;break;}
        if(aligned>=2&&chordSeed)supported=true; // a wide stemless vertical cluster is a chord
        if(!supported)for(int j=0;j<candidates.size();++j)if(i!=j&&(candidates[j].stem||candidates[j].hollow)&&qAbs(candidates[i].head.x-candidates[j].head.x)<s*1.15&&qAbs(candidates[i].head.y-candidates[j].head.y)<s*5.0){supported=true;break;}
        if(supported)found<<candidates[i].head;
    }
    std::sort(found.begin(),found.end(),[](const auto&a,const auto&b){return a.x==b.x?a.y<b.y:a.x<b.x;});
    QVector<FoundHead> unique;
    for(const auto &head:found){
        bool duplicate=false;
        for(const auto &kept:unique)if(qAbs(kept.x-head.x)<s*.45&&qAbs(kept.y-head.y)<s*.38){duplicate=true;break;}
        if(!duplicate)unique<<head;
    }
    return unique;
}

QVector<double> barlinesForStaff(const cv::Mat &ink,const Staff &staff) {
    const int s=qMax(5,int(qRound(staff.spacing)));
    const int top=qMax(0,int(staff.lines.first()-s*.2));
    const int bottom=qMin(ink.rows-1,int(staff.lines.last()+s*.2));
    cv::Mat roi=ink(cv::Rect(staff.left,top,staff.right-staff.left+1,bottom-top+1));
    cv::Mat vertical;
    cv::morphologyEx(roi,vertical,cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_RECT,{1,qMax(5,int(s*3.2))}));
    std::vector<std::vector<cv::Point>> contours;cv::findContours(vertical,contours,cv::RETR_EXTERNAL,cv::CHAIN_APPROX_SIMPLE);
    QVector<double> bars;
    for(const auto &contour:contours){const cv::Rect r=cv::boundingRect(contour);if(r.height>=s*3.2&&r.width<=s*.8)bars<<staff.left+r.x+r.width*.5;}
    std::sort(bars.begin(),bars.end());
    QVector<double> unique;for(double x:bars)if(unique.isEmpty()||x-unique.last()>s*.7)unique<<x;
    return unique;
}

QVector<double> sharedBarlines(const cv::Mat &ink,const Staff &top,const Staff &bottom) {
    const auto a=barlinesForStaff(ink,top),b=barlinesForStaff(ink,bottom);
    QVector<double> bars;const double tolerance=qMax(top.spacing,bottom.spacing)*.8;
    for(double x:a)for(double y:b)if(qAbs(x-y)<=tolerance){bars<<(x+y)*.5;break;}
    std::sort(bars.begin(),bars.end());
    QVector<double> unique;for(double x:bars)if(unique.isEmpty()||x-unique.last()>tolerance)unique<<x;
    return unique;
}

double quantizeBeat(double beat){return qRound(beat*4.0)/4.0;}

int tempoFromDocument(QPdfDocument &document,int fallbackTempo){
    const int fallback=qBound(30,fallbackTempo>0?fallbackTempo:96,260);
    if(document.pageCount()<1)return fallback;
    const QString firstPageText=document.getAllText(0).text();
    const auto tempoMatch=QRegularExpression(QStringLiteral("=\\s*(\\d{2,3})")).match(firstPageText);
    if(!tempoMatch.hasMatch())return fallback;
    bool valid=false;const int printedTempo=tempoMatch.captured(1).toInt(&valid);
    return valid&&printedTempo>=30&&printedTempo<=260?printedTempo:fallback;
}
}

int PdfRecognizer::detectPrintedTempo(const QString &path,int fallbackTempo){
    QPdfDocument document;
    if(document.load(path)!=QPdfDocument::Error::None)return qBound(30,fallbackTempo>0?fallbackTempo:96,260);
    return tempoFromDocument(document,fallbackTempo);
}

PdfRecognitionResult PdfRecognizer::recognize(const QString &path,int fallbackTempo,
                                               std::function<void(int)> progress,
                                               std::function<bool()> canceled){
    PdfRecognitionResult result;result.tempo=qBound(30,fallbackTempo>0?fallbackTempo:96,260);
    QPdfDocument document;
    if(document.load(path)!=QPdfDocument::Error::None){result.error="VArpeggio could not render this PDF.";return result;}
    result.pages=document.pageCount();
    if(result.pages<1){result.error="The PDF contains no pages.";return result;}
    result.tempo=tempoFromDocument(document,fallbackTempo);
    double scoreBeat=0.0;
    for(int page=0;page<result.pages;++page){
        if(canceled&&canceled()){result.canceled=true;return result;}
        const QSizeF points=document.pagePointSize(page);
        const int width=2400,height=qBound(1200,qRound(width*points.height()/qMax(1.0,points.width())),4200);
        QImage rendered=document.render(page,{width,height}).convertToFormat(QImage::Format_Grayscale8);
        if(rendered.isNull())continue;
        cv::Mat view(rendered.height(),rendered.width(),CV_8UC1,rendered.bits(),rendered.bytesPerLine());
        cv::Mat ink;cv::threshold(view,ink,0,255,cv::THRESH_BINARY_INV|cv::THRESH_OTSU);
        cv::Mat debug;
        const QString debugDirectory=qEnvironmentVariable("VARPEGGIO_OMR_DEBUG_DIR");
        if(!debugDirectory.isEmpty())cv::cvtColor(view,debug,cv::COLOR_GRAY2BGR);
        const QVector<Staff> staves=findStaves(ink);
        for(int staffIndex=0;staffIndex+1<staves.size();staffIndex+=2){
            if(canceled&&canceled()){result.canceled=true;return result;}
            const Staff &treble=staves[staffIndex],&bass=staves[staffIndex+1];
            const double averageSpacing=(treble.spacing+bass.spacing)*.5;
            // Spoken directions and lyrics can make a legitimate grand staff
            // unusually deep. Ordered five-line pairs with strong horizontal
            // overlap remain safer evidence than a narrow fixed gap.
            if(bass.lines.first()-treble.lines.last()>averageSpacing*35.0)continue;
            QVector<FoundHead> heads=findHeads(ink,treble,Hand::Right);
            heads+=findHeads(ink,bass,Hand::Left);
            if(!debug.empty())for(const auto &head:heads)cv::circle(debug,{qRound(head.x),qRound(head.y)},qMax(5,qRound(averageSpacing*.7)),head.hand==Hand::Right?cv::Scalar(40,40,230):cv::Scalar(230,100,30),3);
            if(heads.isEmpty())continue;
            QVector<double> bars=sharedBarlines(ink,treble,bass);
            const double contentLeft=qMax(treble.left,bass.left)+averageSpacing*10.5;
            const double contentRight=qMin(treble.right,bass.right);
            while(!bars.isEmpty()&&bars.first()<contentLeft-averageSpacing)bars.removeFirst();
            if(bars.isEmpty()||bars.first()>contentLeft+averageSpacing*3)bars.prepend(contentLeft);
            if(bars.last()<contentRight-averageSpacing*2)bars<<contentRight;
            if(bars.size()<2)continue;
            struct TimedHead { FoundHead head; double start; double barEnd; };
            QVector<TimedHead> timed;
            for(const auto &head:heads){
                int bar=-1;for(int i=0;i+1<bars.size();++i)if(head.x>=bars[i]-averageSpacing*.4&&head.x<bars[i+1]+averageSpacing*.2){bar=i;break;}
                if(bar<0)continue;
                const double fraction=qBound(0.0,(head.x-bars[bar])/qMax(1.0,bars[bar+1]-bars[bar]),.999);
                double beatInBar=quantizeBeat(fraction*4.0);
                if(beatInBar<.5)beatInBar=0.0;
                const double start=scoreBeat+bar*4.0+beatInBar;
                timed<<TimedHead{head,start,scoreBeat+(bar+1)*4.0};
            }
            QVector<double> rightOnsets,leftOnsets;for(const auto &note:timed)(note.head.hand==Hand::Right?rightOnsets:leftOnsets)<<note.start;
            auto prepare=[](QVector<double>&onsets){std::sort(onsets.begin(),onsets.end());onsets.erase(std::unique(onsets.begin(),onsets.end(),[](double a,double b){return qAbs(a-b)<.01;}),onsets.end());};
            prepare(rightOnsets);prepare(leftOnsets);
            for(const auto &detected:timed){
                const auto &onsets=detected.head.hand==Hand::Right?rightOnsets:leftOnsets;
                double next=detected.barEnd;for(double onset:onsets)if(onset>detected.start+.01){next=qMin(next,onset);break;}
                const double duration=qBound(.25,quantizeBeat(next-detected.start),4.0);
                result.notes<<NoteEvent{detected.head.midi,detected.start,duration,92,detected.head.hand,AccidentalMark::Automatic};
            }
            scoreBeat+=(bars.size()-1)*4.0;++result.staffSystems;
        }
        if(!debug.empty()){
            QDir().mkpath(debugDirectory);
            cv::cvtColor(debug,debug,cv::COLOR_BGR2RGB);
            QImage overlay(debug.data,debug.cols,debug.rows,debug.step,QImage::Format_RGB888);
            overlay.copy().save(QDir(debugDirectory).filePath(QString("page-%1.png").arg(page+1,3,10,QChar('0'))));
        }
        if(progress)progress(qRound((page+1)*100.0/result.pages));
    }
    std::sort(result.notes.begin(),result.notes.end(),[](const auto&a,const auto&b){return a.start==b.start?a.note<b.note:a.start<b.start;});
    if(result.notes.isEmpty())result.error="VArpeggio could not confidently identify playable noteheads. This native reader currently requires a clean, conventionally engraved piano score with paired treble and bass staves.";
    return result;
}
