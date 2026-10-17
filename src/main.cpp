#include "mainwindow.h"
#include "helpmanual.h"
#include "pdfrecognizer.h"
#include "synthesizer.h"

#include <QApplication>
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPushButton>
#include <QTimer>

static QIcon makeIcon() {
    QPixmap pix(256,256); pix.fill(Qt::transparent); QPainter p(&pix); p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor("#112824")); p.setPen(Qt::NoPen); p.drawRoundedRect(pix.rect().adjusted(8,8,-8,-8),48,48);
    p.setPen(QPen(QColor("#bfe3d9"),13,Qt::SolidLine,Qt::RoundCap));
    for(int y=75;y<=155;y+=20)p.drawLine(43,y,213,y);
    p.setBrush(QColor("#ef775f"));p.setPen(Qt::NoPen);p.save();p.translate(134,116);p.rotate(-18);p.drawEllipse(QRectF(-28,-18,56,36));p.restore();
    p.setPen(QPen(QColor("#ef775f"),12,Qt::SolidLine,Qt::RoundCap));p.drawLine(157,112,157,53);
    return QIcon(pix);
}

int main(int argc,char*argv[]){
    QApplication app(argc,argv);app.setApplicationName("VArpeggio");app.setApplicationDisplayName("VArpeggio");app.setOrganizationName("Voncloft");app.setOrganizationDomain("voncloft.com");app.setDesktopFileName("com.voncloft.VArpeggio");app.setWindowIcon(makeIcon());
    const QStringList args=app.arguments();const int shot=args.indexOf("--screenshot");const int chordDialogShot=args.indexOf("--chord-dialog-screenshot");const int routineDialogShot=args.indexOf("--routine-dialog-screenshot");const int helpManualShot=args.indexOf("--help-manual-screenshot");const int open=args.indexOf("--open");const int importVideo=args.indexOf("--import-video");
    const int testFontSize=args.indexOf("--test-font-size");if(testFontSize>=0&&testFontSize+1<args.size()){bool ok=false;const int points=args[testFontSize+1].toInt(&ok);if(ok){QFont font=app.font();font.setPointSize(qBound(8,points,32));app.setFont(font);}}
    int screenshotDelay=1200;const int screenshotDelayArg=args.indexOf("--screenshot-delay");if(screenshotDelayArg>=0&&screenshotDelayArg+1<args.size()){bool ok=false;const int requested=args[screenshotDelayArg+1].toInt(&ok);if(ok)screenshotDelay=qBound(50,requested,30000);}
    const int transcribe=args.indexOf("--transcribe-falling");const int transcribeAudio=args.indexOf("--transcribe-audio");const int transcribePdf=args.indexOf("--transcribe-pdf");const int estimateTempo=args.indexOf("--estimate-tempo");const int renderWav=args.indexOf("--render-wav");const int renderVideo=args.indexOf("--render-video");const int renderDetector=args.indexOf("--render-detector");const int scoreShot=args.indexOf("--score-screenshot");
    if(scoreShot>=0&&scoreShot+2<args.size()){
        QVector<NoteEvent> notes;QVector<TimeSignatureEvent> meters;QVector<double> repeats;QVector<KeySignatureEvent> keys;QVector<StaffRestEvent> rests;QVector<PedalEvent> pedals;QString title,error;int tempo=96;
        if(!MusicDocument::importFile(args[scoreShot+1],notes,title,tempo,&error,&meters,&repeats,&keys,&rests,&pedals)){fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 2;}
        ScoreWidget score;score.setNotes(notes);score.setTimeSignatures(meters);score.setKeySignatures(keys);score.setRests(rests);score.setRepeatEnds(repeats);score.setPedalEvents(pedals);score.setShowNames(args.contains("--show-note-names"));score.setShowFingering(args.contains("--show-fingering"));score.resize(1100,440);
        if(scoreShot+3<args.size()){bool ok=false;const double beat=args[scoreShot+3].toDouble(&ok);if(ok)score.setFollowBeat(beat);}
        QImage image(1100,440,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);QPainter painter(&image);score.paintScore(painter,QRectF(0,0,1100,440));painter.end();
        if(!image.save(args[scoreShot+2])){fprintf(stderr,"Could not save the live-score screenshot.\n");return 3;}
        return 0;
    }
    if(renderWav>=0&&renderWav+2<args.size()){
        QVector<NoteEvent> notes;QString title,error;int tempo=96;
        if(!MusicDocument::importFile(args[renderWav+1],notes,title,tempo,&error)){fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 2;}
        SynthInstrument instrument=SynthInstrument::AcousticGrand;
        if(renderWav+3<args.size()){
            const QString requested=args[renderWav+3].toLower();
            if(requested=="bright")instrument=SynthInstrument::BrightPiano;
            else if(requested=="mellow"||requested=="soft")instrument=SynthInstrument::SoftPiano;
        }
        if(!Synthesizer::saveWav(args[renderWav+2],notes,tempo,&error,instrument)){fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 3;}
        fprintf(stderr,"Rendered %lld generated notes at %d BPM.\n",qint64(notes.size()),tempo);return 0;
    }
    if(renderVideo>=0&&renderVideo+2<args.size()){
        QVector<NoteEvent> notes;QString title,error;int tempo=96;
        if(!MusicDocument::importFile(args[renderVideo+1],notes,title,tempo,&error)){fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 2;}
        SynthInstrument instrument=SynthInstrument::BrightPiano;
        if(renderVideo+3<args.size()){
            const QString requested=args[renderVideo+3].toLower();
            if(requested=="acoustic"||requested=="concert")instrument=SynthInstrument::AcousticGrand;
            else if(requested=="mellow"||requested=="soft")instrument=SynthInstrument::SoftPiano;
        }
        if(!MainWindow::saveFallingNoteVideoFile(args[renderVideo+2],notes,tempo,title,instrument,nullptr,&error)){fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 3;}
        fprintf(stderr,"Rendered %lld generated notes as a falling-note piano video at %d BPM.\n",qint64(notes.size()),tempo);return 0;
    }
    if(renderDetector>=0&&renderDetector+2<args.size()){
        QVector<NoteEvent> notes;QString error;TranscriptionInfo info;
        if(!MusicDocument::transcribeFallingNotesVideo(args[renderDetector+1],0,notes,&error,[](int p){if(p%10==0)fprintf(stderr,"Detector analysis: %d%%\n",p);},{},&info)){
            fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 2;
        }
        const QVector<NoteEvent> directorNotes=info.directorNotes.isEmpty()?notes:info.directorNotes;
        const double directorStart=info.directorNotes.isEmpty()?info.sourceStartSeconds:info.directorStartSeconds;
        if(!MainWindow::saveDetectorVideoFile(args[renderDetector+2],args[renderDetector+1],directorNotes,info.tempo,directorStart,info,nullptr,&error)){
            fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 3;
        }
        fprintf(stderr,"Rendered %lld accepted source bars over the original video.\n",qint64(directorNotes.size()));return 0;
    }
    if(estimateTempo>=0&&estimateTempo+1<args.size()){
        int tempo=0;double confidence=0;QString error;
        if(!MusicDocument::detectTempo(args[estimateTempo+1],tempo,&confidence,&error,[](int p){if(p%10==0)fprintf(stderr,"Tempo analysis: %d%%\n",p);})){fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 2;}
        fprintf(stdout,"%d BPM (confidence %.2f)\n",tempo,confidence);return 0;
    }
    if(transcribeAudio>=0&&transcribeAudio+2<args.size()){
        int tempoHint=0;if(transcribeAudio+3<args.size()&&!args[transcribeAudio+3].startsWith("--")){bool valid=false;const int requested=args[transcribeAudio+3].toInt(&valid);if(!valid){fprintf(stderr,"The optional BPM must be a number.\n");return 64;}tempoHint=qBound(30,requested,260);}
        QVector<NoteEvent> notes;QString error;TranscriptionInfo info;
        if(!MusicDocument::transcribeMedia(args[transcribeAudio+1],tempoHint,notes,&error,[](int p){if(p%10==0)fprintf(stderr,"Sound transcription: %d%%\n",p);},{},&info)){
            fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 2;
        }
        const int tempo=info.tempo;
        const QString projectPath=args[transcribeAudio+2];const QString title=QFileInfo(args[transcribeAudio+1]).completeBaseName()+" — sound transcription";
        if(!MusicDocument::saveJson(projectPath,notes,tempo,title,&error)){fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 3;}
        const QFileInfo output(projectPath);const QString base=output.dir().filePath(output.completeBaseName());
        if(!MusicDocument::saveMidi(base+".mid",notes,tempo,&error)||!MusicDocument::saveEngravedScore(base+".musicxml",base+".pdf",notes,tempo,title,&error)){
            fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 4;
        }
        fprintf(stderr,"Created %lld sound-detected notes plus MIDI, detailed MusicXML, and playable PDF at %d BPM%s.\n",qint64(notes.size()),tempo,info.tempoEstimated?" (estimated)":"");return 0;
    }
    if(transcribePdf>=0&&transcribePdf+2<args.size()){
        const QString input=args[transcribePdf+1],output=args[transcribePdf+2];
        const PdfRecognitionResult recognized=PdfRecognizer::recognize(input,96,[](int p){fprintf(stderr,"Native PDF recognition: %d%%\n",p);});
        if(recognized.notes.isEmpty()){fprintf(stderr,"%s\n",recognized.error.toLocal8Bit().constData());return 2;}
        QString error;const QString title=QFileInfo(input).completeBaseName();
        if(!MusicDocument::saveJson(output,recognized.notes,recognized.tempo,title,&error,recognized.timeSignatures,{},recognized.keySignatures,{})){
            fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 3;
        }
        const QFileInfo target(output);const QString xml=target.dir().filePath(target.completeBaseName()+".musicxml");
        if(!MusicDocument::saveEngravedScore(xml,{},recognized.notes,recognized.tempo,title,&error,recognized.timeSignatures,{},recognized.keySignatures,{})){
            fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 4;
        }
        fprintf(stderr,"Recognized %lld notes in %d staff systems without an external OMR program.\n",qint64(recognized.notes.size()),recognized.staffSystems);return 0;
    }
    if(transcribe>=0&&transcribe+2<args.size()){
        int tempoHint=0;if(transcribe+3<args.size()&&!args[transcribe+3].startsWith("--")){bool valid=false;const int requested=args[transcribe+3].toInt(&valid);if(!valid){fprintf(stderr,"The optional BPM must be a number.\n");return 64;}tempoHint=qBound(30,requested,260);}
        QVector<NoteEvent> notes;QString error;TranscriptionInfo info;
        if(!MusicDocument::transcribeFallingNotesVideo(args[transcribe+1],tempoHint,notes,&error,[](int p){if(p%10==0)fprintf(stderr,"Visual transcription: %d%%\n",p);},{},&info)){ 
            fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 2;
        }
        const int tempo=info.tempo;
        const QString projectPath=args[transcribe+2];
        const QString transcriptionTitle=QFileInfo(args[transcribe+1]).completeBaseName()+" — falling-note transcription";
        if(!MusicDocument::saveJson(projectPath,notes,tempo,transcriptionTitle,&error)){
            fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 3;
        }
        const QFileInfo output(projectPath);const QString base=output.dir().filePath(output.completeBaseName());
        if(!MusicDocument::saveMidi(base+".mid",notes,tempo,&error)||
           !MusicDocument::saveEngravedScore(base+".musicxml",base+".pdf",notes,tempo,transcriptionTitle,&error)){
            fprintf(stderr,"%s\n",error.toLocal8Bit().constData());return 4;
        }
        fprintf(stderr,"Created %lld notes plus MIDI, detailed MusicXML, and playable PDF at %d BPM%s; audio agreement %.2f.\n",qint64(notes.size()),tempo,info.tempoEstimated?" (estimated)":"",info.audiovisualAgreement);return 0;
    }
    if(helpManualShot>=0&&helpManualShot+1<args.size()){
        HelpManualDialog dialog;dialog.show();QTimer::singleShot(screenshotDelay,&dialog,[&dialog,&app,path=args[helpManualShot+1]]{dialog.grab().save(path);dialog.close();app.quit();});return app.exec();
    }
    if(shot>=0||chordDialogShot>=0||routineDialogShot>=0)app.setQuitOnLastWindowClosed(false);
    MainWindow window;if(open>=0&&open+1<args.size())window.openPath(args[open+1]);window.show();
    if(importVideo>=0&&importVideo+1<args.size()){int tempo=0;if(importVideo+2<args.size()&&!args[importVideo+2].startsWith("--")){bool ok=false;const int requested=args[importVideo+2].toInt(&ok);if(!ok){fprintf(stderr,"The optional BPM must be a number.\n");return 64;}tempo=qBound(30,requested,260);}if(!window.transcribeVideoPath(args[importVideo+1],tempo))return 5;}
    if(args.contains("--live-follow"))window.showLiveScore();
    if(args.contains("--start-listen"))window.startListening();
    if(shot>=0&&shot+1<args.size())QTimer::singleShot(screenshotDelay,&window,[&window,&app,path=args[shot+1]]{window.grab().save(path);app.quit();});
    if(chordDialogShot>=0&&chordDialogShot+1<args.size()){
        QTimer::singleShot(0,&window,[&window]{for(auto *action:window.findChildren<QAction*>())if(action->text().startsWith("Chord practice")){action->trigger();break;}});
        QTimer::singleShot(900,&window,[&app,path=args[chordDialogShot+1]]{if(auto *dialog=QApplication::activeModalWidget()){dialog->grab().save(path);dialog->close();}app.quit();});
    }
    if(routineDialogShot>=0&&routineDialogShot+1<args.size()){
        QTimer::singleShot(0,&window,[&window]{for(auto *control:window.findChildren<QPushButton*>())if(control->text().contains("practice plan",Qt::CaseInsensitive)||control->toolTip().contains("practice plan",Qt::CaseInsensitive)){control->click();break;}});
        if(args.contains("--cycle-cadence"))QTimer::singleShot(450,&window,[]{if(auto *dialog=QApplication::activeModalWidget())for(auto *control:dialog->findChildren<QPushButton*>())if(control->text()=="Change cadence pattern"){control->click();break;}});
        QTimer::singleShot(900,&window,[&app,path=args[routineDialogShot+1]]{if(auto *dialog=QApplication::activeModalWidget()){dialog->grab().save(path);dialog->close();}app.quit();});
    }
    return app.exec();
}
