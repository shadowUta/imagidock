#include "PreviewPane.h"
#include "../render/ModelView.h"
#include <QAudioOutput>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QHideEvent>
#include <QImageReader>
#include <QLabel>
#include <QMediaPlayer>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QVideoWidget>
#include <QWheelEvent>
#include <QtConcurrent>
#include <climits>
#include <memory>

ImageCanvas::ImageCanvas(QWidget *p):QGraphicsView(p){setScene(new QGraphicsScene(this));setDragMode(ScrollHandDrag);setTransformationAnchor(AnchorUnderMouse);setResizeAnchor(AnchorViewCenter);setFrameShape(QFrame::NoFrame);setRenderHint(QPainter::SmoothPixmapTransform);}
void ImageCanvas::setImage(const QImage &im){scene()->clear();m_item=scene()->addPixmap(QPixmap::fromImage(im));scene()->setSceneRect(m_item->boundingRect());fit();}
void ImageCanvas::fit(){m_fit=true;if(m_item)fitInView(m_item,Qt::KeepAspectRatio);}
void ImageCanvas::actualSize(){m_fit=false;resetTransform();}
void ImageCanvas::wheelEvent(QWheelEvent *e){if(!m_item)return;const double next=transform().m11()*(e->angleDelta().y()>0?1.2:1./1.2);if(next>=.01&&next<=64){m_fit=false;scale(e->angleDelta().y()>0?1.2:1./1.2,e->angleDelta().y()>0?1.2:1./1.2);}e->accept();}
void ImageCanvas::resizeEvent(QResizeEvent *e){QGraphicsView::resizeEvent(e);if(m_fit)fit();}
void ImageCanvas::drawBackground(QPainter *p,const QRectF &){p->save();p->resetTransform();const bool dark=palette().color(QPalette::Base).lightness()<128;const QColor a=dark?QColor(27,31,37):QColor(236,238,242),b=dark?QColor(32,37,44):QColor(249,250,252);p->fillRect(viewport()->rect(),a);for(int y=0;y<viewport()->height();y+=18)for(int x=0;x<viewport()->width();x+=18)if(((x/18)+(y/18))%2)p->fillRect(x,y,18,18,b);p->restore();}

static QString clockText(qint64 ms){const auto seconds=ms/1000;return QString("%1:%2").arg(seconds/60).arg(seconds%60,2,10,QChar('0'));}
PreviewPane::PreviewPane(RpcClient *rpc,QWidget *p):QWidget(p),m_rpc(rpc){
    auto layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(4);
    auto toolbar=new QToolBar(this);toolbar->setIconSize({16,16});toolbar->addAction(tr("适应"),this,[this]{m_image->fit();});toolbar->addAction(tr("100%"),this,[this]{m_image->actualSize();});toolbar->addSeparator();toolbar->addAction(tr("复位模型"),this,[this]{m_model->resetCamera();});auto anim=toolbar->addAction(tr("播放动画"));anim->setCheckable(true);connect(anim,&QAction::toggled,this,[this](bool on){m_model->setAnimationPlaying(on);});toolbar->addAction(tr("保存模型快照"),this,[this]{if(m_asset["type"]!="model")return;const QString directory=m_rpc->libraryPath()+"/cache/snapshots";QDir().mkpath(directory);const QString path=directory+"/"+m_asset["id"].toString()+"-"+QString::number(QDateTime::currentMSecsSinceEpoch())+".png";if(m_model->saveSnapshot(path))emit snapshotSaved(path);else m_status->setText(m_model->lastError());});layout->addWidget(toolbar);
    m_stack=new QStackedWidget(this);m_stack->setMinimumSize(240,200);m_image=new ImageCanvas;m_stack->addWidget(m_image);m_model=new ModelView;m_stack->addWidget(m_model);auto video=new QVideoWidget;m_stack->addWidget(video);auto empty=new QLabel(tr("选择资产开始预览"));empty->setAlignment(Qt::AlignCenter);m_stack->addWidget(empty);m_stack->setCurrentIndex(3);layout->addWidget(m_stack,1);
    m_player=new QMediaPlayer(this);m_audio=new QAudioOutput(this);m_player->setAudioOutput(m_audio);m_player->setVideoOutput(video);m_audio->setVolume(.8);
    m_controls=new QWidget;auto controls=new QHBoxLayout(m_controls);controls->setContentsMargins(4,0,4,0);m_play=new QPushButton(tr("播放"));controls->addWidget(m_play);m_seek=new QSlider(Qt::Horizontal);m_seek->setRange(0,0);controls->addWidget(m_seek,1);m_time=new QLabel("0:00 / 0:00");controls->addWidget(m_time);auto volume=new QSlider(Qt::Horizontal);volume->setRange(0,100);volume->setValue(80);volume->setMaximumWidth(85);volume->setToolTip(tr("音量"));controls->addWidget(volume);auto loop=new QPushButton(tr("循环"));loop->setCheckable(true);controls->addWidget(loop);layout->addWidget(m_controls);m_controls->hide();
    connect(m_play,&QPushButton::clicked,this,[this]{if(m_player->playbackState()==QMediaPlayer::PlayingState)m_player->pause();else m_player->play();});connect(m_player,&QMediaPlayer::playbackStateChanged,this,[this](QMediaPlayer::PlaybackState s){m_play->setText(s==QMediaPlayer::PlayingState?tr("暂停"):tr("播放"));});connect(m_player,&QMediaPlayer::durationChanged,this,[this](qint64 d){m_seek->setMaximum(int(qMin(d,qint64(INT_MAX))));});connect(m_player,&QMediaPlayer::positionChanged,this,[this](qint64 pos){if(!m_seek->isSliderDown())m_seek->setValue(int(pos));m_time->setText(clockText(pos)+" / "+clockText(m_player->duration()));});connect(m_seek,&QSlider::sliderMoved,m_player,&QMediaPlayer::setPosition);connect(volume,&QSlider::valueChanged,this,[this](int v){m_audio->setVolume(v/100.);});connect(loop,&QPushButton::toggled,this,[this](bool on){m_player->setLoops(on?QMediaPlayer::Infinite:1);});
    m_status=new QLabel;m_status->setWordWrap(true);m_status->setObjectName("muted");layout->addWidget(m_status);connect(m_player,&QMediaPlayer::errorOccurred,this,[this](QMediaPlayer::Error,const QString &err){m_status->setText(tr("媒体解码失败：%1").arg(err));});connect(m_model,&ModelView::errorOccurred,this,[this](const QString &err){m_status->setText(tr("模型预览失败：%1").arg(err));});connect(m_model,&ModelView::modelLoaded,this,[this](const QJsonObject &meta){m_status->setText(tr("拖动旋转 · 滚轮缩放 · 中键平移"));emit metadataReady(meta);});
}
void PreviewPane::stop(){m_player->pause();m_model->setAnimationPlaying(false);}
void PreviewPane::hideEvent(QHideEvent *e){stop();QWidget::hideEvent(e);}
void PreviewPane::setAsset(const QJsonObject &asset){stop();m_player->setSource({});m_asset=asset;const auto generation=++m_generation;m_controls->hide();m_stack->setCurrentIndex(3);m_status->setText(tr("正在读取预览…"));resolve(generation);}
void PreviewPane::resolve(quint64 generation){m_rpc->call("asset.resolve",{{"asset_id",m_asset["id"]}},this,[this,generation](const QJsonObject &r,const QString &err){if(generation!=m_generation)return;if(!err.isEmpty()){m_status->setText(err);return;}if(!r["job_id"].toString().isEmpty()){m_status->setText(tr("正在从备份准备预览…"));QTimer::singleShot(800,this,[this,generation]{if(generation==m_generation)resolve(generation);});return;}const QString path=r["path"].toString();if(!r["exists"].toBool()||path.isEmpty()){m_status->setText(tr("来源文件不可用。可在右侧重新链接或从备份取用。"));if(!m_asset["preview_path"].toString().isEmpty())image(m_asset["preview_path"].toString(),generation,tr("缓存预览 · 原文件丢失"));return;}load(path,generation);});}
void PreviewPane::load(const QString &path,quint64 generation){const QString type=m_asset["type"].toString();if(type=="model"){m_stack->setCurrentWidget(m_model);renderBindings(path,generation);return;}if(type=="audio"||type=="video"){m_player->setSource(QUrl::fromLocalFile(path));m_controls->show();if(type=="video")m_stack->setCurrentIndex(2);else{const QString preview=m_asset["thumbnail_path"].toString();if(!preview.isEmpty())image(preview,generation,tr("音频波形 · 点击播放"));else m_stack->setCurrentIndex(3);}m_status->setText(tr("媒体已就绪 · 点击播放"));return;}const QString ext=QFileInfo(path).suffix().toLower();if(QImageReader::supportedImageFormats().contains(ext.toLatin1())){image(path,generation,{});return;}const QString preview=m_asset["preview_path"].toString();if(!preview.isEmpty()){image(preview,generation,m_asset["preview_kind"].toString());return;}m_rpc->call("preview.resolve",{{"asset_id",m_asset["id"]},{"size",2048}},this,[this,generation](const QJsonObject &r,const QString &err){if(generation!=m_generation)return;const QString path=r["path"].toString();if(!path.isEmpty()){image(path,generation,r["preview_kind"].toString(tr("派生预览 · 可关联高清预览图")));return;}if(!err.isEmpty()||r["state"]=="failed"){m_status->setText(err.isEmpty()?tr("无法生成预览。请配置源文件工具，或添加高清预览图。"):err);return;}QTimer::singleShot(1200,this,[this,generation]{if(generation==m_generation)load(m_asset["source_path"].toString(),generation);});});}
void PreviewPane::image(const QString &path,quint64 generation,const QString &caption){auto watcher=new QFutureWatcher<QImage>(this);connect(watcher,&QFutureWatcher<QImage>::finished,this,[this,watcher,generation,caption]{const auto image=watcher->result();watcher->deleteLater();if(generation!=m_generation)return;if(image.isNull()){m_status->setText(tr("无法解码此预览图。"));return;}m_image->setImage(image);m_stack->setCurrentWidget(m_image);m_status->setText(QString("%1 × %2  ·  ").arg(image.width()).arg(image.height())+(caption.isEmpty()?tr("滚轮缩放 · 拖动平移"):caption));});watcher->setFuture(QtConcurrent::run([path]{QImageReader reader(path);reader.setAutoTransform(true);reader.setAllocationLimit(512);const auto size=reader.size();if(qint64(size.width())*size.height()>100000000)reader.setScaledSize(size.scaled(8192,8192,Qt::KeepAspectRatio));return reader.read();}));}
void PreviewPane::setImagePath(const QString &path,const QString &caption){stop();m_controls->hide();image(path,++m_generation,caption);}
void PreviewPane::renderBindings(const QString &path,quint64 generation){const auto relations=m_asset["relations"].toArray();auto bindings=std::make_shared<QJsonArray>();auto remaining=std::make_shared<int>(0);for(const auto &v:relations)if(!v.toObject()["role"].toString().isEmpty())++*remaining;if(*remaining==0){m_model->loadModel(path,{});return;}for(const auto &v:relations){auto b=v.toObject();if(b["role"].toString().isEmpty())continue;m_rpc->call("asset.resolve",{{"asset_id",b["target_id"]}},this,[this,path,generation,b,bindings,remaining](const QJsonObject &r,const QString &){if(generation!=m_generation)return;auto resolved=b;if(r["exists"].toBool()){resolved["path"]=r["path"];bindings->append(resolved);}if(--*remaining==0)m_model->loadModel(path,*bindings);});}}
