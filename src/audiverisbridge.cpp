#include "audiverisbridge.h"

#include "musicdocument.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QUrl>
#include <algorithm>

namespace {
struct Launcher { QString program;QStringList prefix;bool flatpak=false;explicit operator bool()const{return !program.isEmpty();} };

Launcher findLauncher(){
    for(const QString &name:{QStringLiteral("Audiveris"),QStringLiteral("audiveris")}){
        const QString executable=QStandardPaths::findExecutable(name);if(!executable.isEmpty())return {executable,{},false};
    }
    const QString installed=QStringLiteral("/opt/audiveris/bin/Audiveris");
    if(QFileInfo(installed).isExecutable())return {installed,{},false};
    const QString bundled=QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("audiveris-runtime/opt/audiveris/bin/Audiveris"));
    if(QFileInfo(bundled).isExecutable())return {bundled,{},false};
    const QString downloaded=QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("audiveris-runtime/opt/audiveris/bin/Audiveris"));
    if(QFileInfo(downloaded).isExecutable())return {downloaded,{},false};
    const QString flatpak=QStandardPaths::findExecutable(QStringLiteral("flatpak"));
    if(!flatpak.isEmpty()){
        QProcess info;info.start(flatpak,{QStringLiteral("info"),QStringLiteral("org.audiveris.audiveris")});
        if(info.waitForFinished(5000)&&info.exitStatus()==QProcess::NormalExit&&info.exitCode()==0)return {flatpak,{QStringLiteral("run"),QStringLiteral("org.audiveris.audiveris")},true};
    }
    return {};
}

bool runInstallerProcess(const QString &program,const QStringList &arguments,QString *error){
    QProcess process;process.setProcessChannelMode(QProcess::MergedChannels);process.start(program,arguments);
    if(!process.waitForStarted(10000)){if(error)*error=process.errorString();return false;}
    process.waitForFinished(-1);
    if(process.exitStatus()!=QProcess::NormalExit||process.exitCode()!=0){if(error)*error=QString::fromLocal8Bit(process.readAll()).trimmed().right(2000);return false;}
    return true;
}

QByteArray download(const QUrl &url,const QByteArray &accept,std::function<void(qint64,qint64)> progress,std::function<bool()> canceled,QString *error){
    QNetworkAccessManager manager;QNetworkRequest request(url);request.setRawHeader("User-Agent","VArpeggio/0.1");if(!accept.isEmpty())request.setRawHeader("Accept",accept);
    QNetworkReply *reply=manager.get(request);QEventLoop loop;
    QObject::connect(reply,&QNetworkReply::downloadProgress,&loop,[&](qint64 received,qint64 total){if(progress)progress(received,total);if(canceled&&canceled())reply->abort();});
    QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);loop.exec();
    const QByteArray data=reply->readAll();
    if(reply->error()!=QNetworkReply::NoError){if(error)*error=(canceled&&canceled())?QStringLiteral("Download canceled."):reply->errorString();reply->deleteLater();return {};}
    reply->deleteLater();return data;
}
}

bool AudiverisBridge::available(QString *description){
    const Launcher launcher=findLauncher();
    if(description)*description=launcher?(launcher.flatpak?QStringLiteral("Audiveris (Flatpak)"):QFileInfo(launcher.program).fileName()):QString();
    return bool(launcher);
}

bool AudiverisBridge::ensureAvailable(QString *error,std::function<void(int,const QString&)> progress,std::function<bool()> canceled){
    if(available())return true;
    const QString flatpak=QStandardPaths::findExecutable(QStringLiteral("flatpak"));
    if(!flatpak.isEmpty()){
        if(progress)progress(5,"Preparing the official Flathub source…");
        QString installError;
        runInstallerProcess(flatpak,{"remote-add","--if-not-exists","--user","flathub","https://flathub.org/repo/flathub.flatpakrepo"},&installError);
        if(canceled&&canceled()){if(error)*error="Installation canceled.";return false;}
        if(progress)progress(15,"Downloading the open-source Audiveris engine from Flathub…");
        if(runInstallerProcess(flatpak,{"install","--user","-y","flathub","org.audiveris.audiveris"},&installError)&&available())return true;
        // Continue with the official release fallback when Flatpak itself is
        // present but the remote or installation is unavailable.
    }
    if(progress)progress(5,"Finding the latest official Audiveris Linux release…");
    QString networkError;
    const QByteArray releaseData=download(QUrl("https://api.github.com/repos/Audiveris/audiveris/releases/latest"),"application/vnd.github+json",{},canceled,&networkError);
    if(releaseData.isEmpty()){if(error)*error="Could not read the official Audiveris release information: "+networkError;return false;}
    const QJsonDocument release=QJsonDocument::fromJson(releaseData);QString assetUrl,assetName;
    const QString architecture=QSysInfo::currentCpuArchitecture();
    for(const auto &value:release.object().value("assets").toArray()){
        const QJsonObject asset=value.toObject();const QString name=asset.value("name").toString();const QString lower=name.toLower();
        const bool linuxPackage=lower.endsWith(".deb")&&(lower.contains("linux")||lower.contains("ubuntu"));
        const bool architectureMatches=(architecture.contains("64")&&lower.contains("x86_64"))||(architecture.contains("arm")&&lower.contains("arm64"));
        if(linuxPackage&&architectureMatches){
            if(assetUrl.isEmpty()||lower.contains("ubuntu24.04")){assetUrl=asset.value("browser_download_url").toString();assetName=name;}
            if(lower.contains("ubuntu24.04"))break;
        }
    }
    if(assetUrl.isEmpty()){if(error)*error="The latest Audiveris release has no compatible Linux package for "+architecture+".";return false;}
    if(progress)progress(10,"Downloading "+assetName+" from the official Audiveris GitHub release…");
    const QByteArray package=download(QUrl(assetUrl),"application/octet-stream",[&](qint64 received,qint64 total){if(progress&&total>0)progress(10+int(received*70/qMax<qint64>(1,total)),"Downloading the open-source Audiveris engine…");},canceled,&networkError);
    if(package.isEmpty()){if(error)*error="Could not download Audiveris: "+networkError;return false;}
    QTemporaryDir temporary;if(!temporary.isValid()){if(error)*error="Could not create the Audiveris installation workspace.";return false;}
    const QString packagePath=temporary.filePath(assetName);QFile file(packagePath);
    if(!file.open(QIODevice::WriteOnly)||file.write(package)!=package.size()){if(error)*error="Could not save the downloaded Audiveris package.";return false;}file.close();
    const QString bsdtar=QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
    if(bsdtar.isEmpty()){if(error)*error="VArpeggio downloaded Audiveris, but bsdtar is unavailable to unpack the open-source Linux package.";return false;}
    const QString unpacked=temporary.filePath("package");QDir().mkpath(unpacked);QString unpackError;
    if(progress)progress(82,"Unpacking the Audiveris runtime…");
    if(!runInstallerProcess(bsdtar,{"-xf",packagePath,"-C",unpacked},&unpackError)){if(error)*error="Could not unpack the Audiveris package: "+unpackError;return false;}
    QString payload;QDirIterator payloads(unpacked,QStringList{"data.tar","data.tar.gz","data.tar.xz","data.tar.zst"},QDir::Files);
    if(payloads.hasNext())payload=payloads.next();
    if(payload.isEmpty()){if(error)*error="The Audiveris Linux package did not contain its runtime payload.";return false;}
    const QString installRoot=QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("audiveris-runtime");QDir().mkpath(installRoot);
    if(!runInstallerProcess(bsdtar,{"-xf",payload,"-C",installRoot},&unpackError)){if(error)*error="Could not install the Audiveris runtime: "+unpackError;return false;}
    if(progress)progress(100,"Audiveris is ready.");
    if(!available()){if(error)*error="Audiveris was unpacked, but its executable was not found in the expected runtime folder.";return false;}
    return true;
}

PdfRecognitionResult AudiverisBridge::recognize(const QString &pdfPath,int fallbackTempo,std::function<bool()> canceled){
    PdfRecognitionResult result;result.tempo=PdfRecognizer::detectPrintedTempo(pdfPath,fallbackTempo);result.engine="Audiveris";
    const Launcher launcher=findLauncher();if(!launcher){result.error="Audiveris is not installed.";return result;}
    QTemporaryDir workspace;if(!workspace.isValid()){result.error="Could not create the Audiveris workspace.";return result;}
    QStringList arguments=launcher.prefix;
    if(launcher.flatpak){
        arguments.insert(1,QStringLiteral("--filesystem=")+QFileInfo(pdfPath).absolutePath()+QStringLiteral(":ro"));
        arguments.insert(2,QStringLiteral("--filesystem=")+workspace.path());
    }
    arguments<<QStringLiteral("-batch")<<QStringLiteral("-transcribe")<<QStringLiteral("-export")
             <<QStringLiteral("-output")<<workspace.path()<<QStringLiteral("--")<<pdfPath;
    QProcess process;process.setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment environment=QProcessEnvironment::systemEnvironment();
    environment.insert("XDG_CACHE_HOME",workspace.filePath("cache"));environment.insert("XDG_CONFIG_HOME",workspace.filePath("config"));environment.insert("XDG_DATA_HOME",workspace.filePath("data"));
    environment.insert("_JAVA_OPTIONS",QStringLiteral("-Djava.awt.headless=true"));
    process.setProcessEnvironment(environment);process.start(launcher.program,arguments);
    if(!process.waitForStarted(10000)){result.error="Audiveris could not start: "+process.errorString();return result;}
    QByteArray diagnostic;
    while(!process.waitForFinished(250)){
        diagnostic+=process.readAll();if(diagnostic.size()>12000)diagnostic=diagnostic.right(12000);
        if(canceled&&canceled()){process.terminate();if(!process.waitForFinished(2000)){process.kill();process.waitForFinished(2000);}result.canceled=true;return result;}
    }
    diagnostic+=process.readAll();if(diagnostic.size()>12000)diagnostic=diagnostic.right(12000);
    QStringList exports;QDirIterator iterator(workspace.path(),QDir::Files,QDirIterator::Subdirectories);
    while(iterator.hasNext()){const QString candidate=iterator.next();const QString suffix=QFileInfo(candidate).suffix().toLower();if(suffix=="mxl"||suffix=="musicxml"||suffix=="xml")exports<<candidate;}
    exports.sort(Qt::CaseInsensitive);
    double nextStart=0.0;QStringList errors;
    for(const QString &scorePath:exports){
        QVector<NoteEvent> movement;QVector<TimeSignatureEvent> meters;QVector<KeySignatureEvent> keys;QVector<double> repeats;QVector<StaffRestEvent> rests;
        QString title,error;int tempo=result.tempo;
        if(!MusicDocument::importFile(scorePath,movement,title,tempo,&error,&meters,&repeats,&keys,&rests)||movement.isEmpty()){errors<<QFileInfo(scorePath).fileName()+": "+error;continue;}
        double movementEnd=0;for(NoteEvent note:movement){note.start+=nextStart;movementEnd=qMax(movementEnd,note.start+note.duration);result.notes<<note;}
        if(result.staffSystems==0){result.tempo=tempo;if(!meters.isEmpty())result.timeSignatures=meters;if(!keys.isEmpty())result.keySignatures=keys;}
        nextStart=movementEnd+1.0;++result.staffSystems;
    }
    std::sort(result.notes.begin(),result.notes.end(),[](const auto&a,const auto&b){return a.start==b.start?a.note<b.note:a.start<b.start;});
    if(result.notes.isEmpty()){
        result.error=exports.isEmpty()?QStringLiteral("Audiveris did not export a MusicXML score."):QStringLiteral("Audiveris exported a score, but it contained no playable notes.");
        if(!errors.isEmpty())result.error+="\n\n"+errors.join("\n");const QString tail=QString::fromLocal8Bit(diagnostic).trimmed().right(1800);if(!tail.isEmpty())result.error+="\n\nAudiveris details:\n"+tail;
    }
    return result;
}
