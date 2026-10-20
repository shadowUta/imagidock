#include "AssetGrid.h"
#include "Theme.h"
#include "UiMetrics.h"
#include <QApplication>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QImageReader>
#include <QMimeData>
#include <QPainter>
#include <QScrollBar>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>

ThumbnailStore::ThumbnailStore(RpcClient *rpc,QObject *parent):QObject(parent),m_rpc(rpc),m_cache(256*1024){}
void ThumbnailStore::clear(){m_cache.clear();m_retryAt.clear();}
QPixmap ThumbnailStore::get(const QJsonObject &a,int size){
    const QString id=a["id"].toString(); if(id.isEmpty())return {};const QString key=id+":"+a["revision"].toVariant().toString()+":"+QString::number(size);
    if(auto p=m_cache.object(key))return *p;
    if(m_pending.contains(key)||m_pending.size()>=12||m_retryAt.value(key)>QDateTime::currentMSecsSinceEpoch())return {};
    m_pending.insert(key);
    const QString local=a["thumbnail_path"].toString();
    if(!local.isEmpty()&&QFileInfo::exists(local)){load(key,id,local);return {};}
    m_rpc->call("thumbnail.request",{{"asset_id",id},{"size",size},{"priority",100}},this,[this,key,id](const QJsonObject &r,const QString &error){
        const QString path=r["path"].toString();
        if(error.isEmpty()&&!path.isEmpty()){load(key,id,path);return;}
        m_pending.remove(key); m_retryAt[key]=QDateTime::currentMSecsSinceEpoch()+(error.isEmpty()?1200:10000);
        QTimer::singleShot(error.isEmpty()?1300:10000,this,[this,id]{emit available(id);});
    });return {};
}
void ThumbnailStore::load(const QString &key,const QString &id,const QString &path){
    auto watcher=new QFutureWatcher<QImage>(this);
    connect(watcher,&QFutureWatcher<QImage>::finished,this,[this,key,id,watcher]{ const auto image=watcher->result();if(!image.isNull())m_cache.insert(key,new QPixmap(QPixmap::fromImage(image)),qMax(1,int(image.sizeInBytes()/1024)));else m_retryAt[key]=QDateTime::currentMSecsSinceEpoch()+10000;m_pending.remove(key);watcher->deleteLater();emit available({});});
    const QString cacheRoot=m_rpc->libraryPath()+"/cache/thumbs";
    watcher->setFuture(QtConcurrent::run([path,cacheRoot]{QImageReader reader(path);reader.setAutoTransform(true);auto image=reader.read();if(!image.isNull()){const QString resolved=QFileInfo(path).canonicalFilePath(),root=QFileInfo(cacheRoot).canonicalFilePath();if(!root.isEmpty()&&resolved.startsWith(root+'/',Qt::CaseInsensitive)){QFile generated(path);if(generated.open(QIODevice::ReadWrite))generated.setFileTime(QDateTime::currentDateTimeUtc(),QFileDevice::FileModificationTime);}}return image;}));
}
AssetModel::AssetModel(RpcClient *rpc,ThumbnailStore *thumbs,QObject *parent):QAbstractListModel(parent),m_rpc(rpc),m_thumbs(thumbs){connect(thumbs,&ThumbnailStore::available,this,[this](const QString &id){if(id.isEmpty()){if(!m_assets.isEmpty())emit dataChanged(index(0),index(m_assets.size()-1),{Qt::DecorationRole});return;}for(int i=0;i<m_assets.size();++i)if(m_assets[i]["id"]==id){emit dataChanged(index(i),index(i),{Qt::DecorationRole});break;}});}
int AssetModel::rowCount(const QModelIndex &p)const{return p.isValid()?0:m_assets.size();}
QVariant AssetModel::data(const QModelIndex &i,int role)const{if(!i.isValid()||i.row()>=m_assets.size())return {};const auto &a=m_assets[i.row()];if(role==AssetRole)return a;if(role==Qt::DisplayRole||role==Qt::AccessibleTextRole)return a["name"].toString();if(role==Qt::AccessibleDescriptionRole)return a["extension"].toString().toUpper()+" · "+(a["missing"].toBool()?tr("来源丢失"):tr("来源有效"))+" · "+a["source_path"].toString()+" · "+tr("按 Enter 打开详情");if(role==Qt::DecorationRole)return m_thumbs->get(a);if(role==Qt::ToolTipRole)return a["name"].toString()+"\n"+a["source_path"].toString();return {};}
QJsonObject AssetModel::asset(int row)const{return row>=0&&row<m_assets.size()?m_assets[row]:QJsonObject();}
void AssetModel::setFilter(QJsonObject filter){++m_generation;beginResetModel();m_filter=filter;m_assets.clear();m_total=0;m_loading=false;endResetModel();fetchMore();}
void AssetModel::refreshRange(int first,int count){if(m_refreshing||m_loading)return;if(m_assets.isEmpty()){setFilter(m_filter);return;}first=qBound(0,first,m_assets.size()-1);count=qBound(1,count,qMin(1000,m_assets.size()-first));m_refreshing=true;const auto generation=m_generation;auto params=m_filter;params["offset"]=first;params["limit"]=count;m_rpc->call("asset.list",params,this,[this,first,count,generation](const QJsonObject &r,const QString &err){m_refreshing=false;if(generation!=m_generation)return;if(!err.isEmpty()){emit error(err);return;}const auto items=r["items"].toArray();bool stable=items.size()==count;for(int i=0;stable&&i<items.size();++i)stable=m_assets[first+i]["id"]==items[i].toObject()["id"];if(!stable||r["total"].toInt()!=m_total){setFilter(m_filter);return;}for(int i=0;i<items.size();++i)m_assets[first+i]=items[i].toObject();if(!items.isEmpty())emit dataChanged(index(first),index(first+items.size()-1));emit loaded(m_assets.size(),m_total);});}
bool AssetModel::canFetchMore(const QModelIndex &p)const{return !p.isValid()&&!m_loading&&m_assets.size()<m_total;}
void AssetModel::fetchMore(const QModelIndex &p){if(p.isValid()||m_loading)return;m_loading=true;const auto generation=m_generation;auto params=m_filter;params["offset"]=m_assets.size();params["limit"]=200;m_rpc->call("asset.list",params,this,[this,generation](const QJsonObject &r,const QString &err){if(generation!=m_generation)return;m_loading=false;if(!err.isEmpty()){emit error(err);return;}const auto items=r["items"].toArray();m_total=r["total"].toInt();if(!items.isEmpty()){const int first=m_assets.size();beginInsertRows({},first,first+items.size()-1);for(const auto &v:items)m_assets.append(v.toObject());endInsertRows();}emit loaded(m_assets.size(),m_total);});}
void AssetDelegate::paint(QPainter *p,const QStyleOptionViewItem &o,const QModelIndex &i)const{
    p->save();p->setRenderHint(QPainter::Antialiasing);
    const auto a=i.data(AssetModel::AssetRole).toJsonObject();
    const bool selected=o.state&QStyle::State_Selected,hover=o.state&QStyle::State_MouseOver,focused=o.state&QStyle::State_HasFocus;
    const auto theme=themeColors();const QRect card=o.rect.adjusted(4,4,-4,-4);
    QColor surface=selected?theme.accent:theme.card;
    surface.setAlpha(selected?(theme.light?40:36):(theme.light?(hover?126:104):(hover?23:14)));
    p->setPen(Qt::NoPen);p->setBrush(surface);p->drawRoundedRect(card,UiMetrics::CardRadius,UiMetrics::CardRadius);

    // One quiet boundary separates the full-image preview from its metadata.
    QColor divider=theme.edge;divider.setAlpha(theme.light?112:28);
    p->setPen(QPen(divider,1));p->drawLine(card.left()+12,card.bottom()-54,card.right()-12,card.bottom()-54);
    const QRect image=card.adjusted(10,10,-10,-66);
    const QPixmap pix=i.data(Qt::DecorationRole).value<QPixmap>();
    if(!pix.isNull()){
        const QSize fitted=pix.size().scaled(image.size()-QSize(8,8),Qt::KeepAspectRatio);
        const QRect dest(image.center()-QPoint(fitted.width()/2,fitted.height()/2),fitted);
        p->setRenderHint(QPainter::SmoothPixmapTransform);p->drawPixmap(dest,pix);
    }else{
        p->setPen(theme.muted);QFont f=o.font;f.setPointSize(22);p->setFont(f);
        const QString type=a["type"].toString();
        const QString symbol=type=="model"?QString::fromUtf8("◇"):type=="audio"?QString::fromUtf8("∿"):type=="video"?QString::fromUtf8("▷"):QString::fromUtf8("▧");
        p->drawText(image,Qt::AlignCenter,symbol);
    }

    p->setFont(o.font);p->setPen(theme.text);
    const QRect name(card.left()+12,card.bottom()-44,card.width()-24,20);
    p->drawText(name,Qt::AlignVCenter,o.fontMetrics.elidedText(a["name"].toString(),Qt::ElideMiddle,name.width()));
    QFont small=o.font;small.setPointSizeF(qMax(8.,small.pointSizeF()-1));p->setFont(small);p->setPen(theme.muted);
    QString label=a["extension"].toString().toUpper();const QString backup=a["backup_state"].toString();
    if(a["missing"].toBool())label+=QString::fromUtf8("  ·  来源丢失");
    else if(backup=="ready")label+=QString::fromUtf8("  ·  已备份");
    else if(backup=="stale")label+=QString::fromUtf8("  ·  备份过期");
    if(a["missing"].toBool()||backup=="ready"||backup=="stale")p->setPen(theme.text);
    const QRect status(card.left()+12,card.bottom()-24,card.width()-24,19);
    p->drawText(status,Qt::AlignLeft|Qt::AlignVCenter,p->fontMetrics().elidedText(label,Qt::ElideRight,status.width()));
    if(a["favorite"].toBool()){
        p->setPen(QColor("#edc778"));p->drawText(image.adjusted(6,3,-5,-4),Qt::AlignTop|Qt::AlignRight,QString::fromUtf8("★"));
    }

    // A local marker keeps keyboard focus visible without framing the whole card.
    if(selected||focused){
        QColor marker=theme.accent;marker.setAlpha(focused?255:190);
        p->setPen(QPen(marker,3,Qt::SolidLine,Qt::RoundCap));
        p->drawLine(QPointF(card.left()+12,card.top()+6),
                    QPointF(card.left()+(focused?36:28),card.top()+6));
    }
    p->restore();
}
AssetGrid::AssetGrid(QWidget *p):QListView(p),m_delegate(new AssetDelegate(this)){setObjectName("assetGrid");setAccessibleName(tr("素材卡片列表"));setAccessibleDescription(tr("方向键选择素材，Enter 打开详情；Ctrl 或 Shift 多选。"));setViewMode(IconMode);setResizeMode(Adjust);setMovement(Static);setWrapping(true);setUniformItemSizes(true);setLayoutMode(Batched);setBatchSize(80);setSelectionMode(ExtendedSelection);setVerticalScrollMode(ScrollPerPixel);setMouseTracking(true);setSpacing(0);setAcceptDrops(true);setDragEnabled(false);setItemDelegate(m_delegate);setCardSize(176);}
void AssetGrid::setCardSize(int size){m_delegate->setCardSize(size);setGridSize({size,size+58});doItemsLayout();}
void AssetGrid::dragEnterEvent(QDragEnterEvent *e){if(e->mimeData()->hasUrls())e->acceptProposedAction();else QListView::dragEnterEvent(e);}
void AssetGrid::dragMoveEvent(QDragMoveEvent *e){if(e->mimeData()->hasUrls())e->acceptProposedAction();else QListView::dragMoveEvent(e);}
void AssetGrid::dropEvent(QDropEvent *e){if(e->mimeData()->hasUrls()){QStringList paths;for(const auto &u:e->mimeData()->urls())if(u.isLocalFile())paths<<u.toLocalFile();emit filesDropped(paths);e->acceptProposedAction();}else QListView::dropEvent(e);}
void AssetGrid::paintEvent(QPaintEvent *event){QElapsedTimer timer;if(m_measure){timer.start();if(m_frameClock.isValid())m_frameSamples.append(m_frameClock.nsecsElapsed()/1000000.);m_frameClock.restart();}QListView::paintEvent(event);if(!model()||gridSize().height()<1)return;const int columns=qMax(1,viewport()->width()/gridSize().width()),row=verticalScrollBar()->value()/gridSize().height();const int first=qMax(0,(row-1)*columns),last=qMin(model()->rowCount(),(row+(viewport()->height()*2)/gridSize().height()+2)*columns);for(int i=first;i<last;++i)model()->index(i,0).data(Qt::DecorationRole);if(m_measure&&m_paintSamples.size()<10000)m_paintSamples.append(timer.nsecsElapsed()/1000000.);}
void AssetGrid::beginMeasurement(){m_paintSamples.clear();m_frameSamples.clear();m_frameClock.invalidate();m_measure=true;}
QJsonObject AssetGrid::timingReport()const{auto summarize=[](QVector<double> values){if(values.isEmpty())return QJsonObject{};std::sort(values.begin(),values.end());double total=0;for(auto v:values)total+=v;return QJsonObject{{"count",values.size()},{"mean_ms",total/values.size()},{"p50_ms",values[(values.size()-1)/2]},{"p95_ms",values[qMin(values.size()-1,qsizetype(values.size()*.95))]},{"max_ms",values.last()}};};return {{"paint_cpu",summarize(m_paintSamples)},{"paint_event_intervals",summarize(m_frameSamples)}};}
