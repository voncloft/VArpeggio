#include "mainwindow.h"
#include "audiverisbridge.h"
#include "pdfrecognizer.h"

#include <QApplication>
#include <QAudioOutput>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QFontMetrics>
#include <QFutureWatcher>
#include <QGraphicsDropShadowEffect>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMediaPlayer>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPdfView>
#include <QPdfWriter>
#include <QProgressDialog>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPointer>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTableWidget>
#include <QTextStream>
#include <QVideoFrame>
#include <QVideoSink>
#include <QStatusBar>
#include <QStackedWidget>
#include <QSvgGenerator>
#include <QUrl>
#include <QVBoxLayout>
#include <QtMath>
#include <QtConcurrentRun>
#include <atomic>
#include <limits>

#ifndef VARPEGGIO_VERSION
#define VARPEGGIO_VERSION "0.2.1"
#endif

namespace {
QLabel *label(const QString &text,const QString &name={}){auto*l=new QLabel(text);if(!name.isEmpty())l->setObjectName(name);return l;}
QPushButton *button(const QString &text,const QString &name={}){auto*b=new QPushButton(text);if(!name.isEmpty())b->setObjectName(name);b->setCursor(Qt::PointingHandCursor);return b;}
QFrame *line(bool vertical=false){auto*f=new QFrame;f->setFrameShape(vertical?QFrame::VLine:QFrame::HLine);f->setObjectName("divider");return f;}
void shadow(QWidget*w){auto*s=new QGraphicsDropShadowEffect(w);s->setBlurRadius(32);s->setOffset(0,10);s->setColor(QColor(24,49,45,24));w->setGraphicsEffect(s);}
QString safeName(QString name){name=name.trimmed().replace(QRegularExpression("[^A-Za-z0-9._-]+"),"-");return name.isEmpty()?"varpeggio-score":name;}

struct DailyRoutineTheme { QString keyName; int transpose=0; int index=0; int chordOrder=0; int earFocus=0; bool useOde=false; };
DailyRoutineTheme dailyRoutineTheme(const QDate &date){
    const QVector<QPair<QString,int>> rotation{
        {"C major",0},{"G major",7},{"F major",5},{"D major",2},
        {"B♭ major",10},{"A major",9},{"E♭ major",3}
    };
    const qint64 day=date.toJulianDay();const int index=int(day%rotation.size());
    return {rotation[index].first,rotation[index].second,index,int(day%3),int(day%4),bool(day%2)};
}
QString earFocusName(int focus){const QStringList names{"Single-note recognition","Major and minor chord recognition","Interval recognition","Mixed notes and chords"};return names[qBound(0,focus,names.size()-1)];}
QVector<NoteEvent> transposed(QVector<NoteEvent> notes,int semitones){
    for(auto &note:notes)note.note=qBound(0,note.note+semitones,127);
    return notes;
}

class DetectorOverlayWidget final : public QWidget {
public:
    DetectorOverlayWidget(QVector<NoteEvent> notes,int tempo,double sourceStart,
                          TranscriptionInfo geometry,QWidget *parent=nullptr)
        :QWidget(parent),m_notes(std::move(notes)),m_tempo(qMax(1,tempo)),
          m_sourceStart(sourceStart),m_geometry(std::move(geometry)){
        setMinimumSize(900,520);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    }
    void setFrame(const QImage &frame){m_frame=frame;update();}
    void setPosition(qint64 milliseconds){m_position=milliseconds;update();}
    QSize sizeHint()const override{return {1120,650};}
protected:
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.fillRect(rect(),QColor("#07111d"));p.setRenderHint(QPainter::Antialiasing);
        if(m_frame.isNull()){
            p.setPen(Qt::white);p.drawText(rect(),Qt::AlignCenter,"Loading source video…");return;
        }
        QSizeF fitted=m_frame.size();fitted.scale(size(),Qt::KeepAspectRatio);
        const QRectF video((width()-fitted.width())*.5,(height()-fitted.height())*.5,fitted.width(),fitted.height());
        p.drawImage(video,m_frame);
        const double sourceSecond=m_position/1000.0,secondsPerBeat=60.0/m_tempo;
        const double sx=video.width()/qMax(1,m_geometry.videoWidth),sy=video.height()/qMax(1,m_geometry.videoHeight);
        const double strike=video.top()+m_geometry.keyboardTop*sy;
        const double keyBottom=video.top()+qBound(m_geometry.keyboardTop+8,
            m_geometry.keyboardBottom>m_geometry.keyboardTop?m_geometry.keyboardBottom:m_geometry.videoHeight,
            m_geometry.videoHeight)*sy;
        const double fall=qMax(12.0,m_geometry.fallPixelsPerSecond)*sy;
        QStringList held;int visibleBars=0;
        p.save();p.setClipRect(video);
        for(int noteIndex=0;noteIndex<m_notes.size();++noteIndex){const auto &note=m_notes[noteIndex];
            if(note.note<0||note.note>=m_geometry.laneCenters.size())continue;
            const double center=m_geometry.directorBarCenters.size()==m_notes.size()&&m_geometry.directorBarCenters[noteIndex]>=0
                ?m_geometry.directorBarCenters[noteIndex]:m_geometry.laneCenters[note.note];if(center<0)continue;
            const double exactWidth=m_geometry.directorBarWidths.size()==m_notes.size()?m_geometry.directorBarWidths[noteIndex]:-1;
            const double sourceWidth=exactWidth>0?exactWidth:(note.note<m_geometry.laneBarWidths.size()&&m_geometry.laneBarWidths[note.note]>0
                ?m_geometry.laneBarWidths[note.note]:m_geometry.whiteKeyWidth*.62);
            const double laneWidth=qMax(4.0,sourceWidth*sx);
            const double attack=m_sourceStart+note.start*secondsPerBeat;
            const double release=m_sourceStart+(note.start+note.duration)*secondsPerBeat;
            const double bottom=strike+(sourceSecond-attack)*fall;
            const double top=strike+(sourceSecond-release)*fall;
            const double shownTop=qMax(video.top(),top),shownBottom=qMin(strike,bottom);
            const QColor colour=note.hand==Hand::Left?QColor(255,105,78):QColor(94,230,210);
            if(shownBottom>shownTop&&bottom>=video.top()&&top<=strike){
                const QRectF bar(video.left()+center*sx-laneWidth*.5,shownTop,laneWidth,shownBottom-shownTop);
                p.setBrush(QColor(colour.red(),colour.green(),colour.blue(),46));
                p.setPen(QPen(colour,3));p.drawRoundedRect(bar,3,3);++visibleBars;
            }
            if(sourceSecond>=attack&&sourceSecond<release){
                const double remaining=release-sourceSecond,total=release-attack;
                held<<QString("%1  %2 / %3 sec").arg(MusicDocument::noteName(note.note)).arg(total-remaining,0,'f',2).arg(total,0,'f',2);
                const QRectF key(video.left()+center*sx-laneWidth*.62,strike,laneWidth*1.24,qMax(8.0,keyBottom-strike));
                p.fillRect(key,QColor(colour.red(),colour.green(),colour.blue(),96));
                p.setPen(QPen(colour,3));p.drawRect(key);
            }
        }
        p.restore();
        p.fillRect(QRectF(video.left()+12,video.top()+12,qMin(650.0,video.width()-24),held.isEmpty()?76:112),QColor(3,12,20,205));
        p.setPen(Qt::white);QFont bold=p.font();bold.setBold(true);bold.setPointSize(12);p.setFont(bold);
        const int minutes=int(sourceSecond)/60;const double seconds=sourceSecond-minutes*60;
        p.drawText(QPointF(video.left()+25,video.top()+39),QString("DETECTOR VIEW   %1:%2").arg(minutes,2,10,QChar('0')).arg(seconds,5,'f',2,QChar('0')));
        QFont normal=p.font();normal.setBold(false);normal.setPointSize(10);p.setFont(normal);p.setPen(QColor("#d7ebe6"));
        p.drawText(QPointF(video.left()+25,video.top()+63),QString("%1 accepted outlines visible · cyan right hand · coral left hand").arg(visibleBars));
        if(held.isEmpty())p.drawText(QPointF(video.left()+25,video.top()+87),"DETECTED HELD: none");
        else p.drawText(QRectF(video.left()+25,video.top()+72,qMin(620.0,video.width()-50),44),Qt::TextWordWrap,"DETECTED HELD: "+held.mid(0,8).join("   "));
        p.setPen(QPen(QColor("#f5df75"),2,Qt::DashLine));p.drawLine(QPointF(video.left(),strike),QPointF(video.right(),strike));
        p.fillRect(QRectF(video.left()+12,video.bottom()-42,video.width()-24,30),QColor(3,12,20,205));
        p.setPen(QColor("#ffffff"));p.drawText(QRectF(video.left()+22,video.bottom()-38,video.width()-44,24),Qt::AlignVCenter,
            "If a source colour bar remains after its outline/key highlight vanishes, VArpeggio shortened that note.");
    }
private:
    QVector<NoteEvent> m_notes;int m_tempo=96;double m_sourceStart=0;TranscriptionInfo m_geometry;
    QImage m_frame;qint64 m_position=0;
};

QString assTime(double seconds){
    const int centiseconds=qMax(0,qRound(seconds*100.0));
    const int hours=centiseconds/360000,minutes=(centiseconds/6000)%60,wholeSeconds=(centiseconds/100)%60,cs=centiseconds%100;
    return QString("%1:%2:%3.%4").arg(hours).arg(minutes,2,10,QChar('0')).arg(wholeSeconds,2,10,QChar('0')).arg(cs,2,10,QChar('0'));
}

QString assEscape(QString text){return text.replace('\\',QStringLiteral("\\\\")).replace('{',QStringLiteral("\\{")).replace('}',QStringLiteral("\\}"));}

QString assRectangle(int layer,double begin,double end,double x,double y0,double y1,double width,double height,const QString &colour){
    const int travelMs=qMax(1,qRound((end-begin)*1000.0));
    return QString("Dialogue: %1,%2,%3,Art,,0,0,0,,{\\an7\\move(%4,%5,%4,%6,0,%7)\\p1\\bord0\\shad0\\1c&H%8&}m 0 0 l %9 0 %9 %10 0 %10\n")
        .arg(layer).arg(assTime(begin)).arg(assTime(end)).arg(qRound(x)).arg(qRound(y0)).arg(qRound(y1)).arg(travelMs).arg(colour).arg(qRound(width)).arg(qRound(height));
}

QString assOverlayRectangle(int layer,double begin,double end,double x,double y0,double y1,
                            double width,double height,const QString &colour,const QString &alpha="78",int clipBottom=-1){
    const int travelMs=qMax(1,qRound((end-begin)*1000.0));
    const QString clip=clipBottom>0?QString("\\clip(0,0,10000,%1)").arg(clipBottom):QString();
    return QString("Dialogue: %1,%2,%3,Art,,0,0,0,,{\\an7\\move(%4,%5,%4,%6,0,%7)\\p1\\bord3\\3c&H%8&\\1c&H%8&\\1a&H%9&%10}m 0 0 l %11 0 %11 %12 0 %12\n")
        .arg(layer).arg(assTime(begin)).arg(assTime(end)).arg(qRound(x)).arg(qRound(y0)).arg(qRound(y1)).arg(travelMs).arg(colour,alpha).arg(clip).arg(qRound(width)).arg(qRound(height));
}

bool saveDetectorVideo(const QString &path,const QString &source,const QVector<NoteEvent> &notes,int tempo,
                       double sourceStart,const TranscriptionInfo &geometry,QWidget *parent,QString *error){
    if(notes.isEmpty()||geometry.laneCenters.isEmpty()||geometry.videoWidth<=0||geometry.videoHeight<=0){if(error)*error="No visual detector data is available to export.";return false;}
    QTemporaryDir temp;if(!temp.isValid()){if(error)*error="Could not create the detector-video workspace.";return false;}
    const QString script=temp.filePath("detector-overlay.ass");QFile ass(script);
    if(!ass.open(QIODevice::WriteOnly|QIODevice::Text)){if(error)*error=ass.errorString();return false;}
    QTextStream out(&ass);out.setEncoding(QStringConverter::Utf8);
    out<<QString("[Script Info]\nScriptType: v4.00+\nPlayResX: %1\nPlayResY: %2\nScaledBorderAndShadow: yes\n").arg(geometry.videoWidth).arg(geometry.videoHeight)
       <<"[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
         "Style: Art,Noto Sans,28,&H00FFFFFF,&H00FFFFFF,&H00000000,&H90000000,0,0,0,0,100,100,0,0,1,2,0,7,0,0,0,1\n"
         "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    QProcess probe;probe.start("ffprobe",{"-v","error","-show_entries","format=duration","-of","default=nw=1:nk=1",source});probe.waitForFinished(10000);
    const double sourceDuration=QString::fromLocal8Bit(probe.readAllStandardOutput()).trimmed().toDouble();
    out<<QString("Dialogue: 10,0:00:00.00,%1,Art,,0,0,0,,{\\an7\\pos(20,18)\\p0\\fs24\\b1\\bord2\\3c&H000000&}VARPEGGIO DETECTOR — outlined bars are accepted notes\n").arg(assTime(qMax(1.0,sourceDuration)));
    const double secondsPerBeat=60.0/qMax(1,tempo),fall=qMax(12.0,geometry.fallPixelsPerSecond);
    for(int noteIndex=0;noteIndex<notes.size();++noteIndex){const auto &note=notes[noteIndex];
        if(note.note<0||note.note>=geometry.laneCenters.size()||geometry.laneCenters[note.note]<0)continue;
        const double center=geometry.directorBarCenters.size()==notes.size()&&geometry.directorBarCenters[noteIndex]>=0
            ?geometry.directorBarCenters[noteIndex]:geometry.laneCenters[note.note];
        const double exactWidth=geometry.directorBarWidths.size()==notes.size()?geometry.directorBarWidths[noteIndex]:-1;
        const double sourceWidth=exactWidth>0?exactWidth:(note.note<geometry.laneBarWidths.size()&&geometry.laneBarWidths[note.note]>0
            ?geometry.laneBarWidths[note.note]:geometry.whiteKeyWidth*.62);
        const double barWidth=qMax(4.0,sourceWidth);
        const double attack=sourceStart+note.start*secondsPerBeat,release=sourceStart+(note.start+note.duration)*secondsPerBeat;
        const double visible=qMax(0.0,attack-geometry.keyboardTop/fall),height=qMax(6.0,(release-attack)*fall);
        const double initialTop=geometry.keyboardTop+(visible-release)*fall;
        const double x=center-barWidth*.5;const QString colour=note.hand==Hand::Left?QStringLiteral("4E69FF"):QStringLiteral("D2E65E");
        out<<assOverlayRectangle(5,visible,release,x,initialTop,geometry.keyboardTop,barWidth,height,colour,"78",geometry.keyboardTop);
        const int keyBottom=qBound(geometry.keyboardTop+8,
            geometry.keyboardBottom>geometry.keyboardTop?geometry.keyboardBottom:geometry.videoHeight,
            geometry.videoHeight);
        out<<assOverlayRectangle(6,attack,release,x,geometry.keyboardTop,geometry.keyboardTop,barWidth,keyBottom-geometry.keyboardTop,colour,"98");
    }
    ass.close();
    std::unique_ptr<QProgressDialog> progress;
    if(parent){progress=std::make_unique<QProgressDialog>("Saving source video with detector outlines…","Cancel",0,0,parent);progress->setWindowTitle("Save detector video");progress->setWindowModality(Qt::WindowModal);progress->setMinimumDuration(0);}
    QString escaped=script;escaped.replace("'",QStringLiteral("'\\''"));QProcess ff;ff.setProcessChannelMode(QProcess::SeparateChannels);
    ff.start("ffmpeg",{"-v","error","-y","-i",source,"-vf",QString("ass=filename='%1'").arg(escaped),"-map","0:v:0","-map","0:a?","-c:v","libx264","-preset","veryfast","-crf","19","-pix_fmt","yuv420p","-c:a","aac","-b:a","192k","-movflags","+faststart",path});
    if(!ff.waitForStarted(10000)){if(error)*error="FFmpeg could not start the detector-video export.";return false;}
    while(!ff.waitForFinished(100)){QApplication::processEvents();if(progress&&progress->wasCanceled()){ff.kill();ff.waitForFinished();QFile::remove(path);if(error)*error="Detector-video export canceled.";return false;}}
    if(ff.exitStatus()!=QProcess::NormalExit||ff.exitCode()!=0){if(error)*error="FFmpeg could not save the detector video: "+QString::fromLocal8Bit(ff.readAllStandardError()).trimmed();return false;}
    return QFileInfo(path).size()>10000;
}

bool saveFallingNoteVideo(const QString &path,const QVector<NoteEvent> &notes,int tempo,const QString &title,
                          SynthInstrument instrument,QWidget *parent,QString *error){
    if(notes.isEmpty()){if(error)*error="There are no notes to animate.";return false;}
    QTemporaryDir temp;if(!temp.isValid()){if(error)*error="Could not create the video workspace.";return false;}
    const QString audio=temp.filePath("piano.wav"),script=temp.filePath("falling.ass");
    if(!Synthesizer::saveWav(audio,notes,tempo,error,instrument))return false;
    double lastBeat=0;for(const auto &note:notes)lastBeat=qMax(lastBeat,note.start+note.duration);
    const double secondsPerBeat=60.0/qMax(1,tempo),duration=lastBeat*secondsPerBeat+1.5;
    QFile ass(script);if(!ass.open(QIODevice::WriteOnly|QIODevice::Text)){if(error)*error=ass.errorString();return false;}
    QTextStream out(&ass);out.setEncoding(QStringConverter::Utf8);
    out<<"[Script Info]\nScriptType: v4.00+\nPlayResX: 1280\nPlayResY: 720\nScaledBorderAndShadow: yes\n"
         "[V4+ Styles]\nFormat: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding\n"
         "Style: Art,Noto Sans,34,&H00FFFFFF,&H00FFFFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,0,0,7,0,0,0,1\n"
         "[Events]\nFormat: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text\n";
    out<<QString("Dialogue: 0,0:00:00.00,%1,Art,,0,0,0,,{\\an7\\pos(38,28)\\p1\\bord0\\shad0\\1c&H25170C&}m 0 0 l 1204 0 1204 684 0 684\n").arg(assTime(duration));
    out<<QString("Dialogue: 1,0:00:00.00,%1,Art,,0,0,0,,{\\an7\\pos(48,24)\\p0\\fs26\\b1\\1c&H00FFFFFF&}%2\n").arg(assTime(duration),assEscape(title));
    // The source tutorials reach the keyboard in roughly two seconds.  The
    // old five-second flight compressed every duration bar to less than half
    // the source's apparent length even when its musical duration was right.
    constexpr int low=21,high=108;const double keyboardLeft=38,keyboardWidth=1204,keyboardTop=548,keyboardBottom=710,strike=538,lookAhead=2.2;
    auto black=[](int midi){const int pc=((midi%12)+12)%12;return pc==1||pc==3||pc==6||pc==8||pc==10;};
    int whiteCount=0;for(int midi=low;midi<=high;++midi)if(!black(midi))++whiteCount;const double whiteWidth=keyboardWidth/whiteCount;
    auto keyRect=[&](int midi){int whites=0;for(int n=low;n<=midi;++n)if(!black(n))++whites;if(black(midi))return QRectF(keyboardLeft+(whites-.34)*whiteWidth,keyboardTop,whiteWidth*.68,(keyboardBottom-keyboardTop)*.62);return QRectF(keyboardLeft+(whites-1)*whiteWidth,keyboardTop,whiteWidth,keyboardBottom-keyboardTop);};
    // Full 88-key piano and the blue-green strike line remain visible for the
    // whole export, mirroring the tutorial instead of producing a blank WAV.
    for(int pass=0;pass<2;++pass)for(int midi=low;midi<=high;++midi)if(black(midi)==bool(pass)){
        const QRectF r=keyRect(midi);const QString colour=pass?QStringLiteral("181511"):QStringLiteral("EEEDEA");
        out<<assRectangle(pass?3:2,0,duration,r.x(),r.y(),r.y(),r.width()-1,r.height(),colour);
    }
    out<<assRectangle(4,0,duration,keyboardLeft,strike,strike,keyboardWidth,3,QStringLiteral("B8C466"));
    const double pixelsPerSecond=(strike-40.0)/lookAhead;
    for(const auto &note:notes){
        const double attack=note.start*secondsPerBeat,release=(note.start+note.duration)*secondsPerBeat;
        const double visible=qMax(0.0,attack-lookAhead),barHeight=qMax(10.0,(release-attack)*pixelsPerSecond);
        const double initialTop=strike-barHeight-(attack-visible)*pixelsPerSecond;
        const QRectF key=keyRect(note.note);const double barWidth=qMax(7.0,key.width()*.76),x=key.center().x()-barWidth*.5;
        const QString colour=note.hand==Hand::Left?QStringLiteral("5F77EF"):QStringLiteral("B8CB77");
        out<<assRectangle(5,visible,release,x,initialTop,strike,barWidth,barHeight,colour);
        out<<assRectangle(6,attack,release,key.x()+1,key.y(),key.y(),key.width()-2,key.height(),colour);
    }
    ass.close();
    std::unique_ptr<QProgressDialog> progress;
    if(parent){
        progress=std::make_unique<QProgressDialog>("Rendering piano visualization…","Cancel",0,0,parent);
        progress->setWindowTitle("Save falling-note video");progress->setWindowModality(Qt::WindowModal);progress->setMinimumDuration(0);
    }
    QProcess ff;ff.setProcessChannelMode(QProcess::SeparateChannels);
    QString escapedScript=script;escapedScript.replace("'",QStringLiteral("'\\''"));
    const QString filter=QString("ass=filename='%1'").arg(escapedScript);
    ff.start("ffmpeg",{"-v","error","-y","-f","lavfi","-i",QString("color=c=0x0c1725:s=1280x720:r=30:d=%1").arg(duration,0,'f',3),"-i",audio,"-vf",filter,"-map","0:v:0","-map","1:a:0","-c:v","libx264","-preset","veryfast","-crf","20","-pix_fmt","yuv420p","-c:a","aac","-b:a","256k","-shortest",path});
    if(!ff.waitForStarted(10000)){if(error)*error="FFmpeg could not start the video export.";return false;}
    while(!ff.waitForFinished(100)){QApplication::processEvents();if(progress&&progress->wasCanceled()){ff.kill();ff.waitForFinished();QFile::remove(path);if(error)*error="Video export canceled.";return false;}}
    if(ff.exitStatus()!=QProcess::NormalExit||ff.exitCode()!=0){if(error)*error="FFmpeg could not render the visualization: "+QString::fromLocal8Bit(ff.readAllStandardError()).trimmed();return false;}
    return QFileInfo(path).size()>10000;
}

QString matchingScoreSidecar(const QFileInfo &pdf) {
    const QString wantedBase=pdf.completeBaseName();
    const QStringList priority={QStringLiteral("varpeggio"),QStringLiteral("musicxml"),QStringLiteral("mxl"),
                                QStringLiteral("xml"),QStringLiteral("mid"),QStringLiteral("midi")};
    const QFileInfoList files=QDir(pdf.absolutePath()).entryInfoList(QDir::Files|QDir::Readable,QDir::Name);
    for (const QString &extension : priority)
        for (const QFileInfo &candidate : files)
            if (candidate.completeBaseName().compare(wantedBase,Qt::CaseInsensitive)==0
                && candidate.suffix().compare(extension,Qt::CaseInsensitive)==0)
                return candidate.absoluteFilePath();
    return {};
}

QString scorePosition(double beat,QVector<TimeSignatureEvent> signatures) {
    if(signatures.isEmpty())signatures.append({0.0,4,4});
    std::sort(signatures.begin(),signatures.end(),[](const auto &a,const auto &b){return a.beat<b.beat;});
    if(signatures.first().beat>.001)signatures.prepend({0.0,4,4});
    int measure=1;
    for(int index=0;index<signatures.size();++index){
        const auto &meter=signatures[index];
        const double unit=4.0/qMax(1,meter.beatType),length=qMax(.125,meter.beats*unit);
        const double segmentEnd=index+1<signatures.size()?signatures[index+1].beat:std::numeric_limits<double>::max();
        if(beat<segmentEnd-.001){
            const double local=qMax(0.0,beat-meter.beat);measure+=int(std::floor(local/length));
            const double beatInMeasure=std::fmod(local,length)/unit+1.0;
            return QString("MEASURE %1 · BEAT %2").arg(measure).arg(beatInMeasure,0,'f',qAbs(beatInMeasure-qRound(beatInMeasure))<.001?0:2);
        }
        measure+=qMax(1,int(std::ceil(qMax(0.0,segmentEnd-meter.beat)/length-.0001)));
    }
    return QString("MEASURE %1").arg(measure);
}

void showPdfRecognitionDialog(QWidget *parent,const QString &title,const QString &message) {
    QDialog dialog(parent);dialog.setWindowTitle(title);dialog.setMinimumWidth(660);
    auto*layout=new QVBoxLayout(&dialog);layout->setContentsMargins(24,20,24,18);layout->setSpacing(14);
    auto*heading=label(title);heading->setStyleSheet("font-size:19px;font-weight:800");layout->addWidget(heading);
    auto*body=label(message);body->setWordWrap(true);body->setTextInteractionFlags(Qt::TextSelectableByMouse);body->setMinimumWidth(590);layout->addWidget(body);
    auto*buttons=new QDialogButtonBox(QDialogButtonBox::Close);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);layout->addWidget(buttons);
    dialog.exec();
}
}

bool MainWindow::saveFallingNoteVideoFile(const QString &path,const QVector<NoteEvent> &notes,int tempo,
                                          const QString &title,SynthInstrument instrument,QWidget *parent,
                                          QString *error){
    return saveFallingNoteVideo(path,notes,tempo,title,instrument,parent,error);
}

bool MainWindow::saveDetectorVideoFile(const QString &path,const QString &source,
                                       const QVector<NoteEvent> &notes,int tempo,double sourceStart,
                                       const TranscriptionInfo &geometry,QWidget *parent,QString *error){
    return saveDetectorVideo(path,source,notes,tempo,sourceStart,geometry,parent,error);
}

MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),m_synth(this),m_midi(this),m_midiOutput(this){
    setWindowTitle("VArpeggio — piano practice & notation studio");setMinimumSize(1080,760);resize(1420,930);
    m_sourceAudioOutput=new QAudioOutput(this);m_sourcePlayer=new QMediaPlayer(this);m_sourcePlayer->setAudioOutput(m_sourceAudioOutput);
    m_keyMap={{Qt::Key_A,60},{Qt::Key_W,61},{Qt::Key_S,62},{Qt::Key_E,63},{Qt::Key_D,64},{Qt::Key_F,65},{Qt::Key_T,66},{Qt::Key_G,67},{Qt::Key_Y,68},{Qt::Key_H,69},{Qt::Key_U,70},{Qt::Key_J,71},{Qt::Key_K,72}};
    auto*root=new QWidget;auto*rootLayout=new QVBoxLayout(root);rootLayout->setContentsMargins(0,0,0,0);rootLayout->setSpacing(0);rootLayout->addWidget(buildHeader());auto*body=new QWidget;auto*bodyLayout=new QHBoxLayout(body);bodyLayout->setContentsMargins(0,0,0,0);bodyLayout->setSpacing(0);bodyLayout->addWidget(buildSidebar());bodyLayout->addWidget(buildWorkspace(),1);rootLayout->addWidget(body,1);setCentralWidget(root);
    buildMenus();applyStyle();connectUi();setSong("Morning Light",MusicDocument::morningLight(),96);setMode(Mode::Wait);
    m_frameTimer.setInterval(25);connect(&m_frameTimer,&QTimer::timeout,this,&MainWindow::updateClocks);m_frameTimer.start();m_metroClock.start();m_waitAdvanceClock.start();m_midi.start();statusBar()->hide();
}
MainWindow::~MainWindow(){if(m_sourcePlayer)m_sourcePlayer->stop();m_midi.stop();m_midi.wait(1000);}
void MainWindow::openPath(const QString &path){openScore(path);}
void MainWindow::showLiveScore(){if(m_liveScoreButton&&m_liveScoreButton->isEnabled())m_liveScoreButton->click();}
void MainWindow::startListening(){setMode(Mode::Listen);if(!m_playing)togglePlayback();}
void MainWindow::setImportedMediaSource(const QString &path,double scoreStartSeconds){
    if(!m_sourcePlayer||!m_sourceAudio)return;
    const QFileInfo source(path);
    if(!source.exists()||!source.isFile()){clearImportedMediaSource();return;}
    m_sourcePlayer->stop();m_importedMediaPath=source.absoluteFilePath();m_importedMediaStartMs=qMax<qint64>(0,qRound64(scoreStartSeconds*1000.0));m_importedMediaTempo=m_tempo;
    m_sourcePlayer->setSource(QUrl::fromLocalFile(m_importedMediaPath));
    m_sourcePlayer->setPlaybackRate(1.0);
    // Generated notation is the primary result and therefore the default
    // playback.  The untouched recording remains available only as a clearly
    // labelled reference; it may contain notes the draft did not capture.
    const QSignalBlocker blocker(m_sourceAudio);
    m_sourceAudio->setEnabled(true);m_sourceAudio->setChecked(false);
    m_sourceAudio->setToolTip(QString("Reference only: hear the untouched %1 instead of the generated staff notes").arg(source.fileName()));
    if(m_detectorButton){const bool available=!m_videoDetectionInfo.laneCenters.isEmpty();m_detectorButton->setVisible(available);m_detectorButton->setEnabled(available);}
}

void MainWindow::clearImportedMediaSource(){
    if(!m_sourcePlayer||!m_sourceAudio)return;
    m_sourcePlayer->stop();m_sourcePlayer->setPlaybackRate(1.0);m_sourcePlayer->setSource(QUrl());m_importedMediaPath.clear();m_importedMediaStartMs=0;m_importedMediaTempo=0;m_videoDetectionInfo=TranscriptionInfo{};
    const QSignalBlocker blocker(m_sourceAudio);m_sourceAudio->setChecked(false);m_sourceAudio->setEnabled(false);
    m_sourceAudio->setToolTip("Import audio or video to make its untouched recording available for an A/B comparison");
    if(m_detectorButton)m_detectorButton->hide();
}

bool MainWindow::useImportedMediaAudio()const{return m_mode==Mode::Listen&&m_sourceAudio&&m_sourceAudio->isChecked()&&!m_importedMediaPath.isEmpty();}

QWidget *MainWindow::buildHeader() {
  auto *w = new QFrame;
  w->setObjectName("header");
  w->setFixedHeight(68);
  auto *l = new QHBoxLayout(w);
  l->setContentsMargins(25, 0, 22, 0);
  auto *mark = label("▥", "brandMark");
  auto *brand = label("VArpeggio", "brand");
  l->addWidget(mark);
  l->addWidget(brand);
  l->addStretch();
  m_midiButton = button("●   Connect MIDI keyboard", "midiButton");
  l->addWidget(m_midiButton);
  l->addStretch();
  auto *help = button("?", "roundButton");
  help->setToolTip("Keyboard shortcuts");
  connect(help, &QPushButton::clicked, this, [this] {
    QMessageBox::information(
        this, "VArpeggio shortcuts",
        "A W S E D F T G Y H U J K   Play from C4\nSpace   Start, stop, or "
        "hear the ear-training sound again\nR   Start or stop Free Play "
        "recording\nEsc   Stop everything");
  });
  l->addWidget(help);
  auto *avatar = button("VA", "avatar");
  l->addWidget(avatar);
  return w;
}

QWidget *MainWindow::buildSidebar() {
  auto *scroll = new QScrollArea;
  scroll->setObjectName("sidebar");
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll->viewport()->setObjectName("sidebarViewport");
  auto *w = new QFrame;
  w->setObjectName("sidebarContent");
  // KDE may be configured with a substantially larger system font.  Keep the
  // user's chosen size and give the two-line navigation copy room instead of
  // clipping it inside the original compact sidebar.
  const QFontMetrics systemMetrics(QApplication::font());
  const int sidebarWidth =
      qMax(340, systemMetrics.horizontalAdvance("Experimental transcription") + 100);
  w->setMinimumWidth(sidebarWidth);
  scroll->setFixedWidth(sidebarWidth + 14);
  auto *l = new QVBoxLayout(w);
  l->setContentsMargins(14, 23, 14, 16);
  l->setSpacing(5);
  l->addWidget(label("STUDIO", "sideHeading"));
  m_modeButtons = new QButtonGroup(this);
  m_modeButtons->setExclusive(true);
  struct Entry {
    QString text, tip;
    Mode mode;
    QString icon;
  };
  QVector<Entry> entries = {
      {"Wait mode", "Waits for the right key", Mode::Wait, "◎"},
      {"Listen", "Hear the imported sheet", Mode::Listen, "♫"},
      {"Play along", "Follow the tempo", Mode::Follow, "▶"},
      {"Free play", "Play freely or record", Mode::Free, "⌁"}};
  for (auto &e : entries) {
    auto *b =
        button(e.icon + "    " + e.text + "\n       " + e.tip, "modeButton");
    b->setCheckable(true);
    b->setProperty("mode", int(e.mode));
    b->setMinimumHeight(qMax(68, systemMetrics.height() * 2 + 20));
    m_modeButtons->addButton(b);
    m_modeButtons->setId(b, int(e.mode));
    l->addWidget(b);
  }
  m_modeButtons->button(int(Mode::Wait))->setChecked(true);
  l->addSpacing(10);
  l->addWidget(line());
  l->addSpacing(7);
  l->addWidget(label("MUSIC", "sideHeading"));
  auto *morning =
      button("𝄞    Morning Light\n       18 notes · beginner", "libraryButton");
  auto *ode =
      button("♫    Ode to Joy\n       23 notes · beginner", "libraryButton");
  auto *import = button("＋  Import score…", "importButton");
  morning->setMinimumHeight(qMax(68, systemMetrics.height() * 2 + 20));
  ode->setMinimumHeight(qMax(68, systemMetrics.height() * 2 + 20));
  import->setMinimumHeight(qMax(46, systemMetrics.height() + 18));
  l->addWidget(morning);
  l->addWidget(ode);
  l->addWidget(import);
  connect(morning, &QPushButton::clicked, this, [this] {
    setSong("Morning Light", MusicDocument::morningLight(), 96);
  });
  connect(ode, &QPushButton::clicked, this,
          [this] { setSong("Ode to Joy", MusicDocument::odeToJoy(), 92); });
  connect(import, &QPushButton::clicked, this, &MainWindow::importScore);
  auto *daily = new QFrame;
  daily->setObjectName("dailyCard");
  daily->setMinimumHeight(qMax(176, systemMetrics.height() * 4 + 78));
  auto *dl = new QVBoxLayout(daily);
  dl->setContentsMargins(12, 11, 12, 11);
  auto *top = new QHBoxLayout;
  auto *day =
      label(QDate::currentDate().toString("dddd") + " focus", "dailyTitle");
  top->addWidget(day);
  top->addStretch();
  top->addWidget(label("14 MIN", "dailyTime"));
  dl->addLayout(top);
  dl->addWidget(label("●  ●  ●  ○  ◉  ○  ○", "weekDots"));
  dl->addWidget(label("SCALE   CHORDS   EAR   SONG", "routineParts"));
  auto *routine = button("Open practice plan      →", "routineButton");
  routine->setMinimumHeight(qMax(46, systemMetrics.height() + 18));
  dl->addWidget(routine);
  connect(routine, &QPushButton::clicked, this, &MainWindow::openRoutineDialog);
  l->addWidget(daily);
  l->addStretch();
  auto *lab =
      button("✦   Video → notes       LAB\n      Experimental transcription",
             "labButton");
  lab->setMinimumHeight(qMax(72, systemMetrics.height() * 2 + 20));
  l->addWidget(lab);
  connect(lab, &QPushButton::clicked, this, &MainWindow::openVideoLab);
  scroll->setWidget(w);
  return scroll;
}

QWidget *MainWindow::makeCard(const QString &name) {
  auto *w = new QFrame;
  if (!name.isEmpty())
    w->setObjectName(name);
  else
    w->setObjectName("card");
  shadow(w);
  return w;
}

QWidget *MainWindow::buildWorkspace() {
  auto *scroll = new QScrollArea;
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setObjectName("workspaceScroll");
  auto *page = new QWidget;
  page->setObjectName("workspace");
  auto *l = new QVBoxLayout(page);
  l->setContentsMargins(34, 27, 34, 38);
  l->setSpacing(14);
  auto *songRow = new QHBoxLayout;
  auto *songInfo = new QVBoxLayout;
  m_modeLabel = label("WAIT MODE · PRACTICE", "eyebrow");
  m_titleEdit = new QLineEdit("Morning Light");
  m_titleEdit->setObjectName("songTitle");
  m_titleEdit->setMinimumWidth(0);
  m_titleEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto *meta = new QHBoxLayout;
  m_statusLabel = label("C major", "meta");
  m_noteCountLabel = label("18 notes", "meta");
  m_durationLabel = label("0:15", "meta");
  meta->addWidget(m_statusLabel);
  meta->addWidget(label("·", "meta"));
  meta->addWidget(m_noteCountLabel);
  meta->addWidget(label("·", "meta"));
  meta->addWidget(m_durationLabel);
  meta->addSpacing(12);
  m_sourceAudio = new QCheckBox("Play original recording");
  m_sourceAudio->setEnabled(false);
  m_sourceAudio->setToolTip("A/B comparison: use the untouched imported audio "
                            "instead of VArpeggio's generated notes");
  meta->addWidget(m_sourceAudio);
  m_detectorButton = button("Open Director", "detectorButton");
  m_detectorButton->setToolTip("Play the source video with VArpeggio's "
                               "accepted note bars drawn over it");
  m_detectorButton->setMinimumSize(124, 32);
  m_detectorButton->setStyleSheet(
      "QPushButton{color:#ffffff;background:#2d7d70;border:2px solid "
      "#205e54;border-radius:7px;padding:6px "
      "12px;font-weight:800}QPushButton:hover{background:#235f56;border-color:#"
      "173f39}QPushButton:pressed{background:#173f39}QPushButton:disabled{"
      "color:#40514d;background:#c8d0cd;border-color:#a9b5b1}");
  m_detectorButton->hide();
  meta->addWidget(m_detectorButton);
  meta->addStretch();
  songInfo->addWidget(m_modeLabel);
  songInfo->addWidget(m_titleEdit);
  songInfo->addLayout(meta);
  songRow->addLayout(songInfo, 1);
  auto *transport = new QHBoxLayout;
  auto *rewind = button("↶", "transportButton");
  m_playButton = button("▶  Listen", "playButton");
  m_playButton->setToolTip("Listen to the loaded score (Space)");
  auto *stop = button("■", "transportButton");
  transport->addWidget(rewind);
  transport->addWidget(m_playButton);
  transport->addWidget(stop);
  auto *tempoDown = button("−", "tempoButton");
  m_tempoControl = new QSpinBox;
  m_tempoControl->setObjectName("tempoControl");
  m_tempoControl->setRange(30, 260);
  m_tempoControl->setValue(96);
  m_tempoControl->setSuffix(" BPM");
  m_tempoControl->setAlignment(Qt::AlignCenter);
  m_tempoControl->setButtonSymbols(QAbstractSpinBox::NoButtons);
  m_tempoControl->setFixedWidth(
      qMax(112, QFontMetrics(QApplication::font()).horizontalAdvance("260 BPM") + 30));
  m_tempoControl->setToolTip("Type an exact tempo; changes immediately retime "
                             "sound, live score, and falling notes");
  auto *tempoUp = button("＋", "tempoButton");
  transport->addSpacing(7);
  transport->addWidget(tempoDown);
  transport->addWidget(m_tempoControl);
  transport->addWidget(tempoUp);
  songRow->addLayout(transport);
  m_exportCombo = new QComboBox;
  m_exportCombo->setObjectName("exportCombo");
  for (auto item :
       QVector<QPair<QString, QString>>{{"MIDI (.mid)", "mid"},
                                        {"MusicXML", "musicxml"},
                                        {"ABC notation", "abc"},
                                        {"Audio (.wav)", "wav"},
                                        {"Falling-note video (.mp4)", "video"},
                                        {"Score (.svg)", "svg"},
                                        {"Score (.png)", "png"},
                                        {"Score (.pdf)", "pdf"},
                                        {"VArpeggio project", "json"},
                                        {"Note list (.csv)", "csv"}})
    m_exportCombo->addItem(item.first, item.second);
  auto *save = button("Save", "saveButton");
  auto *saveLayout = new QHBoxLayout;
  saveLayout->setSpacing(0);
  saveLayout->addWidget(m_exportCombo);
  saveLayout->addWidget(save);
  songRow->addSpacing(24);
  songRow->addLayout(saveLayout);
  l->addLayout(songRow);
  connect(rewind, &QPushButton::clicked, this, [this] {
    stopPlayback(true);
    rebuildPractice();
  });
  connect(stop, &QPushButton::clicked, this, [this] {
    stopPlayback(true);
    if (m_mode == Mode::Wait)
      rebuildPractice();
  });
  connect(tempoDown, &QPushButton::clicked, this,
          [this] { m_tempoControl->setValue(m_tempoControl->value() - 1); });
  connect(tempoUp, &QPushButton::clicked, this,
          [this] { m_tempoControl->setValue(m_tempoControl->value() + 1); });
  connect(m_tempoControl, qOverload<int>(&QSpinBox::valueChanged), this,
          &MainWindow::changeTempo);
  connect(save, &QPushButton::clicked, this, &MainWindow::exportScore);
  m_notice = new QFrame;
  m_notice->setObjectName("notice");
  auto *nl = new QHBoxLayout(m_notice);
  nl->setContentsMargins(12, 8, 8, 8);
  m_noticeText = label("");
  m_noticeText->setWordWrap(true);
  nl->addWidget(label("i", "noticeIcon"));
  nl->addWidget(m_noticeText, 1);
  m_noticeClose = button("×", "noticeClose");
  nl->addWidget(m_noticeClose);
  m_notice->hide();
  l->addWidget(m_notice);
  m_routineSession = new QFrame;
  m_routineSession->setObjectName("routineSession");
  auto *rsl = new QHBoxLayout(m_routineSession);
  rsl->setContentsMargins(14, 9, 10, 9);
  rsl->setSpacing(10);
  auto *routineText = new QVBoxLayout;
  routineText->setSpacing(1);
  m_routineStepLabel = label("DAILY PLAN · STEP 1 OF 8", "routineSessionStep");
  m_routineDetailLabel =
      label("C major scale · Right hand", "routineSessionDetail");
  routineText->addWidget(m_routineStepLabel);
  routineText->addWidget(m_routineDetailLabel);
  rsl->addLayout(routineText, 1);
  m_routinePreviousButton = button("← Previous", "routineSessionSecondary");
  m_routineNextButton = button("Next step →", "routineSessionPrimary");
  auto *endRoutine = button("Close plan", "routineSessionSecondary");
  for (auto *b : {m_routinePreviousButton, m_routineNextButton, endRoutine})
    b->setMinimumHeight(34);
  rsl->addWidget(m_routinePreviousButton);
  rsl->addWidget(m_routineNextButton);
  rsl->addWidget(endRoutine);
  connect(m_routinePreviousButton, &QPushButton::clicked, this, [this] {
    if (m_routineStep > 0)
      loadRoutineStep(m_routineStep - 1);
  });
  connect(m_routineNextButton, &QPushButton::clicked, this, [this] {
    if (m_routineStep < 7)
      loadRoutineStep(m_routineStep + 1);
    else
      finishRoutinePlan();
  });
  connect(endRoutine, &QPushButton::clicked, this, [this] {
    m_earTraining = false;
    m_synth.stopSequence();
    m_routineStep = -1;
    m_routineSession->hide();
    toast("Practice plan closed");
  });
  m_routineSession->hide();
  l->addWidget(m_routineSession);
  auto *scoreCard = makeCard("card");
  auto *sl = new QVBoxLayout(scoreCard);
  sl->setContentsMargins(0, 0, 0, 0);
  sl->setSpacing(0);
  auto *scoreTop = new QWidget;
  scoreTop->setObjectName("cardTop");
  auto *st = new QHBoxLayout(scoreTop);
  st->setContentsMargins(16, 7, 13, 7);
  m_barLabel = label("SCORE   ·   Bar 1 of 3", "sectionHeading");
  st->addWidget(m_barLabel);
  m_freeEditButton = button("✎  Edit staff…", "freeStaffButton");
  m_freeEditButton->setToolTip("Change pitch, timing, length, hand, or remove "
                               "notes from the Free Play grand staff");
  m_freeClearButton = button("×  Clear staff", "freeStaffButton");
  m_freeClearButton->setToolTip(
      "Remove every note from the Free Play grand staff");
  m_freeEditButton->hide();
  m_freeClearButton->hide();
  st->addWidget(m_freeEditButton);
  st->addWidget(m_freeClearButton);
  m_scoreDisplayButtons = new QButtonGroup(this);
  m_scoreDisplayButtons->setExclusive(true);
  m_scoreViewButton = button("Exact PDF", "scoreViewButton");
  m_scoreViewButton->setCheckable(true);
  m_scoreViewButton->setToolTip(
      "Show the page-perfect original or engraved score");
  m_liveScoreButton = button("Live follow", "scoreViewButton");
  m_liveScoreButton->setCheckable(true);
  m_liveScoreButton->setToolTip(
      "Show simplified practice notation that moves with your place");
  m_scoreDisplayButtons->addButton(m_scoreViewButton, 0);
  m_scoreDisplayButtons->addButton(m_liveScoreButton, 1);
  m_scoreViewButton->hide();
  m_liveScoreButton->hide();
  st->addWidget(m_scoreViewButton);
  st->addWidget(m_liveScoreButton);
  st->addStretch();
  m_handButtons = new QButtonGroup(this);
  m_handButtons->setExclusive(true);
  auto *hands = new QWidget;
  hands->setObjectName("hands");
  auto *hl = new QHBoxLayout(hands);
  hl->setContentsMargins(2, 2, 2, 2);
  hl->setSpacing(0);
  for (auto h : QVector<QPair<QString, int>>{
           {"Right", 1}, {"Left", 0}, {"Together", 2}}) {
    auto *b = button(h.first, "handButton");
    b->setCheckable(true);
    m_handButtons->addButton(b, h.second);
    hl->addWidget(b);
  }
  m_handButtons->button(2)->setChecked(true);
  st->addWidget(hands);
  m_metronome = new QCheckBox("Metronome");
  m_noteNames = new QCheckBox("Note names");
  m_noteNames->setToolTip(
      "Optional learning aid: show pitch names beside notes");
  m_bubbles = new QCheckBox("Falling notes");
  m_bubbles->setChecked(true);
  for (auto *c : {m_metronome, m_noteNames, m_bubbles})
    st->addWidget(c);
  sl->addWidget(scoreTop);
  m_scoreStack = new QStackedWidget;
  m_score = new ScoreWidget;
  m_scoreStack->addWidget(m_score);
  m_pdfDocument = new QPdfDocument(this);
  m_pdfView = new QPdfView;
  m_pdfView->setDocument(m_pdfDocument);
  m_pdfView->setPageMode(QPdfView::PageMode::MultiPage);
  m_pdfView->setZoomMode(QPdfView::ZoomMode::FitToWidth);
  m_scoreStack->addWidget(m_pdfView);
  m_scoreStack->setMinimumHeight(520);
  sl->addWidget(m_scoreStack, 1);
  m_scoreHistoryRow = new QWidget;
  m_scoreHistoryRow->setObjectName("scoreHistoryRow");
  auto *historyLayout = new QHBoxLayout(m_scoreHistoryRow);
  historyLayout->setContentsMargins(14, 5, 14, 6);
  historyLayout->setSpacing(8);
  historyLayout->addWidget(label("← EARLIER", "historyLabel"));
  m_scoreHistoryBar = new QScrollBar(Qt::Horizontal);
  m_scoreHistoryBar->setObjectName("scoreHistoryBar");
  m_scoreHistoryBar->setRange(0, 0);
  m_scoreHistoryBar->setSingleStep(1);
  m_scoreHistoryBar->setPageStep(32);
  m_scoreHistoryBar->setToolTip("Drag left to review an earlier measure; "
                                "move fully right to follow the live measure again");
  historyLayout->addWidget(m_scoreHistoryBar, 1);
  historyLayout->addWidget(label("LATEST →", "historyLabel"));
  m_scoreHistoryRow->hide();
  sl->addWidget(m_scoreHistoryRow);
  l->addWidget(scoreCard, 3);
  auto *practice = new QHBoxLayout;
  practice->setAlignment(Qt::AlignTop);
  auto *flow = makeCard("flowCard");
  flow->setFixedHeight(300);
  auto *fl = new QVBoxLayout(flow);
  fl->setContentsMargins(0, 0, 0, 0);
  fl->setSpacing(0);
  auto *flowTop = new QWidget;
  flowTop->setObjectName("flowTop");
  auto *ft = new QHBoxLayout(flowTop);
  ft->setContentsMargins(16, 6, 14, 6);
  ft->addWidget(
      label("NOTE FLOW   ·   Notes move to the play line", "flowHeading"));
  ft->addStretch();
  ft->addWidget(label("● NEXT     ● PLAYED", "flowLegend"));
  fl->addWidget(flowTop);
  m_bubble = new BubbleWidget;
  m_bubble->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  fl->addWidget(m_bubble, 1);
  practice->addWidget(flow, 1);
  auto *coach = makeCard("coachCard");
  coach->setFixedWidth(330);
  coach->setFixedHeight(300);
  auto *cl = new QVBoxLayout(coach);
  cl->setContentsMargins(15, 14, 15, 12);
  cl->addWidget(label("LIVE COACH", "coachHeading"));
  auto *next = new QFrame;
  next->setObjectName("nextNote");
  auto *nextL = new QVBoxLayout(next);
  nextL->setContentsMargins(10, 7, 10, 7);
  nextL->addWidget(label("UP NEXT", "miniHeading"));
  m_nextNoteLabel = label("C4", "nextNoteText");
  m_nextNoteLabel->setAlignment(Qt::AlignCenter);
  m_nextNoteLabel->setWordWrap(true);
  m_nextNoteLabel->setMinimumWidth(0);
  m_nextNoteLabel->setFixedHeight(58);
  m_nextNoteLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  nextL->addWidget(m_nextNoteLabel);
  cl->addWidget(next);
  m_coachLabel = label(
      "Find middle C\nTake your time. The music waits for you.", "coachText");
  m_coachLabel->setWordWrap(true);
  m_coachLabel->setMinimumHeight(62);
  cl->addWidget(m_coachLabel);
  cl->addStretch();
  cl->addWidget(line());
  auto *stats = new QHBoxLayout;
  m_accuracyLabel = label("—\nACCURACY", "stat");
  m_streakLabel = label("0\nSTREAK", "stat");
  m_progressLabel = label("0%\nPROGRESS", "stat");
  for (auto *s : {m_accuracyLabel, m_streakLabel, m_progressLabel}) {
    s->setAlignment(Qt::AlignCenter);
    stats->addWidget(s);
  }
  cl->addLayout(stats);
  practice->addWidget(coach);
  l->addLayout(practice);
  auto *keyboard = makeCard("card");
  keyboard->setFixedHeight(235);
  auto *kl = new QVBoxLayout(keyboard);
  kl->setContentsMargins(0, 0, 0, 0);
  kl->setSpacing(0);
  auto *kTop = new QWidget;
  kTop->setObjectName("cardTop");
  auto *kRows = new QVBoxLayout(kTop);
  kRows->setContentsMargins(16, 6, 13, 7);
  kRows->setSpacing(3);
  auto *kt = new QHBoxLayout;
  kt->setSpacing(8);
  kt->addWidget(label("KEYBOARD", "sectionHeading"));
  m_liveNoteLabel = label("Ready", "liveNote");
  kt->addWidget(m_liveNoteLabel);
  kt->addStretch();
  kt->addWidget(
      label("Computer keys: A W S E D F T G Y H U J K", "computerHint"));
  m_recordButton = button("●  Record free play", "recordButton");
  m_recordButton->setToolTip(
      "Optional: capture this Free Play session and turn it into a score");
  kt->addWidget(m_recordButton);
  kRows->addLayout(kt);
  auto *audioRow = new QHBoxLayout;
  audioRow->setSpacing(8);
  audioRow->addWidget(label("PIANO SOUND", "sectionHeading"));
  m_instrumentCombo = new QComboBox;
  m_instrumentCombo->setObjectName("instrumentCombo");
  m_instrumentCombo->addItem("Kawai upright — bright",
                             int(SynthInstrument::BrightPiano));
  m_instrumentCombo->addItem("Kawai upright — natural",
                             int(SynthInstrument::AcousticGrand));
  m_instrumentCombo->addItem("Kawai upright — soft",
                             int(SynthInstrument::SoftPiano));
  m_instrumentCombo->setToolTip("Direct playback of the recorded Kawai upright "
                                "piano; natural is the default");
  m_instrumentCombo->setFixedWidth(190);
  audioRow->addWidget(m_instrumentCombo);
  m_sound = new QCheckBox("Sound");
  m_sound->setChecked(true);
  audioRow->addWidget(m_sound);
  m_midiOutputCheck = new QCheckBox("Use keyboard MIDI sound");
  m_midiOutputCheck->setToolTip(
      "Send score notes through the adapter MIDI OUT to the keyboard MIDI IN; "
      "VArpeggio's piano is silenced during score playback");
  audioRow->addWidget(m_midiOutputCheck);
  m_keyboardToneCombo = new QComboBox;
  m_keyboardToneCombo->setObjectName("keyboardToneCombo");
  const QStringList ctkTones = {
      "PIANO 1",      "PIANO 2",      "HONKY-TONK",   "STUDIO PIANO",
      "ELEC PIANO 1", "ELEC PIANO 2", "ELEC PIANO 3", "ELEC PIANO 4",
      "HARPSICHORD",  "CLAVELECTRO",  "ELEC ORGAN 1", "ELEC ORGAN 2",
      "ELEC ORGAN 3", "ELEC ORGAN 4", "CHURCH ORGAN", "PIPE ORGAN",
      "REED ORGAN",   "ACCORDION",    "BANDONEON",    "HARMONICA",
      "GUT GUITAR",   "ACOUS GUITAR", "JAZZ GUITAR",  "ELEC GUITAR",
      "MUTE GUITAR",  "DIST GUITAR",  "WOOD BASS",    "ELEC BASS",
      "SLAP BASS",    "BANJO",        "VIOLIN",       "CELLO",
      "HARP",         "STRINGS 1",    "STRINGS 2",    "STRINGS 3",
      "SYNTH-STR 1",  "SYNTH-STR 2",  "CHOIR",        "VOICE OOH",
      "TRUMPET",      "TUBA",         "BRASS ENS",    "FR.HORN 1",
      "FR.HORN 2",    "BRASS 1",      "BRASS 2",      "SYN-BRASS 1",
      "SYN-BRASS 2",  "SYN-BRASS 3",  "SOPRANO SAX",  "ALTO SAX",
      "TENOR SAX",    "OBOE",         "CLARINET",     "PICCOLO",
      "FLUTE",        "RECORDER",     "PAN FLUTE",    "WHISTLE",
      "SYN-LEAD 1",   "SYN-LEAD 2",   "SYN-LEAD 3",   "SYN-CALLIOPE",
      "SYN-PAD 1",    "SYN-PAD 2",    "SYN-PAD 3",    "GLASS HMCA",
      "COUNTRY FARM", "SYN-BASS",     "PEARL DROP",   "COSMIC SOUND",
      "SOUNDTRACK",   "SPACE PAD",    "VOICE BASS",   "APPLAUSE",
      "SYNTH-SFX",    "VEHICLE",      "PHONE",        "FUNNY",
      "STR PIANO",    "STR E.PIANO",  "CHOIR E.P",    "CHOIR ORGAN",
      "STR GUITAR",   "STR HARP",     "BRASS STR",    "VIB PAD",
      "12 STR GTR",   "CHOIR STR",    "BASS/PIANO",   "BASS/E.PIANO",
      "BASS/VIB",     "BASS/GUT GTR", "V.BASS/OOH",   "STR/PIANO",
      "STR/TRUMPET",  "VIBRAPHONE",   "MARIMBA",      "PERCUSSION"};
  for (int tone = 0; tone < ctkTones.size(); ++tone)
    m_keyboardToneCombo->addItem(
        QString("%1  %2").arg(tone, 2, 10, QChar('0')).arg(ctkTones[tone]),
        tone);
  m_keyboardToneCombo->setCurrentIndex(0);
  m_keyboardToneCombo->setFixedWidth(174);
  m_keyboardToneCombo->setToolTip(
      "CTK-511 built-in tone. With the keyboard connected, selection works in "
      "both directions. Set the keyboard TONE MAP to N for its native 00–99 "
      "list.");
  audioRow->addWidget(m_keyboardToneCombo);
  m_volumeLabel = label("Volume 115%", "volumeLabel");
  m_volumeLabel->setFixedWidth(88);
  m_volumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  audioRow->addWidget(m_volumeLabel);
  m_volume = new QSlider(Qt::Horizontal);
  m_volume->setObjectName("volumeSlider");
  m_volume->setRange(0, 150);
  m_volume->setValue(115);
  m_volume->setFixedWidth(155);
  m_volume->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  m_volume->setToolTip("Playback and practice volume");
  audioRow->addWidget(m_volume);
  audioRow->addStretch();
  kRows->addLayout(audioRow);
  kl->addWidget(kTop);
  m_piano = new PianoWidget;
  kl->addWidget(m_piano, 1);
  l->addWidget(keyboard, 2);
  scroll->setWidget(page);
  return scroll;
}

void MainWindow::buildMenus() {
  auto *file = menuBar()->addMenu("&File");
  auto *open = file->addAction("&Import score…", QKeySequence::Open);
  connect(open, &QAction::triggered, this, &MainWindow::importScore);
  auto *exp = file->addAction("&Export…", QKeySequence::SaveAs);
  connect(exp, &QAction::triggered, this, &MainWindow::exportScore);
  file->addSeparator();
  file->addAction("Quit", this, &QWidget::close, QKeySequence::Quit);
  auto *practice = menuBar()->addMenu("&Practice");
  practice->addAction("Daily practice plan…", this,
                      &MainWindow::openRoutineDialog);
  practice->addAction("Ear training — Find the sound", this, [this] {
    m_routinePlanDate = QDate::currentDate();
    m_routineVariation = dailyRoutineTheme(m_routinePlanDate).chordOrder;
    loadRoutineStep(4);
  });
  practice->addSeparator();
  practice->addAction("Video/audio to notes…", this, &MainWindow::openVideoLab);
  practice->addAction("Open Director…", this, &MainWindow::openDetectorView);
  auto *help = menuBar()->addMenu("&Help");
  help->addAction("About VArpeggio", this, [this] {
    QMessageBox::about(
        this, "About VArpeggio",
        QString("<h2>VArpeggio</h2><p><b>Piano Practice &amp; Notation "
                "Studio</b></p><p>Version %1</p><p>Built by "
                "Voncloft<br>Copyright © 2026 Voncloft</p><p>VArpeggio is free "
                "and open-source software released under the GNU General Public "
                "License, version 3. License and third-party notices are included "
                "with the application.</p>")
            .arg(VARPEGGIO_VERSION));
  });
}

void MainWindow::applyStyle() {
  setStyleSheet(R"(
*{font-family:"Noto Sans","Manrope",sans-serif;font-size:12px;color:#18312d}QMainWindow{background:#f5f1e8}QMenuBar{background:#0e2420;color:#cbdcd7;padding:2px 8px}QMenuBar::item:selected,QMenu{background:#203d37;color:white}QMenu::item:selected{background:#2d7d70}QMessageBox,QDialog,QProgressDialog{background:#f5f1e8;color:#18312d}QMessageBox QLabel,QDialog QLabel,QProgressDialog QLabel{color:#18312d;background:transparent}QMessageBox QPushButton,QDialogButtonBox QPushButton,QProgressDialog QPushButton{color:#18312d;background:#fffefa;border:1px solid #b8b4aa;border-radius:7px;padding:7px 13px;min-width:80px}QMessageBox QPushButton:hover,QDialogButtonBox QPushButton:hover,QProgressDialog QPushButton:hover{color:white;background:#2d7d70;border-color:#2d7d70}QMessageBox QPushButton:default,QDialogButtonBox QPushButton:default{color:white;background:#2d7d70;border-color:#2d7d70;font-weight:700}#header{background:#112824;border-bottom:1px solid #28433d}#brand{color:white;font-size:20px;font-weight:800}#brandMark{color:#ef775f;font-size:23px}#midiButton{color:#dceae6;background:#1d3933;border:1px solid #3b5751;border-radius:17px;padding:8px 14px}#midiButton[connected="true"]{color:#8be0c9;border-color:#4f9686}#roundButton,#avatar{min-width:32px;max-width:32px;min-height:32px;max-height:32px;border-radius:16px;color:#dceae6;background:#203b35;border:1px solid #3b5751}#avatar{color:#17312c;background:#bfe3d9;font-weight:800}#sidebar{background:#17312c}#sideHeading{color:#718f87;font-family:"Noto Sans Mono";font-size:9px;font-weight:700;letter-spacing:2px;padding:5px 9px}#modeButton,#libraryButton{color:#d5e2df;text-align:left;background:transparent;border:0;border-radius:8px;padding:7px 10px}#modeButton{font-weight:700}#modeButton:checked{color:white;background:#24423c;border-left:3px solid #ef775f}#modeButton:hover,#libraryButton:hover{background:#203d37}#importButton{color:#a9c2bc;background:transparent;border:1px dashed #4b6861;border-radius:8px;padding:9px}#dailyCard{background:#203c36;border:1px solid #2c4a43;border-radius:10px;margin-top:10px}#dailyTitle{color:#e4eeeb;font-weight:700}#dailyTime{color:#ffab98;font-size:9px}#weekDots{color:#70a597;letter-spacing:4px}#routineParts{color:#6f8e86;font-size:7px}#routineButton{color:#add4c9;text-align:left;background:transparent;border:0;border-top:1px solid #36524c;padding-top:7px}#labButton{color:#d6e4e0;text-align:left;background:#2b3c35;border:1px solid #64483e;border-radius:9px;padding:10px}#workspaceScroll,#workspace{background:#f5f1e8}#eyebrow,#sectionHeading{color:#2d7d70;font-family:"Noto Sans Mono";font-size:9px;font-weight:700;letter-spacing:1px}#songTitle{font-size:29px;font-weight:800;background:transparent;border:0;padding:0}#meta{color:#72807d;font-size:10px}#transportButton,#tempoButton{min-width:32px;max-width:32px;min-height:32px;max-height:32px;border-radius:16px;color:#2d7d70;background:#fffefa;border:1px solid #dedbd1}#playButton{min-width:92px;max-width:92px;min-height:42px;max-height:42px;border-radius:21px;color:white;background:#2d7d70;border:0;font-size:12px;font-weight:800}#playButton:hover{background:#256b61}#tempoText{font-weight:800;padding:0 3px}#exportCombo{background:#fffefa;border:1px solid #dedbd1;border-radius:7px 0 0 7px;padding:7px;min-width:120px}#saveButton{color:white;background:#173f39;border:0;border-radius:0 7px 7px 0;padding:8px 13px;font-weight:700}#notice{background:#fff7d2;border:1px solid #ddc673;border-radius:8px}#noticeIcon{min-width:19px;max-width:19px;min-height:19px;max-height:19px;border:1px solid #c6ad54;border-radius:9px;text-align:center;color:#765f22}#noticeClose{border:0;background:transparent;font-size:17px}#card,#coachCard{background:#fffefa;border:1px solid #dedbd1;border-radius:12px}#cardTop{background:#fffefa;border-bottom:1px solid #e8e4db}#hands{background:#eeece5;border-radius:7px}#handButton{background:transparent;border:0;border-radius:5px;padding:4px 7px;color:#7a8582;font-size:9px}#handButton:checked{color:white;background:#2d7d70}QCheckBox{spacing:6px;font-size:9px;color:#687671}QCheckBox::indicator{width:25px;height:13px;border-radius:7px;background:#c7cdca}QCheckBox::indicator:checked{background:#2d7d70;border:3px solid #bfe3d9}#flowCard{background:#152e29;border:1px solid #223f39;border-radius:12px}#flowTop{background:#152e29;border-bottom:1px solid #29443e}#flowHeading{color:#83c6b7;font-family:"Noto Sans Mono";font-size:9px}#flowLegend{color:#79928b;font-size:8px}#nextNote{background:#eff1e9;border-left:4px solid #ef775f;border-radius:7px}#miniHeading{color:#89938f;font-family:"Noto Sans Mono";font-size:7px}#nextNoteText{font-family:"Noto Serif";font-size:38px;font-weight:700;color:#173f39}#coachText{color:#71807c;font-size:10px}#stat{font-weight:700;font-size:12px;color:#18312d}#liveNote{color:#71817d;font-size:10px}#computerHint{color:#929c99;font-family:"Noto Sans Mono";font-size:8px}#recordButton{color:#bd5945;background:#fff7f5;border:1px solid #e4a99d;border-radius:7px;padding:6px 10px}#recordButton[recording="true"]{color:white;background:#ef775f}#divider{color:#304b45}QScrollBar:vertical{width:9px;background:#eeeae1}QScrollBar::handle:vertical{background:#a9b4b0;border-radius:4px;min-height:30px}QToolTip{color:white;background:#17312c;border:1px solid #4b6861;padding:5px}
)");
  setStyleSheet(styleSheet() + R"(
#scoreViewButton{color:#2d7d70;background:#eef7f4;border:1px solid #9fc9be;border-radius:6px;padding:4px 8px;font-size:9px}
#scoreViewButton:checked{color:white;background:#2d7d70;border-color:#2d7d70;font-weight:700}
#scoreViewButton:disabled{color:#9aa7a3;background:#f0f1ed;border-color:#d7ddd9}
#freeStaffButton{color:#31534d;background:#f3faf7;border:1px solid #aebdb8;border-radius:6px;padding:4px 8px;font-size:9px;font-weight:700}
#freeStaffButton:hover{color:white;background:#2d7d70;border-color:#2d7d70}
#scoreHistoryRow{background:#f5f7f3;border-top:1px solid #e1e6e2}
#historyLabel{color:#71807c;font-family:"Noto Sans Mono";font-size:8px;font-weight:700}
#scoreHistoryBar::groove:horizontal{height:6px;background:#d6ddd9;border-radius:3px}
#scoreHistoryBar::handle:horizontal{width:18px;margin:-5px 0;background:#2d7d70;border:2px solid #ffffff;border-radius:9px}
#scoreHistoryBar::sub-page:horizontal{background:#9fc9be;border-radius:3px}
#playButton{min-width:126px;max-width:126px}
#tempoControl{font-weight:800;background:#fffefa;border:1px solid #dedbd1;border-radius:7px;padding:5px 2px}
#tempoControl:focus{border-color:#2d7d70}
#instrumentCombo,#keyboardToneCombo{color:#31534d;background:#f4f7f3;border:1px solid #cbd4d0;border-radius:6px;padding:4px 7px;font-size:9px}
#volumeLabel{color:#687671;font-size:9px}
#volumeSlider::groove:horizontal{height:5px;background:#c7cdca;border-radius:2px}
#volumeSlider::sub-page:horizontal{background:#2d7d70;border-radius:2px}
#volumeSlider::handle:horizontal{width:14px;margin:-5px 0;background:#ef775f;border:1px solid #bd5945;border-radius:7px}
#routineSession{background:#e7f4ef;border:1px solid #9bc8bc;border-radius:10px}
#routineSessionStep{color:#2d7d70;font-family:"Noto Sans Mono";font-size:9px;font-weight:800;letter-spacing:1px}
#routineSessionDetail{color:#29433d;font-size:12px;font-weight:700}
#routineSessionPrimary{color:white;background:#2d7d70;border:1px solid #205e54;border-radius:7px;padding:6px 12px;font-weight:800;min-width:104px}
#routineSessionPrimary:hover{background:#235f56}
#routineSessionSecondary{color:#31534d;background:#fffefa;border:1px solid #aebdb8;border-radius:7px;padding:6px 10px;font-weight:700}
#routineSessionSecondary:hover{background:#f3faf7;border-color:#2d7d70}
#routineSessionSecondary:disabled{color:#9aa7a3;background:#edf1ee;border-color:#d1dad6}
#coachHeading{color:#2d7d70;font-family:"Noto Sans Mono";font-size:11px;font-weight:800;letter-spacing:1px}
#miniHeading{color:#73817c;font-family:"Noto Sans Mono";font-size:9px;font-weight:700}
#nextNoteText{font-family:"Noto Serif";font-size:44px;font-weight:700;color:#173f39}
#coachText{color:#536762;font-size:12px;font-weight:600}
#stat{font-weight:800;font-size:13px;color:#18312d}
#sidebar,#sidebarViewport,#sidebarContent{background:#17312c;border:0}
#sidebar QScrollBar:vertical{width:12px;background:#17312c;margin:0}
#sidebar QScrollBar::handle:vertical{background:#52736b;border-radius:6px;min-height:34px}
#sidebar QScrollBar::add-line:vertical,#sidebar QScrollBar::sub-line:vertical{height:0}
)");
  // Keep KDE in charge of the typeface.  The stylesheet still supplies the
  // hierarchy (heading/note sizes and weights), but never substitutes a
  // bundled or hard-coded font family for the user's system choice.
  QString systemStyle = styleSheet();
  QString systemFamily = QApplication::font().family();
  systemFamily.replace('"', "\\\"");
  systemStyle.replace("Noto Sans Mono", systemFamily);
  systemStyle.replace("Noto Serif", systemFamily);
  systemStyle.replace("Noto Sans", systemFamily);
  systemStyle.replace("Manrope", systemFamily);
  const qreal systemPointSize = QApplication::font().pointSizeF();
  if(systemPointSize>0)
    systemStyle.replace("font-size:12px",
                        QString("font-size:%1pt").arg(systemPointSize));
  const qreal coachPointSize = qMax<qreal>(8.0, systemPointSize - 6.0);
  systemStyle += QString("#coachHeading,#miniHeading,#nextNoteText,#coachText,#stat{font-size:%1pt}")
                     .arg(coachPointSize, 0, 'f', 1);
  const QFontMetrics systemFontMetrics(QApplication::font());
  int widestActionLabel = 0;
  for(const QString &text : {QStringLiteral("Restart wait"),
                             QStringLiteral("Listen"),
                             QStringLiteral("Play along"),
                             QStringLiteral("Preview score"),
                             QStringLiteral("Hear again"),
                             QStringLiteral("Resume"),
                             QStringLiteral("Pause")})
    widestActionLabel =
        qMax(widestActionLabel, systemFontMetrics.horizontalAdvance(text));
  const int playButtonWidth = qMax(180, widestActionLabel + 64);
  systemStyle += QString("#playButton{min-width:%1px;max-width:%1px}")
                     .arg(playButtonWidth);
  setStyleSheet(systemStyle);
}

void MainWindow::connectUi() {
  m_scoreStack->setMinimumHeight(520);
  m_sourceAudio->setText("Hear original recording (reference only)");
  m_sourceAudio->setToolTip(
      "Reference only; leave off to hear the generated staff notes");
  m_midiOutputCheck->setToolTip(
      "Send score notes through the adapter MIDI OUT to the keyboard MIDI IN; "
      "VArpeggio's piano is silenced during score playback");
  QSettings audioSettings("Voncloft", "VArpeggio");
  int savedInstrument =
      audioSettings
          .value("audio/instrument", int(SynthInstrument::AcousticGrand))
          .toInt();
  // Existing installs defaulted to the narrow concert patch.  Migrate once
  // to the tutorial grand selected from the source comparison; subsequent
  // user choices remain untouched.
  if (audioSettings.value("audio/pianoProfileVersion", 0).toInt() < 3) {
    savedInstrument = int(SynthInstrument::AcousticGrand);
    audioSettings.setValue("audio/instrument", savedInstrument);
    audioSettings.setValue("audio/pianoProfileVersion", 3);
  }
  const int savedInstrumentIndex = m_instrumentCombo->findData(savedInstrument);
  m_instrumentCombo->setCurrentIndex(
      savedInstrumentIndex < 0 ? 0 : savedInstrumentIndex);
  m_synth.setInstrument(
      SynthInstrument(m_instrumentCombo->currentData().toInt()));
  const int savedKeyboardTone =
      qBound(0, audioSettings.value("midi/ctk511Tone", 0).toInt(), 99);
  m_keyboardToneCombo->setCurrentIndex(savedKeyboardTone);
  if (audioSettings.value("midi/useOutput", false).toBool()) {
    QString error;
    if (m_midiOutput.openFirst(&error)) {
      m_midiOutputCheck->setChecked(true);
      m_midiOutput.programChange(savedKeyboardTone);
    } else
      audioSettings.setValue("midi/useOutput", false);
  }
  connect(
      m_keyboardToneCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        const int tone = m_keyboardToneCombo->currentData().toInt();
        QSettings("Voncloft", "VArpeggio").setValue("midi/ctk511Tone", tone);
        if (m_midiOutputCheck->isChecked() && m_midiOutput.isOpen())
          m_midiOutput.programChange(tone);
        toast(QString("CTK-511 tone %1 selected")
                  .arg(m_keyboardToneCombo->currentText()));
      });
  connect(m_modeButtons, &QButtonGroup::idClicked, this,
          [this](int id) { setMode(Mode(id)); });
  connect(m_handButtons, &QButtonGroup::idClicked, this,
          [this](int id) { setHandFilter(id == 2 ? -1 : id); });
  connect(
      m_scoreDisplayButtons, &QButtonGroup::idClicked, this, [this](int id) {
        m_scoreStack->setCurrentWidget(id == 0
                                           ? static_cast<QWidget *>(m_pdfView)
                                           : static_cast<QWidget *>(m_score));
      });
  connect(m_playButton, &QPushButton::clicked, this,
          &MainWindow::togglePlayback);
  connect(m_recordButton, &QPushButton::clicked, this,
          &MainWindow::toggleRecord);
  connect(m_freeEditButton, &QPushButton::clicked, this,
          &MainWindow::editFreePlayStaff);
  connect(m_freeClearButton, &QPushButton::clicked, this,
          &MainWindow::clearFreePlayStaff);
  connect(m_detectorButton, &QPushButton::clicked, this,
          &MainWindow::openDetectorView);
  connect(m_piano, &PianoWidget::notePressed, this, &MainWindow::userNoteOn);
  connect(m_piano, &PianoWidget::noteReleased, this, &MainWindow::userNoteOff);
  connect(m_noteNames, &QCheckBox::toggled, m_score,
          &ScoreWidget::setShowNames);
  connect(m_bubbles, &QCheckBox::toggled, m_bubble,
          &BubbleWidget::setBubblesVisible);
  connect(m_sound, &QCheckBox::toggled, this, [this](bool on) {
    m_synth.setMuted(!on);
    m_sourceAudioOutput->setMuted(!on);
  });
  connect(m_instrumentCombo, &QComboBox::currentIndexChanged, this,
          [this](int) {
            const auto instrument =
                SynthInstrument(m_instrumentCombo->currentData().toInt());
            m_synth.setInstrument(instrument);
            QSettings("Voncloft", "VArpeggio")
                .setValue("audio/instrument", int(instrument));
            toast(m_instrumentCombo->currentText() + " selected");
          });
  connect(m_volume, &QSlider::valueChanged, this, [this](int value) {
    m_synth.setVolume(value / 100.0);
    m_sourceAudioOutput->setVolume(qBound(0.0, value / 100.0, 1.0));
    if (m_midiOutputCheck->isChecked() && m_midiOutput.isOpen())
      m_midiOutput.channelVolume(qRound(value * 127.0 / 150.0));
    m_volumeLabel->setText(
        QString(m_midiOutputCheck->isChecked() ? "Keyboard %1%" : "Volume %1%")
            .arg(value));
  });
  connect(m_midiOutputCheck, &QCheckBox::toggled, this, [this](bool checked) {
    if (m_playing || m_playbackPaused)
      stopPlayback(true);
    if (checked && !m_midiOutput.isOpen()) {
      QString error;
      if (!m_midiOutput.openFirst(&error)) {
        const QSignalBlocker blocker(m_midiOutputCheck);
        m_midiOutputCheck->setChecked(false);
        QSettings("Voncloft", "VArpeggio").setValue("midi/useOutput", false);
        showNotice(error + " Connect adapter MIDI OUT to keyboard MIDI IN, "
                           "then try again.",
                   true);
        return;
      }
    }
    if (!checked) {
      m_midiOutput.allNotesOff();
      m_midiOutput.close();
    }
    QSettings("Voncloft", "VArpeggio").setValue("midi/useOutput", checked);
    if (checked) {
      m_midiOutput.programChange(m_keyboardToneCombo->currentData().toInt());
      m_midiOutput.channelVolume(qRound(m_volume->value() * 127.0 / 150.0));
    }
    m_instrumentCombo->setVisible(!checked);
    m_keyboardToneCombo->setVisible(checked);
    m_volumeLabel->setText(QString(checked ? "Keyboard %1%" : "Volume %1%")
                               .arg(m_volume->value()));
    toast(checked ? "Score playback will use " + m_midiOutput.activeDevice() +
                        " — set loudness on the keyboard"
                  : "Score playback will use VArpeggio's piano");
  });
  connect(m_sourceAudio, &QCheckBox::toggled, this, [this](bool) {
    if (m_playing || m_playbackPaused)
      stopPlayback(true);
    updatePrimaryButton();
  });
  connect(m_metronome, &QCheckBox::toggled, this, [this](bool) {
    m_metroClock.restart();
    m_lastMetroBeat = -1;
  });
  connect(m_noticeClose, &QPushButton::clicked, m_notice, &QWidget::hide);
  connect(m_titleEdit, &QLineEdit::textChanged, this,
          [this](const QString &t) { m_title = t; });
  m_instrumentCombo->setVisible(!m_midiOutputCheck->isChecked());
  m_keyboardToneCombo->setVisible(m_midiOutputCheck->isChecked());
  connect(m_scoreHistoryBar, &QScrollBar::valueChanged, this,
          [this](int value) {
            if (m_mode == Mode::Free) {
              m_freePlayAutoFollow = value >= m_scoreHistoryBar->maximum();
              m_score->setFollowBeat(value / 8.0);
              if (!m_freePlayAutoFollow)
                m_barLabel->setText("FREE PLAY   ·   Viewing earlier notes — "
                                    "drag to Latest for live follow");
              else
                m_barLabel->setText("FREE PLAY   ·   Live grand staff");
            } else {
              m_scoreAutoFollow = value >= m_scoreHistoryBar->maximum();
              m_score->setFollowBeat(m_scoreAutoFollow ? m_scoreLiveBeat
                                                       : value / 8.0);
              if (!m_scoreAutoFollow)
                m_barLabel->setText("SCORE   ·   Viewing an earlier measure — "
                                    "drag to Latest for live follow");
              else
                updateExpected();
            }
            m_score->repaint();
          });
  connect(m_score, &ScoreWidget::historyStepRequested, this, [this](int steps) {
    if (m_mode == Mode::Free || !m_scoreHistoryBar->isEnabled())
      return;
    m_scoreHistoryBar->setValue(
        qBound(m_scoreHistoryBar->minimum(),
               m_scoreHistoryBar->value() + steps * m_scoreHistoryBar->pageStep(),
               m_scoreHistoryBar->maximum()));
  });
  connect(m_score, &ScoreWidget::seekPreviewChanged, this, [this](double beat) {
    if (m_playing)
      pausePlayback();
    m_score->setPlayedBeat(beat);
    m_bubble->setPlayhead(beat);
  });
  connect(m_score, &ScoreWidget::seekRequested, this, [this](double beat) {
    seekPlayback(beat);
  });
  connect(m_score, &ScoreWidget::editCursorChanged, this, [this](double beat) {
    if (m_mode != Mode::Free)
      return;
    if (m_recording || !m_freePlayStarts.isEmpty()) {
      m_score->setEditCursorBeat(m_freePlayEditBeat);
      toast("Release the keys and stop recording before moving the insertion "
            "line");
      return;
    }
    m_freePlayEditBeat = beat;
    m_freePlayEditAdvance = 0;
    m_freePlayEditPressMs.clear();
    m_freePlayAutoFollow = false;
    m_score->setFollowBeat(beat);
    const QString position = scorePosition(beat, m_freePlayTimeSignatures);
    m_barLabel->setText(m_freePlayGroupEntry
                            ? "FREE PLAY   ·   " + position +
                                  " · GROUPING · Right-click to ungroup"
                            : "FREE PLAY   ·   " + position +
                                  " · Each note steps forward");
  });
  connect(m_score, &ScoreWidget::noteDeleteRequested, this, [this](int index) {
    if (m_mode != Mode::Free || index < 0 || index >= m_freePlayNotes.size())
      return;
    if (m_recording || !m_freePlayStarts.isEmpty()) {
      toast("Release the keys and stop recording before deleting a note");
      return;
    }
    const QString removed =
        MusicDocument::noteName(m_freePlayNotes[index].note);
    m_freePlayNotes.removeAt(index);
    syncFreePlayScore();
    m_score->setNotes(m_freePlayNotes);
    m_bubble->setNotes(m_freePlayNotes);
    updateFreePlayHistory(false);
    m_score->setEditCursorBeat(m_freePlayEditBeat);
    toast("Deleted " + removed + " from the staff");
  });
  connect(
      m_score, &ScoreWidget::noteContextRequested, this,
      [this](int index, const QPoint &globalPosition) {
        if(m_mode!=Mode::Free||index<0||index>=m_freePlayNotes.size()||m_recording||!m_freePlayStarts.isEmpty())
          return;
        QMenu menu(this);
        QMenu *length = menu.addMenu("Note length");
        const QVector<QPair<QString,double>> lengths={{"Sixteenth note — 1/4 beat",.25},{"Eighth note — 1/2 beat",.5},{"Dotted eighth — 3/4 beat",.75},{"Quarter note — 1 beat",1.0},{"Dotted quarter — 1 1/2 beats",1.5},{"Half note — 2 beats",2.0},{"Dotted half — 3 beats",3.0},{"Whole note — 4 beats",4.0}};
        for(const auto &choice:lengths){
          QAction *action = length->addAction(choice.first);
          action->setData("duration:" + QString::number(choice.second, 'f', 2));
          action->setCheckable(true);
          action->setChecked(
              qAbs(m_freePlayNotes[index].duration - choice.second) < .04);
        }
        QMenu *accidental=menu.addMenu("Accidental");
        for(const auto &choice:QVector<QPair<QString,int>>{{"Automatic for key signature",int(AccidentalMark::Automatic)},{"♮  Natural",int(AccidentalMark::Natural)},{"♯  Sharp",int(AccidentalMark::Sharp)},{"♭  Flat",int(AccidentalMark::Flat)}}){
          QAction *action = accidental->addAction(choice.first);
          action->setData("accidental:" + QString::number(choice.second));
          action->setCheckable(true);
          action->setChecked(int(m_freePlayNotes[index].accidental) ==
                             choice.second);
        }
        menu.addSeparator();
        QAction *group = menu.addAction(m_freePlayGroupEntry
                                            ? "Done grouping — resume stepping"
                                            : "Group notes with this note");
        group->setData("toggle-group");
        QAction *remove=menu.addAction("Delete "+MusicDocument::noteName(m_freePlayNotes[index].note));
        remove->setData("delete");
        QAction *selected=menu.exec(globalPosition);
        if (!selected)
          return;
        const QString command = selected->data().toString();
        if(command=="toggle-group"){
          setFreePlayGrouping(!m_freePlayGroupEntry,
                              m_freePlayNotes[index].start);
        } else if (command == "delete") {
          const QString name =
              MusicDocument::noteName(m_freePlayNotes[index].note);
          m_freePlayNotes.removeAt(index);
          toast("Deleted " + name + " from the staff");
        } else if (command.startsWith("duration:")) {
          const double duration = qMax(.125, command.mid(9).toDouble());
          m_freePlayNotes[index].duration = duration;
          toast("Changed to " + selected->text());
        }else if(command.startsWith("accidental:")){
          m_freePlayNotes[index].accidental =
              AccidentalMark(command.mid(11).toInt());
          toast("Updated the written accidental for this played pitch");
        }
        syncFreePlayScore();
        m_score->setNotes(m_freePlayNotes);
        m_bubble->setNotes(m_freePlayNotes);
        updateFreePlayHistory(false);
        m_score->setEditCursorBeat(m_freePlayEditBeat);
      });
  connect(m_score, &ScoreWidget::restDeleteRequested, this, [this](int index) {
    if (m_mode != Mode::Free || index < 0 || index >= m_freePlayRests.size() ||
        m_recording || !m_freePlayStarts.isEmpty())
      return;
    m_freePlayRests.removeAt(index);
    m_score->setRests(m_freePlayRests);
    syncFreePlayScore();
    updateFreePlayHistory(false);
    toast("Deleted the rest from the staff");
  });
  connect(
      m_score, &ScoreWidget::staffContextRequested, this,
      [this](double beat, const QPoint &globalPosition) {
        if(m_mode!=Mode::Free||m_recording||!m_freePlayStarts.isEmpty())
          return;
        QMenu menu(this);
        menu.setTitle(QString("Staff at beat %1").arg(beat, 0, 'f', 3));
        QAction *group = menu.addAction(m_freePlayGroupEntry
                                            ? "Done grouping — resume stepping"
                                            : "Group notes at this position");
        group->setData("toggle-group");
        menu.addSeparator();
        QMenu *meter=menu.addMenu("Time signature here");
        for(const QString &value:{QStringLiteral("2/4"),QStringLiteral("3/4"),QStringLiteral("4/4"),QStringLiteral("5/4"),QStringLiteral("6/8"),QStringLiteral("9/8"),QStringLiteral("12/8")}){
          QAction *action = meter->addAction(value);
          action->setData("meter:" + value);
        }
        QMenu *key=menu.addMenu("Key signature here");
        const QVector<QPair<QString,int>> keys={{"C♭ major / A♭ minor — 7 flats",-7},{"G♭ major / E♭ minor — 6 flats",-6},{"D♭ major / B♭ minor — 5 flats",-5},{"A♭ major / F minor — 4 flats",-4},{"E♭ major / C minor — 3 flats",-3},{"B♭ major / G minor — 2 flats",-2},{"F major / D minor — 1 flat (B♭)",-1},{"C major / A minor — no sharps or flats",0},{"G major / E minor — 1 sharp (F♯)",1},{"D major / B minor — 2 sharps",2},{"A major / F♯ minor — 3 sharps",3},{"E major / C♯ minor — 4 sharps",4},{"B major / G♯ minor — 5 sharps",5},{"F♯ major / D♯ minor — 6 sharps",6},{"C♯ major / A♯ minor — 7 sharps",7}};
        for(const auto &choice:keys){
          QAction *action = key->addAction(choice.first);
          action->setData("key:" + QString::number(choice.second));
        }
        QMenu *restMenu=menu.addMenu("Insert rest");
        for (const auto &staff : QVector<QPair<QString, QString>>{
                 {"Treble staff", "R"}, {"Bass staff", "L"}}) {
          QMenu *staffMenu = restMenu->addMenu(staff.first);
          for (const auto &length :
               QVector<QPair<QString, double>>{{"Eighth rest", .5},
                                               {"Quarter rest", 1.0},
                                               {"Half rest", 2.0},
                                               {"Whole rest", 4.0}}) {
            QAction *action = staffMenu->addAction(length.first);
            action->setData(
                QString("rest:%1:%2").arg(staff.second).arg(length.second));
          }
        }
        const bool hasRepeat=std::any_of(m_freePlayRepeatEnds.cbegin(),m_freePlayRepeatEnds.cend(),[beat](double value){return qAbs(value-beat)<.04;});
        QAction *repeat=menu.addAction(hasRepeat?"Remove repeat end :||":"Add repeat end :||");
        repeat->setData("repeat");
        const auto meterAt=std::find_if(m_freePlayTimeSignatures.cbegin(),m_freePlayTimeSignatures.cend(),[beat](const auto &value){return qAbs(value.beat-beat)<.04;});
        if(beat>.001&&meterAt!=m_freePlayTimeSignatures.cend()){
          QAction *removeMeter = menu.addAction("Remove time signature change");
          removeMeter->setData("remove-meter");
        }
        const auto keyAt=std::find_if(m_freePlayKeySignatures.cbegin(),m_freePlayKeySignatures.cend(),[beat](const auto &value){return qAbs(value.beat-beat)<.04;});
        if(beat>.001&&keyAt!=m_freePlayKeySignatures.cend()){
          QAction *removeKey = menu.addAction("Remove key signature change");
          removeKey->setData("remove-key");
        }
        QAction *selected=menu.exec(globalPosition);
        if (!selected)
          return;
        const QString command = selected->data().toString();
        if(command=="toggle-group"){
          setFreePlayGrouping(!m_freePlayGroupEntry, beat);
        }else if(command.startsWith("meter:")){
          const QStringList pieces = command.mid(6).split('/');
          if (pieces.size() != 2)
            return;
          m_freePlayTimeSignatures.erase(
              std::remove_if(m_freePlayTimeSignatures.begin(),
                             m_freePlayTimeSignatures.end(),
                             [beat](const auto &value) {
                               return qAbs(value.beat - beat) < .04;
                             }),
              m_freePlayTimeSignatures.end());
          m_freePlayTimeSignatures.append(
              {beat, pieces[0].toInt(), pieces[1].toInt()});
          std::sort(
              m_freePlayTimeSignatures.begin(), m_freePlayTimeSignatures.end(),
              [](const auto &a, const auto &b) { return a.beat < b.beat; });
          toast("Added " + pieces.join('/') + " at the orange line");
        }else if(command=="remove-meter"){
          m_freePlayTimeSignatures.erase(
              std::remove_if(m_freePlayTimeSignatures.begin(),
                             m_freePlayTimeSignatures.end(),
                             [beat](const auto &value) {
                               return qAbs(value.beat - beat) < .04 &&
                                      value.beat > .001;
                             }),
              m_freePlayTimeSignatures.end());
          toast("Removed the time signature change");
        }else if(command.startsWith("key:")){
          const int fifths = qBound(-7, command.mid(4).toInt(), 7);
          m_freePlayKeySignatures.erase(
              std::remove_if(m_freePlayKeySignatures.begin(),
                             m_freePlayKeySignatures.end(),
                             [beat](const auto &value) {
                               return qAbs(value.beat - beat) < .04;
                             }),
              m_freePlayKeySignatures.end());
          m_freePlayKeySignatures.append({beat, fifths});
          std::sort(
              m_freePlayKeySignatures.begin(), m_freePlayKeySignatures.end(),
              [](const auto &a, const auto &b) { return a.beat < b.beat; });
          toast(
              fifths == -1
                  ? "Added one flat — every B is flat until the next key change"
                  : "Added the key signature at the orange line");
        }else if(command=="remove-key"){
          m_freePlayKeySignatures.erase(
              std::remove_if(m_freePlayKeySignatures.begin(),
                             m_freePlayKeySignatures.end(),
                             [beat](const auto &value) {
                               return qAbs(value.beat - beat) < .04 &&
                                      value.beat > .001;
                             }),
              m_freePlayKeySignatures.end());
          toast("Removed the key signature change");
        }else if(command.startsWith("rest:")){
          const QStringList pieces = command.split(':');
          if (pieces.size() != 3)
            return;
          const Hand hand = pieces[1] == "L" ? Hand::Left : Hand::Right;
          const double duration = pieces[2].toDouble();
          const bool overlaps =
              std::any_of(m_freePlayNotes.cbegin(), m_freePlayNotes.cend(),
                          [beat, duration, hand](const auto &note) {
                            return note.hand == hand &&
                                   note.start < beat + duration - .001 &&
                                   note.start + note.duration > beat + .001;
                          });
          if (overlaps) {
            showNotice("That staff already has a note during this rest. Delete "
                       "or move the note first.",
                       true);
            return;
          }
          m_freePlayRests.erase(
              std::remove_if(m_freePlayRests.begin(), m_freePlayRests.end(),
                             [beat, hand](const auto &value) {
                               return qAbs(value.beat - beat) < .04 &&
                                      value.hand == hand;
                             }),
              m_freePlayRests.end());
          m_freePlayRests.append({beat, duration, hand});
          std::sort(
              m_freePlayRests.begin(), m_freePlayRests.end(),
              [](const auto &a, const auto &b) { return a.beat < b.beat; });
          m_freePlayEditBeat = beat;
          m_score->setEditCursorBeat(m_freePlayEditBeat);
          toast("Added the rest at the orange line");
        }else if(command=="repeat"){
          if (hasRepeat) {
            m_freePlayRepeatEnds.erase(
                std::remove_if(
                    m_freePlayRepeatEnds.begin(), m_freePlayRepeatEnds.end(),
                    [beat](double value) { return qAbs(value - beat) < .04; }),
                m_freePlayRepeatEnds.end());
            toast("Removed the repeat end");
          } else {
            m_freePlayRepeatEnds.append(beat);
            std::sort(m_freePlayRepeatEnds.begin(), m_freePlayRepeatEnds.end());
            toast("Added repeat end :|| at the orange line");
          }
        }
        m_score->setTimeSignatures(m_freePlayTimeSignatures);
        m_score->setKeySignatures(m_freePlayKeySignatures);
        m_score->setRests(m_freePlayRests);
        m_score->setRepeatEnds(m_freePlayRepeatEnds);
        syncFreePlayScore();
        updateFreePlayHistory(false);
      });
  connect(m_sourceAudio, &QCheckBox::toggled, this, [this](bool checked) {
    if (checked) {
      if (m_mode != Mode::Listen)
        setMode(Mode::Listen);
      toast("Original reference selected — this is not the generated staff "
            "playback");
    } else if (m_sourceAudio->isEnabled())
      toast("Generated-score playback selected");
  });
  m_sourceAudioOutput->setVolume(1.0);
  m_sourceAudioOutput->setMuted(!m_sound->isChecked());
  connect(m_sourcePlayer, &QMediaPlayer::errorOccurred, this,
          [this](QMediaPlayer::Error, const QString &message) {
            if (!message.isEmpty() && m_usingImportedMediaAudio)
              showNotice("The original recording could not play: " + message,
                         true);
          });
  connect(m_sourcePlayer, &QMediaPlayer::mediaStatusChanged, this,
          [this](QMediaPlayer::MediaStatus status) {
            if (status == QMediaPlayer::LoadedMedia ||
                status == QMediaPlayer::BufferedMedia) {
              const double rate = m_importedMediaTempo > 0
                                      ? double(m_tempo) / m_importedMediaTempo
                                      : 1.0;
              m_sourcePlayer->setPlaybackRate(rate);
            }
            if ((status == QMediaPlayer::LoadedMedia ||
                 status == QMediaPlayer::BufferedMedia) &&
                m_playing && m_usingImportedMediaAudio) {
              const qint64 wanted = m_importedMediaStartMs +
                                    qRound64(m_playbackOffsetSeconds * 1000.0);
              if (qAbs(m_sourcePlayer->position() - wanted) > 150)
                m_sourcePlayer->setPosition(wanted);
              if (m_sourcePlayer->playbackState() != QMediaPlayer::PlayingState)
                m_sourcePlayer->play();
            } else if (status == QMediaPlayer::EndOfMedia &&
                       m_usingImportedMediaAudio)
              stopPlayback(true);
          });
  connect(&m_midi, &MidiInput::noteOn, this, &MainWindow::userNoteOn);
  connect(&m_midi, &MidiInput::noteOff, this, &MainWindow::userNoteOff);
  connect(&m_midi, &MidiInput::sustain, this, [this](bool down) {
    m_sustain = down;
    if (!down) {
      for (int n : m_sustainedNotes) {
        if (m_userVoiceIds.contains(n))
          m_synth.noteOffVoice(m_userVoiceIds.take(n));
        m_piano->setActive(n, false);
      }
      m_sustainedNotes.clear();
    }
  });
  connect(&m_midi, &MidiInput::programChanged, this, [this](int program) {
    if (program < 0 || program >= m_keyboardToneCombo->count())
      return;
    const QSignalBlocker blocker(m_keyboardToneCombo);
    m_keyboardToneCombo->setCurrentIndex(program);
    QSettings("Voncloft", "VArpeggio").setValue("midi/ctk511Tone", program);
    toast("Keyboard selected " + m_keyboardToneCombo->currentText());
  });
  connect(&m_midi, &MidiInput::devicesChanged, this,
          [this](const QStringList &devices) {
            bool ok = !devices.isEmpty();
            m_midiButton->setProperty("connected", ok);
            m_midiButton->style()->unpolish(m_midiButton);
            m_midiButton->style()->polish(m_midiButton);
            m_midiButton->setText(ok ? "●   " + devices.first()
                                     : "●   Connect MIDI keyboard");
            m_midiLabel = nullptr;
            if (ok)
              toast("MIDI connected: " + devices.first());
          });
  connect(&m_midi, &MidiInput::midiError, this,
          [this](const QString &e) { showNotice(e, true); });
  connect(m_midiButton, &QPushButton::clicked, this, [this] {
    const auto inputs = m_midi.connectedDevices();
    const auto outputs = m_midiOutput.availableDevices();
    if (inputs.isEmpty() && outputs.isEmpty()) {
      showNotice("No MIDI adapter is visible yet. Connect it by USB and turn "
                 "on the keyboard.");
      return;
    }
    QString text =
        "MIDI IN to VArpeggio:\n" +
        (inputs.isEmpty() ? QString("None") : inputs.join("\n")) +
        "\n\nMIDI OUT to keyboard:\n" +
        (outputs.isEmpty() ? QString("None") : outputs.join("\n")) +
        "\n\nThe CTK-511 tone selector follows Program Change messages in both "
        "directions. Use TONE MAP N on the keyboard for the native 00–99 list.";
    QMessageBox::information(this, "MIDI keyboard", text);
  });
}

void MainWindow::setMode(Mode mode) {
  const Mode previous = m_mode;
  if (m_earTraining && mode != Mode::Wait) {
    m_earTraining = false;
    m_synth.stopSequence();
  }
  if (m_recording && mode != Mode::Free)
    finishRecording();
  if (m_playing || m_playbackPaused)
    stopPlayback(false);
  m_mode = mode;
  m_bubble->setWaitMode(mode == Mode::Wait);
  QString text;
  switch (mode) {
  case Mode::Wait:
    text = "WAIT MODE · MUSIC STOPS FOR YOU";
    break;
  case Mode::Listen:
    text = "LISTEN · HEAR THE SHEET";
    break;
  case Mode::Follow:
    text = "PLAY ALONG · FOLLOW TEMPO";
    break;
  case Mode::Free:
    text = "FREE PLAY · LIVE NOTATION";
    break;
  }
  m_modeLabel->setText(text);
  if (auto *b = m_modeButtons->button(int(mode)))
    b->setChecked(true);
  m_recordButton->setVisible(mode == Mode::Free);
  m_freeEditButton->hide();
  m_freeClearButton->setVisible(mode == Mode::Free);
  m_scoreHistoryRow->show();
  m_score->setLiveWritingMode(mode == Mode::Free);
  m_score->setEditEnabled(mode == Mode::Free);
  m_score->setSeekEnabled(mode != Mode::Free);
  if (mode != Mode::Free) {
    m_scoreAutoFollow = true;
    m_scoreLiveBeat = 0.0;
    updateScoreHistory(0.0, true);
  }
  if (mode == Mode::Free && previous != Mode::Free) {
    m_freePlayNotes.clear();
    m_freePlayStarts.clear();
    m_freePlayIndexes.clear();
    m_freePlayPressMs.clear();
    m_freePlayEditPressMs.clear();
    m_freePlayApplied = false;
    m_freePlayAutoFollow = true;
    m_freePlayGroupEntry = false;
    m_freePlayOffsetBeat = 0;
    m_freePlayEditBeat = -1;
    m_freePlayEditAdvance = 0;
    m_freePlayTimeSignatures = {{0.0, 4, 4}};
    m_freePlayKeySignatures = {{0.0, 0}};
    m_freePlayRests.clear();
    m_freePlayRepeatEnds.clear();
    m_freePlayClock.invalidate();
    m_handFilter = -1;
    m_score->setHandFilter(-1);
    m_bubble->setHandFilter(-1);
    if (auto *b = m_handButtons->button(2))
      b->setChecked(true);
    m_score->setNotes(m_freePlayNotes);
    m_score->setEditCursorBeat(-1);
    m_score->setTimeSignatures(m_freePlayTimeSignatures);
    m_score->setKeySignatures(m_freePlayKeySignatures);
    m_score->setRests(m_freePlayRests);
    m_score->setRepeatEnds(m_freePlayRepeatEnds);
    m_bubble->setNotes(m_freePlayNotes);
    m_score->setExpected({}, 0);
    m_bubble->setExpected({}, 0);
    m_score->setFollowBeat(0);
    m_bubble->setPlayhead(0);
    m_barLabel->setText("FREE PLAY   ·   Click staff to insert · Right-click "
                        "for notation tools");
    m_noteCountLabel->setText("0 notes");
    m_durationLabel->setText("0:00");
    updateFreePlayHistory(true);
    m_scoreStack->setCurrentWidget(m_score);
    if (m_liveScoreButton->isVisible())
      m_liveScoreButton->setChecked(true);
  } else if (mode != Mode::Free && previous == Mode::Free) {
    m_freePlayEditBeat = -1;
    m_score->setEditCursorBeat(-1);
    m_score->setTimeSignatures({{0.0, 4, 4}});
    m_score->setKeySignatures(
        {{0.0, MusicDocument::inferKeySignature(filteredNotes())}});
    m_score->setRests({});
    m_score->setRepeatEnds({});
    m_score->setNotes(m_notes);
    m_bubble->setNotes(m_notes);
    updateSongMeta();
  }
  rebuildPractice();
  if (mode == Mode::Free) {
    m_score->setExpected({}, 0);
    m_bubble->setExpected({}, 0);
    if (m_freePlayEditBeat < 0)
      m_barLabel->setText("FREE PLAY   ·   Click staff to insert · Right-click "
                          "for notation tools");
  }
  updatePrimaryButton();
}
void MainWindow::setSong(const QString&title,const QVector<NoteEvent>&notes,int tempo){stopPlayback(false);clearImportedMediaSource();m_bubble->resetState();m_title=title;m_notes=notes;m_tempo=qBound(30,tempo,260);m_titleEdit->setText(title);m_titleEdit->setCursorPosition(0);if(m_tempoControl){const QSignalBlocker blocker(m_tempoControl);m_tempoControl->setValue(m_tempo);}m_pdfDocument->close();m_generatedScoreDir.reset();m_scoreViewButton->setText("Exact PDF");m_scoreViewButton->hide();m_liveScoreButton->hide();m_liveScoreButton->setEnabled(true);m_liveScoreButton->setChecked(true);m_scoreStack->setCurrentWidget(m_score);m_score->setTimeSignatures({{0.0,4,4}});m_score->setKeySignatures({{0.0,MusicDocument::inferKeySignature(notes)}});m_score->setRests({});m_score->setRepeatEnds({});m_score->setNotes(notes);m_bubble->setNotes(notes);m_notice->hide();rebuildPractice();updateSongMeta();}
void MainWindow::setExactScoreAvailable(const QString &label){m_scoreViewButton->setText(label);m_scoreViewButton->show();m_liveScoreButton->setEnabled(!m_notes.isEmpty());m_liveScoreButton->show();m_scoreViewButton->setChecked(true);m_scoreStack->setCurrentWidget(m_pdfView);}

void MainWindow::updatePrimaryButton(){if(m_playing){m_playButton->setText("Ⅱ  Pause");return;}if(m_playbackPaused){m_playButton->setText("▶  Resume");return;}if(m_earTraining){m_playButton->setText("♫  Hear again");return;}switch(m_mode){case Mode::Wait:m_playButton->setText("◎  Restart wait");break;case Mode::Listen:m_playButton->setText("▶  Listen");break;case Mode::Follow:m_playButton->setText("▶  Play along");break;case Mode::Free:m_playButton->setText("▶  Preview score");break;}}
QVector<NoteEvent> MainWindow::filteredNotes()const{QVector<NoteEvent>out;for(const auto&n:m_notes)if(m_handFilter<0||(m_handFilter==0&&n.hand==Hand::Left)||(m_handFilter==1&&n.hand==Hand::Right))out<<n;return out;}
void MainWindow::setHandFilter(int filter){if(m_playing||m_playbackPaused)stopPlayback(true);m_handFilter=filter;m_score->setHandFilter(filter);m_bubble->setHandFilter(filter);if(m_mode!=Mode::Free)m_score->setKeySignatures({{0.0,MusicDocument::inferKeySignature(filteredNotes())}});rebuildPractice();QString hand=filter<0?"Hands together":filter==0?"Left hand":"Right hand";toast(hand+" practice");}
void MainWindow::rebuildPractice(){m_groups.clear();auto notes=filteredNotes();std::sort(notes.begin(),notes.end(),[](auto&a,auto&b){return a.start<b.start;});for(const auto&n:notes){if(m_groups.isEmpty()||qAbs(m_groups.last().start-n.start)>.015)m_groups<<PracticeGroup{n.start,{}};m_groups.last().notes.insert(n.note);}m_groupIndex=0;m_matched.clear();m_attempts=m_correct=m_streak=m_bestStreak=0;m_waitBeat=m_groups.isEmpty()?0.0:m_groups.first().start;m_waitAdvanceClock.restart();m_lastWaitAdvanceMs=0;m_scoreAutoFollow=true;m_score->setPlayedBeat(-1);m_score->setFollowBeat(m_waitBeat);updateExpected();updateScoreHistory(m_waitBeat,true);m_bubble->setPlayhead(m_waitBeat);updateStats();}
void MainWindow::updateExpected(){
    QSet<int>expected;double start=0;QString displayText="✓";
    const qreal kdePointSize=QApplication::font().pointSizeF()>0?QApplication::font().pointSizeF():10.0;
    qreal displaySize=qMax<qreal>(8.0,kdePointSize-6.0);
    if(m_groupIndex<m_groups.size()){
        expected=m_groups[m_groupIndex].notes;start=m_groups[m_groupIndex].start;
        QList<int>sortedNotes=expected.values();std::sort(sortedNotes.begin(),sortedNotes.end());QStringList names;for(int n:sortedNotes)names<<m_score->noteNameForBeat(n,start);
        if(names.size()<=2)displayText=names.join(" + ");
        else{const int split=(names.size()+1)/2;displayText=names.mid(0,split).join(" + ")+"\n"+names.mid(split).join(" + ");}
        displaySize=qMax<qreal>(8.0,kdePointSize-6.0-qMin(4,names.size()-1));
        m_coachLabel->setText(expected.size()>1?QString("Play all %1 chord notes\nWait mode advances when the complete chord sounds.").arg(expected.size()):"Find "+names.first()+"\nTake your time. The music waits for you.");
    }else m_coachLabel->setText(m_groups.isEmpty()?"Choose or import a score\nThen start playing.":"Practice complete!\nExcellent focus — run it once more.");
    m_nextNoteLabel->setStyleSheet(QString("font-size:%1pt;font-weight:700;color:#173f39").arg(displaySize,0,'f',1));m_nextNoteLabel->setText(displayText);
    m_score->setExpected(expected,start);if(m_mode==Mode::Free||m_scoreAutoFollow)m_score->setFollowBeat(start);m_bubble->setExpected(expected,start);m_barLabel->setText(QString("SCORE   ·   Bar %1 of %2").arg(qMax(1,int(start/4)+1)).arg(qMax(1,int((m_notes.isEmpty()?0:m_notes.last().start)/4)+1)));
}
void MainWindow::updateStats(){const bool completed=m_routineStep>=0&&m_routineCompletedSteps.contains(m_routineStep);const int progress=completed?100:(m_earTraining?qMin(100,m_earTrainingSolved*10):(m_groups.isEmpty()?0:m_groupIndex*100/m_groups.size()));QString acc=m_attempts?QString::number(m_correct*100/m_attempts)+"%":"—";m_accuracyLabel->setText(acc+"\nACCURACY");m_streakLabel->setText(QString::number(m_streak)+"\nSTREAK");m_progressLabel->setText(QString::number(progress)+"%\nPROGRESS");}

void MainWindow::restartCompletedPracticePass(){
    if(m_groups.isEmpty())return;
    const bool routineStep=m_routineStep>=0;
    if(routineStep)m_routineCompletedSteps.insert(m_routineStep);
    m_groupIndex=0;m_matched.clear();m_waitBeat=m_groups.first().start;m_waitAdvanceClock.restart();m_lastWaitAdvanceMs=0;
    m_scoreAutoFollow=true;m_score->setPlayedBeat(-1);m_score->setFollowBeat(m_groups.first().start);updateScoreHistory(m_groups.first().start,true);
    m_bubble->setPlayhead(m_groups.first().start);
    updateExpected();
    if(routineStep)
      m_routineStepLabel->setText(QString("DAILY PLAN · STEP %1 OF 8 · COMPLETE ✓").arg(m_routineStep+1));
    m_coachLabel->setText(routineStep
        ?"Completed ✓ — ready to try again\nPlay the first highlighted note, or choose Next step."
        :"Completed ✓ — starting again\nPlay the first highlighted note to continue practicing.");
    updateStats();
}
void MainWindow::updateSongMeta(){m_noteCountLabel->setText(QString::number(m_notes.size())+" notes");double end=0;for(const auto&n:m_notes)end=qMax(end,n.start+n.duration);int sec=qRound(end*60.0/qMax(1,m_tempo));m_durationLabel->setText(QString("%1:%2").arg(sec/60).arg(sec%60,2,10,QChar('0')));}

void MainWindow::changeTempo(int tempo){
    tempo=qBound(30,tempo,260);if(tempo==m_tempo)return;
    const int oldTempo=m_tempo;
    double currentBeat=0.0;
    if(m_playing){
        if(m_usingImportedMediaAudio&&m_sourcePlayer->position()>=m_importedMediaStartMs){
            const int sourceTempo=m_importedMediaTempo>0?m_importedMediaTempo:oldTempo;
            currentBeat=(m_sourcePlayer->position()-m_importedMediaStartMs)/1000.0*sourceTempo/60.0;
        }else currentBeat=(m_playbackOffsetSeconds+m_playClock.elapsed()/1000.0)*oldTempo/60.0;
    }
    m_tempo=tempo;
    if(m_tempoControl&&m_tempoControl->value()!=tempo){const QSignalBlocker blocker(m_tempoControl);m_tempoControl->setValue(tempo);}
    if(m_importedMediaTempo>0){
        const double rate=double(m_tempo)/m_importedMediaTempo;
        const qint64 sourcePosition=m_sourcePlayer->position();
        const bool mediaWasPlaying=m_usingImportedMediaAudio&&m_sourcePlayer->playbackState()==QMediaPlayer::PlayingState;
        m_sourcePlayer->setPlaybackRate(rate);
        if(mediaWasPlaying){m_sourcePlayer->setPosition(sourcePosition);m_sourcePlayer->play();}
        if(m_sourceAudio&&m_sourceAudio->isChecked())toast(QString("Media tempo: %1 BPM · %2× speed").arg(m_tempo).arg(rate,0,'f',2));
    }

    if(m_playing&&!m_usingImportedMediaAudio){
        m_midiOutput.allNotesOff();m_midiOutputNotes.clear();
        if(!m_midiOutputCheck->isChecked())m_synth.startSequence(filteredNotes(),m_tempo,currentBeat);
        else m_synth.stopSequence();
        m_playbackOffsetSeconds=currentBeat*60.0/m_tempo;m_playClock.restart();
        for(int note=21;note<=108;++note)if(!m_heldNotes.contains(note))m_piano->setActive(note,false);
        m_startedEvents.clear();m_endedEvents.clear();
        auto included=[&](const NoteEvent &note){return m_handFilter<0||(m_handFilter==0&&note.hand==Hand::Left)||(m_handFilter==1&&note.hand==Hand::Right);};
        for(int i=0;i<m_notes.size();++i){
            const auto &note=m_notes[i];if(!included(note)||note.start>currentBeat)continue;
            m_startedEvents.insert(i);
            if(note.start+note.duration<=currentBeat)m_endedEvents.insert(i);else playbackNoteOn(i);
        }
        m_groupIndex=0;while(m_groupIndex+1<m_groups.size()&&m_groups[m_groupIndex+1].start<=currentBeat+.08)++m_groupIndex;
        updateExpected();m_score->setPlayedBeat(currentBeat);m_score->setFollowBeat(currentBeat);m_bubble->setPlayhead(currentBeat);
    }
    m_metroClock.restart();m_lastMetroBeat=-1;updateSongMeta();
}

void MainWindow::userNoteOn(int note,int velocity){if(m_heldNotes.contains(note))return;m_heldNotes.insert(note);
    const bool fromHardware=sender()==&m_midi;
    if(m_midiOutputCheck->isChecked()&&m_midiOutput.isOpen()){
        // The physical keyboard already sounds its own keys.  Only route the
        // on-screen/computer keyboard back to MIDI OUT, avoiding doubled notes.
        if(!fromHardware){m_midiOutput.noteOn(note,velocity);m_liveMidiOutputNotes.insert(note);}
    }else m_userVoiceIds[note]=m_synth.noteOn(note,velocity);
    const double displayBeat=(m_groupIndex<m_groups.size()?m_groups[m_groupIndex].start:0.0);
    m_piano->setActive(note,true);m_bubble->pulseNote(note);m_liveNoteLabel->setText(m_score->noteNameForBeat(note,displayBeat));
    if(m_mode==Mode::Free&&!m_freePlayStarts.contains(note)){
        if(!m_freePlayClock.isValid())m_freePlayClock.start();
        const bool inserting=m_freePlayEditBeat>=0&&!m_recording;
        if(inserting&&m_freePlayEditPressMs.isEmpty())m_freePlayEditAdvance=0;
        const qint64 pressMs=m_freePlayClock.elapsed();
        const double rawBeat=m_freePlayOffsetBeat+pressMs/1000.0*m_tempo/60.0;
        double start=0;
        if(inserting)start=m_freePlayEditBeat;
        else if(m_recording)start=qRound(rawBeat*8.0)/8.0;
        else if(!m_freePlayStarts.isEmpty()){
            start=std::numeric_limits<double>::max();
            for(double heldStart:m_freePlayStarts)start=qMin(start,heldStart);
        }else{
            for(const auto &event:m_freePlayNotes)start=qMax(start,event.start+event.duration);
            for(const auto &rest:m_freePlayRests)start=qMax(start,rest.beat+rest.duration);
            start=qRound(start*8.0)/8.0;
        }
        const Hand hand=note<60?Hand::Left:Hand::Right;
        if(inserting){m_freePlayRests.erase(std::remove_if(m_freePlayRests.begin(),m_freePlayRests.end(),[start,hand](const auto &rest){return rest.hand==hand&&start>=rest.beat-.001&&start<rest.beat+rest.duration-.001;}),m_freePlayRests.end());m_score->setRests(m_freePlayRests);}
        m_freePlayStarts[note]=start;m_freePlayIndexes[note]=m_freePlayNotes.size();m_freePlayPressMs[note]=pressMs;if(inserting)m_freePlayEditPressMs[note]=pressMs;m_freePlayNotes.append({note,start,.125,velocity,hand});
        syncFreePlayScore();m_score->setNotes(m_freePlayNotes);m_score->setExpected({note},start);m_score->setPlayedBeat(start);if(inserting){m_freePlayAutoFollow=false;m_score->setFollowBeat(start);}else updateFreePlayHistory(m_freePlayAutoFollow);
        m_bubble->setNotes(m_freePlayNotes);m_bubble->setExpected({note},start);m_bubble->setPlayhead(start);m_barLabel->setText("FREE PLAY   ·   "+MusicDocument::noteName(note)+" on the grand staff");m_score->repaint();
    }
    if(m_recording&&!m_recordStarts.contains(note))m_recordStarts[note]=m_recordClock.elapsed()/1000.0*m_tempo/60.0;
    if(m_earTraining&&!m_earExpected.isEmpty()){
        m_synth.stopSequence();++m_attempts;
        if(m_earExpected.contains(note)){
            if(!m_matched.contains(note)){m_matched.insert(note);++m_correct;++m_streak;m_bestStreak=qMax(m_bestStreak,m_streak);}
            if(m_matched==m_earExpected){
                ++m_earTrainingSolved;
                QList<int>answerNotes=m_earExpected.values();std::sort(answerNotes.begin(),answerNotes.end());QStringList answer;for(int pitch:answerNotes)answer<<MusicDocument::noteName(pitch);
                m_score->setNotes(m_earPrompt);m_score->setExpected(m_earExpected,0);m_score->setFollowBeat(0);m_score->setPlayedBeat(0);
                m_bubble->setNotes(m_earPrompt);m_bubble->setExpected(m_earExpected,0);m_bubble->setPlayhead(0);
                m_nextNoteLabel->setText(answer.join(" + "));m_coachLabel->setText("Correct — "+answer.join(" + ")+"\nThe answer is now shown on the grand staff.");
                const int completedRound=m_earTrainingRound;
                m_earExpected.clear();
                if(m_earTrainingSolved<10)QTimer::singleShot(1400,this,[this,completedRound]{if(m_earTraining&&m_earTrainingRound==completedRound)startEarTrainingRound();});
                else{
                    m_routineCompletedSteps.insert(m_routineStep);
                    m_routineStepLabel->setText(QString("DAILY PLAN · STEP %1 OF 8 · COMPLETE ✓").arg(m_routineStep+1));
                    m_coachLabel->setText("10 rounds complete ✓\nStarting again so you can keep practicing, or choose Next step.");
                    m_earTrainingSolved=0;m_earTrainingRound=0;
                    QTimer::singleShot(1400,this,[this]{if(m_earTraining)startEarTrainingRound();});
                }
            }else m_coachLabel->setText(QString("Correct — %1 of %2 chord notes found.\nFind the remaining note%3.").arg(m_matched.size()).arg(m_earExpected.size()).arg(m_earExpected.size()-m_matched.size()==1?"":"s"));
        }else{m_streak=0;m_coachLabel->setText(MusicDocument::noteName(note)+" is not it yet.\nListen again or try another key.");}
        updateStats();
    }else if((m_mode==Mode::Wait||m_mode==Mode::Follow)&&m_groupIndex<m_groups.size()){const auto&expected=m_groups[m_groupIndex].notes;m_attempts++;if(expected.contains(note)){if(!m_matched.contains(note)){m_matched.insert(note);m_correct++;m_streak++;m_bestStreak=qMax(m_bestStreak,m_streak);}if(m_mode==Mode::Wait)m_coachLabel->setText(expected.size()>1?QString("Correct pitch — %1 of %2 chord notes held\nKeep holding for the written lengths.").arg(m_matched.size()).arg(expected.size()):"Correct pitch — keep holding\nThe line and falling note now follow your key length.");else m_coachLabel->setText("Correct pitch — keep playing with the moving line\nPlay Along advances by tempo, not by a quick touch.");}else{m_streak=0;m_coachLabel->setText(m_score->noteNameForBeat(note,m_groups[m_groupIndex].start)+" is close — try "+m_nextNoteLabel->text()+".");}updateStats();}}
void MainWindow::userNoteOff(int note){
    m_heldNotes.remove(note);
    if(m_liveMidiOutputNotes.remove(note))m_midiOutput.noteOff(note);
    if(m_mode==Mode::Free&&m_freePlayStarts.contains(note)){
        const bool inserted=m_freePlayEditPressMs.contains(note);
        const double end=m_freePlayOffsetBeat+m_freePlayClock.elapsed()/1000.0*m_tempo/60.0,start=m_freePlayStarts.take(note);
        const int index=m_freePlayIndexes.take(note);
        const bool hasPressTime=m_freePlayPressMs.contains(note);const qint64 pressMs=m_freePlayPressMs.take(note);m_freePlayEditPressMs.remove(note);
        const double rawDuration=hasPressTime?(m_freePlayClock.elapsed()-pressMs)*m_tempo/60000.0:end-start;
        const double duration=qMax(.125,qRound(rawDuration*8.0)/8.0);
        if(index>=0&&index<m_freePlayNotes.size())m_freePlayNotes[index].duration=duration;
        if(inserted)m_freePlayEditAdvance=qMax(m_freePlayEditAdvance,duration);
        if(inserted&&m_freePlayEditPressMs.isEmpty()){
            std::sort(m_freePlayNotes.begin(),m_freePlayNotes.end(),[](const auto&a,const auto&b){return qAbs(a.start-b.start)<.001?a.note<b.note:a.start<b.start;});
            if(m_freePlayGroupEntry)m_barLabel->setText("FREE PLAY   ·   "+scorePosition(m_freePlayEditBeat,m_freePlayTimeSignatures)+" · GROUPING · Right-click when done");
            else{m_freePlayEditBeat+=qMax(.125,m_freePlayEditAdvance);m_score->setEditCursorBeat(m_freePlayEditBeat);m_score->setFollowBeat(m_freePlayEditBeat);m_barLabel->setText("FREE PLAY   ·   "+scorePosition(m_freePlayEditBeat,m_freePlayTimeSignatures)+" · Each note steps forward");}
            m_freePlayEditAdvance=0;
        }
        syncFreePlayScore();m_score->setNotes(m_freePlayNotes);m_bubble->setNotes(m_freePlayNotes);
        if(inserted)updateFreePlayHistory(false);else updateFreePlayHistory(m_freePlayAutoFollow);m_score->repaint();
    }
    if(m_recording&&m_recordStarts.contains(note)){double end=m_recordClock.elapsed()/1000.0*m_tempo/60.0,start=m_recordStarts.take(note);start=qRound(start*4)/4.0;double duration=qMax(.25,qRound((end-start)*4)/4.0);m_recorded<<NoteEvent{note,start,duration,96,note<60?Hand::Left:Hand::Right};}
    if(m_sustain){m_sustainedNotes.insert(note);return;}
    if(m_userVoiceIds.contains(note))m_synth.noteOffVoice(m_userVoiceIds.take(note));m_piano->setActive(note,false);if(m_heldNotes.isEmpty())m_liveNoteLabel->setText(m_recording?"Recording…":"Ready");
}
void MainWindow::playbackNoteOn(int i){const auto&n=m_notes[i];m_piano->setActive(n.note,true);m_bubble->pulseNote(n.note);if(m_midiOutputCheck->isChecked()&&!m_usingImportedMediaAudio&&m_midiOutput.isOpen()){m_midiOutput.noteOn(n.note,n.velocity);m_midiOutputNotes[n.note]=m_midiOutputNotes.value(n.note)+1;}}
void MainWindow::playbackNoteOff(int i){const auto&n=m_notes[i];if(!m_heldNotes.contains(n.note))m_piano->setActive(n.note,false);if(m_midiOutputNotes.contains(n.note)){const int remaining=m_midiOutputNotes.value(n.note)-1;if(remaining<=0){m_midiOutputNotes.remove(n.note);m_midiOutput.noteOff(n.note);}else m_midiOutputNotes[n.note]=remaining;}}
void MainWindow::togglePlayback(){
    if(m_playing){pausePlayback();return;}
    if(m_playbackPaused){
        const int clockTempo=m_usingImportedMediaAudio&&m_importedMediaTempo>0?m_importedMediaTempo:m_tempo;
        const double currentBeat=m_playbackOffsetSeconds*clockTempo/60.0;
        if(m_usingImportedMediaAudio){m_sourcePlayer->setPlaybackRate(m_importedMediaTempo>0?double(m_tempo)/m_importedMediaTempo:1.0);m_sourcePlayer->setPosition(m_importedMediaStartMs+qRound64(m_playbackOffsetSeconds*1000.0));m_sourcePlayer->play();}
        else{
            const QVector<NoteEvent> sequence=m_mode==Mode::Free?m_freePlayNotes:filteredNotes();
            if(!m_midiOutputCheck->isChecked())m_synth.startSequence(sequence,m_tempo,currentBeat);
        }
        m_startedEvents.clear();m_endedEvents.clear();m_midiOutputNotes.clear();
        auto included=[&](const NoteEvent &note){return m_handFilter<0||(m_handFilter==0&&note.hand==Hand::Left)||(m_handFilter==1&&note.hand==Hand::Right);};
        for(int i=0;i<m_notes.size();++i){const auto &note=m_notes[i];if(!included(note)||note.start>currentBeat+.001)continue;m_startedEvents.insert(i);if(note.start+note.duration<=currentBeat+.001)m_endedEvents.insert(i);else if(!m_usingImportedMediaAudio)playbackNoteOn(i);}
        m_groupIndex=0;while(m_groupIndex+1<m_groups.size()&&m_groups[m_groupIndex+1].start<=currentBeat+.08)++m_groupIndex;
        m_playing=true;m_playbackPaused=false;m_playClock.restart();m_metroClock.restart();m_lastMetroBeat=-1;if(m_mode==Mode::Free){m_score->setEditEnabled(false);m_score->setSeekEnabled(true);m_score->setExpected({},0);m_bubble->setExpected({},0);m_score->setPlayedBeat(currentBeat);updateFreePlayPreviewHistory(currentBeat,true);}else{updateExpected();m_score->setPlayedBeat(currentBeat);updateScoreHistory(currentBeat);}m_bubble->setPlayhead(currentBeat);updatePrimaryButton();toast(m_usingImportedMediaAudio?"Original recording resumed":"Score playback resumed");return;
    }
    if(m_earTraining){playEarTrainingPrompt();return;}
    if(m_mode==Mode::Wait){stopPlayback(true);rebuildPractice();m_metroClock.restart();toast("Wait mode restarted — nothing moves until you play the correct key");return;}
    if(m_mode==Mode::Free){
        if(m_recording)finishRecording();
        if(m_freePlayNotes.isEmpty()){toast("Add at least one note before previewing the score");return;}
        syncFreePlayScore();m_usingImportedMediaAudio=false;m_playbackPaused=false;
        double scoreEnd=0;for(const auto &note:m_freePlayNotes)scoreEnd=qMax(scoreEnd,note.start+note.duration);
        const double startBeat=m_freePlayEditBeat>=0?qBound(0.0,m_freePlayEditBeat,scoreEnd):0.0;
        m_playbackOffsetSeconds=startBeat*60.0/qMax(1,m_tempo);
        if(!m_midiOutputCheck->isChecked())m_synth.startSequence(m_freePlayNotes,m_tempo,startBeat);
        m_playing=true;m_playClock.restart();m_metroClock.restart();m_lastMetroBeat=-1;m_startedEvents.clear();m_endedEvents.clear();m_midiOutputNotes.clear();
        for(int i=0;i<m_notes.size();++i){const auto &note=m_notes[i];if(note.start>startBeat+.001)continue;m_startedEvents.insert(i);if(note.start+note.duration<=startBeat+.001)m_endedEvents.insert(i);else playbackNoteOn(i);}
        m_freePlayAutoFollow=true;m_score->setEditCursorBeat(-1);m_score->setEditEnabled(false);m_score->setSeekEnabled(true);m_score->setExpected({},0);m_bubble->setExpected({},0);m_score->setPlayedBeat(startBeat);m_bubble->setPlayhead(startBeat);updateFreePlayPreviewHistory(startBeat,true);updatePrimaryButton();
        toast(m_midiOutputCheck->isChecked()?"Sending the Free Play staff to the keyboard from the orange line":"Playing the Free Play staff from the orange line");return;
    }
    if(m_recording)finishRecording();if(m_notes.isEmpty()){showNotice("Import or record some notes first.",true);return;}
    m_usingImportedMediaAudio=useImportedMediaAudio();m_playbackPaused=false;m_playbackOffsetSeconds=0;
    if(m_usingImportedMediaAudio){m_sourcePlayer->setPlaybackRate(m_importedMediaTempo>0?double(m_tempo)/m_importedMediaTempo:1.0);m_sourcePlayer->setPosition(m_importedMediaStartMs);m_sourcePlayer->play();}
    else if(!m_midiOutputCheck->isChecked())m_synth.startSequence(filteredNotes(),m_tempo);
    m_playing=true;m_playClock.restart();m_metroClock.restart();m_lastMetroBeat=-1;m_startedEvents.clear();m_endedEvents.clear();m_midiOutputNotes.clear();m_groupIndex=0;m_scoreAutoFollow=true;updateExpected();updateScoreHistory(0,true);updatePrimaryButton();
    toast(m_usingImportedMediaAudio?"Playing the original imported recording":m_midiOutputCheck->isChecked()?"Sending the notation to the keyboard":m_mode==Mode::Listen?"Listening to the notation playback":"Play along started");
}

void MainWindow::pausePlayback(){
    if(!m_playing)return;
    double elapsedSeconds=m_playbackOffsetSeconds+m_playClock.elapsed()/1000.0;
    int clockTempo=m_tempo;
    if(m_usingImportedMediaAudio&&m_sourcePlayer->position()>=m_importedMediaStartMs){elapsedSeconds=(m_sourcePlayer->position()-m_importedMediaStartMs)/1000.0;clockTempo=m_importedMediaTempo>0?m_importedMediaTempo:m_tempo;m_sourcePlayer->pause();}
    const double beat=elapsedSeconds*clockTempo/60.0;
    for(int i:m_startedEvents)if(!m_endedEvents.contains(i))playbackNoteOff(i);
    m_synth.stopSequence();m_midiOutput.allNotesOff();m_midiOutputNotes.clear();
    m_playbackOffsetSeconds=elapsedSeconds;m_playing=false;m_playbackPaused=true;
    m_score->setPlayedBeat(beat);if(m_mode==Mode::Free)updateFreePlayPreviewHistory(beat);else updateScoreHistory(beat);m_bubble->setPlayhead(beat);updatePrimaryButton();toast("Paused — press Play to resume");
}

void MainWindow::seekPlayback(double beat){
    if(m_notes.isEmpty())return;
    if(m_playing)pausePlayback();
    double end=0;for(const auto &note:filteredNotes())end=qMax(end,note.start+note.duration);beat=qBound(0.0,beat,end);
    m_synth.stopSequence();m_midiOutput.allNotesOff();m_midiOutputNotes.clear();
    for(int note=21;note<=108;++note)if(!m_heldNotes.contains(note))m_piano->setActive(note,false);
    m_startedEvents.clear();m_endedEvents.clear();m_matched.clear();
    m_groupIndex=0;while(m_groupIndex<m_groups.size()&&m_groups[m_groupIndex].start<beat-.001)++m_groupIndex;if(m_groupIndex>=m_groups.size()&&!m_groups.isEmpty())m_groupIndex=m_groups.size()-1;
    if(m_mode==Mode::Wait){m_playing=false;m_playbackPaused=false;m_usingImportedMediaAudio=false;updateExpected();const double target=m_groupIndex<m_groups.size()?m_groups[m_groupIndex].start:beat;m_waitBeat=target;m_scoreAutoFollow=true;m_score->setPlayedBeat(target);updateScoreHistory(target,true);m_bubble->setPlayhead(target);updatePrimaryButton();toast("Wait mode moved — play the highlighted note or chord");return;}
    m_usingImportedMediaAudio=useImportedMediaAudio();const int clockTempo=m_usingImportedMediaAudio&&m_importedMediaTempo>0?m_importedMediaTempo:m_tempo;
    m_playbackOffsetSeconds=beat*60.0/qMax(1,clockTempo);m_playbackPaused=true;m_playing=false;
    if(m_usingImportedMediaAudio)m_sourcePlayer->setPosition(m_importedMediaStartMs+qRound64(m_playbackOffsetSeconds*1000.0));
    for(int i=0;i<m_notes.size();++i){const auto &note=m_notes[i];const bool use=m_handFilter<0||(m_handFilter==0&&note.hand==Hand::Left)||(m_handFilter==1&&note.hand==Hand::Right);if(!use||note.start>beat+.001)continue;m_startedEvents.insert(i);if(note.start+note.duration<=beat+.001)m_endedEvents.insert(i);}
    if(m_mode==Mode::Free){m_freePlayAutoFollow=true;m_score->setEditEnabled(false);m_score->setSeekEnabled(true);m_score->setExpected({},0);m_bubble->setExpected({},0);m_score->setPlayedBeat(beat);updateFreePlayPreviewHistory(beat,true);}else{updateExpected();m_scoreAutoFollow=true;m_score->setPlayedBeat(beat);updateScoreHistory(beat,true);}m_bubble->setPlayhead(beat);updatePrimaryButton();toast("Moved to this point — press Play to resume");
}

void MainWindow::stopPlayback(bool rewind){
    if(m_playing||m_playbackPaused){for(int i:m_startedEvents)if(!m_endedEvents.contains(i))playbackNoteOff(i);}
    // Clear playback state before stopping QMediaPlayer.  Its status signal can
    // fire synchronously; leaving m_playing true until afterwards allowed the
    // callback to start the source again and made Stop appear to need several
    // clicks.
    m_playing=false;m_playbackPaused=false;m_usingImportedMediaAudio=false;m_playbackOffsetSeconds=0;
    m_sourcePlayer->stop();m_startedEvents.clear();m_endedEvents.clear();m_synth.stopSequence();m_midiOutput.allNotesOff();m_midiOutputNotes.clear();
    for(int n=21;n<=108;++n)if(!m_heldNotes.contains(n))m_piano->setActive(n,false);
    if(rewind){m_score->setPlayedBeat(-1);if(m_mode==Mode::Free)m_score->setFollowBeat(m_freePlayEditBeat>=0?m_freePlayEditBeat:0);else{m_scoreAutoFollow=true;updateScoreHistory(0,true);}m_bubble->setPlayhead(0);}if(m_mode==Mode::Free){m_score->setEditEnabled(true);m_score->setSeekEnabled(false);m_score->setEditCursorBeat(m_freePlayEditBeat);}updatePrimaryButton();
}

void MainWindow::updateClocks(){
    double beat=0;bool clockActive=false;
    if(m_playing){
        double elapsedSeconds=m_playbackOffsetSeconds+m_playClock.elapsed()/1000.0;
        int clockTempo=m_tempo;
        if(m_usingImportedMediaAudio&&m_sourcePlayer->position()>=m_importedMediaStartMs){elapsedSeconds=(m_sourcePlayer->position()-m_importedMediaStartMs)/1000.0;clockTempo=m_importedMediaTempo>0?m_importedMediaTempo:m_tempo;}
        beat=elapsedSeconds*clockTempo/60.0;clockActive=true;
        for(int i=0;i<m_notes.size();++i){const auto&n=m_notes[i];bool use=m_handFilter<0||(m_handFilter==0&&n.hand==Hand::Left)||(m_handFilter==1&&n.hand==Hand::Right);if(!use)continue;if(!m_startedEvents.contains(i)&&beat>=n.start){m_startedEvents.insert(i);playbackNoteOn(i);}if(m_startedEvents.contains(i)&&!m_endedEvents.contains(i)&&beat>=n.start+n.duration){m_endedEvents.insert(i);playbackNoteOff(i);}}
        m_score->setPlayedBeat(beat);m_bubble->setPlayhead(beat);if(m_mode==Mode::Free)updateFreePlayPreviewHistory(beat);else{const int previousGroup=m_groupIndex;while(m_groupIndex+1<m_groups.size()&&m_groups[m_groupIndex+1].start<=beat+.08)m_groupIndex++;if(m_groupIndex!=previousGroup){m_matched.clear();updateExpected();}updateScoreHistory(beat);}
        double end=0;for(const auto&n:m_notes){const bool use=m_handFilter<0||(m_handFilter==0&&n.hand==Hand::Left)||(m_handFilter==1&&n.hand==Hand::Right);if(use)end=qMax(end,n.start+n.duration);}if(beat>end+.2){const bool repeat=m_mode==Mode::Follow;stopPlayback(repeat);if(repeat)QTimer::singleShot(0,this,[this]{if(m_mode==Mode::Follow&&!m_playing&&!m_playbackPaused&&!m_notes.isEmpty())togglePlayback();});}
    }else if(m_mode==Mode::Wait&&!m_earTraining&&!m_groups.isEmpty()){
        const qint64 now=m_waitAdvanceClock.elapsed();
        const double elapsedBeats=qMax<qint64>(0,now-m_lastWaitAdvanceMs)*m_tempo/60000.0;
        m_lastWaitAdvanceMs=now;
        const auto notes=filteredNotes();
        QSet<int> required;
        for(const auto &note:notes)if(note.start<=m_waitBeat+.001&&note.start+note.duration>m_waitBeat+.001)required.insert(note.note);
        // Every written attack in the current group must be played after that
        // group becomes current.  This deliberately differs from merely
        // checking whether a pitch is still held: two consecutive C notes are
        // two attacks, while an actual tie was already imported as one longer
        // NoteEvent and therefore continues without a second key press.
        bool canAdvance=m_matched.contains(m_groups[m_groupIndex].notes);
        for(int pitch:required)if(!m_heldNotes.contains(pitch)&&!m_sustainedNotes.contains(pitch)){canAdvance=false;break;}
        if(canAdvance&&elapsedBeats>0){
            double nextBoundary=std::numeric_limits<double>::max();
            for(const auto &note:notes){if(note.start>m_waitBeat+.001)nextBoundary=qMin(nextBoundary,note.start);const double end=note.start+note.duration;if(end>m_waitBeat+.001)nextBoundary=qMin(nextBoundary,end);}
            m_waitBeat+=elapsedBeats;if(nextBoundary<std::numeric_limits<double>::max())m_waitBeat=qMin(m_waitBeat,nextBoundary);
        }
        double endBeat=0;for(const auto &note:notes)endBeat=qMax(endBeat,note.start+note.duration);
        if(endBeat>0&&m_waitBeat>=endBeat-.001){restartCompletedPracticePass();return;}
        int nextGroup=0;while(nextGroup+1<m_groups.size()&&m_groups[nextGroup+1].start<=m_waitBeat+.001)++nextGroup;
        if(nextGroup!=m_groupIndex){m_groupIndex=nextGroup;m_matched.clear();updateExpected();}
        m_score->setPlayedBeat(m_waitBeat);updateScoreHistory(m_waitBeat);m_bubble->setPlayhead(m_waitBeat);
    }else if(m_mode==Mode::Free&&m_freePlayClock.isValid()){
        beat=m_freePlayOffsetBeat+m_freePlayClock.elapsed()/1000.0*m_tempo/60.0;clockActive=true;bool durationChanged=false;
        for(auto it=m_freePlayStarts.cbegin();it!=m_freePlayStarts.cend();++it){const int note=it.key(),index=m_freePlayIndexes.value(note,-1);if(index<0||index>=m_freePlayNotes.size())continue;const double raw=m_freePlayPressMs.contains(note)?(m_freePlayClock.elapsed()-m_freePlayPressMs.value(note))*m_tempo/60000.0:beat-it.value();const double duration=qMax(.125,qRound(raw*8.0)/8.0);if(qAbs(m_freePlayNotes[index].duration-duration)>.001){m_freePlayNotes[index].duration=duration;durationChanged=true;}}
        if(durationChanged){syncFreePlayScore();m_score->setNotes(m_freePlayNotes);m_bubble->setNotes(m_freePlayNotes);updateFreePlayHistory(m_freePlayEditBeat<0&&m_freePlayAutoFollow);m_score->repaint();}
        double scoreEnd=0;for(const auto &event:m_freePlayNotes)scoreEnd=qMax(scoreEnd,event.start+event.duration);const double visualBeat=m_freePlayEditBeat>=0?m_freePlayEditBeat:(m_recording?beat:scoreEnd);m_bubble->setPlayhead(visualBeat);
    }else if(m_metronome->isChecked()){beat=m_metroClock.elapsed()/1000.0*m_tempo/60.0;clockActive=true;}
    if(m_metronome->isChecked()&&clockActive){int whole=int(std::floor(beat));if(whole!=m_lastMetroBeat){m_lastMetroBeat=whole;m_synth.click(whole%4==0);}}
}
void MainWindow::toggleRecord(){if(m_recording){finishRecording();return;}stopPlayback(false);if(m_mode!=Mode::Free)setMode(Mode::Free);m_freePlayGroupEntry=false;m_freePlayEditBeat=-1;m_freePlayEditAdvance=0;m_freePlayPressMs.clear();m_freePlayEditPressMs.clear();m_score->setEditCursorBeat(-1);m_freePlayAutoFollow=true;double scoreEnd=0;for(const auto &event:m_freePlayNotes)scoreEnd=qMax(scoreEnd,event.start+event.duration);for(const auto &rest:m_freePlayRests)scoreEnd=qMax(scoreEnd,rest.beat+rest.duration);m_freePlayOffsetBeat=scoreEnd>0?scoreEnd+.5:0;m_freePlayClock.restart();m_recording=true;m_recorded.clear();m_recordStarts.clear();m_recordClock.restart();m_metroClock.restart();m_lastMetroBeat=-1;m_recordButton->setText("■  Stop recording");m_recordButton->setProperty("recording",true);m_recordButton->style()->unpolish(m_recordButton);m_recordButton->style()->polish(m_recordButton);m_liveNoteLabel->setText("Recording…");toast("Free Play recording started — real pauses and timing will be preserved");}
void MainWindow::finishRecording(){if(!m_recording)return;for(int n:m_recordStarts.keys())userNoteOff(n);m_recording=false;m_recordButton->setText("●  Record free play");m_recordButton->setProperty("recording",false);m_recordButton->style()->unpolish(m_recordButton);m_recordButton->style()->polish(m_recordButton);if(!m_recorded.isEmpty()){std::sort(m_recorded.begin(),m_recorded.end(),[](auto&a,auto&b){return a.start==b.start?a.note<b.note:a.start<b.start;});double first=m_recorded.first().start;for(auto&n:m_recorded)n.start-=first;setSong("My recording",m_recorded,m_tempo);m_freePlayNotes=m_notes;m_freePlayStarts.clear();m_freePlayIndexes.clear();m_freePlayApplied=true;double endBeat=0;for(const auto&n:m_freePlayNotes)endBeat=qMax(endBeat,n.start+n.duration);m_freePlayOffsetBeat=endBeat+.5;m_freePlayClock.restart();m_score->setNotes(m_freePlayNotes);m_bubble->setNotes(m_freePlayNotes);m_bubble->setPlayhead(endBeat);updateFreePlayHistory(true);m_barLabel->setText("FREE PLAY   ·   Recorded score");toast(QString("Recorded %1 notes and created the score").arg(m_notes.size()));}else toast("Recording stopped — no notes captured");}

void MainWindow::setFreePlayGrouping(bool grouped,double beat){
    if(m_mode!=Mode::Free||m_recording||!m_freePlayStarts.isEmpty())return;
    if(grouped){
        if(beat>=0)m_freePlayEditBeat=beat;
        if(m_freePlayEditBeat<0){double endBeat=0;for(const auto &note:m_freePlayNotes)endBeat=qMax(endBeat,note.start+note.duration);m_freePlayEditBeat=endBeat;}
        m_freePlayGroupEntry=true;m_freePlayEditAdvance=0;m_freePlayAutoFollow=false;
        m_score->setEditCursorBeat(m_freePlayEditBeat);m_score->setFollowBeat(m_freePlayEditBeat);updateFreePlayHistory(false);
        m_barLabel->setText("FREE PLAY   ·   "+scorePosition(m_freePlayEditBeat,m_freePlayTimeSignatures)+" · GROUPING · Right-click when done");toast("Grouping is on — every note stays at the orange line");
        return;
    }
    if(!m_freePlayGroupEntry)return;
    const double groupedBeat=m_freePlayEditBeat;double longest=0;
    for(const auto &note:m_freePlayNotes)if(qAbs(note.start-groupedBeat)<.04)longest=qMax(longest,note.duration);
    m_freePlayGroupEntry=false;m_freePlayEditAdvance=0;if(longest>0)m_freePlayEditBeat=groupedBeat+longest;
    m_score->setEditCursorBeat(m_freePlayEditBeat);m_score->setFollowBeat(m_freePlayEditBeat);updateFreePlayHistory(false);
    m_barLabel->setText("FREE PLAY   ·   "+scorePosition(m_freePlayEditBeat,m_freePlayTimeSignatures)+" · Each note steps forward");toast("Grouping finished — normal stepping is back on");
}

void MainWindow::updateFreePlayHistory(bool followLatest){
    double endBeat=0;for(const auto &event:m_freePlayNotes)endBeat=qMax(endBeat,event.start+event.duration);for(const auto &rest:m_freePlayRests)endBeat=qMax(endBeat,rest.beat+rest.duration);for(const auto &signature:m_freePlayTimeSignatures)endBeat=qMax(endBeat,signature.beat);for(const auto &signature:m_freePlayKeySignatures)endBeat=qMax(endBeat,signature.beat);for(double repeat:m_freePlayRepeatEnds)endBeat=qMax(endBeat,repeat);if(m_freePlayEditBeat>=0)endBeat=qMax(endBeat,m_freePlayEditBeat);
    const int maximum=qMax(0,qCeil(endBeat*8.0));const bool follow=followLatest||m_freePlayAutoFollow;const int oldValue=m_scoreHistoryBar->value();const int value=follow?maximum:(m_freePlayEditBeat>=0?qBound(0,qRound(m_freePlayEditBeat*8.0),maximum):qMin(oldValue,maximum));
    {const QSignalBlocker blocker(m_scoreHistoryBar);m_scoreHistoryBar->setRange(0,maximum);m_scoreHistoryBar->setPageStep(32);m_scoreHistoryBar->setValue(value);m_scoreHistoryBar->setEnabled(maximum>0);}
    m_freePlayAutoFollow=follow||value>=maximum;m_score->setFollowBeat(value/8.0);m_score->repaint();
}

void MainWindow::updateFreePlayPreviewHistory(double liveBeat,bool forceFollow){
    double endBeat=0;for(const auto &event:m_freePlayNotes)endBeat=qMax(endBeat,event.start+event.duration);for(const auto &rest:m_freePlayRests)endBeat=qMax(endBeat,rest.beat+rest.duration);for(const auto &signature:m_freePlayTimeSignatures)endBeat=qMax(endBeat,signature.beat);for(const auto &signature:m_freePlayKeySignatures)endBeat=qMax(endBeat,signature.beat);for(double repeat:m_freePlayRepeatEnds)endBeat=qMax(endBeat,repeat);
    liveBeat=qBound(0.0,liveBeat,endBeat);const int maximum=qMax(0,qCeil(endBeat*8.0));const bool follow=forceFollow||m_freePlayAutoFollow;const int oldValue=m_scoreHistoryBar->value();const int value=follow?qBound(0,qRound(liveBeat*8.0),maximum):qMin(oldValue,maximum);
    {const QSignalBlocker blocker(m_scoreHistoryBar);m_scoreHistoryBar->setRange(0,maximum);m_scoreHistoryBar->setPageStep(32);m_scoreHistoryBar->setValue(value);m_scoreHistoryBar->setEnabled(maximum>0);}
    // Preview playback follows the orange play line, not the end of the
    // editable score.  A user can still drag back to inspect an earlier
    // measure; dragging to Latest re-enables live following.
    m_freePlayAutoFollow=follow||value>=maximum;m_score->setFollowBeat(follow?liveBeat:value/8.0);m_score->repaint();
}

void MainWindow::updateScoreHistory(double liveBeat,bool forceFollow){
    if(m_mode==Mode::Free)return;
    m_scoreLiveBeat=qMax(0.0,liveBeat);
    const int maximum=qMax(0,qCeil(m_scoreLiveBeat*8.0));
    const bool follow=forceFollow||m_scoreAutoFollow;
    const int oldValue=m_scoreHistoryBar->value();
    const int value=follow?maximum:qMin(oldValue,maximum);
    {
        const QSignalBlocker blocker(m_scoreHistoryBar);
        m_scoreHistoryBar->setRange(0,maximum);
        // One wheel notch or Page Step moves approximately one 4/4 measure.
        m_scoreHistoryBar->setPageStep(32);
        m_scoreHistoryBar->setValue(value);
        m_scoreHistoryBar->setEnabled(maximum>0);
    }
    m_scoreAutoFollow=follow||value>=maximum;
    m_score->setFollowBeat(m_scoreAutoFollow?m_scoreLiveBeat:value/8.0);
}

void MainWindow::refreshFreePlayStaff(){
    m_freePlayGroupEntry=false;m_freePlayEditAdvance=0;m_freePlayPressMs.clear();m_freePlayEditPressMs.clear();
    double endBeat=0,focusBeat=-1;for(const auto &event:m_freePlayNotes){endBeat=qMax(endBeat,event.start+event.duration);focusBeat=qMax(focusBeat,event.start);}for(const auto &rest:m_freePlayRests)endBeat=qMax(endBeat,rest.beat+rest.duration);
    m_freePlayStarts.clear();m_freePlayIndexes.clear();m_freePlayOffsetBeat=m_freePlayNotes.isEmpty()?0:endBeat+.5;if(m_freePlayNotes.isEmpty()&&m_freePlayRests.isEmpty())m_freePlayClock.invalidate();else m_freePlayClock.restart();
    m_score->setNotes(m_freePlayNotes);m_score->setTimeSignatures(m_freePlayTimeSignatures);m_score->setKeySignatures(m_freePlayKeySignatures);m_score->setRests(m_freePlayRests);m_score->setRepeatEnds(m_freePlayRepeatEnds);m_score->setExpected({},0);m_score->setPlayedBeat(focusBeat);
    m_bubble->setNotes(m_freePlayNotes);m_bubble->setExpected({},0);m_bubble->setPlayhead(qMax(0.0,focusBeat));
    const int seconds=qRound(endBeat*60.0/qMax(1,m_tempo));m_noteCountLabel->setText(QString("%1 note%2").arg(m_freePlayNotes.size()).arg(m_freePlayNotes.size()==1?"":"s"));m_durationLabel->setText(QString("%1:%2").arg(seconds/60).arg(seconds%60,2,10,QChar('0')));
    updateFreePlayHistory(true);m_barLabel->setText(m_freePlayNotes.isEmpty()?"FREE PLAY   ·   Empty grand staff":QString("FREE PLAY   ·   Edited staff · %1 notes").arg(m_freePlayNotes.size()));m_liveNoteLabel->setText("Ready");
}

void MainWindow::syncFreePlayScore(){
    const bool firstApply=!m_freePlayApplied;if(firstApply){m_title="Free Play score";m_titleEdit->setText(m_title);clearImportedMediaSource();m_pdfDocument->close();m_generatedScoreDir.reset();m_scoreViewButton->hide();m_liveScoreButton->hide();}
    m_freePlayApplied=true;m_notes=m_freePlayNotes;double endBeat=0;for(const auto &note:m_freePlayNotes)endBeat=qMax(endBeat,note.start+note.duration);for(const auto &rest:m_freePlayRests)endBeat=qMax(endBeat,rest.beat+rest.duration);const int seconds=qRound(endBeat*60.0/qMax(1,m_tempo));m_noteCountLabel->setText(QString("%1 notes · %2 rests").arg(m_freePlayNotes.size()).arg(m_freePlayRests.size()));m_durationLabel->setText(QString("%1:%2").arg(seconds/60).arg(seconds%60,2,10,QChar('0')));
}

void MainWindow::clearFreePlayStaff(){
    if(m_mode!=Mode::Free)return;
    if(m_recording){showNotice("Stop the Free Play recording before clearing the staff.",true);return;}
    if(!m_freePlayStarts.isEmpty()){toast("Release the held keys before clearing the staff");return;}
    const bool defaultMeter=m_freePlayTimeSignatures.size()==1&&m_freePlayTimeSignatures.first().beats==4&&m_freePlayTimeSignatures.first().beatType==4;const bool defaultKey=m_freePlayKeySignatures.size()==1&&m_freePlayKeySignatures.first().fifths==0;
    if(m_freePlayNotes.isEmpty()&&m_freePlayRests.isEmpty()&&m_freePlayRepeatEnds.isEmpty()&&defaultMeter&&defaultKey){toast("The Free Play staff is already empty");return;}
    if(QMessageBox::question(this,"Clear Free Play staff","Remove every note, rest, meter change, key signature, and repeat mark from the Free Play grand staff?",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes)return;
    m_freePlayNotes.clear();m_freePlayRests.clear();m_freePlayRepeatEnds.clear();m_freePlayTimeSignatures={{0.0,4,4}};m_freePlayKeySignatures={{0.0,0}};m_freePlayGroupEntry=false;m_freePlayEditBeat=-1;m_freePlayEditAdvance=0;m_score->setEditCursorBeat(-1);m_score->setTimeSignatures(m_freePlayTimeSignatures);m_score->setKeySignatures(m_freePlayKeySignatures);m_score->setRests({});m_score->setRepeatEnds({});if(m_freePlayApplied)m_notes.clear();refreshFreePlayStaff();toast("Free Play grand staff cleared");
}

void MainWindow::editFreePlayStaff(){
    if(m_mode!=Mode::Free)return;
    if(m_recording){showNotice("Stop the Free Play recording before editing its staff.",true);return;}
    if(!m_freePlayStarts.isEmpty()){toast("Release the held keys before editing the staff");return;}

    QDialog dialog(this);dialog.setWindowTitle("Edit Free Play grand staff");dialog.resize(780,500);dialog.setMinimumSize(680,420);
    auto *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(18,16,18,16);layout->setSpacing(10);
    auto *heading=label("Edit the notes that appear on the grand staff");heading->setStyleSheet("font-size:19px;font-weight:800");layout->addWidget(heading);
    auto *hint=label("Change pitch, timing, held length, or hand. Check Delete to remove a note. Applying the changes also makes this the score used by the top-right Save formats.");hint->setWordWrap(true);hint->setStyleSheet("color:#526862");layout->addWidget(hint);
    auto *table=new QTableWidget(0,5,&dialog);table->setHorizontalHeaderLabels({"Note","Start beat","Length","Hand","Delete"});table->verticalHeader()->setVisible(false);table->setAlternatingRowColors(true);table->setSelectionMode(QAbstractItemView::NoSelection);table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::Stretch);for(int column=1;column<5;++column)table->horizontalHeader()->setSectionResizeMode(column,QHeaderView::ResizeToContents);layout->addWidget(table,1);

    auto addRow=[table](const NoteEvent &event){
        const int row=table->rowCount();table->insertRow(row);
        auto *pitch=new QComboBox;for(int midi=21;midi<=108;++midi)pitch->addItem(MusicDocument::noteName(midi),midi);pitch->setCurrentIndex(qBound(0,event.note-21,87));table->setCellWidget(row,0,pitch);
        auto *start=new QDoubleSpinBox;start->setRange(0,9999);start->setDecimals(3);start->setSingleStep(.125);start->setValue(qMax(0.0,event.start));start->setSuffix(" beats");table->setCellWidget(row,1,start);
        auto *length=new QDoubleSpinBox;length->setRange(.125,64);length->setDecimals(3);length->setSingleStep(.125);length->setValue(qMax(.125,event.duration));length->setSuffix(" beats");table->setCellWidget(row,2,length);
        auto *hand=new QComboBox;hand->addItem("Left",int(Hand::Left));hand->addItem("Right",int(Hand::Right));hand->setCurrentIndex(event.hand==Hand::Left?0:1);table->setCellWidget(row,3,hand);
        auto *remove=new QCheckBox("Delete");table->setCellWidget(row,4,remove);
    };
    for(const auto &event:m_freePlayNotes)addRow(event);
    auto *controls=new QHBoxLayout;auto *add=button("＋  Add note","routineDialogSecondary");controls->addWidget(add);controls->addStretch();auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Save)->setText("Apply changes");controls->addWidget(buttons);layout->addLayout(controls);
    connect(add, &QPushButton::clicked, &dialog, [&, addRow] {
      double startBeat = 0;
      for (int row = 0; row < table->rowCount(); ++row)
        if (auto *start =
                qobject_cast<QDoubleSpinBox *>(table->cellWidget(row, 1)))
          startBeat = qMax(startBeat, start->value());
      addRow({60, startBeat + .5, 1, 96, Hand::Right});
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted)
      return;

    QVector<NoteEvent> edited;
    for (int row = 0; row < table->rowCount(); ++row) {
      auto *remove = qobject_cast<QCheckBox *>(table->cellWidget(row, 4));
      if (remove && remove->isChecked())
        continue;
      auto *pitch = qobject_cast<QComboBox *>(table->cellWidget(row, 0));
      auto *start = qobject_cast<QDoubleSpinBox *>(table->cellWidget(row, 1));
      auto *length = qobject_cast<QDoubleSpinBox *>(table->cellWidget(row, 2));
      auto *hand = qobject_cast<QComboBox *>(table->cellWidget(row, 3));
      if (!pitch || !start || !length || !hand)
        continue;
      edited.append({pitch->currentData().toInt(), start->value(),
                     length->value(), 96, Hand(hand->currentData().toInt())});
    }
    std::sort(edited.begin(), edited.end(),
              [](const NoteEvent &a, const NoteEvent &b) {
                return a.start == b.start ? a.note < b.note : a.start < b.start;
              });
    m_freePlayNotes = edited;
    if (!m_freePlayApplied) {
      m_title = "Free Play score";
      m_titleEdit->setText(m_title);
    }
    m_freePlayApplied = true;
    m_notes = m_freePlayNotes;
    clearImportedMediaSource();
    m_pdfDocument->close();
    m_generatedScoreDir.reset();
    m_scoreViewButton->hide();
    m_liveScoreButton->hide();
    refreshFreePlayStaff();
    toast(QString("Applied %1 edited notes — ready to Save")
              .arg(m_freePlayNotes.size()));
}

void MainWindow::importScore() {
  QSettings settings("Voncloft", "VArpeggio");
  const QString start =
      settings
          .value("dialogs/importFolder", QStandardPaths::writableLocation(
                                             QStandardPaths::DocumentsLocation))
          .toString();
  QString path = QFileDialog::getOpenFileName(
      this, "Import sheet music", start,
      "Supported scores (*.mid *.midi *.musicxml *.mxl *.xml *.abc *.json "
      "*.varpeggio *.pdf);;MIDI (*.mid *.midi);;MusicXML (*.musicxml "
      "*.mxl *.xml);;ABC notation (*.abc);;PDF sheet music (*.pdf);;VArpeggio "
      "project (*.json *.varpeggio)");
  if (!path.isEmpty()) {
    settings.setValue("dialogs/importFolder", QFileInfo(path).absolutePath());
    openScore(path);
  }
}
void MainWindow::openScore(const QString &path) {
  const QFileInfo info(path);
  const QString extension = info.suffix().toLower();
  if (extension == "pdf") {
    const QString pdfdetach =
        QStandardPaths::findExecutable(QStringLiteral("pdfdetach"));
    QProcess list;
    if (!pdfdetach.isEmpty()) {
      list.start(pdfdetach, {"-list", path});
      list.waitForFinished(5000);
    }
    const QString listing =
        QString::fromLocal8Bit(list.readAllStandardOutput());
    const auto match =
        QRegularExpression(
            "(?m)^\\d+: "
            "(.+\\.(?:varpeggio|musicxml|mxl|xml|mid|midi))\\s*$",
            QRegularExpression::CaseInsensitiveOption)
            .match(listing);
    if (match.hasMatch()) {
      QTemporaryDir temp;
      const QString embeddedName = match.captured(1).trimmed();
      const QString project = temp.filePath(
          "embedded." + QFileInfo(embeddedName).suffix().toLower());
      QProcess extract;
      extract.start(pdfdetach,
                    {"-savefile", embeddedName, "-o", project, path});
      if (extract.waitForFinished(5000) && extract.exitCode() == 0) {
        QVector<NoteEvent> notes;
        QVector<TimeSignatureEvent> meters;
        QVector<double> repeats;
        QVector<KeySignatureEvent> keys;
        QVector<StaffRestEvent> rests;
        QString title;
        int tempo = m_tempo;
        QString importError;
        if (MusicDocument::importFile(project, notes, title, tempo,
                                      &importError, &meters, &repeats, &keys,
                                      &rests)) {
          setSong(title, notes, tempo);
          const QString embeddedSuffix =
              QFileInfo(embeddedName).suffix().toLower();
          const bool editable = embeddedSuffix == "varpeggio";
          if (editable) {
            setMode(Mode::Free);
            m_title = title;
            m_titleEdit->setText(title);
            m_freePlayNotes = notes;
            m_notes = notes;
            m_freePlayTimeSignatures =
                meters.isEmpty() ? QVector<TimeSignatureEvent>{{0.0, 4, 4}}
                                 : meters;
            m_freePlayKeySignatures =
                keys.isEmpty() ? QVector<KeySignatureEvent>{{0.0, 0}} : keys;
            m_freePlayRepeatEnds = repeats;
            m_freePlayRests = rests;
            m_freePlayApplied = true;
            refreshFreePlayStaff();
          }
          if (m_pdfDocument->load(path) == QPdfDocument::Error::None) {
            setExactScoreAvailable();
            if (editable) {
              m_liveScoreButton->setChecked(true);
              m_scoreStack->setCurrentWidget(m_score);
            }
          }
          if (editable) {
            m_barLabel->setText(
                "FREE PLAY   ·   Editable VArpeggio PDF loaded");
            showNotice(
                "Editable VArpeggio PDF loaded. Click the live staff to place "
                "the orange line, then add, group, delete, or change notes. "
                "Exact PDF keeps the saved page available for reference.");
            toast(QString("Restored %1 editable notes from the PDF")
                      .arg(notes.size()));
          } else
            showNotice("Playable PDF loaded. Choose Exact PDF for the original "
                       "engraving or Live follow to watch the practice score "
                       "move with you.");
          return;
        }
      }
    }

    // A normal engraved PDF has no timing or pitch events. Treat a same-name
    // score file beside it as the playable half while retaining the original
    // PDF as the exact visual score.
    const QString sidecar = matchingScoreSidecar(info);
    if (!sidecar.isEmpty()) {
      QVector<NoteEvent> notes;
      QVector<TimeSignatureEvent> meters;
      QVector<double> repeats;
      QVector<KeySignatureEvent> keys;
      QVector<StaffRestEvent> rests;
      QString title;
      int tempo = m_tempo;
      QString importError;
      if (MusicDocument::importFile(sidecar, notes, title, tempo, &importError,
                                    &meters, &repeats, &keys, &rests) &&
          !notes.isEmpty()) {
        if (title.isEmpty())
          title = info.completeBaseName();
        setSong(title, notes, tempo);
        const QString sidecarSuffix = QFileInfo(sidecar).suffix().toLower();
        const bool editable = sidecarSuffix == "varpeggio";
        if (editable) {
          setMode(Mode::Free);
          m_title = title;
          m_titleEdit->setText(title);
          m_freePlayNotes = notes;
          m_notes = notes;
          m_freePlayTimeSignatures =
              meters.isEmpty() ? QVector<TimeSignatureEvent>{{0.0, 4, 4}}
                               : meters;
          m_freePlayKeySignatures =
              keys.isEmpty() ? QVector<KeySignatureEvent>{{0.0, 0}} : keys;
          m_freePlayRepeatEnds = repeats;
          m_freePlayRests = rests;
          m_freePlayApplied = true;
          refreshFreePlayStaff();
        }
        if (m_pdfDocument->load(path) == QPdfDocument::Error::None) {
          setExactScoreAvailable();
          if (editable) {
            m_liveScoreButton->setChecked(true);
            m_scoreStack->setCurrentWidget(m_score);
          }
        }
        showNotice(editable ? QString("Loaded %1 editable notes from %2 and "
                                      "kept the PDF page under Exact PDF.")
                                  .arg(notes.size())
                                  .arg(QFileInfo(sidecar).fileName())
                            : QString("Loaded %1 playable notes from %2 and "
                                      "kept the original PDF engraving. Choose "
                                      "Exact PDF or Live follow at any time.")
                                  .arg(notes.size())
                                  .arg(QFileInfo(sidecar).fileName()));
        return;
      }
      showPdfRecognitionDialog(
          this, "Matching score could not be read",
          QString("VArpeggio found %1 beside this PDF, but could not read "
                  "playable notes from it.\n\n%2")
              .arg(QFileInfo(sidecar).fileName(), importError));
    }

    setSong(info.completeBaseName(), {}, m_tempo);
    if (m_pdfDocument->load(path) != QPdfDocument::Error::None) {
      QMessageBox::warning(this, "Could not open PDF",
                           "VArpeggio could not read this PDF.");
      return;
    }
    setExactScoreAvailable();

    QString opticalEngine;const bool audiverisReady=AudiverisBridge::available(&opticalEngine);
    QProgressDialog progress(
        audiverisReady
            ? QString("Using the installed %1 engine to read the score and export MusicXML…").arg(opticalEngine)
            : QString("Preparing the open-source Audiveris score reader for first use…"),
        "Cancel", 0, audiverisReady?0:100, this);
    progress.setWindowTitle("Making the PDF playable");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    std::atomic_bool cancelRequested{false};
    connect(&progress, &QProgressDialog::canceled, this,
            [&] { cancelRequested.store(true); });
    QFutureWatcher<PdfRecognitionResult> watcher;
    QEventLoop loop;
    QTimer messageTimer;
    int messageIndex = 0;
    const QStringList messages = audiverisReady
        ? QStringList{"Audiveris is reading the printed notation…",
                      "Audiveris is resolving voices, accidentals, rests, and rhythms…",
                      "Importing the Audiveris MusicXML into VArpeggio…"}
        : QStringList{"Downloading or preparing the open-source Audiveris engine…",
                      "VArpeggio will keep this engine in your user data folder for later PDFs…",
                      "If Audiveris is unavailable, VArpeggio will use its built-in reader…",
                      "VArpeggio is rendering and reading the PDF pages…",
                      "Finding paired treble and bass staves, measures, and noteheads…",
                      "Building editable playable notation from the recognized score…"};
    connect(&watcher, &QFutureWatcher<PdfRecognitionResult>::finished, &loop,
            &QEventLoop::quit);
    connect(&messageTimer, &QTimer::timeout, this, [&] {
      messageIndex = (messageIndex + 1) % messages.size();
      progress.setLabelText(messages[messageIndex]);
    });
    messageTimer.start(4500);
    const QPointer<QProgressDialog> progressGuard(&progress);
    watcher.setFuture(QtConcurrent::run([path, tempo=m_tempo, progressGuard, &cancelRequested] {
      QString setupError;
      const bool useAudiveris=AudiverisBridge::ensureAvailable(
          &setupError,
          [progressGuard](int value,const QString &message){
            if(progressGuard)QMetaObject::invokeMethod(progressGuard,[progressGuard,value,message]{
              if(!progressGuard)return;
              progressGuard->setRange(0,100);
              progressGuard->setValue(value);
              progressGuard->setLabelText(message);
            },Qt::QueuedConnection);
          },
          [&]{return cancelRequested.load();});
      if(cancelRequested.load()){
        PdfRecognitionResult canceled;canceled.canceled=true;return canceled;
      }
      if(useAudiveris){
        if(progressGuard)QMetaObject::invokeMethod(progressGuard,[progressGuard]{
          if(progressGuard){progressGuard->setRange(0,0);progressGuard->setLabelText("Audiveris is reading the printed notation and building MusicXML…");}
        },Qt::QueuedConnection);
        PdfRecognitionResult external=AudiverisBridge::recognize(path,tempo,[&]{return cancelRequested.load();});
        if(external.canceled||!external.notes.isEmpty())return external;
      }
      if(progressGuard)QMetaObject::invokeMethod(progressGuard,[progressGuard]{
        if(progressGuard){progressGuard->setRange(0,100);progressGuard->setValue(0);progressGuard->setLabelText("Using VArpeggio's built-in PDF reader…");}
      },Qt::QueuedConnection);
      PdfRecognitionResult native=PdfRecognizer::recognize(path, tempo, [progressGuard](int value) {
        if(progressGuard)QMetaObject::invokeMethod(progressGuard,[progressGuard,value]{if(progressGuard)progressGuard->setValue(value);},Qt::QueuedConnection);
      }, [&] { return cancelRequested.load(); });
      if(native.notes.isEmpty()&&!setupError.isEmpty())native.error=setupError+"\n\n"+native.error;
      return native;
    }));
    loop.exec();
    messageTimer.stop();
    progress.close();
    const PdfRecognitionResult recognized = watcher.result();
    if (recognized.canceled) {
      showNotice("Optical recognition was canceled. The original PDF is still "
                 "open for viewing.",
                 true);
      return;
    }
    if (recognized.notes.isEmpty()) {
      showNotice("The original PDF is open, but optical recognition did not "
                 "produce playable notes.",
                 true);
      showPdfRecognitionDialog(
          this, "This PDF could not be made playable",
          recognized.error +
              "\n\nThe original PDF is still available in Exact PDF view. For "
              "the most reliable result, export MusicXML from a notation "
              "program, save it beside this PDF with the same name, and reopen "
              "it. Native recognition currently targets clean engraved piano "
              "scores rather than handwriting or orchestral pages.");
      return;
    }

    const QString recognizedTitle = info.completeBaseName();
    setSong(recognizedTitle, recognized.notes, recognized.tempo);
    m_score->setTimeSignatures(recognized.timeSignatures);
    m_score->setKeySignatures(recognized.keySignatures);
    if (m_pdfDocument->load(path) == QPdfDocument::Error::None)
      setExactScoreAvailable();
    showNotice(
        QString("%1 recognized %2 playable notes. The "
                "original PDF remains under Exact PDF; Live follow, Listen, "
                "and Wait mode use the recognition draft. Check difficult "
                "rhythms and accidentals before relying on it. Choose "
                "MusicXML in Save to keep the transcription as XML.")
            .arg(recognized.engine)
            .arg(recognized.notes.size()));
    return;
  }

  QVector<NoteEvent> notes;
  QVector<TimeSignatureEvent> meters;
  QVector<double> repeats;
  QVector<KeySignatureEvent> keys;
  QVector<StaffRestEvent> rests;
  QString title;
  int tempo = m_tempo;
  QString error;
  if (!MusicDocument::importFile(path, notes, title, tempo, &error, &meters,
                                 &repeats, &keys, &rests)) {
    QMessageBox::warning(this, "Could not import score", error);
    return;
  }
  setSong(title, notes, tempo);
  if (extension == "json" || extension == "varpeggio") {
    setMode(Mode::Free);
    m_title = title;
    m_titleEdit->setText(title);
    m_freePlayNotes = notes;
    m_notes = notes;
    m_freePlayTimeSignatures =
        meters.isEmpty() ? QVector<TimeSignatureEvent>{{0.0, 4, 4}} : meters;
    m_freePlayKeySignatures =
        keys.isEmpty() ? QVector<KeySignatureEvent>{{0.0, 0}} : keys;
    m_freePlayRepeatEnds = repeats;
    m_freePlayRests = rests;
    m_freePlayApplied = true;
    refreshFreePlayStaff();
    m_barLabel->setText("FREE PLAY   ·   Editable project loaded");
    toast(
        QString(
            "Loaded %1 editable notes, %2 rests, and the saved notation marks")
            .arg(notes.size())
            .arg(rests.size()));
    return;
  }

  // MusicXML and its same-named PDF are two halves of one score: XML drives
  // playback/practice while the PDF preserves the exact engraved notation.
  if (extension == "musicxml" || extension == "mxl" || extension == "xml") {
    const QString sidecarPdf =
        info.dir().filePath(info.completeBaseName() + ".pdf");
    if (QFileInfo::exists(sidecarPdf) &&
        m_pdfDocument->load(sidecarPdf) == QPdfDocument::Error::None) {
      setExactScoreAvailable();
      showNotice(
          QString(
              "Loaded %1 playable notes at %2 BPM. Choose Exact PDF for the "
              "original engraving or Live follow to watch the score advance.")
              .arg(notes.size())
              .arg(tempo));
      return;
    }
  }
  toast(QString("Imported %1 notes from %2")
            .arg(notes.size())
            .arg(extension.toUpper()));
}
void MainWindow::exportScore(){const auto &notes=m_mode==Mode::Free?m_freePlayNotes:m_notes;if(notes.isEmpty()&&!(m_mode==Mode::Free&&!m_freePlayRests.isEmpty())){QMessageBox::information(this,"Nothing to save",m_mode==Mode::Free?"The Free Play grand staff is empty. Play a note or insert a rest first.":"Play a recording or import a note-aware score first.");return;}const QString exportTitle=m_mode==Mode::Free&&!m_freePlayApplied?"Free Play score":m_title;QString type=m_exportCombo->currentData().toString();QMap<QString,QString>ext{{"mid","mid"},{"musicxml","musicxml"},{"abc","abc"},{"wav","wav"},{"video","mp4"},{"svg","svg"},{"png","png"},{"pdf","pdf"},{"json","varpeggio"},{"csv","csv"}};QSettings settings("Voncloft","VArpeggio");const QString start=settings.value("dialogs/exportFolder",QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString()+"/"+safeName(exportTitle)+"."+ext[type];QString path=QFileDialog::getSaveFileName(this,"Save "+m_exportCombo->currentText(),start);if(path.isEmpty())return;if(QFileInfo(path).suffix().isEmpty())path+="."+ext[type];settings.setValue("dialogs/exportFolder",QFileInfo(path).absolutePath());exportTo(type,path);}
void MainWindow::exportTo(const QString &type,const QString &path){
    const auto &notes=m_mode==Mode::Free?m_freePlayNotes:m_notes;const QString title=m_mode==Mode::Free&&!m_freePlayApplied?"Free Play score":m_title;
    const QVector<TimeSignatureEvent> meters=m_mode==Mode::Free?m_freePlayTimeSignatures:QVector<TimeSignatureEvent>{};const QVector<double> repeats=m_mode==Mode::Free?m_freePlayRepeatEnds:QVector<double>{};const QVector<KeySignatureEvent> keys=m_mode==Mode::Free?m_freePlayKeySignatures:QVector<KeySignatureEvent>{};const QVector<StaffRestEvent> rests=m_mode==Mode::Free?m_freePlayRests:QVector<StaffRestEvent>{};
    QString error;bool ok=false;
    if(type=="mid")ok=MusicDocument::saveMidi(path,notes,m_tempo,&error);
    else if(type=="musicxml")ok=MusicDocument::saveEngravedScore(path,{},notes,m_tempo,title,&error,meters,repeats,keys,rests);
    else if(type=="abc")ok=MusicDocument::saveAbc(path,notes,m_tempo,title,&error);
    else if(type=="wav")ok=Synthesizer::saveWav(path,notes,m_tempo,&error,SynthInstrument(m_instrumentCombo->currentData().toInt()));
    else if(type=="video")ok=saveFallingNoteVideo(path,notes,m_tempo,title,SynthInstrument(m_instrumentCombo->currentData().toInt()),this,&error);
    else if(type=="json")ok=MusicDocument::saveJson(path,notes,m_tempo,title,&error,meters,repeats,keys,rests);
    else if(type=="csv")ok=MusicDocument::saveCsv(path,notes,&error);
    else if(type=="png"){QImage image(2200,500,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::white);QPainter p(&image);m_score->paintScore(p,image.rect());p.end();ok=image.save(path);if(!ok)error="Could not encode the PNG image.";}
    else if(type=="svg"){QSvgGenerator gen;gen.setFileName(path);gen.setSize({2200,500});gen.setViewBox(QRect(0,0,2200,500));gen.setTitle(title);QPainter p(&gen);m_score->paintScore(p,QRectF(0,0,2200,500));p.end();ok=true;}
    else if(type=="pdf"){QTemporaryDir temp;if(temp.isValid())ok=MusicDocument::saveEngravedScore(temp.filePath("score.musicxml"),path,notes,m_tempo,title,&error,meters,repeats,keys,rests);else error="Could not create the score workspace.";}
    if(ok)toast("Saved "+QFileInfo(path).fileName());else QMessageBox::warning(this,"Could not save",error);
}

void MainWindow::openDetectorView(){
    if(m_importedMediaPath.isEmpty()||m_videoDetectionInfo.laneCenters.isEmpty()){
        QMessageBox::information(this,"Detector view unavailable","Import a falling-note video with Highlighted keyboard or Automatic mode first.");return;
    }
    stopPlayback(false);
    const QVector<NoteEvent> directorNotes=m_videoDetectionInfo.directorNotes.isEmpty()?m_notes:m_videoDetectionInfo.directorNotes;
    const double directorStart=m_videoDetectionInfo.directorNotes.isEmpty()?m_importedMediaStartMs/1000.0:m_videoDetectionInfo.directorStartSeconds;
    QDialog dialog(this);dialog.setWindowTitle("VArpeggio Director — source video + accepted bars");dialog.resize(1180,780);
    dialog.setStyleSheet("QDialog{background:#edf2ef;color:#102f29} QLabel,QCheckBox{color:#102f29;font-size:12px} QPushButton{color:#102f29;background:#ffffff;border:2px solid #6b8b83;border-radius:8px;padding:8px 14px;font-size:13px;font-weight:700;min-height:22px} QPushButton:hover{color:#ffffff;background:#256b61;border-color:#256b61} QComboBox{color:#102f29;background:#ffffff;border:2px solid #6b8b83;border-radius:7px;padding:7px 24px 7px 10px;font-size:13px;font-weight:700;min-width:72px} QComboBox QAbstractItemView{color:#102f29;background:#ffffff;selection-color:#ffffff;selection-background-color:#256b61} QSlider::groove:horizontal{height:8px;background:#bdcbc7;border-radius:4px} QSlider::handle:horizontal{width:20px;margin:-6px 0;background:#256b61;border:2px solid #ffffff;border-radius:10px}");
    auto *layout=new QVBoxLayout(&dialog);layout->setContentsMargins(10,10,10,10);layout->setSpacing(8);
    auto *viewer=new DetectorOverlayWidget(directorNotes,m_tempo,directorStart,m_videoDetectionInfo,&dialog);layout->addWidget(viewer,1);
    auto *controls=new QHBoxLayout;controls->setSpacing(9);auto *play=button("▶  Play");auto *back=button("◀  5 seconds");auto *forward=button("5 seconds  ▶");auto *saveVideo=button("Save detector video…");
    play->setMinimumWidth(105);back->setMinimumWidth(130);forward->setMinimumWidth(130);saveVideo->setMinimumWidth(175);
    auto *position=new QSlider(Qt::Horizontal);position->setRange(0,1);position->setTracking(true);
    auto *clock=label("00:00.00 / 00:00.00");clock->setMinimumWidth(170);clock->setStyleSheet("color:#102f29;font-family:'Noto Sans Mono';font-size:12px;font-weight:700");
    auto *sound=new QCheckBox("Hear source");sound->setChecked(false);
    auto *speed=new QComboBox;speed->addItem("0.25×",.25);speed->addItem("0.5×",.5);speed->addItem("1×",1.0);speed->setCurrentIndex(2);
    controls->addWidget(play);controls->addWidget(back);controls->addWidget(forward);controls->addWidget(position,1);controls->addWidget(clock);controls->addWidget(sound);controls->addWidget(speed);controls->addWidget(saveVideo);layout->addLayout(controls);
    auto *hint=label("Scrub anywhere. The translucent outlines are exactly the notes and held lengths VArpeggio accepted; the untouched source video remains visible underneath.");hint->setWordWrap(true);hint->setStyleSheet("color:#294b44;background:#ffffff;border:1px solid #aebfba;border-radius:6px;padding:7px 9px;font-size:12px");layout->addWidget(hint);

    QMediaPlayer player;QAudioOutput audio;QVideoSink sink;
    audio.setMuted(true);audio.setVolume(1.0);player.setAudioOutput(&audio);player.setVideoOutput(&sink);player.setSource(QUrl::fromLocalFile(m_importedMediaPath));
    connect(&sink,&QVideoSink::videoFrameChanged,viewer,[viewer](const QVideoFrame &frame){viewer->setFrame(frame.toImage());});
    connect(&player,&QMediaPlayer::durationChanged,&dialog,[position](qint64 duration){position->setRange(0,int(qMin<qint64>(duration,std::numeric_limits<int>::max())));});
    connect(&player,&QMediaPlayer::positionChanged,&dialog,[position,viewer,clock,&player](qint64 value){
        if(!position->isSliderDown())position->setValue(int(value));viewer->setPosition(value);
        auto stamp=[](qint64 ms){const qint64 cs=ms/10;return QString("%1:%2.%3").arg(cs/6000,2,10,QChar('0')).arg((cs/100)%60,2,10,QChar('0')).arg(cs%100,2,10,QChar('0'));};
        clock->setText(stamp(value)+" / "+stamp(player.duration()));
    });
    connect(&player,&QMediaPlayer::playbackStateChanged,&dialog,[play](QMediaPlayer::PlaybackState state){play->setText(state==QMediaPlayer::PlayingState?"Ⅱ  Pause":"▶  Play");});
    connect(play,&QPushButton::clicked,&dialog,[&player]{player.playbackState()==QMediaPlayer::PlayingState?player.pause():player.play();});
    connect(back,&QPushButton::clicked,&dialog,[&player]{player.setPosition(qMax<qint64>(0,player.position()-5000));});
    connect(forward,&QPushButton::clicked,&dialog,[&player]{player.setPosition(qMin(player.duration(),player.position()+5000));});
    connect(position,&QSlider::sliderMoved,&player,[&player](int value){player.setPosition(value);});
    connect(sound,&QCheckBox::toggled,&dialog,[&audio](bool checked){audio.setMuted(!checked);});
    connect(speed,&QComboBox::currentIndexChanged,&dialog,[speed,&player](int){player.setPlaybackRate(speed->currentData().toDouble());});
    connect(saveVideo,&QPushButton::clicked,&dialog,[&,this]{
        player.pause();QSettings settings("Voncloft","VArpeggio");
        const QString folder=settings.value("dialogs/detectorExportFolder",QFileInfo(m_importedMediaPath).absolutePath()).toString();
        QString suggested=folder+"/"+safeName(QFileInfo(m_importedMediaPath).completeBaseName())+"-varpeggio-detector.mp4";
        QString output=QFileDialog::getSaveFileName(&dialog,"Save detector video",suggested,"MP4 video (*.mp4)");if(output.isEmpty())return;
        if(QFileInfo(output).suffix().isEmpty())output+=".mp4";settings.setValue("dialogs/detectorExportFolder",QFileInfo(output).absolutePath());
        QString exportError;if(saveDetectorVideo(output,m_importedMediaPath,directorNotes,m_tempo,directorStart,m_videoDetectionInfo,&dialog,&exportError))
            QMessageBox::information(&dialog,"Detector video saved","Saved the original video and audio with VArpeggio's accepted-note outlines to:\n\n"+output);
        else QMessageBox::warning(&dialog,"Could not save detector video",exportError);
    });
    player.play();dialog.exec();player.stop();
}

void MainWindow::openVideoLab(){
    QSettings settings("Voncloft","VArpeggio");
    const QString startFolder=settings.value("dialogs/mediaFolder",QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)).toString();
    const QString path=QFileDialog::getOpenFileName(
        this,"Video/audio to notes",startFolder,
        "Audio and video (*.wav *.mp3 *.m4a *.ogg *.flac *.mp4 *.mkv *.webm *.mov);;All files (*)");
    if(path.isEmpty())return;
    settings.setValue("dialogs/mediaFolder",QFileInfo(path).absolutePath());

    const QString ext=QFileInfo(path).suffix().toLower();
    const bool isVideo=QStringList{"mp4","mkv","webm","mov","avi","m4v"}.contains(ext);
    TranscriptionMode transcriptionMode=TranscriptionMode::AudioOnly;
    if(isVideo){
        QDialog choice(this);choice.setWindowTitle("Video to sheet music");choice.setMinimumWidth(720);
        auto*layout=new QVBoxLayout(&choice);layout->setContentsMargins(22,20,22,18);layout->setSpacing(10);
        auto*heading=label("How should VArpeggio read this video?");heading->setStyleSheet("font-size:20px;font-weight:800");layout->addWidget(heading);
        auto*explanation=label("VArpeggio checks the complete video, measures however many piano keys are actually visible at the bottom, and tracks which key changes or lights up. Automatic mode also compares visual drafts with the soundtrack and can use sound when the keyboard is hidden. Choose how it should find the notes:");explanation->setWordWrap(true);explanation->setStyleSheet("color:#5f716c;margin-bottom:6px");layout->addWidget(explanation);
        auto*sourceCheck=label("SOURCE CHECK ON  ·  VArpeggio renders the detected staff notes, compares that generated piano playback with the original soundtrack by octave and timing, and keeps the best visual draft.");sourceCheck->setWordWrap(true);sourceCheck->setStyleSheet("color:#205e54;background:#e9f5f1;border:1px solid #9fc9be;border-radius:8px;padding:9px 11px;font-weight:700");layout->addWidget(sourceCheck);
        auto makeChoice=[&](const QString &text){auto*b=button(text,"conversionChoice");b->setMinimumHeight(66);b->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);b->setStyleSheet("QPushButton{text-align:left;background:#fffefa;border:1px solid #c8c9c2;border-radius:9px;padding:10px 14px;font-weight:700}QPushButton:hover{color:white;background:#2d7d70;border-color:#2d7d70}");layout->addWidget(b);return b;};
        auto*automaticButton=makeChoice("Automatic (recommended)\nStrip highlighted keys, render those staff notes, source-check them, and use audio only if the keyboard is unreadable");
        auto*visualButton=makeChoice("Highlighted keyboard only\nMap every changed key at the bottom, regardless of keyboard size or highlight color");
        auto*audioButton=makeChoice("Soundtrack only\nEstimate piano notes from audio when the keyboard is hidden");
        auto*buttons=new QDialogButtonBox(QDialogButtonBox::Cancel);layout->addWidget(buttons);
        bool selected=false;
        connect(automaticButton,&QPushButton::clicked,&choice,[&]{transcriptionMode=TranscriptionMode::Automatic;selected=true;choice.accept();});
        connect(visualButton,&QPushButton::clicked,&choice,[&]{transcriptionMode=TranscriptionMode::VisualOnly;selected=true;choice.accept();});
        connect(audioButton,&QPushButton::clicked,&choice,[&]{transcriptionMode=TranscriptionMode::AudioOnly;selected=true;choice.accept();});
        connect(buttons,&QDialogButtonBox::rejected,&choice,&QDialog::reject);
        choice.exec();if(!selected)return;
    }else{
        const auto answer=QMessageBox::information(
            this,"Audio to sheet music",
            "VArpeggio will analyze the complete recording and create a draft piano score, including clean chords when they can be separated. Longer files take longer. Solo piano works best; vocals, drums, effects, and orchestral layers reduce accuracy.",
            QMessageBox::Ok|QMessageBox::Cancel);
        if(answer!=QMessageBox::Ok)return;
    }

    transcribeMediaPath(path,transcriptionMode,0);
}

bool MainWindow::transcribeVideoPath(const QString &path,int tempo){return transcribeMediaPath(path,TranscriptionMode::Automatic,tempo>0?qBound(30,tempo,260):0);}

bool MainWindow::transcribeMediaPath(const QString &path,TranscriptionMode mode,int transcriptionTempo){
    stopPlayback(false);if(m_recording)finishRecording();

    const bool visualFirst=mode!=TranscriptionMode::AudioOnly;
    const bool automatic=mode==TranscriptionMode::Automatic;
    QProgressDialog progress(visualFirst?"Checking the complete video, measuring the visible keyboard, and tracking complete falling notes…":"Analyzing the complete recording for tempo and piano-like pitches…","Cancel",0,100,this);
    progress.setWindowModality(Qt::WindowModal);progress.setMinimumDuration(0);progress.setAutoClose(false);
    QVector<NoteEvent>notes;QString error;TranscriptionInfo transcriptionInfo;
    QString visualError,audioError;std::atomic_int analysisProgress{0},analysisStage{visualFirst?0:1};std::atomic_bool cancelRequested{false},usedAudioFallback{false};
    connect(&progress,&QProgressDialog::canceled,this,[&]{cancelRequested.store(true);});
    QFutureWatcher<bool> analysisWatcher;QEventLoop analysisLoop;QTimer progressTimer;
    connect(&analysisWatcher,&QFutureWatcher<bool>::finished,&analysisLoop,&QEventLoop::quit);
    int displayedStage=analysisStage.load();
    connect(&progressTimer,&QTimer::timeout,this,[&]{const int stage=analysisStage.load();if(stage!=displayedStage){displayedStage=stage;progress.setLabelText("The keyboard picture was not reliable enough — analyzing the complete soundtrack instead…");}const int value=analysisProgress.load();progress.setValue(automatic?(stage==0?qMin(52,value*52/100):qMin(86,52+value*34/100)):qMin(86,value*86/100));});
    progressTimer.start(40);
    analysisWatcher.setFuture(QtConcurrent::run([&]{
        auto report=[&](int value){analysisProgress.store(value);};
        auto canceled=[&]{return cancelRequested.load();};
        if(visualFirst){
            if(MusicDocument::transcribeFallingNotesVideo(path,transcriptionTempo,notes,&visualError,report,canceled,&transcriptionInfo))return true;
            if(!automatic||canceled()){error=visualError;return false;}
            notes.clear();transcriptionInfo=TranscriptionInfo{};analysisProgress.store(0);analysisStage.store(1);usedAudioFallback.store(true);
        }
        const bool audioOk=MusicDocument::transcribeMedia(path,transcriptionTempo,notes,&audioError,report,canceled,&transcriptionInfo);
        if(!audioOk)error=automatic?QString("The picture could not be read: %1\n\nThe sound could not be converted: %2").arg(visualError,audioError):audioError;
        return audioOk;
    }));
    analysisLoop.exec();progressTimer.stop();
    const bool ok=analysisWatcher.result();
    if(cancelRequested.load())return false;
    if(!ok){QMessageBox::warning(this,"Transcription did not finish",error);return false;}

    const bool usedAudio=mode==TranscriptionMode::AudioOnly||usedAudioFallback.load();
    const int resolvedTempo=transcriptionInfo.tempo>0?transcriptionInfo.tempo:(transcriptionTempo>0?transcriptionTempo:96);
    const QString title=QFileInfo(path).completeBaseName()+" — "+(usedAudio?"sound transcription":"falling-note transcription");
    setSong(title,notes,resolvedTempo);
    m_videoDetectionInfo=transcriptionInfo;
    setImportedMediaSource(path,transcriptionInfo.sourceStartSeconds);
    if(!usedAudio&&transcriptionInfo.refinementPasses>0){
        const QString match=transcriptionInfo.renderedAgreement>=.68?"strong":transcriptionInfo.renderedAgreement>=.45?"partial":"weak";
        m_statusLabel->setText("Source check: "+match);
    }else m_statusLabel->setText("Audio-derived draft");

    // Generate the same detailed MusicXML + engraved, playable PDF workflow used
    // for the earlier HOI4 transcription, then retain it for the score viewer.
    m_generatedScoreDir=std::make_unique<QTemporaryDir>();
    QString engravingError;
    bool engraved=false;
    progress.setLabelText("Engraving the detected notes into a real piano score…");progress.setRange(0,0);QApplication::processEvents();
    if(m_generatedScoreDir->isValid()){
        const QString xmlPath=m_generatedScoreDir->filePath("transcription.musicxml");
        const QString pdfPath=m_generatedScoreDir->filePath("transcription.pdf");
        QFutureWatcher<bool> engravingWatcher;QEventLoop engravingLoop;
        connect(&engravingWatcher,&QFutureWatcher<bool>::finished,&engravingLoop,&QEventLoop::quit);
        engravingWatcher.setFuture(QtConcurrent::run([&]{return MusicDocument::saveEngravedScore(xmlPath,pdfPath,notes,resolvedTempo,title,&engravingError);}));
        engravingLoop.exec();engraved=engravingWatcher.result();
        if(engraved&&m_pdfDocument->load(pdfPath)==QPdfDocument::Error::None){
            setExactScoreAvailable("Engraved score");
        }else engraved=false;
    }else engravingError="Could not create the temporary score workspace.";

    if(engraved){
        progress.setRange(0,100);progress.setValue(100);
        const QString sourceMessage=usedAudio
            ?(usedAudioFallback.load()?"The keyboard picture was not reliable enough, so VArpeggio analyzed the complete soundtrack.":"VArpeggio analyzed the complete recording into a draft piano score.")
            :"VArpeggio read the falling notes from the complete video.";
        QString tempoMessage;
        if(transcriptionInfo.tempoEstimated){const QString confidence=transcriptionInfo.tempoConfidence>=.65?"high":transcriptionInfo.tempoConfidence>=.35?"medium":"low";tempoMessage=QString(" VArpeggio estimated %1 BPM from the timing (%2 confidence).").arg(resolvedTempo).arg(confidence);}
        else if(transcriptionTempo<=0)tempoMessage=QString(" The tempo was unclear, so VArpeggio used %1 BPM; adjust it with −/+ if needed.").arg(resolvedTempo);
        else tempoMessage=QString(" Using the requested %1 BPM.").arg(resolvedTempo);
        QString verification;
        if(!usedAudio&&transcriptionInfo.refinementPasses>0){
            const QString pitchMatch=transcriptionInfo.renderedAgreement>=.68?"strong":transcriptionInfo.renderedAgreement>=.45?"partial":"weak";
            verification=QString(" VArpeggio rendered %1 visual %2 from tracked falling bars, highlighted keys, and scan-line fallbacks, compared each draft's generated pitches and timing with the original soundtrack, and kept the best match (%3 pitch match).")
                .arg(transcriptionInfo.refinementPasses)
                .arg(transcriptionInfo.refinementPasses==1?"draft":"drafts")
                .arg(pitchMatch);
            if(transcriptionInfo.audiovisualAgreement>=0&&transcriptionInfo.audiovisualAgreement<.15)verification+=" Its timing match was weak, so check the result and adjust BPM with −/+ if it drifts.";
        }
        const QString caution=usedAudio?" Sound-based scores may need corrections when other instruments are present.":QString();
        showNotice(QString("%1%2%3 Created %4 notes. Listen plays the generated notes shown in Live follow. Hear original recording is an optional reference only and stays off unless you turn it on. Use Exact PDF for the engraving or Live follow to watch grouped chords, beams, and held-note ties advance.%5").arg(sourceMessage,tempoMessage,verification).arg(notes.size()).arg(caution));
    }else{
        showNotice(QString("Created %1 playable notes, but the engraved PDF preview could not be made: %2 The notes are still available for Listen, Wait mode, and export.").arg(notes.size()).arg(engravingError),true);
    }
    return true;
}
void MainWindow::openRoutineDialog(){
    const QDate today=QDate::currentDate();QDate viewedDate=today;int viewedVariation=dailyRoutineTheme(viewedDate).chordOrder;
    QDialog d(this);d.setWindowTitle("Daily practice plan");d.setMinimumWidth(620);d.setModal(true);
    auto*l=new QVBoxLayout(&d);l->setContentsMargins(24,22,24,20);l->setSpacing(12);
    auto*dateNav=new QHBoxLayout;auto*previousDay=button("← Previous day","routineDialogSecondary");auto*todayButton=button("Today","routineDialogSecondary");auto*nextDay=button("Next day →","routineDialogSecondary");auto*head=label("","routineDialogTitle");head->setAlignment(Qt::AlignCenter);head->setStyleSheet("font-size:22px;font-weight:800");dateNav->addWidget(previousDay);dateNav->addWidget(todayButton);dateNav->addWidget(head,1);dateNav->addWidget(nextDay);l->addLayout(dateNav);
    auto*intro=label("Start once, then use Previous and Next above the score. VArpeggio carries you through both hands and the full piece without reopening this window.");intro->setWordWrap(true);intro->setStyleSheet("color:#526862;margin-bottom:4px");l->addWidget(intro);
    QVector<QPair<QLabel*,QLabel*>> planRows;auto addPlanRow=[&](const QString &number){auto*row=new QFrame;row->setStyleSheet("QFrame{background:#fffefa;border:1px solid #dedbd1;border-radius:9px}");auto*rl=new QHBoxLayout(row);rl->setContentsMargins(13,10,13,10);auto*n=label(number);n->setStyleSheet("color:#ef775f;font-weight:800;font-size:16px;border:0");rl->addWidget(n);auto*copy=new QVBoxLayout;copy->setSpacing(1);auto*t=label("");t->setStyleSheet("font-weight:800;border:0");auto*sub=label("");sub->setStyleSheet("color:#71807c;border:0");copy->addWidget(t);copy->addWidget(sub);rl->addLayout(copy,1);l->addWidget(row);planRows.append(qMakePair(t,sub));};for(const QString &number:{QStringLiteral("01"),QStringLiteral("02"),QStringLiteral("03"),QStringLiteral("04")})addPlanRow(number);
    auto*variation=label("");variation->setStyleSheet("color:#71807c");
    auto*buttons=new QHBoxLayout;auto*shuffle=button("Change chord order","routineDialogSecondary");auto*cancel=button("Not now","routineDialogSecondary");auto*start=button("Start the full plan  →","routineDialogPrimary");start->setDefault(true);for(auto*b:{shuffle,cancel,start}){b->setMinimumHeight(42);b->setMinimumWidth(132);}buttons->addWidget(variation);buttons->addWidget(shuffle);buttons->addStretch();buttons->addWidget(cancel);buttons->addWidget(start);l->addLayout(buttons);
    const QString secondaryStyle="QPushButton{color:#244d45;background:#fffefa;border:1px solid #aebdb8;border-radius:8px;padding:8px 13px;font-weight:700}QPushButton:hover{background:#eef7f4;border-color:#2d7d70}";
    for(auto*b:{previousDay,todayButton,nextDay,shuffle,cancel})b->setStyleSheet(secondaryStyle);start->setStyleSheet("QPushButton{color:white;background:#2d7d70;border:1px solid #205e54;border-radius:8px;padding:8px 16px;font-weight:800}QPushButton:hover{background:#235f56}");
    const auto refresh=[&]{const auto theme=dailyRoutineTheme(viewedDate);head->setText(viewedDate.toString("ddd, MMM d, yyyy"));todayButton->setEnabled(viewedDate!=today);planRows[0].first->setText(theme.keyName+" warm-up · 3 min");planRows[0].second->setText("Right hand → left hand → together");planRows[1].first->setText(theme.keyName+QString(" chord drill · pattern %1 · 4 min").arg(viewedVariation+1));planRows[1].second->setText("Wait mode checks every note in each chord");planRows[2].first->setText(earFocusName(theme.earFocus)+" · 2 min");planRows[2].second->setText("The prompts are hidden until you find them");const QString song=theme.useOde?"Ode to Joy":"Morning Light";planRows[3].first->setText(song+" in "+theme.keyName+" · 5 min");planRows[3].second->setText("Right hand → left hand → together");variation->setText(QString("Chord pattern %1 of 3").arg(viewedVariation+1));start->setText(viewedDate==today?"Start today's plan  →":"Start this plan  →");};
    connect(previousDay,&QPushButton::clicked,&d,[&]{viewedDate=viewedDate.addDays(-1);viewedVariation=dailyRoutineTheme(viewedDate).chordOrder;refresh();});connect(nextDay,&QPushButton::clicked,&d,[&]{viewedDate=viewedDate.addDays(1);viewedVariation=dailyRoutineTheme(viewedDate).chordOrder;refresh();});connect(todayButton,&QPushButton::clicked,&d,[&]{viewedDate=today;viewedVariation=dailyRoutineTheme(viewedDate).chordOrder;refresh();});connect(shuffle,&QPushButton::clicked,&d,[&]{viewedVariation=(viewedVariation+1)%3;refresh();});
    connect(cancel,&QPushButton::clicked,&d,&QDialog::reject);connect(start,&QPushButton::clicked,&d,[&]{m_routinePlanDate=viewedDate;m_routineVariation=viewedVariation;m_routineCompletedSteps.clear();d.accept();loadRoutineStep(0);});refresh();d.exec();
}

void MainWindow::loadRoutineStep(int step){
    if(!m_routinePlanDate.isValid())m_routinePlanDate=QDate::currentDate();const auto theme=dailyRoutineTheme(m_routinePlanDate);
    const QString routineSongName=theme.useOde?"Ode to Joy":"Morning Light";
    struct Stage{QString title,detail,type;int hand,tempo;};
    const QVector<Stage> stages{
        {"Warm-up — right hand",theme.keyName+" contrary motion · right hand only","scale",1,72},
        {"Warm-up — left hand",theme.keyName+" contrary motion · left hand only","scale",0,72},
        {"Warm-up — together",theme.keyName+" contrary motion · both hands","scale",-1,72},
        {"Chord drill — together",theme.keyName+QString(" · chord order %1 · complete every chord").arg(m_routineVariation+1),"chords",-1,68},
        {"Ear training","Hear a hidden note or chord, then find it","ear",-1,84},
        {routineSongName+" — right hand",theme.keyName+" · learn the melody on its own","song",1,84},
        {routineSongName+" — left hand",theme.keyName+" · learn the bass part on its own","song",0,84},
        {routineSongName+" — together",theme.keyName+" · finish with the complete piece","song",-1,84}
    };
    step=qBound(0,step,stages.size()-1);const auto &stage=stages[step];m_routineStep=step;
    m_earTraining=false;m_synth.stopSequence();setMode(Mode::Wait);
    if(stage.type=="scale")setSong("Daily "+theme.keyName+" warm-up",transposed(MusicDocument::scaleRoutine(),theme.transpose),stage.tempo);
    else if(stage.type=="chords")setSong("Daily "+theme.keyName+" chord drill",transposed(MusicDocument::chordRoutine(m_routineVariation),theme.transpose),stage.tempo);
    else if(stage.type=="ear")setSong("Ear training — find the sound",{},stage.tempo);
    else {const QString songName=theme.useOde?"Ode to Joy":"Morning Light";const auto song=theme.useOde?MusicDocument::odeToJoy():MusicDocument::morningLight();setSong(songName+" — "+theme.keyName,transposed(song,theme.transpose),stage.tempo);}
    if(auto*b=m_handButtons->button(stage.hand<0?2:stage.hand))b->setChecked(true);setHandFilter(stage.hand);
    m_routineSession->show();m_routineStepLabel->setText(QString("DAILY PLAN · STEP %1 OF %2%3").arg(step+1).arg(stages.size()).arg(m_routineCompletedSteps.contains(step)?" · COMPLETE ✓":""));m_routineDetailLabel->setText(stage.title+"  ·  "+stage.detail);m_routinePreviousButton->setEnabled(step>0);m_routineNextButton->setText(step==stages.size()-1?"Finish plan  ✓":"Next step  →");
    if(stage.type=="ear"){m_earTraining=true;m_earTrainingRound=0;m_earTrainingSolved=0;m_attempts=m_correct=m_streak=m_bestStreak=0;updateStats();startEarTrainingRound();}
    QSettings s("Voncloft","VArpeggio");s.setValue("lastRoutineDate",m_routinePlanDate);s.setValue("routineVariation",m_routineVariation);s.setValue("routineStep",step);
    toast(QString("Practice plan step %1: %2").arg(step+1).arg(stage.title));
}

void MainWindow::finishRoutinePlan(){
    if(m_routineStep<0)return;m_earTraining=false;m_synth.stopSequence();stopPlayback(true);m_routineStep=-1;m_routinePlanDate=QDate();m_routineSession->hide();if(auto*b=m_handButtons->button(2))b->setChecked(true);setHandFilter(-1);showNotice("Daily practice plan complete. You practiced technique, chords, ear training, each hand separately, and both hands together.");toast("Practice plan complete");
}

void MainWindow::playEarTrainingPrompt(){
    if(!m_earTraining||m_earPrompt.isEmpty())return;m_synth.stopSequence();m_synth.startSequence(m_earPrompt,m_tempo);toast(m_earPrompt.size()>1?"Listen for the chord, then find all of its notes":"Listen for the note, then find it");
}

void MainWindow::startEarTrainingRound(){
    if(!m_earTraining)return;++m_earTrainingRound;m_matched.clear();m_earExpected.clear();m_earPrompt.clear();
    // Keep the first ear-training lesson inside C4–C5 so every answer can be
    // entered with the documented A–K computer-key row as well as MIDI or the
    // onscreen 88-key piano.  Chord roots stop at F4 so the fifth also fits.
    const int focus=dailyRoutineTheme(m_routinePlanDate.isValid()?m_routinePlanDate:QDate::currentDate()).earFocus;
    const bool chord=focus==1||(focus==3&&m_earTrainingRound%3==0);const bool interval=focus==2;
    const int root=(chord||interval)?60+QRandomGenerator::global()->bounded(6):60+QRandomGenerator::global()->bounded(13);
    QVector<int> pitches{root};if(chord){const bool minor=QRandomGenerator::global()->bounded(2);pitches<<root+(minor?3:4)<<root+7;}else if(interval)pitches<<root+3+QRandomGenerator::global()->bounded(5);
    for(int pitch:pitches){m_earExpected.insert(pitch);m_earPrompt.append({pitch,0,1.15,92,pitch<60?Hand::Left:Hand::Right});}
    m_score->setNotes({});m_score->setExpected({},0);m_score->setFollowBeat(0);m_score->setPlayedBeat(-1);m_bubble->setNotes({});m_bubble->setExpected({},0);m_bubble->setPlayhead(0);
    const qreal promptSize=qMax<qreal>(8.0,(QApplication::font().pointSizeF()>0?QApplication::font().pointSizeF():10.0)-6.0);const QString promptType=chord?"chord":interval?"interval":"note";m_nextNoteLabel->setStyleSheet(QString("font-size:%1pt;font-weight:700;color:#173f39").arg(promptSize,0,'f',1));m_nextNoteLabel->setText(chord?"? + ? + ?":interval?"? + ?":"?");m_coachLabel->setText(chord?"Listen to the hidden 3-note chord.\nFind all three notes in any order.":interval?"Listen to the hidden 2-note interval.\nFind both notes in any order.":"Listen to the hidden note.\nFind it on the keyboard.");m_barLabel->setText(QString("EAR TRAINING   ·   Round %1 · Find the %2").arg(m_earTrainingRound).arg(promptType));
    updateStats();updatePrimaryButton();playEarTrainingPrompt();
}
void MainWindow::showNotice(const QString&text,bool warning){m_noticeText->setText(text);m_notice->setProperty("warning",warning);m_notice->show();}
void MainWindow::toast(const QString&text){statusBar()->show();statusBar()->showMessage(text,3500);QTimer::singleShot(3600,statusBar(),&QStatusBar::hide);}

void MainWindow::keyPressEvent(QKeyEvent*e){if(e->isAutoRepeat()){e->accept();return;}if(m_keyMap.contains(e->key())&&!qobject_cast<QLineEdit*>(focusWidget())){userNoteOn(m_keyMap[e->key()],100);e->accept();return;}if(e->key()==Qt::Key_Space&&!qobject_cast<QLineEdit*>(focusWidget())){togglePlayback();e->accept();return;}if(e->key()==Qt::Key_R&&!qobject_cast<QLineEdit*>(focusWidget())){toggleRecord();e->accept();return;}if(e->key()==Qt::Key_Escape){stopPlayback(false);if(m_recording)finishRecording();m_synth.allOff();e->accept();return;}QMainWindow::keyPressEvent(e);}
void MainWindow::keyReleaseEvent(QKeyEvent*e){if(e->isAutoRepeat()){e->accept();return;}if(m_keyMap.contains(e->key())){userNoteOff(m_keyMap[e->key()]);e->accept();return;}QMainWindow::keyReleaseEvent(e);}
void MainWindow::closeEvent(QCloseEvent*e){if(m_recording){auto r=QMessageBox::question(this,"Recording in progress","Stop recording and close VArpeggio?");if(r!=QMessageBox::Yes){e->ignore();return;}finishRecording();}m_sourcePlayer->stop();m_midi.stop();m_midiOutput.allNotesOff();m_midiOutput.close();m_synth.allOff();e->accept();}
