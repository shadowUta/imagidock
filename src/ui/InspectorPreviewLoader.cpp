#include "InspectorPreviewLoader.h"
#include <QFutureWatcher>
#include <QImageReader>
#include <QTimer>
#include <QtConcurrent>

InspectorPreviewLoader::InspectorPreviewLoader(RpcClient *rpc,QObject *parent):QObject(parent),m_rpc(rpc){}

void InspectorPreviewLoader::setAsset(const QJsonObject &asset){
    const auto id=asset["id"].toString(),revision=asset["revision"].toVariant().toString();
    if(id==m_assetId&&revision==m_revision)return;
    ++m_generation;m_assetId=id;m_revision=revision;m_selected.clear();m_cache.clear();m_pending.clear();
}

void InspectorPreviewLoader::select(const QString &key){
    m_selected=key;
    if(m_assetId.isEmpty()||key.isEmpty())return;
    if(auto image=m_cache.object(key)){emit ready(key,*image);return;}
    if(m_pending.contains(key))return;
    m_pending.insert(key);request(key,m_generation);
}

void InspectorPreviewLoader::request(const QString &key,quint64 generation){
    QJsonObject params{{"asset_id",m_assetId},{"size",512},{"priority",100}};
    if(key=="main")params["source_only"]=true;else params["preview_id"]=key;
    m_rpc->call("thumbnail.request",params,this,[this,key,generation](const QJsonObject &result,const QString &error){
        if(generation!=m_generation)return;
        if(!error.isEmpty()){fail(key,generation,error);return;}
        if(!result["path"].toString().isEmpty()){read(result["path"].toString(),key,generation);return;}
        if(!result["job_id"].toString().isEmpty()){waitForJob(result["job_id"].toString(),key,generation);return;}
        fail(key,generation,tr("无法生成此预览图"));
    });
}

void InspectorPreviewLoader::waitForJob(const QString &job,const QString &key,quint64 generation,int attempts){
    if(generation!=m_generation)return;
    if(attempts>=360){fail(key,generation,tr("预览生成超时，可切换页面后重试"));return;}
    m_rpc->call("job.get",{{"job_id",job}},this,[this,job,key,generation,attempts](const QJsonObject &result,const QString &error){
        if(generation!=m_generation)return;
        const auto state=result["state"].toString();
        if(!error.isEmpty()||state=="failed"||state=="cancelled"||state=="interrupted"){
            fail(key,generation,error.isEmpty()?result["error"].toString(tr("无法生成此预览图")):error);return;
        }
        if(state=="completed"){
            const auto path=result["result"].toObject()["path"].toString();
            if(path.isEmpty())fail(key,generation,tr("预览任务没有生成图片"));else read(path,key,generation);
            return;
        }
        QTimer::singleShot(500,this,[this,job,key,generation,attempts]{waitForJob(job,key,generation,attempts+1);});
    });
}

void InspectorPreviewLoader::read(const QString &path,const QString &key,quint64 generation){
    auto watcher=new QFutureWatcher<QImage>(this);
    connect(watcher,&QFutureWatcher<QImage>::finished,this,[this,watcher,key,generation]{
        const auto image=watcher->result();watcher->deleteLater();
        if(generation!=m_generation)return;
        if(image.isNull()){fail(key,generation,tr("无法读取此预览图"));return;}
        m_pending.remove(key);auto pixmap=QPixmap::fromImage(image);
        m_cache.insert(key,new QPixmap(pixmap),qMax(1,int(image.sizeInBytes()/1024)));
        if(key==m_selected)emit ready(key,pixmap);
    });
    watcher->setFuture(QtConcurrent::run([path]{
        QImageReader reader(path);reader.setAutoTransform(true);
        const auto size=reader.size();if(size.width()>512||size.height()>512)reader.setScaledSize(size.scaled(512,512,Qt::KeepAspectRatio));
        return reader.read();
    }));
}

void InspectorPreviewLoader::fail(const QString &key,quint64 generation,const QString &message){
    if(generation!=m_generation)return;
    m_pending.remove(key);if(key==m_selected)emit failed(key,message);
}
