#include "pdfrecognizer.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <cstdio>

static bool check(bool condition,const char *message){if(!condition)std::fprintf(stderr,"FAIL: %s\n",message);return condition;}

int main(int argc,char **argv){
    QGuiApplication app(argc,argv);QTemporaryDir temp;if(!temp.isValid())return 2;
    const QString path=temp.filePath("native-omr.pdf");
    QPdfWriter writer(path);writer.setResolution(72);writer.setPageSize(QPageSize(QPageSize::A4));
    QPainter painter(&writer);painter.fillRect(QRectF(0,0,595,842),Qt::white);painter.setRenderHint(QPainter::Antialiasing);painter.setPen(QPen(Qt::black,1));
    painter.drawText(QPointF(70,130),QStringLiteral("Quarter note = 123"));
    const int left=65,right=530,spacing=8;
    for(int staffTop:{190,270})for(int line=0;line<5;++line)painter.drawLine(left,staffTop+line*spacing,right,staffTop+line*spacing);
    for(int x:{155,340,530}){painter.drawLine(x,190,x,222);painter.drawLine(x,270,x,302);}
    auto note=[&](int x,int y,bool stemUp){painter.setBrush(Qt::black);painter.drawEllipse(QRectF(x-5,y-3,10,6));painter.setBrush(Qt::NoBrush);painter.drawLine(stemUp?x+5:x-5,y,stemUp?x+5:x-5,stemUp?y-24:y+24);};
    note(205,210,true);note(260,202,true);note(205,286,false);note(260,294,false);
    note(390,206,true);note(450,198,true);note(390,290,false);note(450,282,false);
    painter.end();
    const PdfRecognitionResult result=PdfRecognizer::recognize(path,96);
    bool ok=true;ok&=check(result.error.isEmpty(),"native PDF recognition succeeds");
    ok&=check(PdfRecognizer::detectPrintedTempo(path,96)==123,"printed PDF tempo is detected independently");
    ok&=check(result.tempo==123,"native PDF recognition keeps the printed tempo");
    ok&=check(result.staffSystems==1,"paired treble and bass staves form one system");
    ok&=check(result.notes.size()>=6,"printed noteheads become playable notes");
    bool rightHand=false,leftHand=false;for(const auto &note:result.notes){rightHand|=note.hand==Hand::Right;leftHand|=note.hand==Hand::Left;}
    ok&=check(rightHand&&leftHand,"both hands are recognized");
    return ok?0:1;
}
