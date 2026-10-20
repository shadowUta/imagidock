#pragma once
#include "RpcClient.h"
#include <QCache>
#include <QPixmap>
#include <QSet>

// Uses the service's derived images, including PSD/SAI, without decoding source
// documents on the UI thread. Outstanding replies never change another asset.
class InspectorPreviewLoader : public QObject {
    Q_OBJECT
public:
    explicit InspectorPreviewLoader(RpcClient *rpc,QObject *parent=nullptr);
    void setAsset(const QJsonObject &asset);
    void select(const QString &key);
signals:
    void ready(const QString &key,const QPixmap &image);
    void failed(const QString &key,const QString &message);
private:
    RpcClient *m_rpc;
    QString m_assetId,m_revision,m_selected;
    quint64 m_generation=0;
    QCache<QString,QPixmap> m_cache{16*1024};
    QSet<QString> m_pending;
    void request(const QString &key,quint64 generation);
    void waitForJob(const QString &job,const QString &key,quint64 generation,int attempts=0);
    void read(const QString &path,const QString &key,quint64 generation);
    void fail(const QString &key,quint64 generation,const QString &message);
};
