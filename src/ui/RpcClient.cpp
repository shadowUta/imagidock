#include "RpcClient.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QUuid>

RpcClient::RpcClient(const QString &library, QObject *parent) : QObject(parent), m_library(QDir(library).absolutePath()) {
    QDir().mkpath(m_library);
    QString canonical = QFileInfo(m_library).canonicalFilePath();
    if (canonical.isEmpty()) canonical = m_library;
    canonical = QDir::fromNativeSeparators(canonical);
#ifdef Q_OS_WIN
    canonical = canonical.toLower();
#endif
    m_pipe = "imagidock-" + QString::fromLatin1(QCryptographicHash::hash(canonical.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
    connect(&m_socket, &QLocalSocket::connected, this, [this] { m_retry.stop(); emit connectionChanged(true, tr("素材库已连接")); flush(); refresh(); });
    connect(&m_socket, &QLocalSocket::readyRead, this, &RpcClient::read);
    connect(&m_socket, &QLocalSocket::disconnected, this, [this] { emit connectionChanged(false,tr("正在重新连接素材库…")); m_retry.start(); });
    connect(&m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) { if (!m_launched) { m_launched=true; const QString exe=QCoreApplication::applicationDirPath()+"/imagidock-service.exe"; QProcess::startDetached(exe,{"--library",m_library},QCoreApplication::applicationDirPath()); } m_retry.start(); });
    m_retry.setInterval(500); connect(&m_retry,&QTimer::timeout,this,&RpcClient::connectService);
    m_poll.setInterval(1600); connect(&m_poll,&QTimer::timeout,this,&RpcClient::refresh); m_poll.start();
    m_timeout.setInterval(1000); connect(&m_timeout,&QTimer::timeout,this,[this] { const auto now=QDateTime::currentMSecsSinceEpoch(); const auto ids=m_pending.keys(); for(const auto &id:ids) { if(now-m_pending[id].started>45000) { auto p=m_pending.take(id); if(p.context) p.reply({},tr("素材库请求超时，请重试。")); } } }); m_timeout.start();
    QTimer::singleShot(0,this,&RpcClient::connectService);
}
void RpcClient::connectService(){ if(m_socket.state()==QLocalSocket::UnconnectedState) m_socket.connectToServer(m_pipe); }
void RpcClient::call(const QString &method,QJsonObject params,QObject *context,Reply reply){
    const QString id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(!m_libraryId.isEmpty()) params.insert("library_id",m_libraryId);
    auto line=QJsonDocument(QJsonObject{{"id",id},{"method",method},{"params",params}}).toJson(QJsonDocument::Compact)+'\n';
    m_pending.insert(id,{line,context,std::move(reply),QDateTime::currentMSecsSinceEpoch(),false}); flush();
}
void RpcClient::flush(){ if(m_socket.state()!=QLocalSocket::ConnectedState)return; for(auto it=m_pending.begin();it!=m_pending.end();++it) if(!it->sent){ m_socket.write(it->line); it->sent=true; } }
void RpcClient::read(){
    m_buffer+=m_socket.readAll();
    while(m_buffer.contains('\n')){ const int end=m_buffer.indexOf('\n'); const auto line=m_buffer.left(end); m_buffer.remove(0,end+1); const auto obj=QJsonDocument::fromJson(line).object(); const QString id=obj["id"].toString(); if(!m_pending.contains(id))continue; auto p=m_pending.take(id); if(!p.context)continue; const auto error=obj["error"].toObject(); p.reply(obj["result"].toObject(),error.isEmpty()?QString():error["message"].toString(tr("请求未完成"))); }
    if(m_buffer.size()>8*1024*1024){ m_buffer.clear(); m_socket.abort(); }
}
void RpcClient::refresh(){ if(m_polling || m_socket.state()!=QLocalSocket::ConnectedState)return; m_polling=true; call("library.info",{},this,[this](const QJsonObject &r,const QString &error){ m_polling=false; if(!error.isEmpty())return; m_libraryId=r["library_id"].toString(); const qint64 rev=r["revision"].toVariant().toLongLong(); emit ready(r); if(rev!=m_revision){ m_revision=rev; emit changed(); } }); }
