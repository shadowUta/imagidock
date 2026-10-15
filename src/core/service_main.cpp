#include "Library.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QPointer>
#include <QLockFile>
#include <QTimer>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("ImagiDock Library Service");
    QCommandLineParser p; p.addHelpOption();
    p.addOption({"library", "Library directory", "directory"});
    p.addOption({"helpers", "Format helpers directory", "directory", QCoreApplication::applicationDirPath()+"/helpers"});
    p.addOption({"python", "Python executable", "path", QCoreApplication::applicationDirPath()+"/python/python.exe"});
    p.process(app);
    if (p.value("library").isEmpty()) { std::fputs("--library is required\n", stderr); return 2; }
    const QString root = QDir(p.value("library")).absolutePath();
    QDir().mkpath(root);
    QLockFile lock(root+"/service.lock"); lock.setStaleLockTime(0);
    if (!lock.tryLock()) { std::fputs("Library already has a service\n", stderr); return 3; }
    try {
        auto library = std::make_shared<imagidock::Library>(root,p.value("helpers"),p.value("python"));
        QLocalServer server; server.setSocketOptions(QLocalServer::UserAccessOption);
        const auto name=imagidock::Library::pipeName(root);
        QLocalServer::removeServer(name);
        if (!server.listen(name)) { std::fputs(qPrintable(server.errorString()),stderr); return 4; }
        QObject::connect(&server,&QLocalServer::newConnection,&app,[&] {
            while (auto *socket=server.nextPendingConnection()) {
                socket->setReadBufferSize(8*1024*1024+1);
                auto pending=std::make_shared<int>(0);
                QObject::connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
                QObject::connect(socket,&QLocalSocket::readyRead,socket,[socket,library,pending] {
                    if (socket->bytesAvailable()>8*1024*1024) { socket->disconnectFromServer(); return; }
                    while (socket->canReadLine()) {
                        const auto line=socket->readLine();
                        if (++*pending>128) { socket->disconnectFromServer(); return; }
                        QJsonParseError err; const auto doc=QJsonDocument::fromJson(line,&err);
                        if(err.error!=QJsonParseError::NoError || !doc.isObject()) {
                            socket->write("{\"error\":{\"code\":\"invalid_json\",\"message\":\"Expected JSON object\"}}\n"); --*pending; continue;
                        }
                        QPointer<QLocalSocket> safe(socket);
                        auto *watcher=new QFutureWatcher<QJsonObject>(socket);
                        const bool shutdown=doc.object()["method"].toString()=="service.shutdown";
                        QObject::connect(watcher,&QFutureWatcher<QJsonObject>::finished,socket,[safe,watcher,pending,shutdown] {
                            --*pending;
                            if(safe) safe->write(QJsonDocument(watcher->result()).toJson(QJsonDocument::Compact)+'\n');
                            if(shutdown&&!watcher->result().contains("error"))QTimer::singleShot(100,QCoreApplication::instance(),&QCoreApplication::quit);
                            watcher->deleteLater();
                        });
                        watcher->setFuture(QtConcurrent::run([library,request=doc.object()] {return library->handle(request);}));
                    }
                });
            }
        });
        const int result=app.exec();
        QThreadPool::globalInstance()->waitForDone();
        return result;
    } catch(const std::exception &e) { std::fprintf(stderr,"%s\n",e.what()); return 5; }
}
